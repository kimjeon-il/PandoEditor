import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {gzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const api=await import('./m974-native-snap.mjs').catch(error=>{if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;return {};});
const clone=structuredClone;
const diagnostics={nearbyObjects:0,visitedSegments:0,segmentEntriesExamined:0,intersectionTests:0,geometryIndexBuilds:0};
const a={kind:'vertex',coordinate:[0,0],ownerIds:['a'],nodeKey:'0.0000000,0.0000000'};
const b={kind:'vertex',coordinate:[1,0],ownerIds:['b'],nodeKey:'1.0000000,0.0000000'};
const expected={case:'contract-only',queryIndex:0,features:[],genericFeatures:[],payload:{coordinate:[0,0],margin:1,activeOwnerIds:[]},actualRequestPayload:{coordinate:[0,0],margin:1,activeOwnerIds:[]},sourceRevision:0,coordinate:[0,0],screenPoint:[0,0],view:{scale:1000},pointerType:'mouse',projectedPoints:[{coordinate:[0,0],screen:[0,0]}],synchronizedSourceOrder:[],candidates:[a,b],result:{...a,distancePx:0},indicator:{kind:'vertex',coordinate:[0,0],ownerIds:['a'],nodeKey:a.nodeKey,segmentKey:null,segmentEndpoints:null},scope:'helper-only',projection:'browser-observed'};
const helper=()=>({case:expected.case,queryIndex:0,scope:'native-app-snap-helper-only',projection:'browser-observed',candidates:clone(expected.candidates),result:clone(expected.result),indicator:clone(expected.indicator),diagnostics:clone(diagnostics)});
function provider(id='contract-only'){
 const definition={id,queries:[{coordinate:[0,0]}]};
 const observation={id,queries:[{cold:{candidates:[]},pending:{sameArray:true,requestCount:1,candidates:[]},ready:{candidates:[a]}}],requests:[{status:'resolved'}]};
 const actual={case:id,scope:'native-provider-only',rawParity:false,queries:[{queryIndex:0,inputEquivalent:true,cold:{candidates:[],status:'pending'},pending:{sameArray:true,requestCount:1,candidates:[]},ready:{candidates:[a],status:'ready'},afterSettlement:{status:'ready',submitted:1}}],lifecycle:{},comparison:{equivalentRowsPassed:true,allRowsProven:true,expectedQueries:1,comparedQueries:1,excludedRows:[],failures:[]}};
 return {definition,observation,actual};
}
test('reproducible gate exports strict artifact and raw-result comparators',()=>{
 for(const name of ['readCapture','verifyExports','nativeHelperInput','compareHelpers','compareMath','compareProviders','parseArguments','runGate'])assert.equal(typeof api[name],'function',name+' must exist');
});
test('helper native input never receives expected outputs',()=>{
 const input=api.nativeHelperInput(expected);for(const key of ['candidates','result','indicator'])assert.equal(Object.hasOwn(input,key),false);
 assert.deepEqual(input.payload,expected.payload);assert.deepEqual(input.projectedPoints,expected.projectedPoints);
});
test('helper gate rejects missing duplicate reordered and wrong-winner observations exactly',()=>{
 assert.equal(api.compareHelpers([expected],[helper()]).passed,true);
 for(const actual of [[],[helper(),helper()],[{...helper(),case:'wrong'}],[{...helper(),queryIndex:1}]])assert.equal(api.compareHelpers([expected],actual).passed,false);
 for(const mutate of [r=>delete r.result,r=>r.candidates.reverse(),r=>r.result.ownerIds=['b'],r=>r.result.distancePx=Number.MIN_VALUE,r=>r.indicator.coordinate=[1,0],r=>r.projection='native',r=>r.extra='unreviewed']){const actual=helper();mutate(actual);assert.equal(api.compareHelpers([expected],[actual]).passed,false);}
 const next={...clone(expected),queryIndex:1};const second={...helper(),queryIndex:1};assert.equal(api.compareHelpers([expected,next],[second,helper()]).passed,false);
});
test('math comparator uses captured literal output with exact identity and no epsilon',()=>{
 // Synthetic comparator contract only; this is never a browser golden.
 const captured=[{id:'literal-test',coordinate:[1,0],hypot:1,result:{kind:'vertex',coordinate:[1,0],ownerIds:['diagnostic'],nodeKey:'literal-test',distancePx:1}}];
 const actual={case:'math:literal-test',queryIndex:0,scope:'native-app-snap-helper-only',diagnostics:clone(diagnostics),candidates:[],result:clone(captured[0].result),indicator:null};
 assert.equal(api.compareMath(captured,[actual]).passed,true);
 for(const mutate of [r=>r.case='math:other',r=>delete r.result,r=>r.result.distancePx=1+Number.EPSILON,r=>r.result.nodeKey='other',r=>r.extra=true]){const altered=clone(actual);mutate(altered);assert.equal(api.compareMath(captured,[altered]).passed,false);}
 assert.equal(api.compareMath(captured,[]).passed,false);assert.equal(api.compareMath(captured,[actual,actual]).passed,false);
});
test('provider compares raw fields and refuses self-reported success or invented exclusions',()=>{
 const {definition,observation,actual}=provider();assert.equal(api.compareProviders([definition],[observation],[actual]).equivalentRowsPassed,true);
 for(const mutate of [r=>r.queries[0].pending.requestCount=2,r=>r.queries[0].ready.candidates=[b],r=>r.queries[0].afterSettlement.status='pending',r=>r.queries[0].inputEquivalent=false,r=>delete r.queries[0].inputEquivalent,r=>r.queries[0].queryIndex=2,r=>r.queries=[],r=>r.case='wrong',r=>r.rawParity=true,r=>r.extra=true]){const altered=clone(actual);mutate(altered);assert.equal(api.compareProviders([definition],[observation],[altered]).equivalentRowsPassed,false);}
 assert.equal(api.compareProviders([definition],[observation],[]).equivalentRowsPassed,false);assert.equal(api.compareProviders([definition],[observation],[actual,actual]).equivalentRowsPassed,false);
});
test('two explicit non-equivalent provider stimuli remain excluded and never proven',()=>{
 const rows=['snap-current-error-retry','snap-lifecycle-source-stale'].map(provider);
 const reasons=['invalid worker margin; canonical Project rejects the web malformed-ring fixture','actual native metadata revision change; identical web geometry reallocation is a native canonical NoOp'];
 for(const [i,row]of rows.entries()){const q=row.actual.queries[0];q.inputEquivalent=false;q.nativeFailureStimulus=reasons[i];q.ready={observed:false};q.afterSettlement.status='empty';row.observation.queries[0].ready={observed:false};}
 const result=api.compareProviders(rows.map(r=>r.definition),rows.map(r=>r.observation),rows.map(r=>r.actual));assert.equal(result.equivalentRowsPassed,true);assert.equal(result.comparedQueries,0);assert.equal(result.excludedRows.length,2);assert.equal(result.allRowsProven,false);
 const bad=clone(rows[0].actual);bad.queries[0].nativeFailureStimulus='unspecified';assert.equal(api.compareProviders([rows[0].definition],[rows[0].observation],[bad]).equivalentRowsPassed,false);
 const hidden=clone(rows[0].actual);hidden.queries[0].inputEquivalent=true;assert.equal(api.compareProviders([rows[0].definition],[rows[0].observation],[hidden]).equivalentRowsPassed,false);
});
test('exported inputs and math must match their actual browser source rows exactly',()=>{
 const q={input:{coordinate:expected.coordinate,screenPoint:expected.screenPoint,view:expected.view,pointerType:'mouse'},sourceSnapshot:{features:[],genericFeatures:[],sourceRevision:0},candidateRequest:{message:{payload:clone(expected.payload)}},ready:{candidates:clone(expected.candidates),result:clone(expected.result),indicator:clone(expected.indicator),projectedPoints:clone(expected.projectedPoints),synchronizedSourceOrder:[]}};
 const math=[{id:'literal-test',coordinate:[1,0],hypot:1,result:{distancePx:1}}];const report={snap:[{id:expected.case,queries:[q]}],math};
 api.verifyExports(report,[expected],math);
 for(const mutate of [rows=>rows.pop(),rows=>rows.push(clone(rows[0])),rows=>rows[0].payload.coordinate=[2,0],rows=>rows[0].projectedPoints[0].screen=[2,0],rows=>rows[0].queryIndex=9,rows=>rows[0].unexpected=true]){const rows=[clone(expected)];mutate(rows);assert.throws(()=>api.verifyExports(report,rows,math));}
 assert.throws(()=>api.verifyExports(report,[expected],[]));const altered=clone(math);altered[0].hypot=1+Number.EPSILON;assert.throws(()=>api.verifyExports(report,[expected],altered));
});
test('artifact loader rejects missing files and tampered decompressed suite before execution',()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'m974-native-contract-'));try{
  const options={browserReport:path.join(root,'report.json'),browserReportGzip:path.join(root,'report.json.gz'),suite:path.join(root,'suite.gz'),suitePin:path.join(root,'pin.json'),reportTransfer:path.join(root,'transfer.json'),probeInput:path.join(root,'inputs.jsonl'),math:path.join(root,'math.json'),commit:'a'.repeat(40),runId:'1'};
  assert.throws(()=>api.readCapture(options));const bytes=Buffer.from('{}');fs.writeFileSync(options.suite,gzipSync(bytes));fs.writeFileSync(options.suitePin,JSON.stringify({decompressedBytes:2,decompressedSha256:'0'.repeat(64),identity:{}}));assert.throws(()=>api.readCapture(options),/suite.*hash|suite.*SHA/i);
 }finally{fs.rmSync(root,{recursive:true,force:true});}
});
test('CLI rejects missing, duplicate and unknown arguments without a success receipt',()=>{
 assert.throws(()=>api.parseArguments([]));assert.throws(()=>api.parseArguments(['--unknown','x']));assert.throws(()=>api.parseArguments(['--commit','a'.repeat(40),'--commit','b'.repeat(40)]));
 const result=spawnSync(process.execPath,[fileURLToPath(new URL('./m974-native-snap.mjs',import.meta.url))],{encoding:'utf8'});assert.notEqual(result.status,0);
});
test('gate contains no Node-derived numeric oracle or epsilon comparator',()=>{
 const source=fs.readFileSync(new URL('./m974-native-snap.mjs',import.meta.url),'utf8');assert.doesNotMatch(source,/Math\.hypot\s*\(|runMathDiagnostic\s*\(/);assert.doesNotMatch(source,/epsilon\s*[:=]/i);
});
test('export hashes reject rewritten bytes even when parsed values are unchanged',()=>{
 const q={input:{coordinate:expected.coordinate,screenPoint:expected.screenPoint,view:expected.view,pointerType:'mouse'},sourceSnapshot:{features:[],genericFeatures:[],sourceRevision:0},candidateRequest:{message:{payload:clone(expected.payload)}},ready:{candidates:clone(expected.candidates),result:clone(expected.result),indicator:clone(expected.indicator),projectedPoints:clone(expected.projectedPoints),synchronizedSourceOrder:[]}};
 const math=[{id:'literal-test',coordinate:[1,0],hypot:1,result:{distancePx:1}}],report={snap:[{id:expected.case,queries:[q]}],math};
 const raw={probeInput:Buffer.from(JSON.stringify(expected)+'\n'),math:Buffer.from(JSON.stringify(math))};api.verifyExports(report,[expected],math,raw);
 assert.throws(()=>api.verifyExports(report,[expected],math,{...raw,math:Buffer.from(JSON.stringify(math)+'\n')}),/export.*hash/i);
 assert.throws(()=>api.verifyExports(report,[expected],math,{...raw,probeInput:Buffer.from(JSON.stringify(expected,null,2)+'\n')}),/export.*hash/i);
});
test('failed execution replaces a stale successful receipt',()=>{
 const output=fs.mkdtempSync(path.join(os.tmpdir(),'m974-native-failure-'));try{
  fs.writeFileSync(path.join(output,'receipt.json'),'{"passed":true}');const result=api.runGate({output,commit:'a'.repeat(40),runId:'1',suite:path.join(output,'missing.gz')});
  assert.equal(result.passed,false);assert.equal(result.state,'failed');assert.equal(JSON.parse(fs.readFileSync(path.join(output,'receipt.json'))).passed,false);
 }finally{fs.rmSync(output,{recursive:true,force:true});}
});
