/** Lossless deterministic gzip storage for immutable observation byte streams. */
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync} from 'node:fs';
import {gzipSync,gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
const root=fileURLToPath(new URL('../../tests/fixtures/web-m973-split/',import.meta.url));
export const splitObservationPins=Object.freeze({
 'lifecycle-observations':'b29f65be6069b98925d4c9a32c11794c723517ad52bd3dc842f6dff1fa2b7f5d',
 'lifecycle-observations-v2':'d2864f47e517684dc7a051ee41ac3220d01d72ba56d5277e97a6baa764460534',
 'presentation-observations':'002c31e980e7e9c978ff8651d87366f9d3ec0818f0a62843004008752f0562ea',
});
const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const gzip=bytes=>gzipSync(bytes,{level:9,mtime:0});
export function readSplitObservationBytes(name='lifecycle-observations-v2'){
 assert.ok(Object.hasOwn(splitObservationPins,name),'Unknown split observation identity');
 const manifest=JSON.parse(readFileSync(resolve(root,'observation-manifest.json')));
 assert.equal(manifest.schema,'pando-split-observation-storage');assert.equal(manifest.version,1);assert.deepEqual(manifest.observations.map(row=>row.name),Object.keys(splitObservationPins),'Missing, duplicate or reordered observation identities');
 const entry=manifest.observations.find(row=>row.name===name);assert.ok(entry,'Missing observation manifest row');
 assert.equal(entry.path,name+'.json.gz');assert.equal(entry.uncompressedSha256,splitObservationPins[name]);
 const stored=readFileSync(resolve(root,entry.path));assert.equal(stored.length,entry.bytes);assert.equal(sha256(stored),entry.sha256);
 const bytes=gunzipSync(stored);assert.equal(bytes.length,entry.uncompressedBytes);assert.equal(sha256(bytes),splitObservationPins[name],'Immutable decompressed observation changed');
 return bytes;
}
export function readSplitObservation(name='lifecycle-observations-v2'){return JSON.parse(readSplitObservationBytes(name));}
export function packSplitObservations({check=false}={}){
 const observations=[];
 for(const [name,pin]of Object.entries(splitObservationPins)){
  const plain=resolve(root,name+'.json'),compressed=resolve(root,name+'.json.gz');
  const bytes=existsSync(plain)?readFileSync(plain):gunzipSync(readFileSync(compressed));
  assert.equal(sha256(bytes),pin,'Refuse to repack altered original bytes: '+name);
  const packed=gzip(bytes),parsed=JSON.parse(bytes);
  assert.deepEqual(gunzipSync(packed),bytes,'Gzip roundtrip must be byte-exact');
  if(check)assert.deepEqual(readFileSync(compressed),packed,'Deterministic gzip drift: '+name);else writeFileSync(compressed,packed);
  observations.push({name,path:name+'.json.gz',encoding:'gzip',bytes:packed.length,sha256:sha256(packed),uncompressedBytes:bytes.length,uncompressedSha256:pin,
   schema:parsed.schema,version:parsed.version,captureVersion:parsed.captureVersion||1,baseBehavioralCommit:parsed.baseBehavioralCommit,behavioralCommit:parsed.behavioralCommit,
   caseCount:parsed.cases.length,caseIds:parsed.cases.map(row=>row.case)});
 }
 const manifest=Buffer.from(JSON.stringify({schema:'pando-split-observation-storage',version:1,description:'Storage-only gzip; decoded bytes and all original observation values are immutable.',observations},null,2)+'\n');
 if(check)assert.deepEqual(readFileSync(resolve(root,'observation-manifest.json')),manifest,'Observation storage manifest drift');else writeFileSync(resolve(root,'observation-manifest.json'),manifest);
 return observations.map(({name,bytes,uncompressedBytes,uncompressedSha256})=>({name,bytes,uncompressedBytes,uncompressedSha256}));
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href)console.log(JSON.stringify(packSplitObservations({check:process.argv.includes('--check')}),null,2));
