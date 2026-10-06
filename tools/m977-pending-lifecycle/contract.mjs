import assert from 'node:assert/strict';
import {pendingProjection} from './sources.mjs';
import {verifyEditEffect} from '../m975-model-exchange/edit-effects.mjs';
const same=(a,b,m)=>assert.deepEqual(a,b,m);
export const documentKeys=Object.freeze(['territorialEntities','sourceInfo','labels','genericFeatures','hydroEdits','timelineRecords','geometries','distributionLayers','distributionEntries']);
export function rawChanges(before,after,path=''){
 if(Object.is(before,after))return [];
 if(!before||!after||typeof before!=='object'||typeof after!=='object'||Array.isArray(before)!==Array.isArray(after))return [{path,kind:'value',before,after}];
 const a=Object.keys(before),b=Object.keys(after),out=[];
 if(!Array.isArray(before)&&a.length===b.length&&a.every(k=>b.includes(k))&&JSON.stringify(a)!==JSON.stringify(b))out.push({path,kind:'object-key-order',before:a,after:b});
 for(const key of new Set([...a,...b])){const child=path+'/'+key.replaceAll('~','~0').replaceAll('/','~1');if(!Object.hasOwn(before,key))out.push({path:child,kind:'added',after:after[key]});else if(!Object.hasOwn(after,key))out.push({path:child,kind:'removed',before:before[key]});else out.push(...rawChanges(before[key],after[key],child));}return out;
}
export function verifyWebRestoration(beforeRaw,afterRaw){
 const before=JSON.parse(beforeRaw),after=JSON.parse(afterRaw);same(Object.keys(after),Object.keys(before),'all root fields retained');
 for(const key of documentKeys)same(after[key],before[key],'exact established document/history scope '+key);
 const expected=[{path:'/genericFeatures/0/properties/source',kind:'object-key-order',before:[] ,after:[]}];
 expected[0].before=['schemaVersion','kind','dataset','version','sourceId','sourceFormat','sourceType','importedAt','details'];
 expected[0].after=['schemaVersion','kind','dataset','sourceId','sourceFormat','sourceType','version','importedAt','details'];
 for(const key of ['countries','regions','distributions','hydro','labels','countryLabels'])expected.push({path:'/itemVisibility/'+key,kind:'added',after:{}});
 same(rawChanges(before,after),expected,'exact source-backed raw history delta inventory; no other presentation loss');
 assert.notEqual(afterRaw,beforeRaw,'raw whole-save equality remains false');return expected;
}
export function verifyWebLifecycleCase(bundle,definition,row){
 same(Object.keys(row).sort(),['case','input','stages','inputs','transport','errors'].sort(),'exact raw lifecycle case');assert.equal(row.case,definition.id);same(row.input,definition);same(Object.keys(row.stages),definition.stages,'all ordered lifecycle stages required');same(row.errors,[]);
 const before=row.stages.before,applied=row.stages.applied;
 for(const [name,s]of Object.entries(row.stages)){
  assert.equal(s.observed,true,'observed '+name);assert.equal(typeof s.canonical,'string');same(JSON.stringify(JSON.parse(s.canonical)),s.canonical);
  const edited=['applied','redo'].includes(name);if(['undo','redo'].includes(name))verifyWebRestoration(name==='undo'?before.canonical:applied.canonical,s.canonical);else assert.equal(s.canonical,edited?applied.canonical:before.canonical,'exact full raw project '+name);
  same(s.history,edited?{undo:1,redo:0}:name==='undo'?{undo:0,redo:1}:{undo:0,redo:0},'actual project history '+name);
  assert.equal(s.revision,before.revision+(['applied','undo','redo'].includes(name)?{applied:1,undo:2,redo:3}[name]:0),'revision '+name);
 }
 same(JSON.parse(before.canonical),JSON.parse(bundle.fixtureRaw),'unchanged persisted model installed before edits');
 for(const key of Object.keys(JSON.parse(before.canonical)))if(!['territorialEntities','timelineRecords','geometries'].includes(key))same(JSON.parse(applied.canonical)[key],JSON.parse(before.canonical)[key],'Apply preserves unrelated field '+key);
 assert.notEqual(applied.canonical,before.canonical,'Apply must change real project');verifyEditEffect({operation:'annex',full:false},JSON.parse(before.canonical),JSON.parse(applied.canonical));
 const projection=pendingProjection(bundle,definition),expectedInputs=[];
 for(const prefix of ['', 'retry']){
  const n=s=>prefix?prefix+s[0].toUpperCase()+s.slice(1):s;
  for(const key of ['activation','firstPending','secondPending','held']){const s=row.stages[n(key)];assert.equal(s.pending,true);assert.equal(s.draftInputActive,false);same(s.coordinates,[]);assert.equal(s.draftUndo,0);assert.equal(s.draftRedo,0);}
  const ready=row.stages[n('ready')];assert.equal(ready.pending,false);assert.equal(ready.draftInputActive,true);same(ready.coordinates,[]);assert.equal(ready.draftUndo,0);assert.equal(ready.draftRedo,0);
  const points=[];
  for(const [i,p] of corpusPoints(bundle.corpus)){
   const name=n(i<2?['firstPending','secondPending'][i]:['firstReady','secondReady','thirdReady','fourthReady'][i-2]);expectedInputs.push(name);
   const input=row.inputs.find(x=>x.stage===name);assert.ok(input);same(input.coordinate,p);same(input.screen,[...projection(p)]);same(input.roundTrip,[...projection.invert(input.screen)]);same(input.roundTrip,input.coordinate,'actual pinned D3 inverse equals intended geographic input');assert.ok(input.screen[0]>=0&&input.screen[0]<definition.profile.width&&input.screen[1]>=0&&input.screen[1]<definition.profile.height);assert.equal(input.pointerType,definition.profile.pointerType);assert.equal(input.prePending,i<2);
   if(i>=2){points.push(input.roundTrip);same(row.stages[name].coordinates,points,'ordered actual ready points');assert.equal(row.stages[name].draftUndo,points.length);}
  }
  assert.equal(row.stages[n('finished')].outcome,true);assert.equal(row.stages[n('finished')].phase,'candidate');
  for(const key of ['preview','review']){const s=row.stages[n(key)];assert.equal(s.previewReady,true);assert.ok(s.preview);assert.equal(s.outcome,true);assert.equal(s.session.parts.length,1);}
  assert.equal(row.stages[n('review')].stage,'review');
 }
 same(row.inputs.map(x=>x.stage),expectedInputs);for(const n of ['before','cancel','applied','undo','redo']){assert.equal(row.stages[n].session,null);assert.equal(row.stages[n].preview,null);}
 verifyTransport(row);return row;
}
const corpusPoints=corpus=>[...corpus.pendingPoints,...corpus.readyPoints].map((p,i)=>[i,p]);

function verifyTransport(row){
 const sent=new Map(),responses=new Map(),delivered=new Set(),holds=new Map(),released=new Set(),ready=new Set(),dead=new Set();let rebases=0;const commands=[];
 for(const [index,e]of row.transport.entries()){
  assert.equal(e.sequence,index);assert.ok(row.input.stages.includes(e.phase)||e.phase==='cleanup');assert.ok(Number.isSafeInteger(e.worker)&&e.worker>0);const m=e.message,key=e.worker+':'+(m?.requestId??e.requestId);
  assert.ok(['request','response','delivered','held','released','terminate'].includes(e.kind));
  if(e.kind==='terminate'){assert.equal(e.phase,'cleanup');assert.ok(!dead.has(e.worker));dead.add(e.worker);continue;}
  if(e.kind==='held'){assert.ok(!delivered.has(key),'hold must precede actual owner delivery');assert.ok(responses.has(key));assert.ok(['secondPending','retrySecondPending'].includes(e.phase));assert.equal(responses.get(key).phase,e.phase);assert.ok(![...holds.values()].some(h=>h.phase===e.phase),'exactly one held preparation per attempt');const pendingStage=e.phase==='secondPending'?'held':'retryHeld';assert.equal(sent.get(key).message.jobKey,row.stages[pendingStage].session.id+':territory-components');same(sent.get(key).message.payload.parts,[]);assert.equal(sent.get(key).message.operation,'territory-components');assert.ok(!holds.has(key));holds.set(key,e);continue;}
  if(e.kind==='released'){assert.ok(holds.has(key));assert.ok(!released.has(key));released.add(key);assert.equal(e.phase,holds.get(key).phase==='secondPending'?'held':'retryHeld','release belongs to matched preparation attempt');continue;}
  assert.ok(m&&typeof m==='object');
  if(e.kind==='request'){
   assert.ok(!dead.has(e.worker));if(m.type==='rebase'){rebases++;continue;}
   if(m.type==='execute'){assert.ok(ready.has(e.worker),'real Worker ready before request');assert.ok(!sent.has(key));sent.set(key,e);commands.push(e);}
   else assert.ok(['discard','commit','boundary-invalidate','sync-patch'].includes(m.type),'known production transport message');
  }else if(e.kind==='response'){
   if(m.type==='ready')continue;assert.equal(m.type,'result');assert.ok(sent.has(key));assert.ok(!responses.has(key));assert.equal(m.ok,true);assert.equal(e.operation,sent.get(key).message.operation);assert.equal(m.targetRevision,sent.get(key).message.targetRevision);responses.set(key,e);
  }else if(e.kind==='delivered'){
   assert.equal(e.terminated,false);if(m.type==='ready'){assert.ok(row.transport.slice(0,index).some(t=>t.kind==='response'&&t.worker===e.worker&&JSON.stringify(t.message)===JSON.stringify(m)));ready.add(e.worker);continue;}
   assert.ok(responses.has(key));same(m,responses.get(key).message,'unaltered actual Worker result');assert.ok(!delivered.has(key));if(holds.has(key))assert.ok(released.has(key),'held result must release before delivery');delivered.add(key);
  }
 }
 assert.equal(rebases,1);assert.equal(sent.size,12);assert.equal(responses.size,sent.size);assert.equal(delivered.size,sent.size);assert.equal(holds.size,2);assert.equal(released.size,2);assert.equal(dead.size,1);
 same(commands.map(e=>e.message.operation),Array(2).fill(['territory-components','territory-selection','annex','territory-components','territory-selection','annex']).flat(),'complete real Worker operation inventory');
 const commit=row.transport.filter(e=>e.kind==='request'&&e.message.type==='commit');assert.equal(commit.length,1);assert.equal(commit[0].phase,'applied');assert.equal(commit[0].message.requestId,row.stages.retryReview.preview.workerRequestId);
 const discard=row.transport.filter(e=>e.kind==='request'&&e.message.type==='discard');assert.ok(discard.some(e=>e.phase==='cancel'&&e.message.requestId===row.stages.review.preview.workerRequestId),'Cancel actually discards original preview');
 for(const stage of ['undo','redo'])assert.ok(row.transport.some(e=>e.kind==='request'&&e.phase===stage&&e.message.type==='sync-patch'),'history synchronizes real Worker '+stage);
 for(const prefix of ['', 'retry']){
  const n=s=>prefix?prefix+s[0].toUpperCase()+s.slice(1):s;
  const finished=row.stages[n('finished')],preview=row.stages[n('preview')],review=row.stages[n('review')];
  assert.equal(finished.session.candidates.length,1);same(finished.session.selectedCandidateIds,finished.session.candidates.map(c=>c.id));assert.equal(finished.session.parts.length,0);
  const candidate=finished.session.candidates[0].geometry;assert.ok(candidate&&candidate.coordinates.length);assert.equal(preview.session.parts[0].method,'polygon');same(preview.session.parts[0].geometry,candidate,'Finish candidate archived unchanged');same(review.session.parts,preview.session.parts);same(review.preview,preview.preview,'review retains exact actual preview');
  const selection=commands.find(e=>e.phase===n('finished')&&e.message.operation==='territory-selection');assert.ok(selection);same(selection.message.payload.selected,[candidate]);same(selection.message.payload.currentGeometry,candidate);
  const archive=commands.find(e=>e.phase===n('preview')&&e.message.operation==='territory-components');assert.ok(archive);same(archive.message.payload.parts,[candidate]);
  for(const [stage,s]of [[n('finished'),finished],[n('preview'),preview]]){
   const request=commands.find(e=>e.message.requestId===s.preview.workerRequestId);assert.ok(request);assert.equal(request.message.operation,'annex');assert.equal(request.phase,stage);
   const result=responses.get(request.worker+':'+request.message.requestId).message.result;
   same(s.preview.afterFeatures,result.features);same(s.preview.affectedIds,result.affectedIds);same(s.preview.removedIds,result.removedIds);same(s.preview.delta,result.preview.delta);same(s.preview.metrics,result.preview.metrics);same(s.preview.validation,result.preview.validation);assert.equal(s.preview.status,'ready');assert.equal(s.preview.operation,'annex');assert.equal(s.preview.baseDataRevision,row.stages.before.revision);assert.equal(s.preview.validation.blocking,false);
  }
  same(preview.preview.afterFeatures,finished.preview.afterFeatures,'same intended geometry after archiving');
 }
 const applied=JSON.parse(row.stages.applied.canonical);for(const f of row.stages.retryReview.preview.afterFeatures){const ref=applied.timelineRecords.geometryBindings.find(b=>b.entityId===f.id).geometryRef;const geometry=applied.geometries.find(g=>g.id===ref.id&&g.version===ref.version);same(geometry.geojson,f.geometry,'Apply commits actual preview geometry '+f.id);}
}
