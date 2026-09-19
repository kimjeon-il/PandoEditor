import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { boundaryTouchesGeometry, removeTerritorialUnits, territorialDeletionAllowed } from '../tests/fixtures/web-structure/source/territorial-interaction-policy.js';
import { changeParent, changeSovereign, changeUnitType, createTerritorialFeature } from '../tests/fixtures/web-structure/source/territorial-units.js';

const root = new URL('../', import.meta.url);
const fixture = new URL('tests/fixtures/web-structure/', root);
const source = new URL('source/', fixture);
const manifest = JSON.parse(fs.readFileSync(new URL('manifest.json', fixture)));
for (const [name, expected] of Object.entries(manifest.blobs)) {
  const bytes = fs.readFileSync(new URL(name, source));
  const blob = Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]);
  assert.equal(crypto.createHash('sha1').update(blob).digest('hex'), expected, `pinned source modified: ${name}`);
}

const polygon = { type: 'Polygon', coordinates: [[[0,0],[10,0],[10,10],[0,10],[0,0]]] };
const multiPolygon = { type: 'MultiPolygon', coordinates: [polygon.coordinates, [[[20,20],[24,20],[24,24],[20,24],[20,20]]]] };
const unit = (id, parentId = '', locked = false, unitType = 'subunit') => ({ id, geometry: polygon, properties: { unitType, parentId, sovereignId: 'A', locked } });
const groups = {
  parent: [
    { case:'country parent',kind:'parent',unit:unit('S'),parentId:'A' },
    { case:'nested subunit',kind:'parent',unit:unit('S'),parentId:'P' }
  ],
  sovereign: [{ case:'region sovereign',kind:'sovereign',unit:unit('R','','false','region'),sovereignId:'B' }],
  containment: [
    { case:'boundary touch',kind:'boundary',geometry:polygon,point:[0,5] },
    { case:'interior',kind:'boundary',geometry:polygon,point:[5,5] },
    { case:'multipolygon island boundary',kind:'boundary',geometry:multiPolygon,point:[24,22] }
  ],
  delete: [
    { case:'unlocked leaf',kind:'delete',targets:['S'],units:[unit('S')] },
    { case:'locked leaf',kind:'delete',targets:['S'],units:[unit('S','',true)] },
    { case:'base child',kind:'delete',targets:['P'],units:[unit('P'),unit('S','P')] },
    { case:'child-first mixed delete',kind:'delete',targets:['S','P'],units:[unit('P'),unit('S','P')] },
    { case:'multi atomicity',kind:'delete',targets:['S','L'],units:[unit('S'),unit('L','',true)] }
  ],
  create: [
    { case:'subunit defaults',kind:'create',unitType:'subunit',parentId:'A',sovereignId:'A',coverageMode:'partition' },
    { case:'region explicit',kind:'create',unitType:'region',parentId:'',sovereignId:'',coverageMode:'explicit' }
  ],
  'convert-preview': [
    { case:'country to subunit',kind:'convert',unit:unit('A','','false','country'),targetType:'subunit' },
    { case:'subunit to country',kind:'convert',unit:unit('S'),targetType:'country' }
  ],
  'transfer-plan': [{ case:'transfer rewires ownership',kind:'transfer',unit:unit('S','A'),destinationId:'B' }],
  'reference-rewrite': [{
    case:'delete cleans known retained references',kind:'reference-delete',targetId:'S',territorialMode:'territorial',
    state:{
      territorialUnits:[unit('S'),unit('R','','false','region')],
      territorialRelations:[{unitId:'S',parentId:'A'},{unitId:'R',parentId:''}],
      distributionEntries:[{mode:'territorial',territorialUnitId:'S',share:25},{mode:'country',territorialUnitId:'S',share:75}],
      itemVisibility:{subunits:{S:false,keep:true},regions:{S:false}},
      labelSettings:{'subunit:S':{pinned:true},'territorial:subunit:S':{pinned:false},'other:S':{pinned:true}}
    }
  }]
};

function webResult(row) {
  if (row.kind === 'delete') return territorialDeletionAllowed(row.targets.map(id => row.units.find(item => item.id === id)), row.units);
  if (row.kind === 'parent') return changeParent(row.unit, row.parentId).properties.parentId;
  if (row.kind === 'sovereign') return changeSovereign(row.unit, row.sovereignId).properties.sovereignId;
  if (row.kind === 'boundary') return boundaryTouchesGeometry(row.geometry, row.point);
  if (row.kind === 'create') {
    const feature = createTerritorialFeature({ id:'N', unitType:row.unitType, name:'N', geometry:polygon, parentId:row.parentId, sovereignId:row.sovereignId, coverageMode:row.coverageMode });
    return { unitType:feature.properties.unitType, parentId:feature.properties.parentId, sovereignId:feature.properties.sovereignId, coverageMode:feature.properties.coverageMode };
  }
  if (row.kind === 'convert') return changeUnitType(row.unit, row.targetType).properties.unitType;
  if (row.kind === 'transfer') {
    const moved = changeSovereign(changeParent(row.unit, row.destinationId), row.destinationId);
    return { parentId:moved.properties.parentId, sovereignId:moved.properties.sovereignId };
  }
  if (row.kind === 'reference-delete') {
    const state=structuredClone(row.state);
    removeTerritorialUnits(state,new Set([row.targetId]),row.territorialMode);
    return state;
  }
  throw new Error(`unhandled oracle kind: ${row.kind}`);
}

const requested = process.argv.includes('--group') ? process.argv[process.argv.indexOf('--group') + 1] : null;
const cases = requested ? (groups[requested] || []) : Object.values(groups).flat();
assert.ok(cases.length, `unknown or empty group: ${requested}`);
const probe = process.argv[2];
assert.ok(probe, 'usage: node tools/m33-structure-oracle.mjs <structure_probe> [--group name]');
const child = spawnSync(probe, [], { input:JSON.stringify(cases), encoding:'utf8' });
assert.equal(child.status, 0, child.stderr || `probe exited ${child.status}`);
const actual = JSON.parse(child.stdout);
const expected = cases.map(row => ({ case:row.case, supported:true, result:webResult(row) }));
assert.deepEqual(actual, expected, 'web/native structure differences');
console.log(`Pinned source ${manifest.commit}: ${Object.keys(manifest.blobs).length} blob hashes verified; ${cases.length} actual web/native cases matched.`);
