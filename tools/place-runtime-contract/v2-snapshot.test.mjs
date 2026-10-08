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

test('all verified review snapshots remain byte-identical to pinned Web blobs', () => {
  assert.match(sourceManifest.webCommit, /^[0-9a-f]{40}$/u);
  assert.ok(sourceManifest.files.length > 2);
  const paths = sourceManifest.files.map(entry => entry.path);
  assert.equal(new Set(paths).size, paths.length, 'Duplicate mirrored file');
  for (const policy of ['historical-display-policy.json', 'korean-map-label-policy.json'])
    assert.ok(paths.includes('reports/places/' + policy), 'Missing review policy ' + policy);
  for (const entry of sourceManifest.files) {
    assert.match(entry.path, /^reports\/places\/[a-z0-9.-]+\.json$/u);
    assert.equal(blobId(file(entry.path)), entry.gitBlobSha, entry.path);
  }
});

test('reviewed city inventory matches every copied Web batch without ID duplication', () => {
  const inventory=sourceManifest.reviewInventory;
  assert.ok(Number.isSafeInteger(inventory.batchCount) && inventory.batchCount > 0);
  assert.ok(Number.isSafeInteger(inventory.recordCount) && inventory.recordCount > 0);
  assert.equal(inventory.distinctGeoNames, inventory.recordCount);
  assert.equal(inventory.byBatch.length, inventory.batchCount);
  const copies=sourceManifest.files.filter(entry=>
    entry.path.startsWith('reports/places/tier1-major-cities-batch') && entry.path.endsWith('.json'));
  assert.equal(copies.length,inventory.batchCount);
  assert.deepEqual(copies.map(x=>x.path).sort(),inventory.byBatch.map(x=>x.path).sort(),
    'Copied batch source list differs from recorded review inventory');
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


test('batch 10 contains the four independently checked Balkan capitals', () => {
  const records=json('reports/places/tier1-major-cities-batch10-balkan-southeast-europe.json').records;
  assert.deepEqual(records.map(record=>record.geonameId),
    [792680,727011,3186886,3196359]);
  assert.deepEqual(records.map(record=>record.defaultDisplayNameKo),
    ['베오그라드','소피아','자그레브','류블랴나']);
  for (const record of records) {
    assert.equal(record.featureClass,'P');
    assert.equal(record.featureCode,'PPLC');
    assert.ok(record.names.some(row=>row.language==='ko'&&row.text===record.defaultDisplayNameKo));
    assert.ok(record.names.some(row=>row.language==='en'&&row.usage==='standard'));
    assert.ok(record.names.some(row=>!['ko','en'].includes(row.language)&&row.usage==='standard'));
    assert.ok(record.displayTimeline[0].fromYear===1801);
    assert.ok([...record.shortDescriptionKo].length<=28);
  }
});

test('batch 11 records all five verified western Balkan cities and exact dated historical display rules', () => {
  const records=json('reports/places/tier1-major-cities-batch11-western-balkans-capitals.json').records;
  assert.deepEqual(records.map(row=>row.geonameId),
    [3191281,3193044,785842,3183875,786714]);
  assert.deepEqual(records.map(row=>row.defaultDisplayNameKo),
    ['사라예보','포드고리차','스코페','티라나','프리슈티나']);
  assert.deepEqual(records.map(row=>row.sourceCountryCode),
    ['BA','ME','MK','AL','XK']);
  for (const record of records) {
    assert.equal(record.featureClass,'P',record.geonameId);
    assert.equal(record.featureCode,'PPLC',record.geonameId);
    assert.ok(record.names.some(n=>n.language==='ko'&&n.usage==='standard'&&n.text===record.defaultDisplayNameKo));
    assert.ok(record.names.some(n=>n.language==='en'&&n.usage==='standard'));
    assert.ok(record.names.some(n=>!['ko','en'].includes(n.language)&&n.usage==='standard'));
    assert.equal(record.displayTimeline[0].fromYear,1801);
    for(const row of record.displayTimeline) {
      assert.ok(record.names.some(n=>n.language==='ko'&&n.text===row.nameKo));
      assert.ok(['historicalLocalName','officialRenaming','koreanEditorialTranscription'].includes(row.kind));
    }
    for(const text of [
      record.shortDescriptionKo,
      ...record.names.map(n=>n.note),
      ...record.displayTimeline.map(n=>n.note),
      ...(record.historicalGeography?.events||[]).map(n=>n.detail),
      ...(record.historicalGeography?.notes||[]).map(n=>n.detail)
    ].filter(Boolean))assert.ok([...text].length<=28,record.geonameId+' screen copy too long: '+text);
  }
  const podgorica=records[1];
  assert.deepEqual(podgorica.displayTimeline.map(t=>[t.fromDate||t.fromYear,t.nameKo]),
    [[1801,'포드고리차'],['1946-07-13','티토그라드'],['1992-04-02','포드고리차']]);
  const labelAt=date=>{
    let chosen=podgorica.defaultDisplayNameKo;
    for(const change of podgorica.displayTimeline){
      const eligible=change.fromDate
        ?change.fromDate<=date
        :change.fromYear<=Number(date.slice(0,4));
      if(!eligible)break;
      chosen=change.nameKo;
    }
    return chosen;
  };
  for (const [date,label] of [
    ['1946-07-12','포드고리차'],['1946-07-13','티토그라드'],
    ['1992-04-01','티토그라드'],['1992-04-02','포드고리차']
  ])assert.equal(labelAt(date),label);
  assert.equal(records[4].historicalGeography.events[0].type,'kosovoIndependenceDeclaration');
  assert.equal(records[4].displayTimeline.length,1,'Political status change is not a city rename');
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
