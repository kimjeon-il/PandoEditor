import assert from 'node:assert/strict';
import test from 'node:test';
import {createHash} from 'node:crypto';
import {readTerritorialSources} from '../../tools/territorial-entity-sources.mjs';
const historicalData={entities:readTerritorialSources().filter(e=>e.sourceInfo.importProvenance)};
test('East Prussia r3 library preserves the reviewed geometry and reports its limitations', () => {
  const entity = historicalData.entities.find(item => item.entityId === 'state:east-prussia');
  const version = entity.geometryVersions[0];
  const coordinates = version.geometry.coordinates.flat(2);
  const coordinateKeys = new Set(coordinates.map(coordinate => coordinate.join(',')));
  const geometrySha256 = createHash('sha256').update(JSON.stringify(version.geometry)).digest('hex');
  assert.equal(geometrySha256, '54c45d4de9f5f16e9dffb06eec24aeaef8f89b82aa455fd7c26b1064fe716237');
  assert.equal(entity.metadata.geometrySha256, geometrySha256);
  assert.equal(version.versionId, 'ostpreussen-1878-1920-r3');
  assert.equal(version.geometry.coordinates.length, 1);
  assert.equal(coordinates.length, 6766);
  assert.ok(coordinateKeys.has('22.76722,54.35627'));
  assert.ok(coordinateKeys.has('22.580668,55.057622'));
  assert.equal(version.certainty, 'medium');
  assert.equal(entity.metadata.approximateGeometry, true);
  assert.equal(entity.metadata.production, false);
  assert.equal(entity.metadata.validation.statisticalAreaWithinOnePercent, false);
  assert.equal(entity.metadata.validation.modernEastUnmatchedLengthM, 0);
  assert.equal(entity.metadata.validation.redistributionPermission, 'unconfirmed');
  assert.equal(entity.metadata.artifactSha256, 'f058012d42205bb02705c8017fba2e3c0e920b2a9f8a9eb21b312b9f7bfbdc0b');
});

test('Prussian province library entries carry their historical province flags', () => {
  for (const [entityId, marker] of [
    ['state:east-prussia', 'id="Oben"'],
    ['state:west-prussia', 'id="Mitte"'],
  ]) {
    const entity = historicalData.entities.find(item => item.entityId === entityId);
    const dataUrl = entity.metadata.defaultFlagDataUrl;
    assert.match(dataUrl, /^data:image\/svg\+xml;base64,/);
    const svg = Buffer.from(dataUrl.slice('data:image/svg+xml;base64,'.length), 'base64').toString('utf8');
    assert.match(svg, /viewBox="0 0 600 400"/);
    assert.match(svg, new RegExp(marker));
  }
});

test('every static library object has a default flag', () => {
  const missing = historicalData.entities
    .filter(entity => !String(entity.metadata?.defaultFlagDataUrl || '').trim())
    .map(entity => entity.entityId);
  assert.deepEqual(missing, []);
});

test('Artsakh uses the right-originating white stepped flag motif', () => {
  const entity = historicalData.entities.find(item => item.entityId === 'state:nagorno-karabakh');
  const dataUrl = entity.metadata.defaultFlagDataUrl;
  const svg = Buffer.from(dataUrl.slice('data:image/svg+xml;base64,'.length), 'base64').toString('utf8');
  assert.match(svg, /M36 0h-6/);
});

