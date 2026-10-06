import assert from 'node:assert/strict';
import test from 'node:test';
import {collectNative as collectFreshNative,verifyNativeLifecycleReport} from './compare.mjs';
import {readLifecycleSources} from './sources.mjs';
test('current Web10 lifecycle input is an explicit four-boundary port of the immutable historical input',async()=>{
 const {projectCurrentLifecycleInput}=await import('./sources.mjs'),bundle=readLifecycleSources();
 assert.equal(typeof projectCurrentLifecycleInput,'function');
 assert.deepEqual(JSON.parse(bundle.currentInput.fixtureRaw),projectCurrentLifecycleInput(JSON.parse(bundle.fixtureRaw)));
 assert.equal(bundle.currentInput.provenance.notWebCandidateFixture,true);
 assert.equal(bundle.currentInput.provenance.expectedGenerated,false);
 assert.deepEqual(bundle.currentInput.corpus.cases,bundle.corpus.cases);
 const {prepareProjectForStorage}=await import('../../tests/fixtures/web-v10-exchange/source/assets/js/modules/project-state.js');
 const fixed=JSON.parse(bundle.currentInput.fixtureRaw),prepared=prepareProjectForStorage(fixed);
 const {compareFullWebProject}=await import('../web-v10-exchange-oracle.mjs');compareFullWebProject(prepared,fixed,'independent fixed Web10 production reader');
});
test('current Web10 literal lifecycle output rejects retired source aliases and coherent field loss',async()=>{
 const {verifyCurrentLifecycleWeb}=await import('./compare.mjs'),bundle=readLifecycleSources();
 assert.equal(typeof verifyCurrentLifecycleWeb,'function');const fixed=JSON.parse(bundle.currentInput.fixtureRaw);
 assert.doesNotThrow(()=>verifyCurrentLifecycleWeb(JSON.stringify(fixed),fixed));
 for(const mutate of [p=>p.schemaVersion=9,p=>p.territorialModel.schemaVersion=5,p=>{p.territorialEntities[0].properties.sourceLibraryId='';},p=>delete p.territorialEntities[0].properties.sourceEntityId,p=>p.genericFeatures[0].properties.source.details.nested.reverse(),p=>p.geometries.pop()]){const p=structuredClone(fixed);mutate(p);assert.throws(()=>verifyCurrentLifecycleWeb(JSON.stringify(p),fixed));}
});
test('current Web10 synthetic native storage accounts full archive, inline creator and opaque ledger without native execution',async()=>{
 const {verifyCurrentNativeInput}=await import('./compare.mjs'),{losslessCanonical,accountWebNativeExchange}=await import('../m975-model-exchange/ownership-accounting.mjs'),{sha256}=await import('./sources.mjs');
 const fixed=JSON.parse(readLifecycleSources().currentInput.fixtureRaw),label=fixed.labels[0];fixed.territorialEntities=[];fixed.timelineRecords={schemaVersion:1,lifetimes:[],parentRelations:[],geometryBindings:[]};fixed.labels=[{...label,territorialUnitId:''}];fixed.genericFeatures=[];fixed.hydroEdits=[];fixed.distributionLayers=[];fixed.distributionEntries=[];
 fixed.geometries=[{id:'independent-original',version:1,geojson:{type:'Point',coordinates:[7,8]}}];
 const shape={type:'Point',coordinates:label.coordinates},ref={id:'web-label:'+label.id,version:1},source=structuredClone(label.source);delete source.schemaVersion;
 const exchangeMetadata=Object.fromEntries(['version','savedAt','baseDataset','landObjectModel','territorialModel','distributionModel','sourceInfo','physicalSourceInfo','physicalSettings'].map(key=>[key,fixed[key]]));
 const native={format:'pandoeditor-project',version:10,documentId:'synthetic-verifier-only',units:[],timelineRecords:fixed.timelineRecords,geometries:[...fixed.geometries,{...ref,geojson:shape}],presentation:{userLayers:[],membership:[],objectStyles:{territorial:{}}},extensions:[],exchangeMetadata,content:{labels:[{id:label.id,name:label.name,kind:label.kind,notes:label.notes,geometryRef:ref,territory:null,source}],hydro:[],genericFeatures:[],distributionLayers:[],distributionEntries:[],countryDetails:[],symbols:[],physicalData:{dataset:'',version:'',source:'',hiddenHydroIds:[]}},geometryProvenance:{schemaVersion:1,originalArchive:[{id:'independent-original',version:1}],inlineAllocations:[{ref,createdFor:{domain:'label',id:label.id},geometrySha256:sha256(losslessCanonical(JSON.stringify(shape))),promoted:false}],opaqueBaseline:[{path:'/exchangeMetadata',sha256:sha256(losslessCanonical(JSON.stringify(exchangeMetadata)))},{path:'/labels/'+label.id+'/source/details',sha256:sha256(losslessCanonical(JSON.stringify(source.details)))}],opaqueUncertain:false}};
 const raw=JSON.stringify(fixed),args={sourceWebRaw:raw,nativeRaw:JSON.stringify(native),webOutputRaw:raw,browserInputRaw:raw,expectedWebSchemaVersion:10};
 assert.equal(accountWebNativeExchange(args).rows.length,2);
 assert.throws(()=>accountWebNativeExchange({...args,expectedWebSchemaVersion:undefined}),'historical default remains Web9');
 for(const key of ['sourceWebRaw','webOutputRaw','browserInputRaw'])assert.throws(()=>accountWebNativeExchange({...args,[key]:raw.replace('"schemaVersion":10','"schemaVersion":9')}),'retired root token rejected at '+key);
 const receipt=verifyCurrentNativeInput(JSON.stringify(native),raw,raw);assert.equal(receipt.rows.length,2);assert.equal(receipt.files.sourceWebSha256,sha256(raw));assert.equal(receipt.files.webOutputSha256,sha256(raw));
 for(const mutate of [n=>n.content.labels[0].name='corrupted',n=>n.version=9,n=>n.geometryProvenance.originalArchive=[],n=>n.geometryProvenance.inlineAllocations[0].geometrySha256='0'.repeat(64),n=>n.geometryProvenance.opaqueBaseline.pop(),n=>n.content.labels[0].source.details.nested.reverse(),n=>n.geometries.push({id:'unaccounted',version:1,geojson:shape}),n=>n.units.push({id:'unrequested',sourceLibraryId:''})]){const n=structuredClone(native);mutate(n);assert.throws(()=>verifyCurrentNativeInput(JSON.stringify(n),raw,raw));}
});
test('current Web10 typed inline native fields reject each single-field mutation independently of Web receipts',async()=>{
 const {compareCurrentInlineStorage}=await import('./compare.mjs');assert.equal(typeof compareCurrentInlineStorage,'function');
 const p=JSON.parse(readLifecycleSources().currentInput.fixtureRaw),id=n=>'97500000-0000-4000-8000-'+String(n).padStart(12,'0');
 const source={kind:'user',dataset:'m975-exchange',version:'9',sourceId:'original',sourceFormat:'geojson',sourceType:'Point',importedAt:'2026-10-05',details:{provider:'new-valid-v9',nested:[{order:2},{order:1}]}};
 const validity={from:null,to:null},content={
  labels:[{id:id(2),name:'서울',kind:'city',notes:'M975 source retained',geometryRef:{id:'web-label:'+id(2),version:1},territory:{domain:'territorial',id:'target'},source}],
  hydro:[{id:id(4),name:'Saved water',notes:'retained',geometryRef:{id:'web-hydroEdits:'+id(4),version:1},source:{...source,sourceType:'LineString'},kind:'river',color:'#123456',locked:false,sourceFeatureId:'water-original'}],
  genericFeatures:[{id:id(3),name:'Fallback point',notes:'ordered provenance',geometryRef:{id:'web-genericFeatures:'+id(3),version:1},source,color:'#123456',locked:false,fallbackOnly:true}],
  distributionLayers:[{id:id(1),name:'Exchange',unit:'',valueScale:{mode:'auto'},color:'#8c68d8',locked:false,parentId:null,groups:['second','first'],validity,metadata:{origin:'M975'}}],
  distributionEntries:['donor','child-left','child-right'].map((owner,i)=>({id:id(100+i),layerId:id(1),territory:{domain:'territorial',id:owner},geometryRef:null,value:0.5+i,certainty:'unknown',validity,metadata:{index:i}})),
 };
 assert.doesNotThrow(()=>compareCurrentInlineStorage(content,p));
 const leaves=(value,prefix=[])=>value&&typeof value==='object'?Object.entries(value).flatMap(([key,v])=>leaves(v,[...prefix,key])):[prefix];let mutations=0;
 for(const path of leaves(content)){const bad=structuredClone(content);let owner=bad;for(const key of path.slice(0,-1))owner=owner[key];const key=path.at(-1),value=owner[key];owner[key]=typeof value==='boolean'?!value:typeof value==='number'?value+0.25:typeof value==='string'?value+'-corrupted':'corrupted';assert.throws(()=>compareCurrentInlineStorage(bad,p),path.join('/'));mutations++;}
 for(const collection of Object.keys(content))for(const mutate of [rows=>rows.pop(),rows=>rows.push(structuredClone(rows[0])),rows=>rows[0].unexpected=true]){const bad=structuredClone(content);mutate(bad[collection]);assert.throws(()=>compareCurrentInlineStorage(bad,p),collection+' exact inventory');mutations++;}
 const reordered=structuredClone(content);reordered.distributionEntries.reverse();assert.throws(()=>compareCurrentInlineStorage(reordered,p));mutations++;
 const dated=structuredClone(p),native=structuredClone(content);dated.distributionLayers[0].validFrom='+12000-02';dated.distributionLayers[0].validTo='+12000-02-28';dated.distributionLayers[0].valueScale={mode:'manual',min:0,max:100};native.distributionLayers[0].validity={from:{text:'+12000-02',precision:'month'},to:{text:'+12000-02-28',precision:'date'}};native.distributionLayers[0].valueScale={mode:'manual',min:0,max:100};assert.doesNotThrow(()=>compareCurrentInlineStorage(native,dated));
 for(const mutate of [n=>n.distributionLayers[0].validity.from.text='+12000-03',n=>n.distributionLayers[0].validity.from.precision='date',n=>n.distributionLayers[0].validity.to.text='+12000-02-27',n=>n.distributionLayers[0].validity.to.precision='month',n=>n.distributionLayers[0].valueScale.min=1,n=>n.distributionLayers[0].valueScale.max=101]){const bad=structuredClone(native);mutate(bad);assert.throws(()=>compareCurrentInlineStorage(bad,dated));mutations++;}
 console.log('current Web10 typed inline single-field/inventory mutation guards: '+mutations);
});
const binary=process.env.M977_PENDING_LIFECYCLE_PROBE;
let captured;const collectNative=(binary,bundle)=>structuredClone(captured??=collectFreshNative(binary,bundle));
test('native matched pending-input tail includes every real lifecycle stage and exact history snapshots',()=>{
 assert.ok(binary,'M977_PENDING_LIFECYCLE_PROBE is required; native coverage cannot be skipped');
 const bundle=readLifecycleSources(),report=collectNative(binary,bundle);verifyNativeLifecycleReport(bundle,report);
});
test('native lifecycle rejects missing events, detached export and missing history evidence',()=>{
 assert.ok(binary);const bundle=readLifecycleSources(),report=collectNative(binary,bundle);
 for(const mutate of [r=>r.cases[0].stages.ready.state.canUndoDraft=true,r=>r.cases[0].stages.ready.mapViewState.rotationRoll=7,r=>r.cases[0].events=[],r=>{r.cases[0].stages.preview.web=r.cases[0].stages.applied.web;},r=>delete r.cases[0].stages.undo,r=>{r.cases[0].stages.review.selection.parts[0].geometry={};},r=>r.cases[0].stages.undo.history.canUndo=true]){const bad=structuredClone(report);mutate(bad);assert.throws(()=>verifyNativeLifecycleReport(bundle,bad));}
});
test('plain comparison cannot relabel Node rows as actual browser evidence',async()=>{
 const {loadLifecycleSources}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{compareLifecycle}=await import('./compare.mjs');const loaded=await loadLifecycleSources();try{const cases=[];for(const d of loaded.bundle.corpus.cases)cases.push(await runPendingLifecycleCase(loaded,d,loaded.bundle.corpus));const n=collectNative(binary,loaded.bundle);assert.equal(compareLifecycle(loaded.bundle,{runtime:{collector:'actual-chromium'},cases},n).actualBrowser,false);}finally{loaded.cleanup();}
});
test('paired interchange compares every exact geometry, identity, parent, reference and lifecycle stage',async()=>{
 const {loadLifecycleSources}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{verifyPairedLifecycle}=await import('./compare.mjs');assert.equal(typeof verifyPairedLifecycle,'function');const l=await loadLifecycleSources();try{const native=collectNative(binary,l.bundle);for(const [i,d]of l.bundle.corpus.cases.entries()){const w=await runPendingLifecycleCase(l,d,l.bundle.corpus);verifyPairedLifecycle(l.bundle,w,native.cases[i]);}}
 finally{l.cleanup();}
});
test('paired contract rejects coherent one-runtime geometry, reference, owner order and history corruption',async()=>{
 const {loadLifecycleSources,sha256}=await import('./sources.mjs'),{runPendingLifecycleCase}=await import('./runtime.mjs'),{verifyPairedLifecycle,decodeBlob}=await import('./compare.mjs');const l=await loadLifecycleSources();try{
  const native=collectNative(binary,l.bundle),def=l.bundle.corpus.cases[0],web=await runPendingLifecycleCase(l,def,l.bundle.corpus);verifyPairedLifecycle(l.bundle,web,native.cases[0]);
  const blob=d=>{const raw=JSON.stringify(d);return {base64:Buffer.from(raw).toString('base64'),sha256:sha256(raw),bytes:Buffer.byteLength(raw)};};
  const changed=structuredClone(native);for(const stage of ['applied','redo'])for(const kind of ['document','web']){const doc=decodeBlob(changed.cases[0].stages[stage][kind]).document;for(const g of doc.geometries.filter(g=>g.version===2)){const walk=v=>{if(!Array.isArray(v))return v;return v.map(x=>typeof x==='number'&&x===4?4.000000000000001:walk(x));};g.geojson.coordinates=walk(g.geojson.coordinates);}changed.cases[0].stages[stage][kind]=blob(doc);}
  // Own-runtime history, references and real-change shape invariants still pass;
  // the exact shared boundary must reject the one-ULP drift.
  verifyNativeLifecycleReport(l.bundle,changed);assert.throws(()=>verifyPairedLifecycle(l.bundle,web,changed.cases[0]),/exact shared geometry boundary/);
  for(const mutate of [
   n=>{for(const stage of ['applied','redo']){const d=decodeBlob(n.stages[stage].web).document;d.timelineRecords.parentRelations[2].parentId='target';n.stages[stage].web=blob(d);}},
   n=>{for(const stage of ['applied','redo']){const d=decodeBlob(n.stages[stage].web).document;d.territorialEntities.reverse();n.stages[stage].web=blob(d);}},
   n=>{n.stages.applied.history.canRedo=true;},
   n=>{n.stages.preview.selection.parts[0].geometry.coordinates[0][0][0]=0.125;},
   n=>{n.inputs[0].screen[0]=-1;},
   n=>{n.inputs[0].roundTrip[0]+=0.000000000000001;},
  ]){const n=structuredClone(native.cases[0]);mutate(n);assert.throws(()=>verifyPairedLifecycle(l.bundle,web,n));}
 }finally{l.cleanup();}
});
