/** Browser-portable driver. All candidate/draft/snap computation is pinned production code. */
export function snapCases(api){
 const square=(x0,y0,x1,y1)=>({type:'Polygon',coordinates:[[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]]}),
  feature=(id,geometry,extra={})=>api.createTerritorialFeature({id,name:id,entityKind:'general',parentId:'',coverageMode:'explicit',geometry,...extra}),
  generic=(id,geometry)=>({type:'Feature',id,properties:{schemaVersion:1,name:id},geometry}),
  view={kind:'flat',scale:1000,translate:[512,384],rotate:[0,0,0],center:[0,0],size:{width:1024,height:768}},
  make=(id,features,queries,extra={})=>({id,features,genericFeatures:[],activeOwnerIds:['a'],sourceGeometry:null,tool:'territorial-border',view:structuredClone(view),queries:queries.map(q=>Array.isArray(q)?{coordinate:q,pointerType:'mouse'}:q),...extra});
 const same=[feature('a',square(0,0,2,2)),feature('b',square(0,0,2,2))],cases=[
  make('snap-vertex-cold-ready',[feature('a',square(0,0,2,2))],[[-0.15,-0.1],[-0.16,-0.1],[-0.01,0.1],[2.1,2.1]]),
  make('snap-distance-before-kind',[feature('a',square(0,0,2,2))],[[0.2,1]]),
  make('snap-intersection-priority',[feature('a',square(-2,-1,0,1)),feature('b',square(-1,-2,1,0))],[[0,0]]),
  make('snap-boundary-priority',[feature('a',square(-2,-2,2,0))],[[0,0]],{sourceGeometry:square(-2,-2,2,0)}),
  make('snap-neighbor',[feature('a',square(-2,-2,0,2)),feature('b',square(1,-2,3,2))],[[1,0]]),
  make('snap-source-order',same,[{coordinate:[0,0]},{coordinate:[0,0],mutation:{kind:'reorder',ids:['b','a']}},{coordinate:[0,0],mutation:{kind:'metadata',id:'a',name:'Updated A'}},{coordinate:[0,0],mutation:{kind:'remove',id:'a'}},{coordinate:[0,0],mutation:{kind:'append',feature:same[0]}}]),
  make('snap-hidden-locked-generic',[feature('a',square(0,0,2,2),{locked:true})],[[0,0],[3,0]],{hiddenIds:['a'],genericFeatures:[generic('g',square(3,0,5,2)),generic('line',{type:'LineString',coordinates:[[0,0],[2,2]]})]}),
  make('snap-holes-multipolygon',[feature('a',{type:'MultiPolygon',coordinates:[[square(-5,-5,5,5).coordinates[0],square(-1,-1,1,1).coordinates[0]],square(10,0,12,2).coordinates]})],[[0,1],[10,1]]),
  make('snap-dateline-raw',[feature('a',{type:'Polygon',coordinates:[[[179,-2],[179,2],[-179,2],[-179,-2],[179,-2]]]})],[{coordinate:[179.8,1.9]},{coordinate:[-179.8,1.9]}],{view:{...view,center:[180,0]}}),
  make('snap-segment-bounds-false-negative',[feature('a',{type:'Polygon',coordinates:[[[0,0],[120,1],[-120,1],[0,0]]]})],[[-60,0.5]],{view:{...view,center:[-60,0.5]}}),
  make('snap-rounding-first-owner',[feature('a',square(-0.00000004,-0.00000004,2,2)),feature('b',square(0,0,2,2))],[[0,0]]),
  make('snap-source-revision',[feature('a',square(0,0,2,2))],[{coordinate:[1,0]},{coordinate:[1,0],mutation:{kind:'source-touch'}},{coordinate:[1,0],mutation:{kind:'source-replace',geometry:square(-2,-2,2,0)}},{coordinate:[1,0],mutation:{kind:'rebase'}}],{sourceGeometry:square(0,0,2,2)}),
  make('snap-current-error-retry',[feature('a',square(0,0,2,2))],[{coordinate:[0,0],mutation:{kind:'geometry',id:'a',geometry:{type:'Polygon',coordinates:[[[0,0],[1,1]],null]}},expectWorkerError:true},{coordinate:[0,0],mutation:{kind:'geometry',id:'a',geometry:square(0,0,2,2)}}]),
  make('snap-scale-cells',[feature('a',square(0,0,2,2))],[{coordinate:[0.1,0.1]},{coordinate:[0.1,0.1],mutation:{kind:'view',scale:10}},{coordinate:[0.1,0.1],mutation:{kind:'view',scale:100000}}]),
  make('snap-globe-screen-distance',[feature('a',square(-20,-10,20,10))],[[19.7,0]],{view:{...view,kind:'globe',rotate:[-15,-10,0]}}),
 ];
 for(const pointerType of ['mouse','touch','pen'])for(const offset of [10,10.00001,18,18.00001])cases.push(make(`snap-threshold-${pointerType}-${offset}`,[feature('a',square(0,0,2,2))],[{coordinate:[0,0],screenOffset:[-offset,0],pointerType}]));
 const busy=Array.from({length:36},(_,i)=>feature('busy-'+i,square(-2,-2,2,2)));
 for(const scenario of ['superseded','cancel','rebase','source-stale','project-replacement'])cases.push(make('snap-lifecycle-'+scenario,busy,[{coordinate:[0,0]}],{activeOwnerIds:['busy-0'],scenario}));
 for(const definition of cases)if(definition.sourceGeometry){definition.tool='draw-territorial-unit';definition.selected={domain:'territorial',type:'entity',id:'a'};definition.sourceContext='selection';}
 for(const scope of ['root','child'])cases.push(make('snap-'+scope+'-split-working-source',[...(scope==='child'?[feature('parent',square(-5,-5,5,5))]:[]),feature('a',square(-2,-2,2,2),{parentId:scope==='child'?'parent':''})],[[0,0]],{tool:'draw-territorial-unit',selected:{domain:'territorial',type:'entity',id:'a'},sourceContext:'selection',sourceGeometry:square(-2,-2,2,0)}));
 cases.push(make('snap-explicit-unit-source-branch',[feature('a',square(-2,-2,2,2))],[[0,2]],{tool:'split-territorial-unit',selected:{domain:'territorial',type:'entity',id:'a'},sourceContext:'entity',sourceGeometry:square(-2,-2,2,0)}));
 return cases;
}

export async function runSnapCase(loaded,definition){
 const {api}=loaded,clone=value=>structuredClone(value),transport=[],requests=[],pending=new Set();
 let features=clone(definition.features),genericFeatures=clone(definition.genericFeatures||[]),source=clone(definition.sourceGeometry),revision=0;
 const view=clone(definition.view),projection=(view.kind==='globe'?api.d3.geo.orthographic():api.d3.geo.equirectangular()).scale(view.scale).translate(view.translate).rotate(view.rotate).center(view.center);
 const state={stateRevision:0,tool:definition.tool,boundaryEditEntityIds:clone(definition.activeOwnerIds),selected:definition.selected||null,coastEditCountryId:definition.coastEditCountryId||null};
 const cut=api.createCutGeometry();
 state.genericFeatures=genericFeatures;state.territorialUnitSplitSourceId=definition.sourceContext==='entity'?'a':null;
 if(definition.sourceContext==='selection')state.territorySelectionSession={tool:definition.tool,stage:'selection',activePhase:'drawing',activeMethod:'line',get workingSourceGeometry(){return source;}};
 cut.connect({projectState:{state},territorialModel:{entityRepository:{get:id=>features.find(f=>f.id===id)}}});
 const client=api.createMapEditWorkerClient({createWorker:()=>{
  const worker=loaded.createWorker(),proxy={onmessage:null,onerror:null,postMessage(message){transport.push({direction:'request',message:clone(message)});worker.postMessage(message);},terminate:()=>worker.terminate()};
  worker.onmessage=event=>{transport.push({direction:'response',message:clone(event.data)});proxy.onmessage?.(event);};worker.onerror=error=>proxy.onerror?.(error);return proxy;
 },getEntities:()=>features,getFeatureById:id=>features.find(f=>f.id===id),getEditSources:()=>[...features.map(feature=>({kind:'territorial',feature})),...genericFeatures.map(feature=>({kind:'generic',feature}))],getTargetRevision:()=>revision});
 const recordingClient={...client,execute(operation,message,options){
  const row={operation,message:clone(message),options:{jobKey:options?.jobKey,priority:options?.priority},status:'pending'};requests.push(row);
  const request=client.execute(operation,message,options).then(result=>{row.status='resolved';row.response=clone(result);return result;},error=>{row.status='rejected';row.error={message:error.message,code:error.code||null,cancelled:error.cancelled===true};throw error;});
  pending.add(request);request.then(()=>pending.delete(request),()=>pending.delete(request));return request;
 }};
 const pointer=api.createPointerTargets();pointer.connect({projectState:{state},countries:{countryLandRevision:0},territorialModel:{entityRepository:{get:id=>features.find(f=>f.id===id)}},hydroPresentation:{hydroEditById:()=>null},mapView:{activeProjection:()=>projection},platform:{clamp:(v,min,max)=>Math.max(min,Math.min(max,v))},draftPresentation:cut,spatialQuery:{mapEditClient:recordingClient}});pointer.initializeSnapCandidateCache();
 const editing=api.createEditingDomain({draftServices:{getToolConfig:tool=>tool==='select'?null:{shape:'line',minimumPoints:2},screenToCoordinate:point=>projection.invert(point),projectCoordinate:coordinate=>projection(coordinate),snapCandidates:({coordinate,excludeNodeKey})=>pointer.localSnapCandidates(coordinate).filter(candidate=>!excludeNodeKey||candidate.nodeKey!==excludeNodeKey)},onEditingStateChanged:snapshot=>{state.tool=snapshot.activeTool;}});
 editing.setTool(definition.tool);editing.startDraft();
 const settle=async()=>{const limit=Date.now()+15000;while(pending.size){if(Date.now()>limit)throw Error('Snap production requests did not settle');await Promise.allSettled([...pending]);}await Promise.resolve();};
 const until=async(predicate)=>{const limit=Date.now()+15000;while(!predicate()){if(Date.now()>limit)throw Error('Snap production request never reached requested state');await new Promise(resolve=>setTimeout(resolve,1));}};
 const mutate=mutation=>{if(!mutation)return;revision++;state.stateRevision++;
  if(mutation.kind==='reorder')features=mutation.ids.map(id=>features.find(f=>f.id===id));
  else if(mutation.kind==='metadata')features=features.map(f=>f.id===mutation.id?{...f,properties:{...f.properties,name:mutation.name}}:f);
  else if(mutation.kind==='geometry')features=features.map(f=>f.id===mutation.id?{...f,geometry:clone(mutation.geometry)}:f);
  else if(mutation.kind==='remove')features=features.filter(f=>f.id!==mutation.id);
  else if(mutation.kind==='append')features.push(clone(mutation.feature));
  else if(mutation.kind==='source-touch')api.touchGeometry(source);
  else if(mutation.kind==='source-replace')source=clone(mutation.geometry);
  else if(mutation.kind==='rebase')client.rebase();
  else if(mutation.kind==='view'){view.scale=mutation.scale;projection.scale(view.scale);}
  else throw Error('Unknown snap fixture mutation '+mutation.kind);
 };
 const queries=[];let lifecycle=null;
 try{
  for(const query of definition.queries){
   mutate(query.mutation);const screenPoint=query.screenPoint||projection(query.coordinate).map((value,index)=>value+(query.screenOffset?.[index]||0)),coordinate=projection.invert(screenPoint),requestStart=requests.length;
   editing.startDraft();const first=pointer.localSnapCandidates(coordinate),coldCandidates=clone(first);
   const firstOutcome=editing.appendDraftScreenPoint(screenPoint,query.pointerType||'mouse',query.options||{}),coldDraft=clone(editing.snapshot().draft),again=pointer.localSnapCandidates(coordinate);
   const pendingObservation={sameArray:first===again,requestCount:requests.length-requestStart,candidates:clone(again)};
   if(definition.scenario){
    await until(()=>transport.some(event=>event.direction==='request'&&event.message.type==='execute'&&event.message.operation==='territorial-snap'));
    const before=clone(editing.snapshot()),requestOffset=requests.length;
    if(definition.scenario==='superseded'){state.stateRevision++;pointer.localSnapCandidates([coordinate[0]+1,coordinate[1]+1]);}
    else if(definition.scenario==='cancel'){client.cancel();editing.clearDraft();}
    else if(definition.scenario==='rebase'){client.rebase();state.stateRevision++;pointer.localSnapCandidates(coordinate);}
    else if(definition.scenario==='source-stale'){features=features.map((f,index)=>index?f:{...f,geometry:clone(f.geometry)});}
    else if(definition.scenario==='project-replacement'){client.rebase([]);state.stateRevision++;editing.resetProject(1);}
    await settle();lifecycle={scenario:definition.scenario,before,after:clone(editing.snapshot()),requestsAdded:requests.length-requestOffset,requests:clone(requests),workerStats:clone(client.stats())};
   }else await settle();
   if(definition.scenario || query.expectWorkerError){
    queries.push({input:{...clone(query),coordinate,screenPoint,view:clone(view)},sourceSnapshot:{features:clone(features),genericFeatures:clone(genericFeatures),sourceGeometry:clone(cut.activeCutDraftSourceGeometry()),sourceRevision:cut.activeCutDraftSourceGeometry()?api.geometryRevision(cut.activeCutDraftSourceGeometry()):0},candidateRequest:requests.length?clone(requests.at(-1)):null,cold:{candidates:coldCandidates,outcome:firstOutcome,draft:coldDraft},pending:pendingObservation,afterSettlementDraft:clone(editing.snapshot().draft),ready:{observed:false,reason:definition.scenario?'Lifecycle interruption ends this input; no extra pointer event is synthesized.':'Expected actual worker error; next explicit fixture input exercises retry.'}});
    continue;
   }
   if(requests.slice(requestStart).some(row=>row.status==='rejected'))throw Error('Unexpected actual snap request rejection: '+JSON.stringify(requests.slice(requestStart)));
   const afterSettlementDraft=clone(editing.snapshot().draft),candidates=clone(pointer.localSnapCandidates(coordinate));
   if(pending.size)await settle();
   const projectedPoints=[...new Map(candidates.flatMap(candidate=>[candidate.coordinate,candidate.a,candidate.b].filter(Boolean)).map(coordinate=>[JSON.stringify(coordinate),{coordinate:clone(coordinate),screen:clone(projection(coordinate))}])).values()];
   const synchronized=new Map();for(const event of transport){if(event.direction!=='request')continue;const message=event.message;if(message.type==='rebase')synchronized.clear();const patch=message.editSources||(message.type==='edit-sync'?message:null);if(patch){for(const key of patch.removedKeys||[])synchronized.delete(key);for(const item of patch.patches||[])synchronized.set(item.key,true);}}
   const result=api.resolveSnap({coordinate,screenPoint,candidates,project:projection,pointerType:query.pointerType||'mouse'}),indicator=api.snapIndicator(result);
   const readyOutcome=editing.appendDraftScreenPoint(screenPoint,query.pointerType||'mouse',query.options||{}),readyDraft=clone(editing.snapshot().draft);
   const undo=editing.performDraftUndo(),undoDraft=clone(editing.snapshot().draft),redo=editing.performDraftRedo(),redoDraft=clone(editing.snapshot().draft);
   queries.push({input:{...clone(query),coordinate,screenPoint,view:clone(view)},sourceSnapshot:{features:clone(features),genericFeatures:clone(genericFeatures),sourceGeometry:clone(cut.activeCutDraftSourceGeometry()),sourceRevision:cut.activeCutDraftSourceGeometry()?api.geometryRevision(cut.activeCutDraftSourceGeometry()):0},candidateRequest:requests.length?clone(requests.at(-1)):null,cold:{candidates:coldCandidates,outcome:firstOutcome,draft:coldDraft},pending:pendingObservation,afterSettlementDraft,ready:{candidates,result,indicator,projectedPoints,synchronizedSourceOrder:[...synchronized.keys()],outcome:readyOutcome,draft:readyDraft},undo:{outcome:undo,draft:undoDraft},redo:{outcome:redo,draft:redoDraft}});
  }
  return {id:definition.id,input:clone(definition),entrypoint:'editing-domain.appendDraftScreenPoint → app-pointer-targets.localSnapCandidates → map-edit-worker-client → territorial-snap → geometrySegmentIndex → geometry-snap.resolveSnap → draft-editor',queries,lifecycle,requests,transport};
 }finally{editing.dispose();client.stop();await settle();}
}
