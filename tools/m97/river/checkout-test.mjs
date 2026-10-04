// Exercise actual Git index-to-working-tree conversion in a fresh directory.
// Uses no commits, network, or changes to the application's checkout/index.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {sha256} from './oracle.mjs';

export function verifyRiverCheckout(root) {
  const files=new Set(['assets/geometry/polygon-clipping-0.15.7.js']);
  function collect(relative) {
    for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true})) {
      const name=relative+'/'+entry.name;
      if(entry.isDirectory())collect(name);else if(entry.isFile())files.add(name);
    }
  }
  for(const directory of ['assets/geometry/river','tests/fixtures/web-m972-river'])collect(directory);
  const manifest=JSON.parse(fs.readFileSync(path.join(root,'tests/fixtures/web-m972-river/manifest.json')));
  for(const source of manifest.sources)files.add(source.localPath);
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'m972-river-checkout-'));
  const staging=path.join(temporary,'index-source'),checkout=path.join(temporary,'clean-checkout');
  fs.mkdirSync(staging);fs.mkdirSync(checkout);
  const env={...process.env,HOME:temporary,XDG_CONFIG_HOME:path.join(temporary,'config'),
    GIT_CONFIG_NOSYSTEM:'1',GIT_ATTR_NOSYSTEM:'1',GIT_CONFIG_GLOBAL:path.join(temporary,'no-global-config')};
  for(const key of ['GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR','GIT_OBJECT_DIRECTORY','GIT_ALTERNATE_OBJECT_DIRECTORIES'])delete env[key];
  const git=(...args)=>{
    const child=spawnSync('git',['-C',staging,'-c','core.autocrlf=true','-c','core.safecrlf=false',...args],{encoding:'utf8',env});
    assert.equal(child.status,0,child.stderr);return child.stdout;
  };
  try {
    git('init','--quiet');
    for(const name of ['.gitattributes',...files]) {
      const destination=path.join(staging,name);fs.mkdirSync(path.dirname(destination),{recursive:true});
      fs.copyFileSync(path.join(root,name),destination);
    }
    git('add','--all');
    git('checkout-index','--all','--prefix='+checkout+path.sep);
    for(const name of files) {
      assert.equal(sha256(fs.readFileSync(path.join(checkout,name))),sha256(fs.readFileSync(path.join(root,name))),
        name+' changed bytes in a clean core.autocrlf=true checkout');
    }
    const attributes=git('check-attr','-z','text','eol','--',...files).split('\0');
    for(let i=0;i+2<attributes.length;i+=3)
      assert.equal(attributes[i+2],attributes[i+1]==='eol'?'lf':'set',attributes[i]+': '+attributes[i+1]);
    return files.size;
  } finally {fs.rmSync(temporary,{recursive:true,force:true});}
}
