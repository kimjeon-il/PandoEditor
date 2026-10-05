import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const keys=(value,expected,label)=>assert.deepEqual(Object.keys(value).sort(),[...expected].sort(),label);
export const requiredNativeCases=Object.freeze(['no-query-retains','ready-boundary-worker-observes-deletion','pending-boundary-cancel-rebases-on-next-query','stopped-worker-ignores-root-delete-undo','settled-boundary-error-retains-history','split-setup-only','annex-setup-only','split-components-ready','annex-components-ready','component-timer-only-cancel','component-request-pending-cancel']);
function canonicalNative(row,side){
 const text=row[side+'DocumentBytesBase64'];assert.equal(typeof text,'string');assert.ok(text.length>0&&text.length<8*1024*1024,'bounded canonical observation');
 const bytes=Buffer.from(text,'base64');assert.equal(bytes.toString('base64'),text,'canonical base64');assert.equal(digest(bytes),row[side+'DocumentSha256'],'canonical hash');
 const document=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));assert.equal(document.format,'pandoeditor-project');assert.equal(document.version,9);
 const geometry=ref=>{const found=document.geometries.filter(item=>item.id===ref.id&&item.version===ref.version);assert.equal(found.length,1,'one immutable geometry reference');return found[0].geojson;};
 const sources=document.units.map(unit=>{const bindings=document.timelineRecords.geometryBindings.filter(item=>item.entityId===unit.id&&item.validFrom===null&&item.validTo===null);assert.equal(bindings.length,1,'one static geometry binding');return {domain:'territorial',id:unit.id,geometry:geometry(bindings[0].geometryRef)};});
 for(const feature of document.content.genericFeatures)sources.push({domain:'generic',id:feature.id,geometry:geometry(feature.geometryRef)});
 assert.equal(new Set(sources.map(row=>row.domain+':'+row.id)).size,sources.length,'unique source refs');return {bytes,sources};
}
function canonicalWeb(stage){
 assert.equal(typeof stage.canonical,'string');const document=JSON.parse(stage.canonical),sources=[...document.entities.map(feature=>({domain:'territorial',id:String(feature.id),geometry:feature.geometry})),...document.genericFeatures.map(feature=>({domain:'generic',id:String(feature.id),geometry:feature.geometry}))];
 assert.deepEqual(stage.sourceRows,sources.map(row=>row.domain+':'+row.id),'actual ordered source rows');return sources;
}
export function compareSourceHistoryCase(web,native){
 const result={case:native?.case,passed:false,rawParity:false,scope:'matched source geometry and selected snap indicator across declared workflow stimuli',queryContextEquivalent:false,selectedIndicatorOnly:true,differences:[]};
 try{
  keys(native,['case','scope','sourceGeometries','restoredSourceGeometries','warm','final','canonicalEqualAfterUndo','beforeDocumentBytesBase64','afterDocumentBytesBase64','beforeDocumentSha256','afterDocumentSha256','transitions'],'native case fields');
  assert.equal(web.input.nativeCase,native.case,'explicit immutable case pairing');assert.equal(native.scope,requiredNativeCases.slice(0,5).includes(native.case)?'native-controller-source-history':'native-controller-selection-source-history','native declared scope');
  const before=canonicalNative(native,'before'),after=canonicalNative(native,'after');assert.deepEqual(before.bytes,after.bytes,'exact canonical bytes after Undo');assert.equal(native.canonicalEqualAfterUndo,true);
  assert.equal(web.stages.restored.canonicalEqual,true);assert.equal(web.stages.warm.canonical,web.stages.restored.canonical,'web exact canonical restoration');
  assert.deepEqual(native.sourceGeometries,before.sources);assert.deepEqual(native.restoredSourceGeometries,after.sources);
  for(const stage of ['warm','restored','final'])assert.deepEqual(canonicalWeb(web.stages[stage]),before.sources,'same actual input geometry and source order '+stage);
  keys(native.transitions,['revisionBefore','revisionAfterDelete','revisionAfterUndo','submittedBeforeDelete','submittedAfterUndo','submittedAfterFinal'],'native transition fields');
  const t=native.transitions;for(const value of Object.values(t))assert.ok(Number.isSafeInteger(value)&&value>=0,'integer transitions');
  assert.equal(t.revisionAfterDelete,t.revisionBefore+1);assert.equal(t.revisionAfterUndo,t.revisionAfterDelete+(native.case==='stopped-worker-ignores-root-delete-undo'?3:1));
  assert.equal(t.submittedBeforeDelete,1);assert.equal(t.submittedAfterUndo,1,'no intervening native snap query');assert.equal(t.submittedAfterFinal,2);
  for(const name of ['warm','final']){
   keys(native[name],['indicator'],'native query fields');const stage=web.stages[name],candidate=stage.indicator,indicator=native[name].indicator;
   keys(indicator,['kind','coordinate','ownerIds','nodeKey','segmentKey','segmentEndpoints'],'native indicator fields');
   assert.deepEqual(indicator,candidate,'actual resolved browser/native selected indicator');assert.equal(candidate.kind,'vertex');assert.equal(indicator.kind,'vertex');assert.equal(stage.winner,candidate.ownerIds[0]);assert.deepEqual(indicator.ownerIds,candidate.ownerIds,'actual snapped owner order');assert.deepEqual(indicator.coordinate,candidate.coordinate,'exact snapped coordinate');assert.deepEqual(indicator.coordinate,web.input.queryCoordinate,'shared query coordinate');assert.equal(indicator.nodeKey,candidate.nodeKey);assert.equal(indicator.segmentKey,null);assert.equal(indicator.segmentEndpoints,null);
  }
  result.passed=true;
 }catch(error){result.differences.push(error.message);}
 return result;
}
const unpairedNativeCases=Object.freeze(['query-while-deleted-appends','root-addition-syncs-generic-deletion','replacement-resets','child-addition-defers','root-metadata-defers']);
export function compareSourceHistoryCollections(web,native){
 keys(native,['schema','version','cases'],'native envelope');assert.equal(native.schema,'pando-m974-native-source-history');assert.equal(native.version,1);
 assert.equal(new Set(native.cases.map(row=>row.case)).size,native.cases.length,'no duplicated native case');
 assert.deepEqual(native.cases.map(row=>row.case).sort(),[...requiredNativeCases,...unpairedNativeCases].sort(),'complete fixed native coverage');
 const paired=web.cases.filter(row=>row.input.nativeCase!==null);assert.deepEqual(paired.map(row=>row.input.nativeCase),requiredNativeCases,'complete fixed paired browser coverage');
 const cases=paired.map(row=>compareSourceHistoryCase(row,native.cases.find(item=>item.case===row.input.nativeCase)));
 return {schema:'pando-m974-source-history-comparison',version:1,passed:cases.every(row=>row.passed),rawParity:false,pairedCases:cases.length,webOnlyCases:web.cases.filter(row=>row.input.nativeCase===null).map(row=>row.case),nativeOnlyCases:unpairedNativeCases,cases,limits:{fullDOM:false,gpu:false,webCanonicalSubset:true,nativeFullDocumentBytes:true,queryContextEquivalent:false,selectedIndicatorOnly:true,heldResultVersusUndeliveredCallback:true,concurrentRevisionReadOnlyAcceptance:false,unpairedCasesAreNotParityPasses:true}};
}
export function parseArguments(args){
 const names={'--browser-directory':'browserDirectory','--native-probe':'nativeProbe','--output':'output','--expected-commit':'expectedCommit','--expected-run-id':'expectedRunId'},options={};
 for(let i=0;i<args.length;i+=2){const name=names[args[i]];assert.ok(name&&args[i+1]&&!Object.hasOwn(options,name),'unknown/missing/duplicate argument');options[name]=args[i+1];}
 for(const name of Object.values(names))assert.ok(options[name],'missing '+name);assert.match(options.expectedCommit,/^[a-f0-9]{40}$/);assert.match(options.expectedRunId,/^\d+$/);return options;
}
export async function runGate(options){
 const {verifySourceHistorySuite,verifySourceHistoryReport}=await import('./m974-snap-boundary/source-history-suite.mjs');
 fs.mkdirSync(options.output,{recursive:true});const destination=path.join(options.output,'comparison.json');
 try{
  const read=name=>fs.readFileSync(path.join(options.browserDirectory,name)),pin=JSON.parse(read('suite-pin.json')),suiteBytes=gunzipSync(read('suite.json.gz'),{maxOutputLength:20*1024*1024});assert.equal(digest(suiteBytes),pin.decompressedSha256);assert.equal(suiteBytes.length,pin.decompressedBytes);
  const suite=verifySourceHistorySuite(JSON.parse(suiteBytes));assert.deepEqual(pin.identity,suite.identity);assert.deepEqual(suite.inputManifest.pairedNativeCases,requiredNativeCases);
  const browserBytes=read('browser-report.json'),transfer=JSON.parse(read('report-transfer.json'));assert.equal(digest(browserBytes),transfer.sha256);assert.equal(browserBytes.length,transfer.bytes);assert.deepEqual(gunzipSync(read('browser-report.json.gz'),{maxOutputLength:32*1024*1024}),browserBytes);
  const report=verifySourceHistoryReport(suite,JSON.parse(browserBytes));assert.deepEqual(JSON.parse(read('source-history-observations.json')),report.cases);
  for(const identity of [suite,suite.identity,report.identity]){assert.equal(identity.commit,options.expectedCommit);assert.equal(identity.runId,options.expectedRunId);}
  const nativeFile=path.resolve(options.output,'native-observations.json');assert.ok(!fs.existsSync(nativeFile),'use fresh output directory to prevent stale or appended observations');
  const binarySha=digest(fs.readFileSync(options.nativeProbe));
  const run=spawnSync(path.resolve(options.nativeProbe),['genericDeleteUndoDefersWorkerSynchronization','selectionSourceHistory','-o','-,txt'],{env:{...process.env,QT_QPA_PLATFORM:'offscreen',QT_QUICK_BACKEND:'software',PANDO_M974_SOURCE_HISTORY_EVIDENCE:nativeFile},encoding:'utf8',timeout:120000,maxBuffer:4*1024*1024});
  fs.writeFileSync(path.join(options.output,'native.stdout'),run.stdout??'');fs.writeFileSync(path.join(options.output,'native.stderr'),run.stderr??'');assert.ifError(run.error);assert.equal(run.status,0,'native public workflows complete');assert.equal(digest(fs.readFileSync(options.nativeProbe)),binarySha);
  const nativeBytes=fs.readFileSync(nativeFile),native=JSON.parse(nativeBytes),result=compareSourceHistoryCollections(report,native);
  result.provenance={commit:options.expectedCommit,runId:options.expectedRunId,browserReportSha256:digest(browserBytes),suiteSha256:digest(suiteBytes),nativeBinarySha256:binarySha,nativeObservationSha256:digest(nativeBytes),comparatorSha256:digest(fs.readFileSync(fileURLToPath(import.meta.url))),ciIdentityVerified:process.env.GITHUB_ACTIONS==='true'&&process.env.GITHUB_SHA===options.expectedCommit&&process.env.GITHUB_RUN_ID===options.expectedRunId};
  if(process.env.GITHUB_ACTIONS==='true')assert.equal(result.provenance.ciIdentityVerified,true);
  fs.writeFileSync(destination,JSON.stringify(result,null,2)+'\n');return result;
 }catch(error){fs.writeFileSync(destination,JSON.stringify({passed:false,rawParity:false,error:error.message},null,2)+'\n');throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))runGate(parseArguments(process.argv.slice(2))).then(result=>{console.log(JSON.stringify(result));if(!result.passed)process.exitCode=1;}).catch(error=>{console.error(error);process.exitCode=1;});
