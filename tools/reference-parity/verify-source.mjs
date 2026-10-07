import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const root=new URL('../../app/reference-web/',import.meta.url);
const manifest=JSON.parse(readFileSync(new URL('manifest.json',root)));
for(const file of manifest.files){
 const bytes=readFileSync(new URL(file.path.split('/').at(-1),root));
 assert.equal(createHash('sha256').update(bytes).digest('hex'),file.sha256);
 if(process.argv[2])assert.deepEqual(bytes,execFileSync('git',['-C',process.argv[2],'show',manifest.commit+':'+file.path]));
}
console.log(JSON.stringify({web:manifest.commit,verifiedFiles:manifest.files.length,mismatches:0}));
