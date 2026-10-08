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

test('batch 12 has four independently verified central-European capital city IDs and source-linked names', () => {
  const records=json('reports/places/tier1-major-cities-batch12-central-eastern-europe-capitals.json').records;
  assert.deepEqual(records.map(r=>r.geonameId),[3060972,618426,2960316,3042030]);
  assert.deepEqual(records.map(r=>r.defaultDisplayNameKo),
    ['브라티슬라바','키시너우','룩셈부르크','파두츠']);
  assert.deepEqual(records.map(r=>r.sourceCountryCode),['SK','MD','LU','LI']);
  for(const record of records) {
    assert.equal(record.featureClass,'P',record.geonameId);
    assert.equal(record.featureCode,'PPLC',record.geonameId);
    assert.equal(record.displayTimeline[0].fromYear,1801);
    assert.ok(record.names.some(n=>n.language==='ko'&&n.text===record.defaultDisplayNameKo));
    assert.ok(record.names.some(n=>n.language==='en'&&n.usage==='standard'));
    assert.ok(record.names.some(n=>!['ko','en'].includes(n.language)&&n.usage==='standard'));
    for(const transition of record.displayTimeline)
      assert.ok(record.names.some(n=>n.language==='ko'&&n.text===transition.nameKo));
    for(const value of [record.shortDescriptionKo,
      ...record.names.map(n=>n.note),
      ...record.displayTimeline.map(n=>n.note),
      ...(record.historicalGeography?.events||[]).map(n=>n.detail),
      ...(record.historicalGeography?.notes||[]).map(n=>n.detail)].filter(Boolean))
      assert.ok([...value].length<=28,record.geonameId+' oversized UI text: '+value);
  }
  const bratislava=records[0];
  assert.deepEqual(bratislava.displayTimeline.map(x=>[x.fromYear||x.fromDate,x.nameKo]),
    [[1801,'프레스부르크'],['1919-03-27','브라티슬라바']]);
  const at=date=>{
    let name=bratislava.defaultDisplayNameKo;
    for(const transition of bratislava.displayTimeline) {
      const applies=transition.fromDate
        ?transition.fromDate<=date
        :transition.fromYear<=Number(date.slice(0,4));
      if(!applies)break;
      name=transition.nameKo;
    }
    return name;
  };
  assert.deepEqual([at('1919-03-26'),at('1919-03-27'),at('1920-01-01')],
    ['프레스부르크','브라티슬라바','브라티슬라바']);
  assert.equal(records[1].displayTimeline.length,1,'No political-sovereignty-derived Chisinau rename');
  assert.ok(records[1].names.some(n=>n.language==='ru'&&n.text==='Кишинёв'));
  assert.equal(records[2].names.find(n=>n.language==='lb'&&n.usage==='standard')?.text,'Lëtzebuerg');
  assert.equal(records[2].names.find(n=>n.language==='en'&&n.usage==='standard')?.text,'Luxembourg City');
  assert.equal(records[3].names.find(n=>n.language==='de'&&n.usage==='standard')?.text,'Vaduz');
  assert.equal(records[3].names.find(n=>n.language==='en'&&n.usage==='standard')?.text,'Vaduz');
});

test('batch 13 distinguishes five capital settlements from identically named sovereigns and administrative areas', () => {
  const records=json('reports/places/tier1-major-cities-batch13-european-microstates-mediterranean.json').records;
  assert.deepEqual(records.map(r=>r.geonameId),[3041563,3168070,2993458,2562305,146268]);
  assert.deepEqual(records.map(r=>r.defaultDisplayNameKo),
    ['안도라라베야','산마리노','모나코','발레타','니코시아']);
  assert.deepEqual(records.map(r=>r.sourceCountryCode),['AD','SM','MC','MT','CY']);
  for(const r of records) {
    assert.equal(r.featureClass,'P',r.geonameId);
    assert.equal(r.featureCode,'PPLC',r.geonameId);
    assert.equal(r.displayTimeline.length,1,'Do not fabricate a city rename from a sovereignty or administrative change');
    assert.equal(r.displayTimeline[0].fromYear,1801);
    assert.equal(r.displayTimeline[0].nameKo,r.defaultDisplayNameKo);
    assert.ok(r.names.some(n=>n.language==='ko'&&n.usage==='standard'&&n.text===r.defaultDisplayNameKo));
    assert.ok(r.names.some(n=>n.language==='en'&&n.usage==='standard'));
    assert.ok(r.names.some(n=>n.language!=='ko'&&n.language!=='en'&&n.usage==='standard'));
    for(const value of [r.shortDescriptionKo,
      ...r.names.map(n=>n.note),
      ...(r.historicalGeography?.events||[]).map(n=>n.detail),
      ...(r.historicalGeography?.notes||[]).map(n=>n.detail)].filter(Boolean))
      assert.ok([...value].length<=28,r.geonameId+' overly long display note: '+value);
  }
  assert.ok(records[1].historicalGeography.notes.some(n=>n.researchDetail?.includes('3168068')));
  assert.ok(records[2].historicalGeography.notes.some(n=>n.researchDetail?.includes('2993457')));
  assert.ok(records[3].historicalGeography.notes.some(n=>n.researchDetail?.includes('8334638')));
  assert.ok(records[4].historicalGeography.notes.some(n=>n.researchDetail?.includes('146267')));
  assert.ok(records[4].names.some(n=>n.language==='el'&&n.text==='Λευκωσία'));
  assert.ok(records[4].names.some(n=>n.language==='tr'&&n.text==='Lefkoşa'));
  assert.deepEqual(records[4].historicalGeography.events.map(e=>[e.date,e.type]),[
    ['1963-12-30','nicosiaGreenLineFirstEstablished'],['1974-08-16','cyprusCeasefireLines']
  ]);
  assert.ok(records[4].historicalGeography.notes.some(n=>n.researchDetail?.includes('기하')));
  assert.equal(records[1].historicalGeography.events[0].type,'traditionalFoundationLegend');
});

test('batch 14 validates five cities, excludes Vatican, and respects approximate 1936 name changes', () => {
  const batch=json('reports/places/tier1-major-cities-batch14-north-atlantic-anatolia-caucasus.json');
  const r=batch.records;
  assert.deepEqual(r.map(x=>x.geonameId),[3413829,323786,611717,616052,587084]);
  assert.deepEqual(r.map(x=>x.defaultDisplayNameKo),['레이캬비크','앙카라','트빌리시','예레반','바쿠']);
  assert.deepEqual(r.map(x=>x.sourceCountryCode),['IS','TR','GE','AM','AZ']);
  assert.ok(batch.selection.includes('Vatican City omitted'));
  assert.ok(r.every(x=>x.featureClass==='P'&&x.featureCode==='PPLC'));
  for(const city of r) {
    assert.ok(city.names.some(n=>n.language==='ko'&&n.text===city.defaultDisplayNameKo&&n.usage==='standard'));
    assert.ok(city.names.some(n=>n.language==='en'&&n.usage==='standard'));
    assert.ok(city.names.some(n=>!['ko','en'].includes(n.language)&&n.usage==='standard'));
    for(const step of city.displayTimeline)
      assert.ok(city.names.some(n=>n.language==='ko'&&n.text===step.nameKo));
    for(const value of [
      city.shortDescriptionKo,...city.names.map(n=>n.note),
      ...city.displayTimeline.map(n=>n.note),
      ...(city.historicalGeography?.events||[]).map(n=>n.detail),
      ...(city.historicalGeography?.notes||[]).map(n=>n.detail)
    ].filter(Boolean))assert.ok([...value].length<=28,city.geonameId+': '+value);
  }
  for(const [index,before,after] of [[2,'티플리스','트빌리시'],[3,'에리반','예레반']]) {
    const city=r[index];
    assert.deepEqual(city.displayTimeline.map(x=>[x.fromYear,x.nameKo]),[[1801,before],[1936,after]]);
    assert.ok(city.displayTimeline.every(x=>!x.fromDate),'Disputed 1936 date must be year-only');
    const nameAt=year=>city.displayTimeline.filter(x=>x.fromYear<=year).at(-1)?.nameKo;
    assert.equal(nameAt(1935),before);
    assert.equal(nameAt(1937),after);
  }
  assert.equal(r[1].historicalGeography.events.find(x=>x.type==='turkishCapitalDesignated').date,'1923-10-13');
  assert.equal(r[4].historicalGeography.events.find(x=>x.type==='azerbaijanGovernmentMovesFromGanja').date,'1918-09-17');
  assert.ok(r.filter((x,i)=>![2,3].includes(i)).every(x=>x.displayTimeline.length===1));
});

test('batch 15 central Asian capitals preserve real renamings, language forms, and modern-city start gates', () => {
  const records=json('reports/places/tier1-major-cities-batch15-central-asia-capitals.json').records;
  assert.deepEqual(records.map(r=>r.geonameId),
    [1526273,1512569,1528675,1221874,162183]);
  assert.deepEqual(records.map(r=>r.defaultDisplayNameKo),
    ['아스타나','타슈켄트','비슈케크','두샨베','아시가바트']);
  assert.deepEqual(records.map(r=>r.sourceCountryCode),['KZ','UZ','KG','TJ','TM']);
  for (const r of records) {
    assert.equal(r.featureClass,'P',r.geonameId);
    assert.equal(r.featureCode,'PPLC',r.geonameId);
    assert.equal(r.displayTimeline[0].fromYear,1801);
    assert.ok(r.names.some(n=>n.language==='ko'&&n.usage==='standard'&&n.text===r.defaultDisplayNameKo));
    assert.ok(r.names.some(n=>n.language==='en'&&n.usage==='standard'));
    assert.ok(r.names.some(n=>n.language!=='ko'&&n.language!=='en'&&n.usage==='standard'));
    for (const entry of r.displayTimeline) {
      assert.ok(r.names.some(n=>n.language==='ko'&&n.text===entry.nameKo),r.geonameId);
      assert.ok(['officialRenaming','historicalLocalName','koreanEditorialTranscription'].includes(entry.kind));
    }
    for (const t of [r.shortDescriptionKo,r.temporalEligibility?.note,
      ...r.names.map(n=>n.note),
      ...r.displayTimeline.map(n=>n.note),
      ...(r.historicalGeography?.events||[]).map(n=>n.detail),
      ...(r.historicalGeography?.notes||[]).map(n=>n.detail)].filter(Boolean))
      assert.ok([...t].length<=28,r.geonameId+' long UI text: '+t);
  }
  const at=(record,date)=>{
    if(record.temporalEligibility?.cityEstablishedFromYear &&
       Number(date.slice(0,4))<record.temporalEligibility.cityEstablishedFromYear)return null;
    let selected=record.defaultDisplayNameKo;
    for(const row of record.displayTimeline) {
      const eligible=row.fromDate?row.fromDate<=date:row.fromYear<=Number(date.slice(0,4));
      if(!eligible)break;
      selected=row.nameKo;
    }
    return selected;
  };
  const a=records[0];
  assert.deepEqual([
    at(a,'1829-12-31'),at(a,'1830-01-01'),at(a,'1862-01-01'),
    at(a,'1961-03-19'),at(a,'1961-03-20'),
    at(a,'1992-07-06'),at(a,'1998-05-06'),at(a,'2019-03-23'),
    at(a,'2022-09-18'),at(a,'2022-09-19')],
    [null,'아크몰라','아크몰린스크','아크몰린스크','첼리노그라드',
      '아크몰라','아스타나','누르술탄','누르술탄','아스타나']);
  assert.ok(a.displayTimeline.find(x=>x.fromDate==='2022-09-19')?.researchNote.includes('2022-09-17'));
  assert.deepEqual(records[2].displayTimeline.map(x=>[x.fromYear||x.fromDate,x.nameKo]),
    [[1801,'피슈페크'],['1926-05-12','프룬제'],['1991-02-05','비슈케크']]);
  assert.equal(at(records[2],'1867-12-31'),null);
  assert.equal(at(records[2],'1991-02-04'),'프룬제');
  assert.equal(at(records[2],'1991-02-05'),'비슈케크');
  assert.deepEqual(records[3].displayTimeline.map(x=>[x.fromYear||x.fromDate,x.nameKo]),
    [[1801,'두샨베'],[1929,'스탈리나바드'],[1961,'두샨베']]);
  assert.equal(at(records[3],'1950-06-15'),'스탈리나바드');
  assert.equal(at(records[3],'1961-07-01'),'두샨베');
  assert.deepEqual(records[4].displayTimeline.map(x=>[x.fromYear||x.fromDate,x.nameKo]),
    [[1801,'아슈하바트'],[1919,'폴토라츠크'],
      ['1927-04-07','아슈하바트'],['1991-10-27','아시가바트']]);
  assert.equal(at(records[4],'1927-04-06'),'폴토라츠크');
  assert.equal(at(records[4],'1927-04-07'),'아슈하바트');
  assert.equal(records[4].displayTimeline.at(-1).kind,'koreanEditorialTranscription');
  assert.equal(records[1].displayTimeline.length,1,'1930 capital transfer was not a rename');
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
