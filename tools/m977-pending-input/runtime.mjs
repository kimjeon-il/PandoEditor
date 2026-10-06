// Browser-portable observation of original production functions. No replacement
// draft-input predicate, geometry, selection policy, or history implementation.
export async function runPendingInputCase(loaded,definition,corpus){
 const {api}=loaded,clone=x=>structuredClone(x),require=(c,m)=>{if(!c)throw Error(m);};
 const life=loaded.runtime.createRuntime(api),make=row=>{const [x0,y0,x1,y1]=row.bounds;return api.createTerritorialFeature({id:row.id,name:row.id,entityKind:'general',geometry:{type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]}});};
 life.entityStore.restoreProject(api.createStaticTerritorialSnapshot(corpus.features.map(make)));
 Object.assign(life.state,{labels:[],distributionLayers:[],distributionEntries:[],labelSettings:{},itemVisibility:{},layerPresentation:{schemaVersion:4,styles:{},objectStyles:{},objectOrder:[]},spacePanActive:false});
 const transport=[],stages={},inputs=[];let phase='setup',workerId=0,hold=true;const held=[];
 const event=(kind,details={})=>transport.push({sequence:transport.length,phase,kind,...clone(details)});
 const createWorker=()=>{
  const actual=loaded.createWorker(),id=++workerId,operations=new Map();let terminated=false;
  const adapter={onmessage:null,onerror:null,postMessage(message){if(message.type==='execute')operations.set(message.requestId,message.operation);event('request',{worker:id,message});actual.postMessage(message);},terminate(){terminated=true;event('terminate',{worker:id});actual.terminate();}};
  actual.onmessage=e=>{const operation=operations.get(e.data.requestId)||null;event('response',{worker:id,operation,message:e.data});
   if(hold&&operation==='territory-components'&&e.data.type==='result'){held.push({id,adapter,e,terminated:()=>terminated});event('held',{worker:id,requestId:e.data.requestId});}
   else{event('delivered',{worker:id,terminated,message:e.data});adapter.onmessage?.(e);}};
  actual.onerror=e=>adapter.onerror?.(e);return adapter;
 };
 const client=api.createMapEditWorkerClient({createWorker,getEntities:life.entityRepository.list,getFeatureById:life.entityRepository.get,getTargetRevision:()=>life.state.stateRevision});
 life.ports.spatialQuery.mapEditClient=client;
 const h=loaded.selectionRuntime.createSelectionRuntime(api,{lifecycle:life,features:life.entityRepository.list()});h.ports.spatialQuery.mapEditClient=client;
 const view=definition.view,projection=api.d3.geo.equirectangular().scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
 const banners=[];h.ports.taskUi.setModeBanner=t=>banners.push(String(t));
 const editing=api.createEditingDomain({projectDomain:h.ports.domains.projectDomain,toolController:{applyToolPresentation:t=>{h.state.tool=t;}},draftServices:{
  getToolConfig:t=>{if(h.state.geometryPreview.session)return null;const config=api.toolDraftDefinition(t,h.state);return config?{...config,minimumPoints:config.shape==='polygon'?3:2}:null;},
  screenToCoordinate:p=>projection.invert(p),projectCoordinate:projection,snapCandidates:()=>[]}});
 h.ports.domains.editingDomain=editing;life.domains.editingDomain=editing;
 h.ports.countryEditingA.editingDraftCoordinates=()=>editing.snapshot().draft.coords;
 h.ports.draftPresentation={editingDraftSnapshot:()=>editing.snapshot().draft};
 h.ports.mapView={screenToGeo:p=>projection.invert(p),activeProjection:()=>projection};
 h.ports.platform.d3={...api.d3,event:{pointerType:definition.profile.pointerType}};
 h.ports.territorySelectionB={territorySelectionCountryPickingActive:h.workflow.countryPickingActive,territorySelectionCountryInstruction:h.workflow.sourceCountryInstruction};
 const picking=api.createObjectPicking();picking.connect(h.ports);
 const canonical=()=>JSON.stringify(loaded.runtime.observe(life).document),before=canonical();
 const record=(name,outcome=null)=>{
  phase=name;const s=h.workflow.activeSession(),draft=editing.snapshot().draft;
  stages[name]={observed:true,outcome,stage:s?.stage||null,phase:s?.activePhase||null,method:s?.activeMethod||null,requestedMethod:s?.requestedMethod||null,
   confirmation:clone(s?.methodChangeConfirmation||null),preparationActive:!!s?.preparation,pending:!!s?.preparation||(s?.stage==='selection'&&s?.activePhase==='preparing'),draftInputActive:editing.draftInputActive(),coordinates:clone(draft.coords),
   draftUndo:draft.historyCount,draftRedo:draft.futureCount,projectUndo:h.state.history.length,projectRedo:h.state.future.length,revision:h.state.stateRevision,
   canonical:canonical(),canonicalUnchanged:canonical()===before,preview:!!h.state.geometryPreview.session,
   identities:{sessionId:s?.id??null,computationEpoch:s?.computationEpoch??null,preparationKey:s?.preparation?.key??null,projectGeneration:s?.projectGeneration??null,sourceRevision:s?.sourceRevision??null,selectionRevision:s?.selectionRevision??null,workerRequests:s?.workerRequests??null},
   rawSession:s?clone(Object.fromEntries(['stage','activePhase','activeMethod','requestedMethod','methodChangeConfirmation','parts','candidates','selectedCandidateIds','selectedComponentKeys','computationPending','previewPending','workerRequests','settingsRevision','sourceRevision','selectionRevision','computationEpoch','projectGeneration'].filter(k=>s[k]!==undefined).map(k=>[k,s[k]]))):null,
   banner:banners.at(-1)||''};return stages[name];
 };
 const until=async f=>{const end=Date.now()+15000;while(!f()){require(Date.now()<end,'Timed out waiting for real Worker component result');await new Promise(r=>setTimeout(r,1));}};
 const release=()=>{hold=false;for(const x of held.splice(0)){event('released',{worker:x.id,requestId:x.e.data.requestId,terminated:x.terminated()});event('delivered',{worker:x.id,message:x.e.data,terminated:x.terminated()});x.adapter.onmessage?.(x.e);}};
 const tap=async(index,name)=>{phase=name;require(index===corpus.tapPointIndexes[name],'declared tap index');const coordinate=corpus.points[index],screen=projection(coordinate),before=h.workflow.activeSession();inputs.push({stage:name,coordinate:clone(coordinate),screen:clone(screen),roundTrip:clone(projection.invert(screen)),pointerType:definition.profile.pointerType,prePhase:before?.activePhase??null,prePreparation:!!before?.preparation,preDraftInputActive:editing.draftInputActive()});const result=await picking.handleMapClick(screen);record(name,result??null);};
 const switchSequence=async()=>{
  record('switch',await h.workflow.selectMethod('line'));
  if(!h.workflow.activeSession()?.methodChangeConfirmation){for(const name of ['cancelSwitch','switchAgain','confirmSwitch'])stages[name]={observed:false,reason:'No method confirmation; dependent decision unavailable'};return;}
  record('cancelSwitch',h.workflow.cancelMethodChange());record('switchAgain',await h.workflow.selectMethod('line'));record('confirmSwitch',await h.workflow.confirmMethodChange());
 };
 let activation;
 try{
  require(h.workflow.start('annex',{targetCountryId:'target',sourceCountryIds:['donor']}),'start annex');require(await h.workflow.advance(),'advance');
  phase='activation';activation=h.workflow.selectMethod('polygon');record('activation');
  if(definition.scenario==='pending-switch'){
   phase='switchPending';const next=h.workflow.selectMethod('line');record('switchPending');release();const stale=await activation,fresh=await next;record('afterLateCompletion',{staleActivation:stale,newActivation:fresh});
  }else{
   if(['two-pending-empty-switch','pending-ready-switch-history'].includes(definition.scenario))await tap(0,'firstPending');
   if(definition.scenario==='two-pending-empty-switch')await tap(1,'secondPending');
   await until(()=>held.length>0);record('held');
   if(definition.scenario.startsWith('held-')){
    phase='afterAction';const action=definition.scenario.slice(5);let newer=null,result;
    if(action==='supersede'){newer=h.workflow.selectMethod('line');result=null;}else result=h.workflow[action==='clear'?'clear':'back']();
    record('afterAction',result);release();const stale=await activation,fresh=newer?await newer:null;await Promise.resolve();record('afterLateCompletion',{staleActivation:stale,newActivation:fresh});
    if(action==='back')await tap(0,'tapAfterBack');
   }else{
    release();require(await activation===true,'activation failed '+JSON.stringify(h.errors));record('ready');
    if(definition.scenario==='two-pending-empty-switch')await switchSequence();
    else{await tap(1,'firstReady');await tap(2,'secondReady');
     if(definition.scenario==='ready-same-method')record('sameMethod',await h.workflow.selectMethod('polygon'));
     else{record('undoDraft',editing.performDraftUndo());record('redoDraft',editing.performDraftRedo());await switchSequence();}
    }
   }
  }
  return {case:definition.id,input:clone(definition),stages,inputs,transport,errors:clone(h.errors),beforeCanonical:before,
   limits:{rawParity:false,fullDOM:false,actualTouchDispatch:false,pixelParity:false,gpu:false,fullProjectSerialization:false,projectApplyUndoRedo:false,snapCandidates:false,heldResultsAreWorkerComplete:true,workerCPUExecutionPhase:false,stimulus:'actual handleMapClick -> actual createEditingDomain with actual toolDraftDefinition',viewport:clone(definition.profile)}};
 }finally{phase='cleanup';h.workflow.clear();release();client.stop();editing.dispose();}
}

// Browser-portable fail-closed integrity check, not a parity golden.
export function verifyWebCase(definition,row,corpus,projection){
 const req=(v,m)=>{if(!v)throw Error(m);},same=(a,b,m)=>req(JSON.stringify(a)===JSON.stringify(b),m);
 req(typeof projection==='function'&&typeof projection.invert==='function','actual pinned D3 projection required');
 req(corpus?.schema==='pando-m977-pending-input-corpus'&&corpus.version===1,'verified corpus required');
 same(corpus.cases.find(c=>c.id===definition.id),definition,'declared corpus case');
 req(row.case===definition.id,'case identity');same(row.input,definition,'exact case input');same(Object.keys(row.stages),definition.stages,'exact ordered stages');same(row.errors,[],'no workflow errors');
 req(typeof row.beforeCanonical==='string','before canonical');req(JSON.stringify(JSON.parse(row.beforeCanonical))===row.beforeCanonical,'canonical encoding');
 const document=JSON.parse(row.beforeCanonical);
 same(Object.keys(document).sort(),['entities','identities','timelineRecords','geometryVersions','distributionLayers','distributionEntries'].sort(),'complete canonical observation shape');
 for(const key of ['entities','identities','geometryVersions','distributionLayers','distributionEntries'])req(Array.isArray(document[key]),'canonical collection '+key);
 same(document.entities.map(f=>f.id),corpus.features.map(f=>f.id),'canonical fixture membership');same(document.identities.map(f=>f.id),corpus.features.map(f=>f.id),'canonical identity membership');
 req(document.geometryVersions.length===corpus.features.length&&document.timelineRecords?.schemaVersion===1,'canonical geometry and timeline evidence');
 for(const key of ['lifetimes','geometryBindings','parentRelations'])req(Array.isArray(document.timelineRecords[key])&&document.timelineRecords[key].length===corpus.features.length,'canonical complete timeline '+key);
 for(const [index,feature]of corpus.features.entries()){
  const [x0,y0,x1,y1]=feature.bounds,geometry={type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]},entity=document.entities[index],identity=document.identities[index];
  same(entity.geometry,geometry,'canonical fixture geometry');req(entity.entityKind==='general'&&entity.parentId===''&&entity.coverageMode==='explicit'&&entity.properties.name===feature.id,'canonical fixture identity');
  req(identity.type==='Feature'&&identity.geometry===null&&identity.properties.entityKind==='general'&&identity.properties.name===feature.id,'canonical identity record');
  const binding=document.timelineRecords.geometryBindings.find(b=>b.entityId===feature.id);req(binding&&binding.validFrom===null&&binding.validTo===null&&binding.geometryRef.version===1,'canonical geometry binding');
  const version=document.geometryVersions.find(g=>g.id===binding.geometryRef.id&&g.version===binding.geometryRef.version);req(version,'canonical referenced geometry version');same(version.geojson,geometry,'canonical version geometry');
  const lifetime=document.timelineRecords.lifetimes.find(l=>l.entityId===feature.id),parent=document.timelineRecords.parentRelations.find(p=>p.entityId===feature.id);
  req(lifetime&&lifetime.validFrom===null&&lifetime.validTo===null,'canonical static lifetime');req(parent&&parent.parentId===''&&parent.coverageMode==='explicit'&&parent.validFrom===null&&parent.validTo===null,'canonical static parent');
 }
 same(document.distributionLayers,[],'bounded distribution layers');same(document.distributionEntries,[],'bounded distribution entries');

 for(const [name,s]of Object.entries(row.stages)){
  req(s.observed===true,'all web stages observed '+name);req(s.canonical===row.beforeCanonical&&s.canonicalUnchanged,'exact document immutability '+name);
  req(s.projectUndo===0&&s.projectRedo===0&&s.revision===0,'project history/revision unchanged '+name);req(s.preview===false,'no preview in bounded corpus '+name);
  req(Array.isArray(s.coordinates)&&Number.isInteger(s.draftUndo)&&s.draftUndo>=0&&Number.isInteger(s.draftRedo)&&s.draftRedo>=0,'actual draft state '+name);
  req(typeof s.preparationActive==='boolean'&&typeof s.draftInputActive==='boolean','explicit domain state '+name);
  req(s.pending===(s.preparationActive||(s.stage==='selection'&&s.phase==='preparing')),'pending describes current preparation '+name);
  req(s.preparationActive===(s.identities.preparationKey!==null),'preparation identity '+name);
  req(s.draftInputActive===(s.stage==='selection'&&s.phase==='drawing'&&['polygon','line'].includes(s.method)),'actual configured drawing domain '+name);
  if(s.rawSession){
   for(const [field,raw]of Object.entries({stage:'stage',phase:'activePhase',method:'activeMethod',requestedMethod:'requestedMethod',confirmation:'methodChangeConfirmation'}))same(s[field],s.rawSession[raw]??null,'raw session '+name+' '+field);
   for(const field of ['computationEpoch','projectGeneration','sourceRevision','selectionRevision','workerRequests'])same(s.identities[field],s.rawSession[field]??null,'raw identity '+name+' '+field);
  }else req(s.stage===null&&s.phase===null&&s.method===null&&s.identities.sessionId===null,'absent session '+name);
 }
 for(const name of ['activation','firstPending','secondPending','held','switchPending'])if(row.stages[name]){
  const s=row.stages[name];req(s.pending&&s.stage==='selection'&&s.phase==='preparing'&&!s.draftInputActive,'actual pending '+name);
  req(s.coordinates.length===0&&s.draftUndo===0&&s.draftRedo===0,'pending taps not retained in draft or history '+name);
 }
 if(row.stages.held)req(row.stages.held.preparationActive&&row.stages.held.identities.workerRequests===1,'held component preparation remains outstanding');
 if(row.stages.ready){const s=row.stages.ready;req(!s.pending&&s.draftInputActive&&s.draftUndo===0&&s.draftRedo===0&&s.identities.workerRequests===0,'genuine ready domain');same(s.coordinates,[],'ready starts empty');}
 const tapNames=definition.stages.filter(name=>['firstPending','secondPending','firstReady','secondReady','tapAfterBack'].includes(name));same(row.inputs.map(input=>input.stage),tapNames,'exact input taps');
 const byTap=Object.fromEntries(row.inputs.map(input=>[input.stage,input]));
 for(const input of row.inputs){
  req(input.pointerType===definition.profile.pointerType,'actual pointer type');
  const index=corpus.tapPointIndexes[input.stage];req(Number.isInteger(index)&&index>=0&&index<corpus.points.length,'declared tap index');same(input.coordinate,corpus.points[index],'exact declared tap coordinate');
  req(input.screen.length===2&&input.roundTrip.length===2&&[...input.screen,...input.roundTrip].every(Number.isFinite),'finite public projection observations');
  same(input.screen,projection(input.coordinate),'screen coordinate is actual pinned D3 projection');same(input.roundTrip,projection.invert(input.screen),'inverse is actual pinned D3 projection');
  const previous=row.stages[definition.stages[definition.stages.indexOf(input.stage)-1]];
  same(input.prePhase,previous.phase,'tap pre-phase');same(input.prePreparation,previous.preparationActive,'tap pre-preparation');same(input.preDraftInputActive,previous.draftInputActive,'tap pre-domain');
 }
 if(row.stages.firstReady)same(row.stages.firstReady.coordinates,[byTap.firstReady.roundTrip],'one ready tap coordinate');
 if(row.stages.secondReady)same(row.stages.secondReady.coordinates,[byTap.firstReady.roundTrip,byTap.secondReady.roundTrip],'ordered ready tap coordinates');
 for(const [name,count]of [['firstReady',1],['secondReady',2]])if(row.stages[name])req(row.stages[name].draftUndo===count&&row.stages[name].draftRedo===0,'ready tap history '+name);
 if(row.stages.undoDraft){
  const u=row.stages.undoDraft,r=row.stages.redoDraft;same(u.coordinates,row.stages.firstReady.coordinates,'Undo preserves first input');req(u.outcome===true&&u.draftUndo===1&&u.draftRedo===1,'draft Undo');
  same(r.coordinates,row.stages.secondReady.coordinates,'draft Redo');req(r.outcome===true&&r.draftUndo===2&&r.draftRedo===0,'draft Redo history');
 }
 if(row.stages.switch){
  const s=row.stages.switch,c=row.stages.cancelSwitch,a=row.stages.switchAgain,f=row.stages.confirmSwitch;
  for(const [name,state]of [['switch',s],['switchAgain',a]]){same(state.confirmation,{type:'method',method:'line'},'actual switch confirmation '+name);req(state.method==='polygon'&&state.requestedMethod==='line'&&state.outcome===false,'pending method decision '+name);}
  for(const state of [c,a]){same(state.coordinates,s.coordinates,'decision preserves points');req(state.draftUndo===s.draftUndo&&state.draftRedo===s.draftRedo,'decision preserves history');}
  req(c.outcome===true&&c.method==='polygon'&&c.requestedMethod==='polygon'&&c.confirmation===null,'cancel clears decision');
  req(f.outcome===true&&f.method==='line'&&f.requestedMethod===null&&f.confirmation===null&&!f.pending&&f.coordinates.length===0&&f.draftUndo===0&&f.draftRedo===0,'confirm clears old draft and decision');
 }
 if(row.stages.sameMethod){same(row.stages.sameMethod.coordinates,row.stages.secondReady.coordinates,'same method preserves points');req(row.stages.sameMethod.draftUndo===row.stages.secondReady.draftUndo&&!row.stages.sameMethod.confirmation,'same method preserves history');}
 if(row.stages.afterLateCompletion){req(row.stages.afterLateCompletion.outcome.staleActivation===false,'old activation rejected');req(row.stages.afterLateCompletion.coordinates.length===0&&row.stages.afterLateCompletion.draftUndo===0&&row.stages.afterLateCompletion.draftRedo===0,'no stale points or history');
  if(definition.scenario==='held-clear')req(row.stages.afterLateCompletion.identities.sessionId===null,'Clear does not resurrect');
  if(definition.scenario==='held-back')for(const name of ['afterAction','afterLateCompletion','tapAfterBack']){const s=row.stages[name];req(s.stage==='setup'&&!s.pending&&!s.draftInputActive&&s.coordinates.length===0&&s.draftUndo===0&&s.draftRedo===0,'Back keeps setup '+name);}
  if(['held-supersede','pending-switch'].includes(definition.scenario))req(row.stages.afterLateCompletion.method==='line'&&!row.stages.afterLateCompletion.pending&&row.stages.afterLateCompletion.outcome.newActivation===true,'new method survives');
 }
 req(row.transport.length>0,'actual Worker transport required');
 // Correlate the full delivery chain. A "held" label without the original
 // response and an identical later delivery is not Worker-result evidence.
 const workers=new Map(),requests=new Map(),key=(worker,id)=>worker+':'+id;
 for(const [i,e]of row.transport.entries()){
  req(e.sequence===i&&Number.isInteger(e.worker)&&e.worker>0,'complete transport sequence');
  req(definition.stages.includes(e.phase)||e.phase==='cleanup','transport phase');
  const m=e.message;
  if(e.kind==='request'&&m?.type==='rebase'){
   req(!workers.has(e.worker),'unique Worker rebase');workers.set(e.worker,{rebase:e,ready:null,readyDelivered:null,termination:null});continue;
  }
  const worker=workers.get(e.worker);req(worker,'transport belongs to actual Worker');
  if(e.kind==='request'&&m?.type==='execute'){
   req(worker.readyDelivered&&!worker.termination,'execute after live Worker ready');req(m.operation==='territory-components'&&Number.isInteger(m.requestId)&&m.requestId>0,'actual production component request');
   const id=key(e.worker,m.requestId);req(!requests.has(id),'unique Worker request');requests.set(id,{request:e,response:null,held:null,released:null,delivered:null,cancel:null});
  }else if(e.kind==='response'&&m?.type==='ready'){
   req(!worker.ready&&!worker.termination&&e.operation===null,'unique live Worker readiness');
   for(const field of ['dataRevision','geometryRevision','targetRevision'])same(m[field],worker.rebase.message[field],'ready identity '+field);
   worker.ready=e;
  }else if(e.kind==='delivered'&&m?.type==='ready'){
   req(worker.ready&&!worker.readyDelivered&&!worker.termination&&e.terminated===false,'ready delivered once');same(m,worker.ready.message,'original ready delivery');worker.readyDelivered=e;
  }else if(e.kind==='terminate'){
   req(!worker.termination,'Worker terminates once');worker.termination=e;
  }else{
   const id=e.requestId??m?.requestId,item=requests.get(key(e.worker,id));req(item,'event belongs to actual execute');
   if(e.kind==='response'&&m?.type==='result'){
    req(!item.response&&!worker.termination&&m.ok===true&&m.result&&typeof m.result==='object','one actual successful result');same(e.operation,item.request.message.operation,'response operation');
    for(const field of ['requestId','jobKey','dataRevision','geometryRevision','targetRevision'])same(m[field],item.request.message[field],'response identity '+field);item.response=e;
   }else if(e.kind==='held'){
    req(item.response&&!item.held&&!item.delivered&&!worker.termination,'hold original completed result once');item.held=e;
   }else if(e.kind==='released'){
    req(item.held&&!item.released&&!item.delivered,'release a held result once');req(e.terminated===!!worker.termination,'release termination evidence');item.released=e;
   }else if(e.kind==='delivered'&&m?.type==='result'){
    req(item.response&&!item.delivered&&(!item.held||item.released),'result delivered once after response/release');same(m,item.response.message,'original result delivery');req(e.terminated===!!worker.termination,'delivery termination evidence');item.delivered=e;
   }else if(e.kind==='request'&&m?.type==='cancel'){
    req(!item.cancel&&!item.delivered&&!worker.termination,'cancel outstanding request once');same(m.targetRevision,item.request.message.targetRevision,'cancel target revision');item.cancel=e;
   }else req(false,'unsupported transport event');
  }
 }
 const exchanges=[...requests.values()];req(exchanges.length===(definition.scenario==='held-supersede'?2:1),'bounded actual component request count');
 for(const worker of workers.values())req(worker.readyDelivered&&worker.termination,'complete Worker lifecycle');
 for(const item of exchanges)req(item.response&&item.delivered,'complete request response delivery');
 const initial=exchanges[0],late=definition.scenario.startsWith('held-');
 if(row.stages.held){
  req(initial.held&&initial.released,'initial actual result held and released');
  for(const e of [initial.released,initial.delivered])req(e.phase===(late?'afterAction':'held')&&e.terminated===late,'release at declared decision boundary');
  if(late){const termination=workers.get(initial.request.worker).termination;req(initial.cancel&&initial.cancel.phase==='afterAction'&&termination.phase==='afterAction'&&initial.held.sequence<initial.cancel.sequence&&initial.cancel.sequence<termination.sequence&&termination.sequence<initial.released.sequence,'cancel and terminate before late delivery');}
  else req(!initial.cancel,'ready result was not cancelled');
 }else req(definition.scenario==='pending-switch'&&!initial.held&&!initial.released&&!initial.cancel&&initial.request.phase==='switchPending','immediate supersession observes only replacement Worker request');
 if(exchanges.length===2){const replacement=exchanges[1];req(replacement.request.worker!==initial.request.worker&&replacement.request.sequence>initial.delivered.sequence&&replacement.request.phase==='afterAction'&&!replacement.held&&!replacement.released&&!replacement.cancel,'new Worker survives held supersession');}
 req(row.limits.fullDOM===false&&row.limits.rawParity===false&&row.limits.heldResultsAreWorkerComplete===true,'scope is explicit');return row;
}
