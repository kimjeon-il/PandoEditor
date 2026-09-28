import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {test} from 'node:test';

const executable=process.env.M72_BOUNDS_PROBE ?? '/tmp/m72-geobounds-probe';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)),'tests/fixtures/world-rendering');
const features=[
  ...JSON.parse(readFileSync(`${root}/countries.geojson`)).features,
  ...JSON.parse(readFileSync(`${root}/sentinels.geojson`)).features,
];

function parts(geometry) {
  return geometry.type==='Polygon'?[geometry.coordinates]:geometry.coordinates;
}

function query(feature) {
  const polygons=parts(feature.geometry);
  const input=[String(polygons.length)];
  for(const polygon of polygons) {
    const outer=polygon[0];input.push(String(outer.length));
    for(const point of outer)input.push(`${point[0]} ${point[1]}`);
  }
  const run=spawnSync(executable,[],{input:input.join('\n')+'\n',encoding:'utf8',maxBuffer:1024*1024});
  assert.equal(run.status,0,`${feature.id}: ${run.stderr || run.error}`);
  const values=run.stdout.trim().split(' ').map(Number);
  assert.equal(values.length,6);
  const [west,south,east,north,wrapped,count]=values;
  assert.equal(count,polygons.length);
  return {west,south,east,north,wrapped:wrapped===1,polygons};
}

function includesLongitude(bounds,raw) {
  const longitude=raw>=-180&&raw<180?raw:
    raw===180?-180:((raw+180)%360+360)%360-180;
  if(!bounds.wrapped&&bounds.west===-180&&bounds.east===180)return true;
  if(longitude===-180&&bounds.east===180)return true;
  return bounds.wrapped?longitude>=bounds.west||longitude<=bounds.east:
    longitude>=bounds.west&&longitude<=bounds.east;
}

test('all M7.1 outer vertices remain covered by their geographic bounds', () => {
  for(const feature of features) {
    const bounds=query(feature);
    for(const polygon of bounds.polygons)for(const [lon,lat] of polygon[0]) {
      assert.ok(includesLongitude(bounds,lon),`${feature.id}: longitude ${lon} omitted`);
      assert.ok(lat>=bounds.south&&lat<=bounds.north,`${feature.id}: latitude ${lat} omitted`);
    }
  }
});

test('real dateline, polar, and synthetic control cases carry their risk envelopes', () => {
  const byId=Object.fromEntries(features.map(feature=>[feature.id,query(feature)]));
  assert.equal(byId.DEU.wrapped,false);
  for(const id of ['RUS','FJI','KIR','DATELINE'])assert.equal(byId[id].wrapped,true,id);
  for(const id of ['ATA','POLAR']) {
    assert.equal(byId[id].west,-180,id);
    assert.equal(byId[id].east,180,id);
  }
  assert.equal(byId.DATELINE.west,179);
  assert.equal(byId.DATELINE.east,-179);
});
