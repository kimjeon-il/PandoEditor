(async function(payload,api,runtime,digest,canonical,options={}) {
  // Ports supply immutable full-source data and collect actual production calls.
  // No selection reducer, candidate composition, revision, or preview is mirrored.
  const clone=value=>structuredClone(value),must=(value,message)=>{if(!value)throw new Error(message);};
  const world=JSON.parse(payload.world.source).features,output=[];
  must(world.length===258,'Required original full-world controller context');
  for(const scenario of (options.lifecycle?payload.controllerLifecycle.scenarios:payload.controllerAnnex.scenarios)){
    const source=payload.controllerAnnex.source,provided=source.inputs.find(row=>row.donorId===scenario.donorId);
    must(provided?.riverFeatures?.length,'Verified full controller river source required');
    const features=world.map(feature=>api.createTerritorialFeature({id:String(feature.id),name:String(feature.id),entityKind:'general',parentId:'',coverageMode:'explicit',geometry:clone(feature.geometry)}));
    const before=await digest(JSON.stringify(canonical(features))),riverBefore=await digest(JSON.stringify(canonical(provided.riverFeatures)));
    const calculations=[],compositions=[],preparations=[],sourceCalls=[],kernelCalls=[];
    const captureApi={...api,createCountryCommandCalculator(...args){
      const calculator=api.createCountryCommandCalculator(...args);
      return {...calculator,calculate(...inputs){const beforeInput=JSON.stringify(inputs[0]);const result=calculator.calculate(...inputs);must(beforeInput===JSON.stringify(inputs[0]),'Controller preview input mutation');calculations.push({input:clone(inputs[0]),output:clone(result)});return result;}};}};
    const h=runtime.createSelectionRuntime(captureApi,{features,riverFeatures:provided.riverFeatures});
    h.ports.landRelations.initializeRingHitTester();
    const contains=(geometry,point)=>h.ports.landRelations.pointInCountryFeature(point,{geometry});
    h.state.hydroManifest={version:source.version,index:{sha256:source.indexSha256}};
    h.ports.territoryComponents={...h.components,installComponentIndex(...args){preparations.push(clone(args[1]));return h.components.installComponentIndex(...args);},installRiverComponentIndex(...args){compositions.push(clone(args[1]));return h.components.installRiverComponentIndex(...args);}};
    // Separate read-only receipt diagnostics; canonical metrics remain unchanged.
    const metricDiagnostic=async(geometry,expectedAreaKm2,{trace=false}={})=>{
      const float64Hex=value=>{const bytes=new ArrayBuffer(8),view=new DataView(bytes);view.setFloat64(0,value,false);return Array.from(new Uint8Array(bytes),byte=>byte.toString(16).padStart(2,'0')).join('');};
      const measure=geometry=>{const steradians=api.d3.geo.area(geometry),radiusSquared=6371.0088**2,productKm2=steradians*radiusSquared,clampedKm2=Math.max(0,productKm2);return {steradians,radiusSquared,productKm2,clampedKm2,formatted:h.components.formatTerritoryArea(clampedKm2),float64:{steradians:float64Hex(steradians),radiusSquared:float64Hex(radiusSquared),productKm2:float64Hex(productKm2)}};};
      const metricGeometry=clone(geometry),normalizedGeometry=api.normalizePolygonGeometry(clone(metricGeometry));must(normalizedGeometry,'Diagnostic normalization failed');
      const diagnostic={sourceIdentity:{d3Sha256:payload.controllerSources.modules['d3.min.js'].sha256,normalizerSha256:payload.controllerSources.modules['polygon-geometry.js'].sha256,formatterSha256:payload.controllerSources.modules['app-territory-components.js'].sha256,formatterEntrypoint:'app-territory-components.formatTerritoryArea'},role:'Read-only exact D3 metric diagnostic; canonical receipt metric is unchanged.',raw:measure(metricGeometry),normalized:measure(normalizedGeometry),
        inputSha256:await digest(JSON.stringify(canonical(metricGeometry))),normalizedInputSha256:await digest(JSON.stringify(canonical(normalizedGeometry))),normalizedGeometry};
      must(diagnostic.raw.productKm2===expectedAreaKm2,'Diagnostic differs from authoritative receipt metric');
      if(trace){
        const originals={sin:Math.sin,cos:Math.cos,atan2:Math.atan2},calls=[];let tracedSteradians;
        try{
          for(const [name,original] of Object.entries(originals))Math[name]=(...args)=>{const result=original(...args);calls.push({function:name,args,result});return result;};
          tracedSteradians=api.d3.geo.area(normalizedGeometry);
        }finally{for(const [name,original] of Object.entries(originals))Math[name]=original;}
        const outputExact=Object.is(tracedSteradians,diagnostic.normalized.steradians);must(outputExact,'D3 diagnostic instrumentation changed its output');
        diagnostic.trigonometryTrace={role:'Diagnostic-only calls to original Math built-ins on the normalized transfer copy.',calls,steradians:tracedSteradians,outputExact};
      }
      return diagnostic;
    };
    const gis=h.ports.domainControllers.gisDomain,compute=gis.computeRiverPartition;
    gis.loadRiverPartitionFeatures=async donors=>{
      const result={features:provided.riverFeatures,diagnostics:{loadedRivers:provided.riverFeatures.length,failedLogicalIds:[],indexSha256:source.indexSha256,version:source.version}};
      sourceCalls.push({donors:clone(donors),diagnostics:clone(result.diagnostics)});return result;
    };
    gis.computeRiverPartition=async request=>{kernelCalls.push(clone(request));return compute(request);};
    must(h.workflow.start('annex',{targetCountryId:scenario.targetId,sourceCountryIds:[scenario.donorId]}),'Actual controller workflow start failed');
    must(await h.workflow.advance(),'Actual controller source advance failed');
    must(await h.workflow.selectMethod('components'),'Actual controller component method failed');await runtime.settle(h);
    h.workflow.toggleRiverBoundaries(true);await runtime.settle(h);
    const session=h.workflow.activeSession();
    must(session.riverPartitionStatus==='ready','Actual controller river preparation failed');
    must(sourceCalls.length===1&&kernelCalls.length===1&&compositions.length===1,'Single actual full-source controller dispatch required');
    const componentFeatures=clone(session.componentFeatures),components=clone(compositions[0].items);
    must(componentFeatures.every(feature=>feature.geometry.type==='MultiPolygon'),'Production prepare MultiPolygon evidence required');
    const selectedKeys=[];
    for(const point of scenario.samplePoints){
      const found=h.components.territoryComponentItems().filter(item=>contains(item.geometry,point));
      must(found.length===1,'Ambiguous actual controller sample '+scenario.name);
      must(!selectedKeys.includes(found[0].key),'Duplicate actual controller sample');selectedKeys.push(found[0].key);
      h.workflow.toggleComponent(found[0].key);await runtime.settle(h);
    }
    must(h.workflow.previewReady()&&h.previews.length&&calculations.length,'Actual controller preview not ready');
    const selectedCells=clone(h.components.territoryComponentItems().filter(item=>item.selected));
    must(selectedCells.length===selectedKeys.length&&selectedCells.every(item=>selectedKeys.includes(item.key)),'Installed controller selection mismatch');
    const preview=h.previews.at(-1),calculation=calculations.at(-1),request=kernelCalls[0];
    must(JSON.stringify(calculation.input)===JSON.stringify({operation:'annex',...preview.payload}),'Preview/calculator request identity mismatch');
    must(before===await digest(JSON.stringify(canonical(features)))&&riverBefore===await digest(JSON.stringify(canonical(provided.riverFeatures))),'Controller source mutation');
    const record={name:scenario.name,selection:clone(scenario),components,componentFeatures,selectedCells,
      expectedTransferAreaKm2:api.d3.geo.area(calculation.output.result.transferredGeometry)*6371.0088**2,
      input:calculation.input,combinedGeometry:clone(session.combinedGeometry),result:calculation.output.result,
      after:calculation.output.afterFeatures.filter(feature=>calculation.output.result.affectedIds.includes(String(feature.id))),
      sourceDiagnostics:sourceCalls[0].diagnostics,donorRevisionStrings:request.donors.map(donor=>donor.geometryRevision),
      editedRiverSignature:request.hydroRevision.slice(source.version.length+source.indexSha256.length+2),hydroRevision:request.hydroRevision,sourceDispatches:sourceCalls.length,kernelDispatches:kernelCalls.length,
      fullWorldBeforeSha256:before,fullWorldAfterSha256:await digest(JSON.stringify(canonical(calculation.output.afterFeatures))),fullWorldFeatureCount:features.length,inputUnchanged:true};
    if(options.lifecycle){
      const checkpoint=async name=>{
        const active=h.workflow.activeSession(),ready=h.workflow.previewReady(),latest=calculations.at(-1);
        const riverActive=active.activePhase==='components'&&active.useRiverBoundaries&&active.riverPartitionStatus==='ready'&&active.componentIndex?.river?.candidates===active.riverPartitionCandidates;
        const ordinaryActive=active.activePhase==='components'&&!active.useRiverBoundaries;
        const observed={name,componentKind:riverActive?'river':ordinaryActive?'ordinary':'unavailable',components:riverActive?clone(compositions.at(-1).items):ordinaryActive?clone(preparations.at(-1).items):[],installedComponents:clone(h.components.territoryComponentItems()),
          componentFeatures:clone(active.componentFeatures),parts:clone(active.parts),componentSnapshots:clone(active.componentSnapshots),
          selectedComponentKeys:clone(active.selectedComponentKeys),combinedGeometry:clone(active.combinedGeometry),workingSourceGeometry:clone(active.workingSourceGeometry),
          archivedGeometry:clone(active.archivedGeometry),currentGeometry:clone(active.currentGeometry),previewReady:ready,
          riverSliverContext:ready?clone(h.previews.at(-1).payload.riverSliverContext):null,input:ready?clone(latest.input):null,result:ready?clone(latest.output.result):null,
          transferredGeometry:ready?clone(latest.output.result.transferredGeometry):null,
          autoIncludedSliverCount:ready?latest.output.result.autoIncludedSlivers.count:null,autoIncludedSliverAreaM2:ready?latest.output.result.autoIncludedSlivers.areaM2:null,
          transferAreaKm2:ready?api.d3.geo.area(latest.output.result.transferredGeometry)*6371.0088**2:null,
          sourceDispatches:sourceCalls.length,kernelDispatches:kernelCalls.length,
          inputUnchanged:before===await digest(JSON.stringify(canonical(features)))&&riverBefore===await digest(JSON.stringify(canonical(provided.riverFeatures)))};
        must(observed.inputUnchanged,'Controller lifecycle source mutation');
        if(riverActive){
          const matching=kernelCalls.findLast(call=>JSON.stringify(call.donors.map(donor=>donor.geometry))===JSON.stringify(active.componentFeatures.map(feature=>feature.geometry)));
          must(matching,'Actual residual river request missing');
          Object.assign(observed,{sourceDiagnostics:clone(sourceCalls.at(-1).diagnostics),donorRevisionStrings:matching.donors.map(donor=>donor.geometryRevision),
            hydroRevision:matching.hydroRevision,editedRiverSignature:matching.hydroRevision.slice(source.version.length+source.indexSha256.length+2)});
        }
        observed.transferAreaDiagnostic=ready?await metricDiagnostic(latest.output.result.transferredGeometry,observed.transferAreaKm2):null;
        return observed;
      };
      record.initialCheckpoint=await checkpoint('initial-selected');record.lifecycleActions=[];const removedParts=new Map();
      for(const action of scenario.actions){
        const details={};
        if(action.op==='archive')must(h.workflow.addPart(),'Actual lifecycle archive rejected');
        else if(action.op==='components'){
          must(await h.workflow.selectMethod('components'),'Actual residual components rejected');await runtime.settle(h);
          must(h.workflow.toggleRiverBoundaries(true),'Actual residual river enable rejected');
        }else if(action.op==='removePart'){
          const part=h.workflow.activeSession().parts[action.index];must(part,'Actual middle part missing');removedParts.set(action.index,clone(part));details.removedPart=clone(part);must(h.workflow.removePart(part.id),'Actual middle removal rejected');
        }else if(action.op==='sampleRemovedPart'){
          const removedPart=removedParts.get(action.index);must(removedPart,'Recorded actual removed part missing');
          const points=scenario.samplePoints.filter(point=>contains(removedPart.geometry,point));must(points.length===1,'Removed part must contain exactly one original sample');
          details.resolvedPoint=clone(points[0]);details.removedPart=clone(removedPart);
          const found=h.components.territoryComponentItems().filter(item=>contains(item.geometry,points[0]));must(found.length===1,'Ambiguous actual restored-part sample');
          must(h.workflow.toggleComponent(found[0].key),'Actual restored-part selection rejected');
        }else if(action.op==='river')must(h.workflow.toggleRiverBoundaries(action.enabled),'Actual residual river toggle rejected');
        else throw new Error('Unsupported bounded controller lifecycle action '+action.op);
        await runtime.settle(h);record.lifecycleActions.push({...clone(action),...details,checkpoint:await checkpoint(action.name)});
      }
      const final=record.lifecycleActions.at(-1).checkpoint,finalCalculation=calculations.at(-1);
      must(final.previewReady,'Final residual controller preview not ready');
      Object.assign(record,{input:clone(final.input),combinedGeometry:clone(final.combinedGeometry),result:clone(final.result),expectedTransferAreaKm2:final.transferAreaKm2,
        after:finalCalculation.output.afterFeatures.filter(feature=>finalCalculation.output.result.affectedIds.includes(String(feature.id))),
        fullWorldAfterSha256:await digest(JSON.stringify(canonical(finalCalculation.output.afterFeatures)))});
    }
    record.transferAreaDiagnostic=await metricDiagnostic(record.result.transferredGeometry,record.expectedTransferAreaKm2,{trace:!options.lifecycle});
    output.push(record);
    h.workflow.clear();
  }
  return output;
})
