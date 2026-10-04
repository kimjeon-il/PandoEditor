(async function(payload,raw,presentations,calculator,clipper,digest,canonical) {
  const polygons=g=>g.type==='Polygon'?[g.coordinates]:g.coordinates;
  const insideRing=(point,ring)=>{let inside=false;for(let i=0,j=ring.length-1;i<ring.length;j=i++){const a=ring[i],b=ring[j];if((a[1]>point[1])!==(b[1]>point[1])&&point[0]<(b[0]-a[0])*(point[1]-a[1])/(b[1]-a[1])+a[0])inside=!inside;}return inside;};
  const contains=(g,p)=>polygons(g).some(r=>insideRing(p,r[0])&&!r.slice(1).some(h=>insideRing(p,h)));
  const features=JSON.parse(payload.world.source).features,beforeHash=await digest(JSON.stringify(canonical(features))),output=[];
  if(features.length!==258)throw new Error('Required original full-world feature count');
  for(const scenario of payload.annex.scenarios) {
    const candidates=scenario.representation==='normalized-filtered-presentation'?presentations.find(r=>r.name===scenario.base).candidates:raw.find(r=>r.name===scenario.base).result.candidates;
    const selected=scenario.samplePoints.map(point=>{const found=candidates.filter(c=>contains(c.geometry,point));if(found.length!==1)throw new Error('Ambiguous annex sample '+scenario.name);return found[0];});
    if(new Set(selected.map(c=>c.key)).size!==selected.length)throw new Error('Duplicate annex cells');
    if(scenario.capturedBrowserKey&&selected[0].key!==scenario.capturedBrowserKey)throw new Error('Moldova captured-browser cell identity changed');
    const target=features.find(f=>f.id===scenario.targetId),donor=features.find(f=>f.id===scenario.donorId);
    const sharesTargetBoundary=selected.some(c=>polygons(c.geometry).some(p=>calculator.sharesBoundary(p,polygons(target.geometry))));
    if(scenario.requireSharedTargetBoundary&&!sharesTargetBoundary)throw new Error('Selected Moldova cell must border Romania');
    const transferredGeometry={type:'MultiPolygon',coordinates:clipper.union(...selected.map(c=>polygons(c.geometry)))};
    const input={operation:'annex',targetId:scenario.targetId,donorIds:[scenario.donorId],transferredGeometry,riverSliverContext:[{donorId:scenario.donorId,polygonIndex:0,unselectedGeometries:candidates.filter(c=>!selected.includes(c)).map(c=>c.geometry)}]};
    const beforeInput=JSON.stringify(input),result=calculator.calculate(input,new Map(features.map(f=>[f.id,f])));
    if(beforeInput!==JSON.stringify(input)||beforeHash!==await digest(JSON.stringify(canonical(features))))throw new Error('Annex input mutation');
    const slivers=result.result.autoIncludedSlivers;if(!slivers||!Number.isInteger(slivers.count)||slivers.count<0||!Number.isFinite(slivers.areaM2))throw new Error('Missing authoritative sliver diagnostics');
    output.push({name:scenario.name,role:payload.annex.role,selection:scenario,selectedCells:selected,sharesTargetBoundary,input,before:[target,donor],result:result.result,after:result.afterFeatures.filter(f=>result.result.affectedIds.includes(String(f.id))),fullWorldBeforeSha256:beforeHash,fullWorldAfterSha256:await digest(JSON.stringify(canonical(result.afterFeatures))),fullWorldFeatureCount:features.length,inputUnchanged:true});
  }
  return output;
})
