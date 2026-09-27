#!/usr/bin/env node
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const PIN = Object.freeze({
  schema: 'pandoeditor-m71-world-corpus', version: 1,
  pandoEditorBaseline: '168fb7cf7ecde29544e9f65e10fca6a2530d1de0',
  worldMapCommit: 'c0bd31d13dc8495593d78cf51f7cc195de7c9469',
  countryIds: 'DEU RUS FJI KIR USA FRA IDN PHL ATA ZAF LSO CHL NOR'.split(' '),
  sources: {
    countries: ['assets/data/countries-ne-5.1.1.geojson', 'd79abcb4a47d49f188e0601c3721d9e10e6c7f52'],
    canonicalPacket: ['assets/data/countries-canonical-v0.33.0.pcg.gz', '54146d9eeb28e4af08e094f5061bf64689e6bdf3'],
    worldMesh: ['assets/data/world-mesh-v0.12.6.bin.gz', '8c73420b92e89ab64cbe75dcc5016efe2c0a22b6'],
    hydroRiver: ['assets/data/hydro/rivers_base.geojson', '6ec119337f9c38b33bb498edbe351573b9e6aa60'],
    hydroLake: ['assets/data/hydro/lakes_base.geojson', 'bf088f97a5362842635369656c57314f4521f398'],
  },
});
export {PIN};
export const RISK_TAGS = Object.freeze({
  DEU: ['control'], RUS: ['very-large', 'multi-part', 'dateline'],
  FJI: ['multi-part', 'dateline'], KIR: ['many-islands', 'dateline'],
  USA: ['very-large', 'multi-part', 'aleutian'], FRA: ['overseas', 'multi-part'],
  IDN: ['many-islands', 'multi-part'], PHL: ['many-islands', 'multi-part'],
  ATA: ['polar', 'very-large'], ZAF: ['enclave-container', 'hole'],
  LSO: ['enclave'], CHL: ['long-narrow'], NOR: ['high-latitude', 'multi-part'],
});

export function gitBlobSha(bytes) {
  const data = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes);
  return createHash('sha1').update(`blob ${data.length}\0`).update(data).digest('hex');
}

function geometryFacts(geometry) {
  const polygons=geometry.type==='Polygon'?[geometry.coordinates]:
    geometry.type==='MultiPolygon'?geometry.coordinates:[];
  const lists=polygons.length?polygons.flatMap(p=>p):
    geometry.type==='LineString'?[geometry.coordinates]:geometry.type==='MultiLineString'?geometry.coordinates:
    geometry.type==='Point'?[[geometry.coordinates]]:[];
  if(!lists.length) throw new Error('unknown corpus geometry type');
  const bounds=[Infinity,Infinity,-Infinity,-Infinity];
  let coordinateCount=0,maxLongitudeJump=0;
  for(const list of lists) {
    let previous;
    for(const p of list) {
      if(!Array.isArray(p)||p.length<2||!p.every(Number.isFinite)) throw new Error('invalid coordinate in corpus');
      bounds[0]=Math.min(bounds[0],p[0]);bounds[1]=Math.min(bounds[1],p[1]);
      bounds[2]=Math.max(bounds[2],p[0]);bounds[3]=Math.max(bounds[3],p[1]);
      if(previous) maxLongitudeJump=Math.max(maxLongitudeJump,Math.abs(p[0]-previous[0]));
      previous=p;coordinateCount++;
    }
  }
  return {polygonCount:polygons.length,ringCount:polygons.reduce((n,p)=>n+p.length,0),
    holeCount:polygons.reduce((n,p)=>n+Math.max(0,p.length-1),0),coordinateCount,bounds,maxLongitudeJump};
}

function verifyRealGeometry(root,manifest) {
  const countries=JSON.parse(readFileSync(join(root,'countries.geojson'),'utf8'));
  const rows=manifest.countries;
  if(countries.type!=='FeatureCollection'||countries.features?.length!==13||rows?.length!==13||
      JSON.stringify(countries.features.map(f=>f.id))!==JSON.stringify(PIN.countryIds)) {
    throw new Error('country IDs/order mismatch in committed corpus');
  }
  const aggregate={polygonCount:0,ringCount:0,holeCount:0,coordinateCount:0};
  for(let i=0;i<13;++i) {
    const feature=countries.features[i],row=rows[i],facts=geometryFacts(feature.geometry);
    if(row.id!==feature.id||row.geometryType!==feature.geometry.type||
        Object.keys(facts).some(key=>JSON.stringify(facts[key])!==JSON.stringify(row[key]))||
        createHash('sha256').update(JSON.stringify(feature.geometry)).digest('hex')!==row.geometrySha256) {
      throw new Error(`country geometry stats mismatch: ${feature.id}`);
    }
    if(JSON.stringify(row.riskTags)!==JSON.stringify(RISK_TAGS[feature.id])) {
      throw new Error(`country risk tags mismatch: ${feature.id}`);
    }
    for(const key of Object.keys(aggregate)) aggregate[key]+=facts[key];
  }
  if(JSON.stringify(aggregate)!==JSON.stringify(manifest.aggregate)) throw new Error('country aggregate stats mismatch');
  for(const [kind,name] of [['river','hydro-river.geojson'],['lake','hydro-lake.geojson']]) {
    const feature=JSON.parse(readFileSync(join(root,name),'utf8')).features?.[0];
    const row=manifest.hydro?.[kind];
    if(!feature||!row||feature.id!==row.id||feature.geometry.type!==row.geometryType||
       Object.entries(geometryFacts(feature.geometry)).some(([key,value])=>JSON.stringify(row[key])!==JSON.stringify(value))||
       createHash('sha256').update(JSON.stringify(feature.geometry)).digest('hex')!==row.geometrySha256) {
      throw new Error(`hydro geometry stats mismatch: ${kind}`);
    }
  }
}

export function readManifest(root) {
  const m = JSON.parse(readFileSync(join(root, 'manifest.json'), 'utf8'));
  for (const key of ['schema', 'version', 'pandoEditorBaseline', 'worldMapCommit']) {
    if (m[key] !== PIN[key]) throw new Error(`manifest ${key} differs from pinned contract`);
  }
  if (JSON.stringify(m.countryIds) !== JSON.stringify(PIN.countryIds)) {
    throw new Error('manifest country IDs or order differ from pinned contract');
  }
  for (const [key, [path, gitBlobSha]] of Object.entries(PIN.sources)) {
    if (m.sources?.[key]?.path !== path || m.sources?.[key]?.gitBlobSha !== gitBlobSha) {
      throw new Error(`manifest source ${key} differs from pinned contract`);
    }
  }
  if (m.upstreamReference?.featureCount !== 258 || m.upstreamReference?.positionCount !== 548454) {
    throw new Error('manifest upstream reference differs from pinned contract');
  }
  for (const name of ['countries.geojson', 'hydro-river.geojson', 'hydro-lake.geojson',
    'sentinels.geojson', 'composition.json']) {
    if (!Object.hasOwn(m.fixtureHashes ?? {}, name)) throw new Error(`manifest missing fixture identity: ${name}`);
  }
  if (!Array.isArray(m.countries) || m.countries.length !== 13) throw new Error('manifest country metadata missing');
  if (!m.hydro?.river || !m.hydro?.lake) throw new Error('manifest hydro metadata missing');
  if (!m.aggregate || !['polygonCount','ringCount','holeCount','coordinateCount'].every(
    key => Number.isSafeInteger(m.aggregate[key]) && m.aggregate[key] >= 0)) {
    throw new Error('manifest aggregate metadata missing');
  }
  return m;
}

function checkCoordinates(geometry) {
  if (!geometry || !['Point', 'LineString', 'MultiLineString', 'Polygon', 'MultiPolygon'].includes(geometry.type)) {
    throw new Error('unsupported fixture geometry');
  }
  function visit(value) {
    if (Array.isArray(value) && value.length >= 2 && typeof value[0] !== 'object') {
      if (!value.every(Number.isFinite)) throw new Error('invalid coordinate in fixture');
    } else if (Array.isArray(value) && value.length) {
      value.forEach(visit);
    } else throw new Error('invalid coordinate in fixture');
  }
  visit(geometry.coordinates);
}

function verifySyntheticScene(root, manifest) {
  const sentinels = JSON.parse(readFileSync(join(root, 'sentinels.geojson'), 'utf8'));
  const features = sentinels.features;
  if (sentinels.type !== 'FeatureCollection' || !Array.isArray(features) ||
      features.length !== 2 || new Set(features.map(f => f.id)).size !== 2 ||
      JSON.stringify(features.map(f => f.id)) !== JSON.stringify(['DATELINE', 'POLAR'])) {
    throw new Error('sentinel IDs must be unique DATELINE and POLAR');
  }
  for (const f of features) {
    if (f.properties?.synthetic !== true) throw new Error(`sentinel ${f.id} missing synthetic marker`);
    checkCoordinates(f.geometry);
  }
  const dateline=[[[179,70],[-179,70],[-179,80],[179,80],[179,70]],
    [[179.5,72],[-179.5,72],[-179.5,74],[179.5,74],[179.5,72]]];
  const polar=[[[[0,89.9],[120,89.8],[-120,89.8],[0,89.9]]],
    [[[10,-89.9],[20,-89.8],[0,-89.8],[10,-89.9]]]];
  if(features[0].properties.purpose!=='dateline-with-hole'||features[0].geometry.type!=='Polygon'||
      JSON.stringify(features[0].geometry.coordinates)!==JSON.stringify(dateline)||
      features[1].geometry.type!=='MultiPolygon'||
      JSON.stringify(features[1].geometry.coordinates)!==JSON.stringify(polar)) {
    throw new Error('sentinel coordinate contract mismatch');
  }
  const composition = JSON.parse(readFileSync(join(root, 'composition.json'), 'utf8'));
  if (composition.schema !== 'pandoeditor-m71-composition' || composition.version !== 1) {
    throw new Error('composition schema or version mismatch');
  }
  const ids = new Set(manifest.countryIds);
  if (!ids.has(composition.controlCountryId)) throw new Error('unresolved control country reference');
  for (const item of [composition.subunit, composition.region]) {
    if (!item?.id || !ids.has(item.parent) || !ids.has(item.sovereign)) {
      throw new Error('unresolved territorial reference in composition');
    }
    checkCoordinates(item.geometry);
    ids.add(item.id);
  }
  for (const item of [...(composition.distributions ?? []), ...(composition.generic ?? []),
      ...(composition.labels ?? [])]) {
    if (!item.id || (item.territory && !ids.has(item.territory))) {
      throw new Error('unresolved object reference in composition');
    }
    checkCoordinates(item.geometry);
  }
  if(composition.distributions?.length!==3||
      JSON.stringify(composition.distributions.map(x=>x.type))!==JSON.stringify(['language','ethnicity','religion'])||
      JSON.stringify(composition.generic?.map(x=>x.geometry?.type))!==JSON.stringify(['Polygon','LineString','Point'])||
      JSON.stringify(composition.labels?.map(x=>x.placement))!==JSON.stringify(['manual','automatic'])) {
    throw new Error('composition mixed-domain contract mismatch');
  }
  if (JSON.stringify(composition.hydroRefs) !== JSON.stringify(['hydro-river.geojson', 'hydro-lake.geojson']) ||
      composition.hydroRefs.some(name => !Object.hasOwn(manifest.fixtureHashes, name))) {
    throw new Error('unresolved hydro reference in composition');
  }
}

export function verifyCommittedCorpus(root) {
  const m = readManifest(root);
  for (const [name, expected] of Object.entries(m.fixtureHashes)) {
    if (!['countries.geojson', 'hydro-river.geojson', 'hydro-lake.geojson',
      'sentinels.geojson', 'composition.json'].includes(name)) {
      throw new Error(`manifest unknown fixture: ${name}`);
    }
    let bytes;
    try { bytes = readFileSync(join(root, name)); }
    catch (error) {
      if (error.code === 'ENOENT') throw new Error(`missing fixture: ${name}`);
      throw error;
    }
    if (typeof expected !== 'string' || !/^[0-9a-f]{64}$/.test(expected) ||
        createHash('sha256').update(bytes).digest('hex') !== expected) {
      throw new Error(`fixture hash mismatch: ${name}`);
    }
  }
  verifyRealGeometry(root,m);
  verifySyntheticScene(root,m);
  return m;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const root = resolve(process.argv[2] ?? 'tests/fixtures/world-rendering');
    const m = verifyCommittedCorpus(root);
    console.log(`M7.1 corpus verified: ${m.countryIds.length} countries, ${Object.keys(m.fixtureHashes).length} fixtures`);
  } catch (error) {
    console.error(`M7.1 corpus verification failed: ${error.message}`);
    process.exitCode = 1;
  }
}
