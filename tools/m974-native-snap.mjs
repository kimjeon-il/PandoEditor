/** Exact native snap gate. Expected numeric values come only from verified Chromium artifacts. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {isDeepStrictEqual as equal} from 'node:util';
import {fileURLToPath} from 'node:url';
import {verifyCaptureSuite,verifyBrowserReport,snapProbeRows} from './m974-snap-boundary/protocol.mjs';

const counts=Object.freeze({snapCases:35,helperRows:45,mathRows:101,providerQueries:51,equivalentQueries:49,excludedQueries:2});
const exclusions=Object.freeze({
  'snap-current-error-retry:0':'invalid worker margin; canonical Project rejects the web malformed-ring fixture',
  'snap-lifecycle-source-stale:0':'actual native metadata revision change; identical web geometry reallocation is a native canonical NoOp',
});
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const json=bytes=>JSON.parse(bytes.toString('utf8'));
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const integer=value=>Number.isSafeInteger(value)&&value>=0;
const rowKey=row=>`${row?.case}:${row?.queryIndex}`;
function keys(value,required,optional=[]){
  return object(value)&&required.every(key=>Object.hasOwn(value,key))&&Object.keys(value).every(key=>required.includes(key)||optional.includes(key));
}
function requireKeys(value,required,label,optional=[]){assert.ok(keys(value,required,optional),`${label}: missing or unexpected fields`);}
function failure(failures,identity,field,reason='exact value differs'){failures.push({case:identity,field,reason});}
function same(failures,identity,field,actual,expected){if(!equal(actual,expected))failure(failures,identity,field);}
function diagnosticsValid(value){return keys(value,['nearbyObjects','visitedSegments','segmentEntriesExamined','intersectionTests','geometryIndexBuilds'])&&Object.values(value).every(integer);}
function helperShape(value,projection){
  const required=['case','queryIndex','scope','candidates','result','indicator','diagnostics'];
  if(projection)required.push('projection');
  return keys(value,required)&&value.scope==='native-app-snap-helper-only'&&(!projection||value.projection==='browser-observed')&&Array.isArray(value.candidates)&&diagnosticsValid(value.diagnostics);
}

export function nativeHelperInput(row){
  const {candidates,result,indicator,...input}=row;
  void candidates;void result;void indicator;
  return structuredClone(input);
}
export function verifyExports(report,probeRows,mathRows,raw){
  const expected=snapProbeRows(report);
  assert.deepEqual(probeRows,expected,'helper export must equal actual browser rows, including ordered IDs and inputs');
  assert.deepEqual(mathRows,report.math,'math export must equal actual browser numeric observations');
  if(raw){
    assert.equal(digest(raw.probeInput),digest(expected.map(row=>JSON.stringify(row)).join('\n')+'\n'),'helper export hash differs from the frozen exporter');
    assert.equal(digest(raw.math),digest(JSON.stringify(report.math)),'math export hash differs from the frozen exporter');
  }
}
export function readCapture(options){
  assert.match(options.commit||'',/^[a-f0-9]{40}$/,'exact commit required');
  assert.match(String(options.runId||''),/^\d+$/,'exact workflow run ID required');
  const bytes={},read=(name,file)=>{assert.equal(typeof file,'string',`${name} path required`);const value=fs.readFileSync(file);bytes[name]=value;return value;};
  const suiteCompressed=read('suite',options.suite),suiteBytes=gunzipSync(suiteCompressed,{maxOutputLength:32*1024*1024});
  const pin=json(read('suitePin',options.suitePin));requireKeys(pin,['decompressedSha256','decompressedBytes','identity'],'suite pin');
  assert.equal(digest(suiteBytes),pin.decompressedSha256,'suite decompressed SHA256 mismatch');
  assert.equal(suiteBytes.length,pin.decompressedBytes,'suite decompressed byte count mismatch');
  const suite=verifyCaptureSuite(json(suiteBytes));
  assert.equal(suite.commit,options.commit,'browser/native requested commit mismatch');
  assert.equal(String(suite.runId),String(options.runId),'browser/native workflow run mismatch');
  assert.deepEqual(pin.identity,suite.identity,'suite pin identity mismatch');
  const reportBytes=read('browserReport',options.browserReport),transfer=json(read('reportTransfer',options.reportTransfer));
  requireKeys(transfer,['characters','bytes','sha256','chunks','chunkCharacters'],'report transfer');
  assert.equal(reportBytes.length,transfer.bytes,'report byte count mismatch');
  assert.equal(digest(reportBytes),transfer.sha256,'report SHA256 mismatch');
  assert.equal(reportBytes.toString('utf8').length,transfer.characters,'report character count mismatch');
  assert.ok(integer(transfer.chunkCharacters)&&transfer.chunkCharacters>0,'invalid transfer chunk size');
  assert.equal(transfer.chunks,Math.ceil(transfer.characters/transfer.chunkCharacters),'report transfer chunk count mismatch');
  const reportCompressed=read('browserReportGzip',options.browserReportGzip||options.browserReport+'.gz');
  assert.deepEqual(gunzipSync(reportCompressed,{maxOutputLength:32*1024*1024}),reportBytes,'compressed report differs from transferred report');
  const report=verifyBrowserReport(suite,json(reportBytes));
  assert.equal(report.identity.commit,options.commit);assert.equal(String(report.identity.runId),String(options.runId));
  const exportBytes=read('probeInput',options.probeInput),lines=exportBytes.toString('utf8').trimEnd().split('\n');
  assert.ok(lines.length&&lines.every(line=>line.trim().length>0),'missing/blank helper JSONL rows');
  const probeRows=lines.map(line=>JSON.parse(line)),mathBytes=read('math',options.math),mathRows=json(mathBytes);
  verifyExports(report,probeRows,mathRows,{probeInput:exportBytes,math:mathBytes});
  assert.equal(suite.inputs.snap.length,counts.snapCases,'complete 35-case workflow corpus required');
  assert.equal(probeRows.length,counts.helperRows,'complete 45-row helper corpus required');
  assert.equal(mathRows.length,counts.mathRows,'complete 101-row numeric corpus required');
  assert.equal(report.snap.reduce((sum,row)=>sum+row.queries.length,0),counts.providerQueries,'complete provider query corpus required');
  return {suite,report,probeRows,mathRows,artifacts:Object.fromEntries(Object.entries(bytes).map(([name,data])=>[name,{bytes:data.length,sha256:digest(data)}]))};
}

export function compareHelpers(expected,actual){
  const failures=[];if(!Array.isArray(actual))actual=[];
  same(failures,'helper','ordered identities',actual.map(rowKey),expected.map(rowKey));
  for(const [index,row]of expected.entries()){
    const observed=actual[index],identity=rowKey(row);
    if(!helperShape(observed,true)){failure(failures,identity,'schema',observed?.error||'missing or unexpected native helper fields');continue;}
    for(const field of ['candidates','result','indicator'])same(failures,identity,field,observed[field],row[field]);
  }
  return {scope:'helper-only',projection:'browser-observed',rawParity:false,expected:expected.length,actual:actual.length,passed:failures.length===0,failures};
}
export function compareMath(expected,actual){
  const failures=[];if(!Array.isArray(actual))actual=[];
  same(failures,'math','ordered identities',actual.map(rowKey),expected.map(row=>`math:${row.id}:0`));
  for(const [index,row]of expected.entries()){
    const observed=actual[index],identity=row.id;
    if(!helperShape(observed,false)){failure(failures,identity,'schema',observed?.error||'missing or unexpected native numeric fields');continue;}
    same(failures,identity,'candidates',observed.candidates,[]);
    same(failures,identity,'result',observed.result,row.result);
    same(failures,identity,'distancePx',observed.result?.distancePx,row.hypot);
  }
  return {scope:'resolver-only',projection:'identity',expected:expected.length,actual:actual.length,passed:failures.length===0,failures};
}
export function compareProviders(definitions,observations,actual){
  const failures=[],excludedRows=[];let comparedQueries=0,expectedQueries=0;
  if(!Array.isArray(actual))actual=[];
  same(failures,'provider','browser ordered identities',observations.map(row=>row.id),definitions.map(row=>row.id));
  same(failures,'provider','native ordered identities',actual.map(row=>row?.case),definitions.map(row=>row.id));
  for(const [caseIndex,definition]of definitions.entries()){
    const browser=observations[caseIndex],native=actual[caseIndex],id=definition.id;
    expectedQueries+=definition.queries.length;
    if(!keys(native,['case','scope','rawParity','queries','lifecycle','comparison'])||native.scope!=='native-provider-only'||native.rawParity!==false||!Array.isArray(native.queries)){
      failure(failures,id,'schema',native?.error||'missing or unexpected provider fields');continue;
    }
    same(failures,id,'query identities',native.queries.map(row=>row?.queryIndex),definition.queries.map((_,i)=>i));
    if(!browser||!Array.isArray(browser.queries)||browser.queries.length!==definition.queries.length){failure(failures,id,'browser queries','missing browser observations');continue;}
    let submitted=0;
    for(const [queryIndex,expected]of browser.queries.entries()){
      const row=native.queries[queryIndex],identity=`${id}:${queryIndex}`,exclusion=exclusions[identity];
      if(!keys(row,['queryIndex','inputEquivalent','cold','pending','ready','afterSettlement'],exclusion?['nativeFailureStimulus']:[])||typeof row.inputEquivalent!=='boolean'||
         !keys(row.cold,['candidates','status'])||!Array.isArray(row.cold.candidates)||!['pending','ready'].includes(row.cold.status)||
         !keys(row.pending,['sameArray','requestCount','candidates'])||typeof row.pending.sameArray!=='boolean'||!integer(row.pending.requestCount)||!Array.isArray(row.pending.candidates)||
         !keys(row.afterSettlement,['status','submitted'])||!integer(row.afterSettlement.submitted)){
        failure(failures,identity,'schema','missing or unexpected provider query fields');continue;
      }
      submitted+=row.pending.requestCount;
      if(exclusion){
        if(row.inputEquivalent!==false||row.nativeFailureStimulus!==exclusion||!keys(row.ready,['observed'])||row.ready.observed!==false||row.afterSettlement.status!=='empty'){
          failure(failures,identity,'input equivalence','declared non-equivalent native stimulus must remain explicit and unproven');continue;
        }
        excludedRows.push({case:id,queryIndex,inputEquivalent:false,reason:exclusion});
        continue;
      }
      if(row.inputEquivalent!==true){failure(failures,identity,'input equivalence','unexpected provider exclusion');continue;}
      ++comparedQueries;
      same(failures,identity,'cold.candidates',row.cold.candidates,expected.cold?.candidates);
      same(failures,identity,'cold.status',row.cold.status,expected.pending?.requestCount?'pending':'ready');
      for(const field of ['sameArray','requestCount','candidates'])same(failures,identity,'pending.'+field,row.pending[field],expected.pending?.[field]);
      if(expected.ready?.observed===false){
        if(!keys(row.ready,['observed'])||row.ready.observed!==false)failure(failures,identity,'ready','interrupted browser input must remain unobserved');
      }else{
        if(!keys(row.ready,['candidates','status']))failure(failures,identity,'ready','missing or unexpected ready fields');
        else{same(failures,identity,'ready.candidates',row.ready.candidates,expected.ready?.candidates);same(failures,identity,'ready.status',row.ready.status,'ready');}
        same(failures,identity,'afterSettlement.status',row.afterSettlement.status,'ready');
      }
      if(!definition.scenario)same(failures,identity,'afterSettlement.submitted',row.afterSettlement.submitted,submitted);
    }
    if(definition.scenario){
      const excluded=Object.hasOwn(exclusions,`${id}:0`);
      if(!keys(native.lifecycle,['inputEquivalent','scenario','requestsAdded','settledStatus'])||!integer(native.lifecycle.requestsAdded))failure(failures,id,'lifecycle','missing or unexpected lifecycle fields');
      else{
        same(failures,id,'lifecycle.scenario',native.lifecycle.scenario,definition.scenario);
        same(failures,id,'lifecycle.inputEquivalent',native.lifecycle.inputEquivalent,!excluded);
        if(!excluded){
          same(failures,id,'lifecycle.requestsAdded',native.lifecycle.requestsAdded,browser.lifecycle?.requestsAdded);
          const status=browser.requests?.at(-1)?.status;
          if(!['resolved','rejected'].includes(status))failure(failures,id,'browser lifecycle','missing terminal request');
          else same(failures,id,'lifecycle.settledStatus',native.lifecycle.settledStatus,status==='resolved'?'ready':'empty');
          same(failures,id,'afterSettlement.status',native.queries.at(-1)?.afterSettlement?.status,native.lifecycle.settledStatus);
          same(failures,id,'afterSettlement.submitted',native.queries.at(-1)?.afterSettlement?.submitted,submitted+native.lifecycle.requestsAdded);
        }
      }
    }else if(!equal(native.lifecycle,{}))failure(failures,id,'lifecycle','unexpected lifecycle observation');
  }
  const required=Object.keys(exclusions).filter(key=>definitions.some(row=>key===`${row.id}:0`));
  same(failures,'provider','declared exclusion identities',excludedRows.map(rowKey),required);
  return {scope:'native-provider-only',rawParity:false,expectedCases:definitions.length,actualCases:actual.length,expectedQueries,comparedQueries,excludedRows,equivalentRowsPassed:failures.length===0,allRowsProven:failures.length===0&&excludedRows.length===0,failures};
}

const argumentNames={
  'browser-report':'browserReport','browser-report-gzip':'browserReportGzip',suite:'suite','suite-pin':'suitePin','report-transfer':'reportTransfer',
  'probe-input':'probeInput',math:'math','helper-probe':'helperProbe','provider-probe':'providerProbe',commit:'commit','run-id':'runId',output:'output',
};
export function parseArguments(args){
  const options={};for(let i=0;i<args.length;i+=2){const name=args[i]?.startsWith('--')?args[i].slice(2):'';assert.ok(Object.hasOwn(argumentNames,name),'unknown argument: '+args[i]);assert.ok(args[i+1]&&!args[i+1].startsWith('--'),'missing value for '+args[i]);const key=argumentNames[name];assert.ok(!Object.hasOwn(options,key),'duplicate argument: '+args[i]);options[key]=args[i+1];}
  for(const key of Object.values(argumentNames).filter(key=>key!=='browserReportGzip'))assert.ok(options[key],'required argument missing: '+key);
  options.browserReportGzip ||= options.browserReport+'.gz';return options;
}
function executeProbe(executable,input){
  const process=spawnSync(executable,[],{input:JSON.stringify(input),encoding:'utf8',timeout:30000,maxBuffer:16*1024*1024});
  if(process.error||process.status!==0)throw new Error(String(process.error||process.stderr||process.stdout||'native probe failed').slice(0,4096));
  const result=JSON.parse(process.stdout);assert.ok(object(result),'native probe must emit exactly one JSON object');return result;
}
function save(output,name,value){const destination=path.join(output,name),temporary=destination+'.pending';fs.writeFileSync(temporary,JSON.stringify(value,null,2)+'\n');fs.renameSync(temporary,destination);return {file:name,sha256:digest(fs.readFileSync(destination))};}
export function runGate(options){
  fs.mkdirSync(options.output,{recursive:true});
  const comparator={file:'tools/m974-native-snap.mjs',sha256:digest(fs.readFileSync(fileURLToPath(import.meta.url)))};
  let receipt={schema:'pando-m974-native-snap-gate',version:1,state:'running',passed:false,rawParity:false,requestedCommit:options.commit,requestedRunId:options.runId,comparator};
  save(options.output,'receipt.json',receipt);
  try{
    const capture=readCapture(options),{suite,report,probeRows,mathRows}=capture;
    const ciIdentityVerified=process.env.GITHUB_ACTIONS==='true';
    if(ciIdentityVerified){assert.equal(process.env.GITHUB_SHA,options.commit,'CI checkout commit mismatch');assert.equal(process.env.GITHUB_RUN_ID,String(options.runId),'CI workflow run mismatch');}
    const binaries={helper:{file:path.basename(options.helperProbe),sha256:digest(fs.readFileSync(options.helperProbe))},provider:{file:path.basename(options.providerProbe),sha256:digest(fs.readFileSync(options.providerProbe))}};
    const inputHashes={helper:[],provider:[],math:[]},actualHelpers=[],actualProviders=[],actualMath=[];
    for(const row of probeRows){const input=nativeHelperInput(row);inputHashes.helper.push({case:row.case,queryIndex:row.queryIndex,sha256:digest(JSON.stringify(input))});try{actualHelpers.push(executeProbe(options.helperProbe,input));}catch(error){actualHelpers.push({case:row.case,queryIndex:row.queryIndex,error:error.message});}}
    for(const [index,observation]of report.snap.entries()){
      const input={definition:suite.inputs.snap[index],observation};inputHashes.provider.push({case:observation.id,sha256:digest(JSON.stringify(input))});
      try{actualProviders.push(executeProbe(options.providerProbe,input));}catch(error){actualProviders.push({case:observation.id,error:error.message});}
    }
    for(const row of mathRows){
      const input={case:'math:'+row.id,queryIndex:0,resolve:{coordinate:[0,0],screenPoint:[0,0],pointerType:'mouse',candidates:[{kind:'vertex',coordinate:row.coordinate,ownerIds:['diagnostic'],nodeKey:row.id}]}};
      inputHashes.math.push({id:row.id,sha256:digest(JSON.stringify(input))});
      try{actualMath.push(executeProbe(options.helperProbe,input));}catch(error){actualMath.push({case:input.case,queryIndex:0,error:error.message});}
    }
    assert.equal(digest(fs.readFileSync(fileURLToPath(import.meta.url))),comparator.sha256,'comparator changed during gate');
    assert.equal(digest(fs.readFileSync(options.helperProbe)),binaries.helper.sha256,'helper executable changed during gate');
    assert.equal(digest(fs.readFileSync(options.providerProbe)),binaries.provider.sha256,'provider executable changed during gate');
    const helper=compareHelpers(probeRows,actualHelpers),provider=compareProviders(suite.inputs.snap,report.snap,actualProviders),math=compareMath(mathRows,actualMath);
    if(provider.comparedQueries!==counts.equivalentQueries||provider.excludedRows.length!==counts.excludedQueries){provider.equivalentRowsPassed=false;failure(provider.failures,'provider','required counts','expected exactly 49 compared and 2 excluded queries');}
    const passed=helper.passed&&provider.equivalentRowsPassed&&math.passed;
    const observations={helper:save(options.output,'helper-observations.json',actualHelpers),provider:save(options.output,'provider-observations.json',actualProviders),math:save(options.output,'math-observations.json',actualMath)};
    receipt={...receipt,state:'complete',passed,ciIdentityVerified,allInputsProven:false,identity:suite.identity,runtime:report.runtime,artifacts:capture.artifacts,binaries,inputHashes,observations,helper,provider,math,
      limits:['Helper resolution uses browser-observed projection inputs; native projection and complete-frame parity are not certified.','Two declared non-equivalent provider stimuli are excluded and remain unproven.','Local runs are comparisons only; exact-commit CI identity is verified only inside the matching GitHub workflow.']};
  }catch(error){receipt={...receipt,state:'failed',passed:false,error:String(error.stack||error)};}
  save(options.output,'receipt.json',receipt);return receipt;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  try{const result=runGate(parseArguments(process.argv.slice(2)));console.log(JSON.stringify({passed:result.passed,state:result.state,ciIdentityVerified:result.ciIdentityVerified,rawParity:result.rawParity,helper:result.helper?.actual,providerCompared:result.provider?.comparedQueries,providerExcluded:result.provider?.excludedRows.length,math:result.math?.actual,error:result.error}));if(!result.passed)process.exitCode=1;}
  catch(error){console.error(String(error.stack||error));process.exitCode=1;}
}
