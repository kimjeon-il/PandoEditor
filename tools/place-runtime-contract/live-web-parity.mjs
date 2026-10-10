#!/usr/bin/env node
// Verify the native review mirror against the current Web work/places branch.
// The pinned SHA-only snapshot test is intentionally separate from this live-drift gate.
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {resolve,relative} from 'node:path';
import {fileURLToPath} from 'node:url';

const root=resolve(fileURLToPath(new URL('../../',import.meta.url)));
const manifest=JSON.parse(readFileSync(resolve(root,'reports/places/source-manifest.json'),'utf8'));
const upstreamUrl='https://api.github.com/repos/kimjeon-il/Pando/contents/reports/places?ref=work%2Fplaces';
const sha1=bytes=>createHash('sha1').update('blob '+bytes.length+'\0').update(bytes).digest('hex');

export function compareCurrentWeb(registered,upstream,readLocal) {
  const files=new Map();
  for(const row of upstream) {
    if(row.type==='file' && typeof row.name==='string' && /^[a-f0-9]{40}$/.test(row.sha))
      files.set('reports/places/'+row.name,row.sha);
  }
  const mirrored=new Set(registered.files.map(row=>row.path));
  const errors=[];
  for(const row of registered.files) {
    const current=files.get(row.path);
    if(!current){errors.push('Missing upstream file: '+row.path);continue;}
    if(current!==row.gitBlobSha)
      errors.push('Stale Web source: '+row.path+' (upstream '+current.slice(0,12)+', pinned '+row.gitBlobSha.slice(0,12)+')');
    const local=sha1(readLocal(row.path));
    if(local!==row.gitBlobSha)
      errors.push('Local mirror differs from pinned Git blob: '+row.path);
  }
  const upstreamBatches=[...files.keys()].filter(path=>/^reports\/places\/tier1-major-cities-batch\d{2}.*\.json$/u.test(path)).sort();
  const localBatches=[...mirrored].filter(path=>/^reports\/places\/tier1-major-cities-batch\d{2}.*\.json$/u.test(path)).sort();
  if(upstreamBatches.join('\n')!==localBatches.join('\n')) {
    errors.push('Batch inventory differs: upstream '+upstreamBatches.length+', mirror '+localBatches.length);
    for(const path of upstreamBatches)if(!mirrored.has(path))errors.push('New upstream review batch not mirrored: '+path);
  }
  return {errors,assetCount:registered.files.length,batchCount:upstreamBatches.length};
}

const called=process.argv[1] && resolve(process.argv[1])===fileURLToPath(import.meta.url);
if(called) {
  try {
    const token=process.env.GH_TOKEN;
    const response=await fetch(upstreamUrl,{
      headers:{
        'Accept':'application/vnd.github+json',
        'X-GitHub-Api-Version':'2022-11-28',
        ...(token?{'Authorization':'Bearer '+token}:{})
      },
      signal:AbortSignal.timeout(15000)
    });
    if(!response.ok)throw Error('Unable to read public Web work/places tree (GitHub HTTP '+response.status+')');
    const list=await response.json();
    if(!Array.isArray(list))throw Error('Unexpected GitHub contents response');
    const result=compareCurrentWeb(manifest,list,path=>{
      const absolute=resolve(root,path);
      if(relative(root,absolute).startsWith('..'))throw Error('Invalid mirrored path');
      return readFileSync(absolute);
    });
    if(result.errors.length) {
      for(const error of result.errors)console.error('PARITY: '+error);
      process.exitCode=1;
    } else {
      console.log('Current Web work/places parity OK: '+result.assetCount+
        ' mirrored assets, '+result.batchCount+' reviewed batches.');
    }
  } catch(error) {
    console.error('Live upstream parity check failed:',error);
    process.exitCode=1;
  }
}
