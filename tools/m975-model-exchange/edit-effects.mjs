import assert from 'node:assert/strict';
export function verifyEditEffect(definition,before,after){
 const ids=p=>p.territorialEntities.map(f=>f.id),beforeIds=ids(before),afterIds=ids(after),binding=(p,id)=>p.timelineRecords.geometryBindings.find(r=>r.entityId===id),parent=(p,id)=>p.timelineRecords.parentRelations.find(r=>r.entityId===id)?.parentId;
 const geometry=(p,id)=>{const ref=binding(p,id)?.geometryRef;assert.ok(ref,'geometry binding '+id);const entry=p.geometries.find(g=>g.id===ref.id&&g.version===ref.version);assert.ok(entry,'bound immutable archive version '+id);return entry.geojson;};
 const changed=id=>{assert.notDeepEqual(binding(after,id).geometryRef,binding(before,id).geometryRef,'new geometry version for '+id);assert.notDeepEqual(geometry(after,id),geometry(before,id),'intended actual shape changed '+id);};
 for(const key of ['sourceInfo','labels','hydroEdits','distributionLayers','distributionEntries'])assert.deepEqual(after[key],before[key],'unrelated content preserved '+key);
 if(definition.operation==='delete'){
  assert.deepEqual(after.genericFeatures,before.genericFeatures.filter(f=>f.id!==definition.deleteId),'exact requested generic deletion and survivor order');assert.ok(before.genericFeatures.some(f=>f.id===definition.deleteId));assert.deepEqual(after.territorialEntities,before.territorialEntities);assert.deepEqual(after.timelineRecords,before.timelineRecords);assert.deepEqual(after.geometries,before.geometries);return;
 }
 assert.deepEqual(after.genericFeatures,before.genericFeatures,'unrelated fallback content preserved');
 for(const entry of before.geometries)assert.deepEqual(after.geometries.find(g=>g.id===entry.id&&g.version===entry.version),entry,'retained original immutable geometry archive');
 if(definition.operation==='annex'){
  changed('target');if(definition.full){assert.deepEqual(afterIds,beforeIds.filter(id=>!['donor','child-left','child-right'].includes(id)),'exact recursive donor removal');}else{assert.deepEqual(afterIds,beforeIds,'partial donor and child identities retained');changed('donor');}
 }else if(definition.operation==='split'){
  const created=afterIds.filter(id=>!beforeIds.includes(id));assert.equal(created.length,1,'exact one new split sibling');assert.deepEqual(afterIds.filter(id=>id!==created[0]),beforeIds,'original split identity order retained');assert.equal(parent(after,created[0]),parent(before,'source'),'new sibling immediate parent');changed('source');assert.ok(geometry(after,created[0]));
 }else if(definition.operation==='boundary'){
  const expected=definition.definition.expected;assert.deepEqual(afterIds,beforeIds.filter(id=>!(expected.deletedIds||[]).includes(id)),'exact boundary descendant removals');for(const id of expected.movedOwnerIds)changed(id);for(const id of expected.reparentedIds||[])assert.notEqual(parent(after,id),parent(before,id),'whole descendant immediately reparented');for(const id of expected.clippedIds||[])changed(id);
  if(definition.id==='m975-boundary-child-fixed-parent'){assert.deepEqual(binding(after,'P'),binding(before,'P'),'fixed parent binding unchanged');assert.deepEqual(geometry(after,'P'),geometry(before,'P'),'fixed parent geometry unchanged');}
 }
 const deleted=beforeIds.filter(id=>!afterIds.includes(id));for(const id of deleted){assert.ok(!after.timelineRecords.lifetimes.some(r=>r.entityId===id));assert.ok(!after.timelineRecords.geometryBindings.some(r=>r.entityId===id));assert.ok(!after.timelineRecords.parentRelations.some(r=>r.entityId===id||r.parentId===id));assert.ok(!after.labels.some(r=>r.territorialUnitId===id));assert.ok(!after.distributionEntries.some(r=>r.territorialUnitId===id));assert.ok(!Object.hasOwn(after.labelSettings,'territorial:'+id),'deleted territorial label setting cleaned');assert.ok(!Object.hasOwn(after.layerPresentation.objectStyles,'territorial:entity:'+id),'deleted territorial style cleaned');}
}
