import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import {compareCurrentWeb} from './live-web-parity.mjs';

const blobSha=bytes=>createHash('sha1').update('blob '+bytes.length+'\0').update(bytes).digest('hex');
const sample=()=>{
  const paths=[
    'reports/places/tier1-major-cities-batch01.json',
    'reports/places/historical-display-policy.json'
  ];
  const contents=new Map(paths.map((path,index)=>[path,Buffer.from('fixture-'+index)]));
  const manifest={files:paths.map(path=>({path,gitBlobSha:blobSha(contents.get(path))}))};
  const upstream=paths.map(path=>({type:'file',name:path.split('/').at(-1),sha:blobSha(contents.get(path))}));
  return {contents,manifest,upstream,read:path=>contents.get(path)};
};
test('matching public Web blob identities pass without a fixed commit number',()=>{
  const {manifest,upstream,read}=sample();
  const result=compareCurrentWeb(manifest,upstream,read);
  assert.deepEqual(result.errors,[]);
  assert.equal(result.batchCount,1);
});
test('changed upstream JSON and newly added batches are detected',()=>{
  const {manifest,upstream,read}=sample();
  upstream[0].sha=blobSha(Buffer.from('new upstream'));
  upstream.push({type:'file',name:'tier1-major-cities-batch02-test.json',
    sha:blobSha(Buffer.from('new batch'))});
  const result=compareCurrentWeb(manifest,upstream,read);
  assert.ok(result.errors.some(e=>e.includes('Stale Web source')));
  assert.ok(result.errors.some(e=>e.includes('New upstream review batch')));
});
test('local bytes inconsistent with the pinned source fail independently of upstream',()=>{
  const {manifest,upstream}=sample();
  const result=compareCurrentWeb(manifest,upstream,()=>Buffer.from('corrupted'));
  assert.ok(result.errors.some(e=>e.includes('Local mirror differs from pinned Git blob')));
});
