import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readdirSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const web=resolve(process.argv[2]||'../map editor');
const native=fileURLToPath(new URL('../assets/defaults/flags/',import.meta.url));
const nativeRepo=resolve(native,'../../..');
const revision='c09927e63705529bbf59ca6684cd9b23225dddad';
const families={native:`assets/vendor/country-flags/${revision}/svg`,
    legacy:'assets/vendor/flag-icons/7.5.0/flags/4x3',political:'assets/vendor/political-flags'};
const pairs=Object.entries(families).flatMap(([family,folder])=>
    readdirSync(resolve(native,family)).filter(name=>name.endsWith('.svg')).map(name=>({family,folder,name})));
const gitBlobs=(repo,paths)=> {
    // Two batch reads avoid hundreds of Windows Git process startups.
    const output=execFileSync('git',['-C',repo,'cat-file','--batch'],{
        input:paths.map(path=>`HEAD:${path}\n`).join(''),maxBuffer:32*1024*1024});
    let offset=0;
    return paths.map(path=> {
        const end=output.indexOf(10,offset);const header=output.subarray(offset,end).toString().split(' ');
        assert.equal(header[1],'blob',`missing Git blob ${path}`);
        const size=Number(header[2]);offset=end+1;const bytes=output.subarray(offset,offset+size);offset+=size+1;
        return bytes;
    });
};
const ours=gitBlobs(nativeRepo,pairs.map(({family,name})=>`assets/defaults/flags/${family}/${name}`));
const theirs=gitBlobs(web,pairs.map(({folder,name})=>`${folder}/${name}`));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
pairs.forEach(({family,folder,name},i)=> {
    assert.equal(ours[i].length,theirs[i].length,`${family}/${name} byte count`);
    assert.equal(sha(ours[i]),sha(theirs[i]),`${family}/${name} SHA-256`);
    assert.deepEqual(readFileSync(resolve(native,family,name)),readFileSync(resolve(web,folder,name)),
        `${family}/${name} local checkout bytes (including line endings)`);
});
assert.equal(pairs.length,241,'complete bundled flag corpus');
const current=execFileSync('git',['-C',web,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
const upstream=execFileSync('git',['-C',web,'show','HEAD:assets/js/modules/territorial-label-flags.js'],{encoding:'utf8'});
const pinned=readFileSync(new URL('../tests/fixtures/web-hydro/source/country-label-flags.js',import.meta.url),'utf8');
assert.equal(upstream.replaceAll('Territorial','Country').replaceAll('territorial','country').trim(),pinned.replace(/\r\n/g,'\n').trim(),
    'current web flag algorithm diverged from the pinned executable oracle');
console.log(`${pairs.length} flag files match byte-for-byte in Git and in local checkouts; current web flag algorithm matches pinned oracle; web ${current}`);
