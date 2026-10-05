import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {sha256} from './oracle.mjs';
import {prepareCorrectedSelectionSources} from '../web-selection-correction.mjs';
import {selectionEntrypoints,createSelectionRuntime,settle} from '../web-selection.mjs';

export const controllerAnnexRole='Actual production controller entry chain; browser preview oracle only.';
export function requiredControllerAnnexScenarios(){return [
  {name:'Serbia-to-Hungary-controller-entry-chain',base:'SRB-controller',representation:'controller-entry-chain',donorId:'SRB',targetId:'HUN',samplePoints:[[19.6,45.7],[20.7,45.5],[19.8,45.0]]},
  {name:'Moldova-to-Romania-controller-entry-chain',base:'MDA-controller',representation:'controller-entry-chain',donorId:'MDA',targetId:'ROU',samplePoints:[[28.5,47.1]]},
];}
export function assertControllerAnnexContract(contract){
  assert.equal(contract?.role,controllerAnnexRole,'Actual production entry-chain scope required');
  assert.deepEqual(contract.scenarios,requiredControllerAnnexScenarios(),'Required two actual controller entry-chain scenarios');
}
export function controllerSourceBundle(){
  const corrected=prepareCorrectedSelectionSources();
  try {
    const modules={};
    const add=name=>{
      if(modules[name])return;
      assert.match(name,/^[a-z0-9-]+(?:\.min)?\.js$/,'Bounded controller dependency name');
      const source=fs.readFileSync(path.join(corrected.root,'assets/js',name==='d3.min.js'?'vendor':'modules',name),'utf8');
      modules[name]={source,sha256:sha256(source)};
      for(const match of source.matchAll(/(?:from\s*|import\s*)['"]\.\/([^'"]+)['"]/g))add(match[1]);
    };
    const entrypoints=[...selectionEntrypoints,'territorial-units','ring-hit-test'].map(name=>name+'.js');
    for(const name of [...entrypoints,'polygon-geometry.js','d3.min.js'])add(name);
    const source=`(()=>{const clone=value=>structuredClone(value),noop=()=>{};return {createSelectionRuntime:${createSelectionRuntime.toString()},settle:${settle.toString()}};})()`;
    return {baseBehavioralCommit:corrected.manifest.baseBehavioralCommit,behavioralCommit:corrected.manifest.behavioralCommit,sourceDelta:corrected.manifest.changes,entrypoints,modules,runtime:{source,sha256:sha256(source)}};
  }finally{corrected.cleanup();}
}
export function assertControllerAnnexObservations(contract,rows){
  assert.equal(rows?.length,2,'Required two browser controller entry-chain observations');
  assert.deepEqual(rows.map(row=>row.name),contract.scenarios.map(row=>row.name),'Ordered controller entry-chain identity');
  for(let i=0;i<rows.length;i++){
    const row=rows[i],scenario=contract.scenarios[i];
    assert.deepEqual(row.selection,scenario,'Controller selection identity');
    assert.ok(row.components?.length&&row.componentFeatures?.length&&row.selectedCells?.length,'Actual production prepare/compose/install evidence required');
    assert.equal(row.selectedCells.length,scenario.samplePoints.length,'Every controller sample must select one cell');
    assert.equal(new Set(row.selectedCells.map(cell=>cell.key)).size,row.selectedCells.length,'Unique controller cells required');
    for(const feature of row.componentFeatures)assert.equal(feature.geometry?.type,'MultiPolygon','Actual prepare geometry type required');
    for(const selected of row.selectedCells){const component=row.components.find(item=>item.key===selected.key);assert.ok(component,'Installed selected cell must come from production composition');assert.deepEqual(selected.geometry,component.geometry,'Installed selected cell geometry unchanged');}
    assert.equal(row.input?.operation,'annex');assert.equal(row.input.targetId,scenario.targetId);assert.deepEqual(row.input.donorIds,[scenario.donorId]);
    assert.deepEqual(row.input.transferredGeometry,row.combinedGeometry,'Actual workflow preview payload geometry');
    assert.ok(row.input.riverSliverContext?.length,'Actual workflow sliver context required');
    assert.ok(row.result?.transferredGeometry&&Number.isInteger(row.result.autoIncludedSlivers?.count)&&Number.isFinite(row.result.autoIncludedSlivers?.areaM2),'Actual calculator result required');
    assert.ok(Number.isFinite(row.expectedTransferAreaKm2)&&row.expectedTransferAreaKm2>=0,'Pinned D3 authoritative transfer area required');
    assert.ok(Array.isArray(row.after)&&row.after.length>0,'Actual changed geometry output required');
    assert.equal(row.fullWorldFeatureCount,258);assert.equal(row.inputUnchanged,true);
    const source=row.sourceDiagnostics;assert.ok(source&&source.loadedRivers>0);assert.deepEqual(source.failedLogicalIds,[]);assert.match(source.indexSha256,/^[0-9a-f]{64}$/);assert.match(source.version,/^0\.13\.[01]$/);
    assert.deepEqual(row.donorRevisionStrings,row.componentFeatures.map(feature=>String(feature.id)+':'+JSON.stringify(feature.geometry.coordinates)),'Actual live-coordinate donor revisions');
    assert.equal(row.editedRiverSignature,'');assert.equal(row.hydroRevision,source.version+':'+source.indexSha256+':','Actual configured hydro revision');
  }
}

export function requiredControllerLifecycleScenarios(){return [{
  ...requiredControllerAnnexScenarios()[0],name:'Serbia-to-Hungary-controller-archive-residual-middle-removal',
  actions:[
    {op:'archive',name:'three-parts-archived'},
    {op:'components',name:'residual-river-ready'},
    {op:'removePart',index:1,name:'middle-part-removed'},
    {op:'river',enabled:false,name:'residual-river-disabled'},
    {op:'river',enabled:true,name:'restored-residual-river-ready'},
    {op:'sampleRemovedPart',index:1,name:'restored-middle-selected'},
  ],
}];}
export function assertControllerLifecycleContract(contract){
  assert.deepEqual(contract?.scenarios,requiredControllerLifecycleScenarios(),'Required Serbia controller lifecycle');
}

export function assertControllerLifecycleObservations(contract,rows){
  assert.equal(rows?.length,1,'Required browser controller lifecycle observation');
  const row=rows[0],scenario=contract.scenarios[0];assert.equal(row.name,scenario.name);assert.deepEqual(row.selection,scenario);
  assert.deepEqual(row.lifecycleActions?.map(({checkpoint,resolvedPoint,removedPart,...action})=>action),scenario.actions,'Complete ordered public lifecycle actions');
  assert.equal(row.initialCheckpoint?.name,'initial-three-selected');assert.equal(row.initialCheckpoint.selectedComponentKeys.length,3);
  const checkpoints=[row.initialCheckpoint,...row.lifecycleActions.map(action=>action.checkpoint)];
  assert.deepEqual(checkpoints.map(value=>value?.parts?.length),[0,3,3,2,2,2,2],'Exact archive/middle-removal part counts');
  for(const checkpoint of checkpoints){
    assert.ok(Array.isArray(checkpoint.components)&&Array.isArray(checkpoint.componentFeatures)&&checkpoint.componentFeatures.length,'Actual checkpoint prepare/components required');
    assert.ok(Array.isArray(checkpoint.componentSnapshots)&&Array.isArray(checkpoint.selectedComponentKeys),'Actual archived snapshots and selection required');
    assert.ok(['river','ordinary','unavailable'].includes(checkpoint.componentKind));
    assert.equal(checkpoint.inputUnchanged,true);assert.equal(typeof checkpoint.previewReady,'boolean');
    for(const feature of checkpoint.componentFeatures)assert.equal(feature.geometry?.type,'MultiPolygon');
    if(checkpoint.previewReady){
      assert.ok(checkpoint.input&&checkpoint.result,'Current preview payload/result required');
      assert.deepEqual(checkpoint.input.transferredGeometry,checkpoint.combinedGeometry);assert.deepEqual(checkpoint.transferredGeometry,checkpoint.result.transferredGeometry);
      assert.deepEqual(checkpoint.riverSliverContext,checkpoint.input.riverSliverContext);assert.equal(checkpoint.autoIncludedSliverCount,checkpoint.result.autoIncludedSlivers.count);assert.equal(checkpoint.autoIncludedSliverAreaM2,checkpoint.result.autoIncludedSlivers.areaM2);
      assert.ok(Number.isFinite(checkpoint.transferAreaKm2)&&checkpoint.transferAreaKm2>=0);
    }else for(const field of ['input','result','transferredGeometry','riverSliverContext','autoIncludedSliverCount','autoIncludedSliverAreaM2','transferAreaKm2'])assert.equal(checkpoint[field],null,'No stale preview at '+checkpoint.name+'/'+field);
    if(checkpoint.sourceDiagnostics){
      assert.deepEqual(checkpoint.donorRevisionStrings,checkpoint.componentFeatures.map(feature=>String(feature.id)+':'+JSON.stringify(feature.geometry.coordinates)));
      assert.equal(checkpoint.hydroRevision,checkpoint.sourceDiagnostics.version+':'+checkpoint.sourceDiagnostics.indexSha256+':');
    }
  }
  for(let i=0;i<row.lifecycleActions.length;i++)assert.equal(row.lifecycleActions[i].checkpoint.name,scenario.actions[i].name);
  const archived=checkpoints[1],residual=checkpoints[2],removed=checkpoints[3],refreshed=checkpoints[5],final=checkpoints[6];
  assert.deepEqual(removed.parts,[archived.parts[0],archived.parts[2]],'Only actual ordered middle part removed');
  assert.deepEqual(row.lifecycleActions[2].removedPart,archived.parts[1]);assert.deepEqual(row.lifecycleActions[5].removedPart,archived.parts[1]);
  assert.ok(scenario.samplePoints.some(point=>JSON.stringify(point)===JSON.stringify(row.lifecycleActions[5].resolvedPoint)),'Restored selection must reuse an original sample');
  assert.ok(JSON.stringify(residual.componentFeatures)!==JSON.stringify(row.componentFeatures),'Actual residual source must differ from original');
  assert.ok(JSON.stringify(refreshed.componentFeatures)!==JSON.stringify(residual.componentFeatures),'Actual middle removal must restore residual source');
  assert.equal(final.previewReady,true);assert.equal(final.selectedComponentKeys.length,1);assert.deepEqual(row.input,final.input);assert.deepEqual(row.result,final.result);assert.deepEqual(row.combinedGeometry,final.combinedGeometry);assert.equal(row.expectedTransferAreaKm2,final.transferAreaKm2);
  assert.ok(row.after?.length);assert.equal(row.fullWorldFeatureCount,258);assert.equal(row.inputUnchanged,true);
}
