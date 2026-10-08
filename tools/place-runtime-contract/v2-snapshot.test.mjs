import assert from 'node:assert/strict';
import test from 'node:test';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

const repo = new URL('../../', import.meta.url);
const file = path => readFileSync(new URL(path, repo));
const json = path => JSON.parse(file(path).toString('utf8'));
const blobId = bytes => createHash('sha1').update('blob ' + bytes.length + '\0').update(bytes).digest('hex');
const names = json('contracts/places/v2.json');
const sourceManifest = json('reports/places/source-manifest.json');

test('native branch retains the exact Web-pinned v2 exchange artifact', () => {
  assert.equal(blobId(file('contracts/places/v2.json')), 'c183d618238b47f297ecc84804b3e2239ca78d3f');
  assert.equal(names.schema, 'pando-place-sync-v2');
  assert.equal(names.dataContractVersion, 2);
  assert.equal(names.wire.headerBytes, 32);
  assert.equal(names.wire.recordBytes, 68);
  assert.equal(names.wire.stringFields.length, 8);
  assert.deepEqual(names.domain.languageOrder, ['ko', 'en', 'native']);
  assert.deepEqual(names.domain.defaultLanguages, { ko: true, en: false, native: false });
});

test('all verified review snapshots remain byte-identical to Web commit', () => {
  assert.equal(sourceManifest.webCommit, 'fc0cb82a9e363be6a3009cf07932a3c9ce59ddd2');
  assert.equal(sourceManifest.files.length, 10);
  for (const entry of sourceManifest.files) {
    assert.match(entry.path, /^reports\/places\/[a-z0-9.-]+\.json$/u);
    assert.equal(blobId(file(entry.path)), entry.gitBlobSha, entry.path);
  }
});

test('reviewed city inventory matches every copied Web batch without ID duplication', () => {
  const inventory=sourceManifest.reviewInventory;
  assert.equal(inventory.batchCount, 8);
  assert.equal(inventory.recordCount, 55);
  assert.equal(inventory.distinctGeoNames, 55);
  const copies=sourceManifest.files.filter(entry=>
    entry.path.startsWith('reports/places/tier1-major-cities-batch') && entry.path.endsWith('.json'));
  assert.equal(copies.length,inventory.batchCount);
  const seen=new Set();
  let total=0;
  for (const batch of inventory.byBatch) {
    assert.ok(copies.some(entry=>entry.path===batch.path),'Missing source batch '+batch.path);
    const records=json(batch.path).records;
    assert.equal(records.length,batch.recordCount,batch.path);
    total+=records.length;
    for (const record of records) {
      assert.ok(Number.isSafeInteger(record.geonameId),batch.path);
      assert.ok(typeof record.defaultDisplayNameKo==='string' && record.defaultDisplayNameKo.trim(),batch.path);
      assert.ok(!seen.has(record.geonameId),'Duplicate GeoNames ID '+record.geonameId);
      seen.add(record.geonameId);
    }
  }
  assert.equal(total,inventory.recordCount);
  assert.equal(seen.size,inventory.distinctGeoNames);
});

test('portable multilingual rows and provenance match reviewed city records', () => {
  let scenarios = 0;
  const sourceCache = new Map();
  for (const vector of names.fixtures) {
    const { reviewFile, geonameId } = vector.sourceReview;
    if (!sourceCache.has(reviewFile)) sourceCache.set(reviewFile, json(reviewFile));
    const records = sourceCache.get(reviewFile).records.filter(item => item.geonameId === geonameId);
    assert.equal(records.length, 1);
    const reviewed = records[0];
    assert.equal(reviewed.defaultDisplayNameKo, vector.input.name);
    assert.deepEqual([reviewed.longitude, reviewed.latitude], vector.input.coordinates);
    for (const scenario of vector.scenarios) {
      const chosen = { ko: vector.input.name, en: vector.input.nameEn, native: vector.input.nameNative };
      if (scenario.date !== null) {
        for (const transition of vector.input.nameTimeline || []) {
          const eligible = transition.fromDate
            ? transition.fromDate <= scenario.date
            : transition.fromYear <= Number(scenario.date.slice(0, 4));
          if (!eligible) break;
          for (const language of ['ko', 'en', 'native'])
            if (transition[language] !== undefined) chosen[language] = transition[language];
        }
      }
      const seen = new Set(), actual = [];
      for (const language of names.domain.languageOrder) {
        if (!scenario.languages[language]) continue;
        const text = String(chosen[language] || '').trim(), key = text.normalize('NFKC').toLocaleLowerCase();
        if (text && !seen.has(key)) { seen.add(key); actual.push([language, text]); }
      }
      assert.deepEqual(actual, scenario.rows, vector.id);
      scenarios++;
    }
  }
  assert.equal(names.fixtures.length, 4);
  assert.equal(scenarios, 11);
});
