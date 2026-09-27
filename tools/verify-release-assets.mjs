#!/usr/bin/env node
// Source-tree integrity only. APK/Windows deployed paths need platform evidence.
import {spawnSync} from 'node:child_process';
import {existsSync,readFileSync,readdirSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';

const rootIndex=process.argv.indexOf('--root');
const root=resolve(rootIndex<0?'.':process.argv[rootIndex+1]);
const outIndex=process.argv.indexOf('--out');
const output=outIndex<0?null:process.argv[outIndex+1];
const checks=[];
function check(name,ok,detail='') {checks.push({name,status:ok?'passed':'failed',detail});}
function run(name,script,arg) {
  const result=spawnSync(process.execPath,[join(root,'tools',script),arg],{cwd:root,encoding:'utf8'});
  check(name,result.status===0,(result.stdout+' '+result.stderr).trim().slice(-800));
}
try {
  run('world assets','verify-world-assets.mjs','assets/world');
  run('M7.1 corpus','verify-m71-world-corpus.mjs','tests/fixtures/world-rendering');
  const cmake=readFileSync(join(root,'app/CMakeLists.txt'),'utf8');
  const world=JSON.parse(readFileSync(join(root,'assets/world/manifest.json'),'utf8'));
  for(const [key,asset] of Object.entries(world)) {
    if(!asset||typeof asset!=='object'||typeof asset.path!=='string')continue;
    const absolute=join(root,'assets/world',asset.path);
    check(`resource ${key}`,existsSync(absolute)&&cmake.includes(`assets/world/${asset.path}`),asset.path);
  }
  for(const stage of ['fill','stroke','point'])for(const extension of ['vert','frag']) {
    const path=`renderer/shaders/${stage}.${extension}`;
    check(`shader ${stage}.${extension}`,existsSync(join(root,path))&&cmake.includes(path),path);
  }
  const flags=join(root,'assets/defaults/flags');
  const required=['NOTICE.political-flags.txt','NOTICE.country-flags.txt','LICENSE.flag-icons.txt','country-flags.js'];
  check('flag license and notices',required.every(name=>existsSync(join(flags,name))));
  check('native flag resources',existsSync(join(flags,'native'))&&
    readdirSync(join(flags,'native')).some(name=>name.endsWith('.svg'))&&cmake.includes('default_flags'));
  check('earcut license',existsSync(join(root,'third_party/earcut/LICENSE')));
  const failed=checks.filter(item=>item.status==='failed').length;
  const report={schema:'pandoeditor-release-assets',version:1,generatedAt:new Date().toISOString(),
    scope:'source tree only',status:failed?'failed':'source-verified',checks,
    windowsDeployedPackage:'NOT RUN',androidApkOrAab:'NOT RUN'};
  if(output)writeFileSync(resolve(output),JSON.stringify(report,null,2)+'\n');
  console.log(`Release source assets: ${checks.length-failed} checks passed, ${failed} failed; deployed packages NOT RUN`);
  if(failed)process.exitCode=1;
}catch(error){console.error(`Release asset check failed: ${error.message}`);process.exitCode=1;}
