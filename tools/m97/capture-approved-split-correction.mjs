// Reproducible discovery capture. Chromium acceptance is intentionally separate.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {loadSplitCandidateModules} from './web-split-candidate.mjs';
import {splitCaseIds,splitCaseDefinition,runSplitLifecycleCase} from './web-split.mjs';
export async function captureApprovedCorrection(candidateRoot) {
 const manifest=JSON.parse(fs.readFileSync(path.join(candidateRoot,'manifest.json')));
 assert.match(manifest.behavioralCommit,/^[a-f0-9]{40}$/);assert.match(manifest.tree,/^[a-f0-9]{40}$/);
 const loaded=await loadSplitCandidateModules({candidateRoot,candidateChanges:manifest.changes});
 try {
  const definitions=[...splitCaseIds.map(splitCaseDefinition),...JSON.parse(fs.readFileSync(path.join(candidateRoot,'case-definitions.json')))],cases=[];
  for(const definition of definitions)cases.push(await runSplitLifecycleCase(loaded,definition));
  return {schema:'pando-approved-split-lifecycle-observations',version:1,baseBehavioralCommit:manifest.baseBehavioralCommit,behavioralCommit:manifest.behavioralCommit,tree:manifest.tree,sourceChanges:manifest.changes,observationMode:'Actual production entry/worker/store/history under Node; actual Chromium acceptance is a separate gate.',runtime:{node:process.version,v8:process.versions.v8},cases};
 }finally{loaded.cleanup();}
}
export function observationParts(capture,maxCompressedBytes=180000) {
 assert.ok(Number.isSafeInteger(maxCompressedBytes)&&maxCompressedBytes>0);
 const {cases,...common}=capture;assert.ok(Array.isArray(cases)&&cases.length);
 const encode=rows=>Buffer.from(JSON.stringify({...common,cases:rows},null,2)+'\n');
 const groups=[];let group=[];
 for(const row of cases){if(group.length&&gzipSync(encode([...group,row]),{level:9,mtime:0}).length>maxCompressedBytes){groups.push(group);group=[];}group.push(row);}
 if(group.length)groups.push(group);
 return groups.map((rows,index)=>{const raw=encode(rows),bytes=gzipSync(raw,{level:9,mtime:0});assert.ok(bytes.length<=maxCompressedBytes,'One case exceeds bounded publication size');return {path:`observations-${index+1}.json.gz`,raw,bytes,caseIds:rows.map(row=>row.case)};});
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 const directory=path.resolve(process.argv[2]||'tests/fixtures/web-m973-split/corrections/dateline');
 const capture=await captureApprovedCorrection(directory),parts=observationParts(capture),sha=b=>createHash('sha256').update(b).digest('hex');
 for(const part of parts)fs.writeFileSync(path.join(directory,part.path),part.bytes);
 fs.writeFileSync(path.join(directory,'observation-manifest.json'),JSON.stringify({schema:'pando-approved-split-observation-storage',version:2,encoding:'gzip-json-parts',behavioralCommit:capture.behavioralCommit,tree:capture.tree,parts:parts.map(row=>({path:row.path,bytes:row.bytes.length,sha256:sha(row.bytes),uncompressedBytes:row.raw.length,uncompressedSha256:sha(row.raw),caseIds:row.caseIds})),caseIds:capture.cases.map(row=>row.case)},null,2)+'\n');
 console.log(`Captured ${capture.cases.length} actual cases in ${parts.length} bounded gzip parts.`);
}
