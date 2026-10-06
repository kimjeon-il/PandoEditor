import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {verifySourceOrderSuite,verifySourceOrderReport} from './m974-snap-boundary/source-order-v2.mjs';
const digest=b=>createHash('sha256').update(b).digest('hex');
const nativeKeys=['schema','version','case','scope','sourceGeometries','restoredSourceGeometries','warm','afterRestore','transitions','canonicalEqualAfterUndo','beforeDocumentSha256','afterDocumentSha256','beforeDocumentBytesBase64','afterDocumentBytesBase64'];
function assertNativeProjectFormat(document){
 assert.equal(document.format,'pandoeditor-project');assert.ok(document.version===9||document.version===10,'supported native project version (9 or 10)');
 if(document.version===9){assert.ok(!Object.hasOwn(document,'geometryProvenance'),'native v9 must not carry the v10 provenance marker');return;}
 const provenance=document.geometryProvenance;assert.ok(provenance&&typeof provenance==='object'&&!Array.isArray(provenance),'native v10 provenance object required');
 assert.deepEqual(Object.keys(provenance).sort(),['schemaVersion','originalArchive','inlineAllocations','opaqueBaseline','opaqueUncertain'].sort(),'native v10 provenance format fields');
 assert.equal(provenance.schemaVersion,1,'native v10 provenance schema');
 for(const key of ['originalArchive','inlineAllocations','opaqueBaseline'])assert.ok(Array.isArray(provenance[key]),'native v10 provenance '+key+' array');
 assert.equal(typeof provenance.opaqueUncertain,'boolean','native v10 provenance uncertainty flag');
 // Ownership semantics remain the production codec's responsibility. The full
 // canonical-byte equality below still includes every ledger and archive row.
}
function canonical(native,side){
 const text=native[side+'DocumentBytesBase64'];assert.equal(typeof text,'string');assert.ok(text.length>0&&text.length<8*1024*1024,'native byte bound');
 const bytes=Buffer.from(text,'base64');assert.equal(bytes.toString('base64'),text,'native canonical base64');assert.equal(digest(bytes),native[side+'DocumentSha256'],'native canonical SHA');
 const doc=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));assertNativeProjectFormat(doc);
 const sources=doc.units.map(unit=>{assert.equal(unit.kind,'general');const bindings=doc.timelineRecords.geometryBindings.filter(r=>r.entityId===unit.id&&r.validFrom===null&&r.validTo===null);assert.equal(bindings.length,1,'one actual static geometry binding');const ref=bindings[0].geometryRef,geometries=doc.geometries.filter(g=>g.id===ref.id&&g.version===ref.version);assert.equal(geometries.length,1,'one referenced immutable geometry');return {id:unit.id,geometry:geometries[0].geojson};});
 return {bytes,sources};
}
export function compareSourceOrder(web,native){
 const result={schema:'pando-m974-source-order-comparison',version:1,passed:false,rawParity:false,fullWebUndoParity:false,scope:'shared first snapped vertex owner/coordinate across distinct client-Worker and controller-history stimuli',differences:[]};
 try{
  assert.deepEqual(Object.keys(native).sort(),nativeKeys.toSorted(),'complete native observation');assert.equal(native.schema,'pando-m974-native-controller-source-order');assert.equal(native.version,1);assert.equal(native.scope,'native-controller-delete-undo');assert.equal(web.case,'source-order-v2-coincident-remove-restore-a');assert.equal(native.case,web.case);
  const before=canonical(native,'before'),after=canonical(native,'after');assert.deepEqual(before.bytes,after.bytes,'actual Undo canonical bytes');assert.equal(native.canonicalEqualAfterUndo,true);
  const sources=web.input.features.map(({id,geometry})=>({id,geometry}));assert.deepEqual(native.sourceGeometries,sources,'same input source geometry');assert.deepEqual(native.restoredSourceGeometries,sources,'restored source geometry');assert.deepEqual(before.sources,sources,'before source references');assert.deepEqual(after.sources,sources,'after source references');
  assert.deepEqual(native.transitions,{deleteStarted:true,deleteApplied:true,undoAvailable:true,undoApplied:true,revisionBefore:0,revisionAfterDelete:1,revisionAfterUndo:2,submittedBeforeDelete:1,submittedAfterUndo:1},'actual delete/Undo with no intervening query');
  for(const stage of ['warm','afterRestore']){
   const candidate=web.stages[stage].candidates[0],indicator=native[stage].indicator;assert.equal(candidate.kind,'vertex');assert.equal(indicator.kind,'vertex');assert.deepEqual(indicator.ownerIds,web.stages[stage].firstCandidateOwnerIds,'actual first vertex owner');assert.deepEqual(indicator.ownerIds,candidate.ownerIds,'candidate/owner receipt');assert.deepEqual(indicator.coordinate,candidate.coordinate,'raw vertex coordinate');assert.deepEqual(indicator.coordinate,web.input.query.coordinate,'common query coordinate');assert.equal(indicator.nodeKey,candidate.nodeKey,'raw vertex key');assert.equal(indicator.segmentKey,null);assert.equal(indicator.segmentEndpoints,null);
  }
  result.passed=true;
 }catch(error){result.differences.push(error.message);}
 return result;
}
export function verifyIdentity(suite,report,options){
 assert.match(options.expectedCommit??'',/^[a-f0-9]{40}$/,'exact expected commit');assert.match(options.expectedRunId??'',/^\d+$/,'exact expected run');
 for(const row of [suite,suite.identity,report.identity]){assert.equal(row?.commit,options.expectedCommit,'capture commit');assert.equal(row?.runId,options.expectedRunId,'capture run');}
}
export function parseArguments(args){
 const names={'--browser-directory':'browserDirectory','--native-probe':'nativeProbe','--output':'output','--expected-commit':'expectedCommit','--expected-run-id':'expectedRunId'},options={};
 for(let i=0;i<args.length;i+=2){const name=names[args[i]];assert.ok(name&&args[i+1]&&!Object.hasOwn(options,name),'unknown/missing/duplicate argument');options[name]=args[i+1];}
 for(const name of Object.values(names))assert.ok(options[name],'missing '+name);assert.match(options.expectedCommit,/^[a-f0-9]{40}$/);assert.match(options.expectedRunId,/^\d+$/);return options;
}
function readCapture(options){
 const read=name=>fs.readFileSync(path.join(options.browserDirectory,name)),pin=JSON.parse(read('suite-pin.json')),suiteBytes=gunzipSync(read('suite.json.gz'),{maxOutputLength:16*1024*1024});assert.equal(digest(suiteBytes),pin.decompressedSha256,'suite byte hash');assert.equal(suiteBytes.length,pin.decompressedBytes);
 const suite=verifySourceOrderSuite(JSON.parse(suiteBytes));assert.deepEqual(suite.identity,pin.identity);const bytes=read('browser-report.json'),transfer=JSON.parse(read('report-transfer.json'));assert.equal(digest(bytes),transfer.sha256,'report byte hash');assert.equal(bytes.length,transfer.bytes);assert.deepEqual(gunzipSync(read('browser-report.json.gz'),{maxOutputLength:8*1024*1024}),bytes,'gzip report identity');
 const report=verifySourceOrderReport(suite,JSON.parse(bytes));verifyIdentity(suite,report,options);assert.deepEqual(JSON.parse(read('source-order-v2-observations.json')),report.cases,'exact exported observations');assert.equal(report.cases.length,1);
 return {report,reportSha256:digest(bytes),suiteSha256:digest(suiteBytes)};
}
export function runGate(options){
 fs.mkdirSync(options.output,{recursive:true});const receiptPath=path.join(options.output,'comparison.json');fs.writeFileSync(receiptPath,JSON.stringify({passed:false,rawParity:false,state:'running'}));
 try{
  const capture=readCapture(options),nativeFile=path.resolve(options.output,'native-observations.json');if(fs.existsSync(nativeFile))fs.unlinkSync(nativeFile);
  const binary=fs.readFileSync(options.nativeProbe),binarySha=digest(binary);const run=spawnSync(path.resolve(options.nativeProbe),['deleteUndoWithoutPointerPreservesWorkerInsertionOrder','-o','-,txt'],{env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software',PANDO_M974_ORDER_EVIDENCE:nativeFile},encoding:'utf8',timeout:120000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(options.output,'native.stdout'),run.stdout??'');fs.writeFileSync(path.join(options.output,'native.stderr'),run.stderr??'');assert.ifError(run.error);assert.equal(run.status,0,'actual native controller test must complete');assert.equal(digest(fs.readFileSync(options.nativeProbe)),binarySha,'native binary changed during capture');
  const nativeBytes=fs.readFileSync(nativeFile),native=JSON.parse(nativeBytes),receipt=compareSourceOrder(capture.report.cases[0],native);
  const dependencies=['./m974-native-source-order.mjs','./m974-snap-boundary/source-order-v2.mjs','./m974-snap-boundary/sources.mjs'];receipt.provenance={commit:options.expectedCommit,runId:options.expectedRunId,ciIdentityVerified:process.env.GITHUB_ACTIONS==='true'&&process.env.GITHUB_SHA===options.expectedCommit&&process.env.GITHUB_RUN_ID===options.expectedRunId,browserReportSha256:capture.reportSha256,suiteSha256:capture.suiteSha256,nativeBinarySha256:binarySha,nativeObservationsSha256:digest(nativeBytes),comparatorHashes:Object.fromEntries(dependencies.map(name=>[name,digest(fs.readFileSync(new URL(name,import.meta.url)))]))};
  if(process.env.GITHUB_ACTIONS==='true')assert.equal(receipt.provenance.ciIdentityVerified,true,'same-workflow identity required');fs.writeFileSync(receiptPath,JSON.stringify(receipt,null,2)+'\n');return receipt;
 }catch(error){fs.writeFileSync(receiptPath,JSON.stringify({passed:false,rawParity:false,error:error.message},null,2)+'\n');throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const result=runGate(parseArguments(process.argv.slice(2)));console.log(JSON.stringify(result));if(!result.passed)process.exitCode=1;}catch(error){console.error(error);process.exitCode=1;}}
