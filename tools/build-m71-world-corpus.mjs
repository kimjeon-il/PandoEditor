#!/usr/bin/env node
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {existsSync, mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {gitBlobSha, PIN, RISK_TAGS, readManifest} from './verify-m71-world-corpus.mjs';

function coordinateLists(geometry) {
  const c = geometry.coordinates;
  switch (geometry.type) {
    case 'Point': return [[c]];
    case 'LineString': return [c];
    case 'MultiLineString': return c;
    case 'Polygon': return c;
    case 'MultiPolygon': return c.flatMap(polygon => polygon);
    default: throw new Error(`unsupported geometry: ${geometry.type}`);
  }
}

export function geometryStats(geometry) {
  const polygons = geometry.type === 'Polygon' ? [geometry.coordinates] :
    geometry.type === 'MultiPolygon' ? geometry.coordinates : [];
  const lists = coordinateLists(geometry);
  let west = Infinity, south = Infinity, east = -Infinity, north = -Infinity;
  let coordinateCount = 0, maxLongitudeJump = 0;
  for (const positions of lists) {
    let previous;
    for (const position of positions) {
      if (!Array.isArray(position) || position.length < 2 || !position.every(Number.isFinite)) {
        throw new Error('non-finite or malformed source position');
      }
      const [lon, lat] = position;
      west = Math.min(west, lon); south = Math.min(south, lat);
      east = Math.max(east, lon); north = Math.max(north, lat);
      if (previous) maxLongitudeJump = Math.max(maxLongitudeJump, Math.abs(lon - previous[0]));
      previous = position;
      coordinateCount++;
    }
  }
  if (!coordinateCount) throw new Error('empty source geometry');
  return {
    polygonCount: polygons.length,
    ringCount: polygons.reduce((n, p) => n + p.length, 0),
    holeCount: polygons.reduce((n, p) => n + Math.max(0, p.length - 1), 0),
    coordinateCount, bounds: [west, south, east, north], maxLongitudeJump,
  };
}

function stableKey(feature) {
  return String(feature.id ?? feature.properties?.name ?? '');
}

export function selectRiverFeature(features) {
  const sorted = features.map((feature, index) => ({feature, index}))
    .filter(({feature}) => ['LineString', 'MultiLineString'].includes(feature.geometry?.type))
    .sort((a, b) => geometryStats(b.feature.geometry).coordinateCount - geometryStats(a.feature.geometry).coordinateCount ||
      stableKey(a.feature).localeCompare(stableKey(b.feature), 'en') || a.index - b.index);
  if (!sorted.length) throw new Error('source contains no line hydro feature');
  return sorted[0].feature;
}

export function selectLakeFeature(features) {
  const sorted = features.map((feature, index) => ({feature, index}))
    .filter(({feature}) => ['Polygon', 'MultiPolygon'].includes(feature.geometry?.type))
    .sort((a, b) => geometryStats(b.feature.geometry).holeCount - geometryStats(a.feature.geometry).holeCount ||
      geometryStats(b.feature.geometry).coordinateCount - geometryStats(a.feature.geometry).coordinateCount ||
      stableKey(a.feature).localeCompare(stableKey(b.feature), 'en') || a.index - b.index);
  if (!sorted.length) throw new Error('source contains no polygon hydro feature');
  return sorted[0].feature;
}

function hash(bytes) { return createHash('sha256').update(bytes).digest('hex'); }
function sourceCommit(root) {
  if (existsSync(join(root, '.git'))) {
    return execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();
  }
  // A source-only snapshot obtained from the pinned GitHub blobs is usable
  // offline when its explicit commit marker and every raw blob agree.
  return readFileSync(join(root, '.world-map-commit'), 'utf8').trim();
}

function verifiedSources(root, manifest) {
  if (sourceCommit(root) !== PIN.worldMapCommit) throw new Error('source commit mismatch');
  const source = {};
  for (const key of ['countries', 'hydroRiver', 'hydroLake']) {
    const identity = manifest.sources[key];
    const bytes = readFileSync(join(root, identity.path));
    if (gitBlobSha(bytes) !== identity.gitBlobSha) throw new Error(`source blob mismatch: ${identity.path}`);
    source[key] = JSON.parse(bytes.toString('utf8'));
  }
  return source;
}

function outputFeature(feature) {
  return {type: 'Feature', id: feature.id, properties: {name: feature.properties?.name ?? ''}, geometry: feature.geometry};
}

export function buildCountryCorpus(source, manifest) {
  if (source.type !== 'FeatureCollection' || source.features.length !== 258) {
    throw new Error('country source feature count differs from upstream reference');
  }
  const allPositions = source.features.reduce((n, f) => n + geometryStats(f.geometry).coordinateCount, 0);
  if (allPositions !== 548454) throw new Error('country source position count differs from upstream reference');
  const features = manifest.countryIds.map(id => {
    const match = source.features.filter(f => f.id === id);
    if (match.length !== 1) throw new Error(`expected exactly one source country ${id}, got ${match.length}`);
    return outputFeature(match[0]);
  });
  return {type: 'FeatureCollection', features};
}

export function buildCorpus(root, out) {
  const template = resolve('tests/fixtures/world-rendering');
  const manifest = readManifest(existsSync(join(out, 'manifest.json')) ? out : template);
  // Check the entire upstream source identity before any output is written.
  const sources = verifiedSources(root, manifest);
  const countries = buildCountryCorpus(sources.countries, manifest);
  const river = outputFeature(selectRiverFeature(sources.hydroRiver.features));
  const lake = outputFeature(selectLakeFeature(sources.hydroLake.features));
  const outputs = {
    'countries.geojson': {type: 'FeatureCollection', features: countries.features},
    'hydro-river.geojson': {type: 'FeatureCollection', features: [river]},
    'hydro-lake.geojson': {type: 'FeatureCollection', features: [lake]},
  };
  manifest.countries = countries.features.map(feature => ({
    id: feature.id, geometryType: feature.geometry.type,
    ...geometryStats(feature.geometry), geometrySha256: hash(JSON.stringify(feature.geometry)),
    riskTags: RISK_TAGS[feature.id],
  }));
  manifest.hydro = Object.fromEntries([['river', river], ['lake', lake]].map(([kind, feature]) => [kind, {
    id: feature.id, geometryType: feature.geometry.type,
    ...geometryStats(feature.geometry), geometrySha256: hash(JSON.stringify(feature.geometry)),
  }]));
  manifest.aggregate = manifest.countries.reduce((a, c) => {
    for (const key of ['polygonCount', 'ringCount', 'holeCount', 'coordinateCount']) a[key] += c[key];
    return a;
  }, {polygonCount: 0, ringCount: 0, holeCount: 0, coordinateCount: 0});
  const bytes = Object.fromEntries(Object.entries(outputs).map(([name, value]) =>
    [name, Buffer.from(JSON.stringify(value) + '\n')]));
  for (const [name, data] of Object.entries(bytes)) manifest.fixtureHashes[name] = hash(data);
  mkdirSync(out, {recursive: true});
  for (const [name, data] of Object.entries(bytes)) writeFileSync(join(out, name), data);
  writeFileSync(join(out, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  return manifest;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    if (args.length !== 4 || args[0] !== '--world-map-root' || args[2] !== '--out') {
      throw new Error('usage: build-m71-world-corpus.mjs --world-map-root <path> --out <path>');
    }
    const m = buildCorpus(resolve(args[1]), resolve(args[3]));
    console.log(`M7.1 corpus built: ${m.countries.length} countries, ${m.aggregate.coordinateCount} positions`);
  } catch (error) {
    console.error(`M7.1 corpus build failed: ${error.message}`);
    process.exitCode = 1;
  }
}
