// Actual Chromium capture is authorized in exact-commit CI only. Local helpers
// construct inputs and test the protocol; they never create browser goldens.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {splitBrowserSourceBundle,createSplitBrowserPage,splitBrowserRuntime} from './web-split-browser-bundle.mjs';
import {loadSplitModules,createSplitRuntime,splitCaseIds,splitCaseDefinition,splitReactivationCaseDefinition} from './web-split.mjs';
import {loadSplitCandidateModules} from './web-split-candidate.mjs';
import {verifyLifecycleSources} from './web-lifecycle.mjs';
import {prepareRiverRemovalCorrectedSelectionSources} from './web-selection-removal-correction.mjs';
import {runtimePin} from './river/suite.mjs';
import {writeBoundedJsonArtifact} from './river/browser/report-transfer.mjs';
const hash=value=>createHash('sha256').update(typeof value==='string'?value:JSON.stringify(value)).digest('hex');
export function splitKernelStressCases(){
 let seed=32498;const rand=()=>((seed=Math.imul(seed,1664525)+1013904223|0)>>>0)/2**32,rows=[];
 for(let n=0;n<250;n++){
  const x=-100+rand()*170,y=-45+rand()*80,w=2+rand()*15,h=2+rand()*15;
  const source={type:'Polygon',coordinates:[[[x,y],[x,y+h],[x+w,y+h],[x+w,y],[x,y]]]};
  const along=0.01+rand()*0.98;let a=[x+w*along,y-0.1],b=[x+w*along,y+h+0.1];
  if(n%4===0){a=[x-0.1,y+h*along];b=[x+w+0.1,y+h*along];}if(n%4===1)b=[x+w*(0.01+rand()*0.98),b[1]];
  const view={kind:'flat',scale:500,translate:[512,384],rotate:[0,0,0],center:[x+w/2,y+h/2],size:{width:1024,height:768},snapDistance:{mouse:10,touch:18},coarsePointer:false};
  if(n%5===0){view.kind='globe';view.rotate=[-view.center[0],-view.center[1],0];view.center=[0,0];}
  rows.push({id:`kernel-stress-${n}`,payload:{source,coords:[a,b],view,buildPreview:true}});
 }
 return rows;
}
export function splitControllerDefinitions(inputs=splitCaseIds.map(splitCaseDefinition)){
 const definitions=structuredClone(inputs),adaptations=[],originalUnrepresentableCaseIds=[];
 for(const definition of definitions){
  const original=structuredClone(definition);
  if(['root-no-cut','root-tangent','root-duplicate-vertex','root-self-intersection'].includes(definition.id)){
   const shift=value=>{if(typeof value[0]==='number')value[1]-=5;else value.forEach(shift);};shift(definition.source.coordinates);shift(definition.coords);definition.view.center[1]-=5;
   adaptations.push({originalCase:original.id,controllerCase:'common-'+original.id,reason:'equator-symmetric source and gesture; preserves the named invalid-cut topology',original,controller:definition});
  }else if(definition.scope==='child'){
   definition.parentGeometry={type:'Polygon',coordinates:[[[-10,-30],[-10,30],[30,30],[30,-30],[-10,-30]]]};
   adaptations.push({originalCase:original.id,controllerCase:'common-'+original.id,reason:'symmetric parent envelope only; child source and gesture remain byte-identical',original,controller:definition});
  }else continue;
  originalUnrepresentableCaseIds.push(original.id);definition.id='common-'+original.id;
 }
 return {definitions,adaptations,originalUnrepresentableCaseIds};
}
export function splitDatelineControllerDefinitions(definitions){
 const copies=structuredClone(definitions),adaptations=[];
 for(const definition of copies){if(!['root-dateline-dependents','child-dateline-dependents'].includes(definition.id))continue;const original=structuredClone(definition);
  const change=value=>{if(typeof value[0]==='number'){if(Math.abs(value[0])===179.2)value[0]=Math.sign(value[0])*179.25;else if(Math.abs(value[0])===179.4)value[0]=Math.sign(value[0])*179.5;}else value.forEach(change);};
  for(const dependent of definition.dependentFeatures)change(dependent.geometry.coordinates);definition.id='common-'+original.id;
  adaptations.push({originalCase:original.id,controllerCase:definition.id,reason:'explicit binary-representable descendant longitudes only; original source/gesture/dateline topology/ancestry preserved',original,controller:definition});
 }
 return {definitions:copies,adaptations};
}
function approvedCorrection(){
 const pins=[['dateline','6c3f930b8573fa09991885b661879ea36725472e','07d3e2053c71573e11c5cf89151f5f6686038511',10],['appearance','07d3e2053c71573e11c5cf89151f5f6686038511','ad78780f79f4f38fcd7c2a3c2fb1fe0ba8e37c32',2]],layers=[];
 for(const [folder,base,commit,count]of pins){
  const root=fileURLToPath(new URL('../../tests/fixtures/web-m973-split/corrections/'+folder+'/',import.meta.url)),manifestText=fs.readFileSync(path.join(root,'manifest.json'),'utf8'),definitionsText=fs.readFileSync(path.join(root,'case-definitions.json'),'utf8');const manifestPins={dateline:'d15bd80373af3ff98c3cc4fcec2f78b6268956c275e3318b08a5a71208c30096',appearance:'f65682dd9dcafbc3df2ee4e2b99a214537f6c8c6b75c63ecc8190d9c49d15f85'},definitionPins={dateline:'6134cf37aeb41d91f8eb4cdc441eb8904f056ebf71e938f5f64146f8d999b9c2',appearance:'b64dc9eb1a7f7c46675f354d1646b32dd68284c77be6a7cc31547312eec20f61'};assert.equal(hash(manifestText),manifestPins[folder],'immutable approved correction manifest');assert.equal(hash(definitionsText),definitionPins[folder],'immutable approved input definitions');const manifest=JSON.parse(manifestText),definitions=JSON.parse(definitionsText);
  assert.equal(manifest.schema,'pando-approved-split-correction');assert.equal(manifest.behavioralCommit,commit);assert.equal(manifest.baseBehavioralCommit,base);assert.equal(manifest.changes.length,count);
  for(const change of manifest.changes){assert.match(change.path,/^assets\/js\/modules\/[a-z0-9-]+\.js$/);const bytes=fs.readFileSync(path.join(root,change.path));assert.equal(bytes.length,change.bytes,'approved correction byte count');assert.equal(hash(bytes.toString('utf8')),change.sha256,'approved correction hash');assert.equal(createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),change.blob,'approved correction Git blob');}
  layers.push({root,manifest,definitions});
 }
 return {layers,manifest:layers.at(-1).manifest,definitions:layers[0].definitions,appearanceDefinitions:layers[1].definitions};
}
function controllerVariant(correction){
 const definitions=splitCaseIds.map(splitCaseDefinition),byId=new Map(definitions.map(c=>[c.id,c]));
 for(const definition of correction.appearanceDefinitions){if(byId.has(definition.id))assert.deepEqual(definition,byId.get(definition.id),'duplicate correction case changed original input');else{definitions.push(definition);byId.set(definition.id,definition);}}
 return splitControllerDefinitions(definitions);
}

function identity(suite){return {schema:suite.schema,commit:suite.commit,runId:suite.runId,runtimePin:suite.runtimePin,correctionsSha256:hash(suite.corrections),bundleSha256:hash(suite.bundle),controllerBundleSha256:hash(suite.controllerBundle),inputAdaptationsSha256:hash(suite.inputAdaptations),originalNativeCasesSha256:hash(suite.originalNativeCases),nativeCasesSha256:hash(suite.nativeCases),kernelCasesSha256:hash(suite.kernelCases),orderedCaseIds:suite.bundle.cases.map(c=>c.id),orderedControllerCaseIds:suite.controllerBundle.cases.map(c=>c.id),controllerSourceHashes:Object.fromEntries(Object.entries(suite.controllerBundle.sources).map(([name,s])=>[name,s.sha256])),sourceHashes:Object.fromEntries(Object.entries(suite.bundle.sources).map(([name,s])=>[name,s.sha256]))};}
export async function createSplitCaptureSuite({commit,runId}={}){
 assert.match(commit??'',/^[a-f0-9]{40}$/,'Exact application commit required');assert.ok(runId,'Run identity required');
 const bundle=await splitBrowserSourceBundle(),correction=approvedCorrection(),variant=controllerVariant(correction),datelineVariant=splitDatelineControllerDefinitions(correction.definitions),nativeCases=[],originalNativeCases=[],root=fs.mkdtempSync(path.join(tmpdir(),'pando-approved-split-'));
 try{
  const changes=new Map();for(const layer of correction.layers)for(const change of layer.manifest.changes){const destination=path.join(root,change.path);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(path.join(layer.root,change.path),destination);changes.set(change.path,change);}
  const options={candidateRoot:root,candidateChanges:[...changes.values()]},controllerBundle=await splitBrowserSourceBundle({...options,caseDefinitions:[...variant.definitions,...datelineVariant.definitions,splitReactivationCaseDefinition()]}),corrections=correction.layers.map(l=>l.manifest);
  controllerBundle.behavioralCommit=correction.manifest.behavioralCommit;controllerBundle.sourceChain=[...controllerBundle.sourceChain,...corrections];delete controllerBundle.testOnlyCandidate;controllerBundle.approvedCorrections=corrections;
  for(const [definitions,destination,isCorrected]of [[controllerBundle.cases,nativeCases,true],[variant.adaptations.map(c=>c.original),originalNativeCases,false],[datelineVariant.adaptations.map(c=>c.original),originalNativeCases,true]]){
   const loaded=isCorrected?await loadSplitCandidateModules(options):await loadSplitModules();try{for(const definition of definitions){const r=createSplitRuntime(loaded,definition);try{destination.push({case:definition.id,definition,features:r.features,genericFeatures:r.runtime.state.genericFeatures});}finally{r.h.workflow.clear();r.client.stop();}}}finally{loaded.cleanup();}
  }
  const kernelCases=[...splitKernelStressCases(),...bundle.cases.map(definition=>({id:'original-input-'+definition.id,payload:{source:definition.source,coords:definition.coords,view:definition.view,buildPreview:true}}))];
  const suite={schema:'pando-m973-split-capture-suite',version:2,commit,runId,runtimePin,corrections,bundle,controllerBundle,inputAdaptations:[...variant.adaptations,...datelineVariant.adaptations],originalNativeCases,nativeCases,kernelCases};suite.identity=identity(suite);return suite;
 }finally{fs.rmSync(root,{recursive:true,force:true});}
}

export function verifySplitCaptureSuite(suite){
 assert.equal(suite.schema,'pando-m973-split-capture-suite');assert.deepEqual(suite.runtimePin,runtimePin,'runtime pin mismatch');assert.deepEqual(suite.identity,identity(suite),'capture identity mismatch');
 assert.deepEqual(suite.bundle.cases,splitCaseIds.map(splitCaseDefinition),'original geographic fixture definitions changed');const correction=approvedCorrection(),variant=controllerVariant(correction),datelineVariant=splitDatelineControllerDefinitions(correction.definitions),controllerDefinitions=[...variant.definitions,...datelineVariant.definitions,splitReactivationCaseDefinition()];assert.deepEqual(suite.corrections,correction.layers.map(l=>l.manifest),'approved correction provenance');assert.equal(suite.controllerBundle.behavioralCommit,correction.manifest.behavioralCommit,'approved behavioral commit');assert.deepEqual(suite.controllerBundle.sourceChain.at(-1),correction.manifest,'approved source chain delta');assert.deepEqual(suite.nativeCases.map(c=>c.case),controllerDefinitions.map(c=>c.id),'missing native lifecycle cases');assert.deepEqual(suite.controllerBundle.cases,controllerDefinitions,'controller case identity mismatch');assert.deepEqual(suite.nativeCases.map(c=>c.definition),controllerDefinitions,'native input definitions must equal actual browser definitions');assert.deepEqual(suite.originalNativeCases.map(c=>c.definition),[...variant.adaptations,...datelineVariant.adaptations].map(c=>c.original),'retained original input definitions changed');assert.deepEqual(suite.inputAdaptations,[...variant.adaptations,...datelineVariant.adaptations],'explicit input adaptation mismatch');assert.deepEqual(suite.originalNativeCases.map(c=>c.case),[...variant.originalUnrepresentableCaseIds,...datelineVariant.adaptations.map(c=>c.originalCase)],'original input evidence missing');assert.deepEqual(suite.kernelCases,[...splitKernelStressCases(),...suite.bundle.cases.map(definition=>({id:'original-input-'+definition.id,payload:{source:definition.source,coords:definition.coords,view:definition.view,buildPreview:true}}))],'kernel case identity mismatch');
 const pinned=verifyLifecycleSources(),approved=prepareRiverRemovalCorrectedSelectionSources();try{
  const baselineHashes=Object.fromEntries(pinned.manifest.sources.map(row=>[row.path,hash(fs.readFileSync(path.join(approved.root,row.path),'utf8'))]));baselineHashes['assets/js/modules/app-object-picking.js']='19e62d26000134b4c7ca695ed21e3a9bbaad176ed3b90b757f59d0b141c13e23';
  assert.deepEqual(Object.fromEntries(Object.entries(suite.bundle.sources).map(([name,item])=>[name,item.sha256])),baselineHashes,'original production source pin mismatch');const finalHashes={...baselineHashes};for(const layer of correction.layers)for(const change of layer.manifest.changes)finalHashes[change.path]=change.sha256;
  assert.deepEqual(Object.fromEntries(Object.entries(suite.controllerBundle.sources).map(([name,item])=>[name,item.sha256])),finalHashes,'approved production source pin mismatch');
 }finally{approved.cleanup();}
 const currentRuntime=splitBrowserRuntime();for(const bundle of [suite.bundle,suite.controllerBundle])assert.deepEqual(bundle.runtime,currentRuntime,'actual harness runtime recorder must match this exact checkout');
 for(const bundle of [suite.bundle,suite.controllerBundle])for(const [name,s]of Object.entries(bundle.sources)){assert.match(name,/^assets\/js\/(modules|workers|vendor)\/[a-zA-Z0-9_.-]+\.js$/,'unsafe source path');assert.equal(hash(s.source),s.sha256,'source hash mismatch '+name);}
 for(const bundle of [suite.bundle,suite.controllerBundle])assert.equal(hash(bundle.runtime.source),bundle.runtime.sha256,'harness source hash mismatch');return suite;
}
export function createSplitCapturePage(suite,{controller=false}={}){
 verifySplitCaptureSuite(suite);const bundle=controller?suite.controllerBundle:suite.bundle,assetPrefix=controller?'/split-oracle-controller/':'/split-oracle-baseline/';
 return createSplitBrowserPage(bundle,{assetPrefix})+`<script type="module">
try{
 while(!['complete','failed'].includes(globalThis.__splitSummary?.state))await new Promise(resolve=>setTimeout(resolve,50));
 if(globalThis.__splitSummary.state!=='complete')throw Error(JSON.stringify(globalThis.__splitSummary));
 const {createMapEditWorkerClient}=await import(${JSON.stringify(assetPrefix+'assets/js/modules/map-edit-worker-client.js')});
 const client=createMapEditWorkerClient({createWorker:()=>new Worker(${JSON.stringify(assetPrefix+'assets/js/workers/map-edit-worker.js')}),getEntities:()=>[],getFeatureById:()=>null,getTargetRevision:()=>0});
 const kernel=[];
 try{for(const row of ${JSON.stringify(suite.kernelCases)}){const response=await client.execute('territorial-cut',{payload:row.payload});kernel.push({id:row.id,result:response.result});globalThis.__splitCaptureSummary={state:'running',completed:kernel.length,total:${suite.kernelCases.length}};}}finally{client.stop();}
 globalThis.__splitCaptureReport={schema:'pando-m973-actual-chromium-split',version:1,identity:${JSON.stringify(suite.identity)},lifecycle:globalThis.__splitReport,kernel};
 globalThis.__splitCaptureSummary={state:'complete',lifecycleCases:globalThis.__splitReport.cases.length,kernelCases:kernel.length};
}catch(error){globalThis.__splitCaptureSummary={state:'failed',error:String(error),stack:error.stack};}
</script>`;
}
export async function exportSplitBrowserReport(page,destination,runtime){
 try{
  const metadata=await page.evaluate(async runtime=>{const report=globalThis.__splitCaptureReport;if(!report||report.schema!=='pando-m973-actual-chromium-split')throw Error('Missing completed actual browser report');report.runtime=runtime;const text=globalThis.__splitCaptureExport=JSON.stringify(report),bytes=new TextEncoder().encode(text);return {characters:text.length,bytes:bytes.length,sha256:Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),byte=>byte.toString(16).padStart(2,'0')).join('')};},runtime);
  return await writeBoundedJsonArtifact(metadata,request=>page.evaluate(({offset,maximum})=>{const text=globalThis.__splitCaptureExport;if(typeof text!=='string')throw Error('Missing frozen browser observation');let next=Math.min(offset+maximum,text.length);if(next<text.length&&text.charCodeAt(next-1)>=0xd800&&text.charCodeAt(next-1)<=0xdbff)next--;return {offset,text:text.slice(offset,next),next};},request),destination);
 }finally{await page.evaluate(()=>{delete globalThis.__splitCaptureExport;}).catch(()=>{});}
}
async function main(){
 assert.equal(process.env.GITHUB_ACTIONS,'true','Actual browser capture runs only in authorized exact-commit CI');const output=process.argv[2];assert.ok(output,'Output directory required');fs.mkdirSync(output,{recursive:true});
 const suite=await createSplitCaptureSuite({commit:process.env.GITHUB_SHA,runId:process.env.GITHUB_RUN_ID});verifySplitCaptureSuite(suite);fs.writeFileSync(path.join(output,'suite.json'),JSON.stringify(suite));const html=createSplitCapturePage(suite),controllerHtml=createSplitCapturePage(suite,{controller:true});fs.writeFileSync(path.join(output,'oracle.html'),html);fs.writeFileSync(path.join(output,'controller-oracle.html'),controllerHtml);
 const require=createRequire(new URL('./river/browser/package.json',import.meta.url)),{chromium}=require('@playwright/test'),packageVersion=require('@playwright/test/package.json').version;assert.equal(packageVersion,runtimePin.playwright);
 const browsers=JSON.parse(fs.readFileSync(path.join(path.dirname(require.resolve('playwright-core/package.json')),'browsers.json'))),pin=browsers.browsers.find(b=>b.name==='chromium');assert.equal(pin.revision,runtimePin.revision);assert.equal(pin.browserVersion,runtimePin.chromium);
 const server=http.createServer((req,res)=>{if(req.url==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}if(req.url==='/controller'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(controllerHtml);return;}const prefix=req.url?.startsWith('/split-oracle-baseline/')?'/split-oracle-baseline/':req.url?.startsWith('/split-oracle-controller/')?'/split-oracle-controller/':null;const key=prefix?req.url.slice(prefix.length):null;const source=key&&(prefix==='/split-oracle-baseline/'?suite.bundle:suite.controllerBundle).sources[key];if(!source){res.writeHead(404);res.end();return;}res.setHeader('Content-Type','text/javascript; charset=utf-8');res.end(source.source);});await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});let browser;
 try{browser=await chromium.launch({headless:true});const page=await browser.newPage(),cdp=await page.context().newCDPSession(page),version=await cdp.send('Browser.getVersion');fs.writeFileSync(path.join(output,'cdp-runtime.json'),JSON.stringify(version,null,2));assert.equal(browser.version(),runtimePin.chromium);assert.equal(version.jsVersion,runtimePin.v8);
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>['complete','failed'].includes(globalThis.__splitCaptureSummary?.state),{},{timeout:240000});const summary=await page.evaluate(()=>globalThis.__splitCaptureSummary);fs.writeFileSync(path.join(output,'browser-summary.json'),JSON.stringify(summary,null,2));assert.equal(summary.state,'complete',JSON.stringify(summary));
  const transfer=await exportSplitBrowserReport(page,path.join(output,'browser-report.json'),{playwright:packageVersion,browserVersion:browser.version(),chromiumRevision:pin.revision,cdp:version});fs.writeFileSync(path.join(output,'report-transfer.json'),JSON.stringify(transfer,null,2));console.log(JSON.stringify(summary));
  const context=await browser.newContext(),controllerPage=await context.newPage();await controllerPage.goto('http://127.0.0.1:'+server.address().port+'/controller');await controllerPage.waitForFunction(()=>['complete','failed'].includes(globalThis.__splitCaptureSummary?.state),{},{timeout:240000});const controllerSummary=await controllerPage.evaluate(()=>globalThis.__splitCaptureSummary);fs.writeFileSync(path.join(output,'controller-browser-summary.json'),JSON.stringify(controllerSummary,null,2));assert.equal(controllerSummary.state,'complete',JSON.stringify(controllerSummary));
  const controllerTransfer=await exportSplitBrowserReport(controllerPage,path.join(output,'controller-browser-report.json'),{playwright:packageVersion,browserVersion:browser.version(),chromiumRevision:pin.revision,cdp:version});fs.writeFileSync(path.join(output,'controller-report-transfer.json'),JSON.stringify(controllerTransfer,null,2));await context.close();

 }catch(error){fs.writeFileSync(path.join(output,'failure.txt'),String(error)+'\n'+error.stack);throw error;}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
