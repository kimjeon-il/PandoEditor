// Bounded observations through the original exported workspace, no math override.
(function(module, request) {
  const result=[];
  for(const donor of (request || {}).donors || []) {
    if(!donor?.geometry || !['Polygon','MultiPolygon'].includes(donor.geometry.type))continue;
    const polygons=donor.geometry.type==='Polygon'?[donor.geometry.coordinates]:donor.geometry.coordinates;
    for(let index=0;index<polygons.length;index++) {
      const component=polygons[index],workspace=module.createRiverPartitionWorkspace(component);
      const angle=workspace.centerLatitude*(Math.PI/180),cosine=Math.cos(angle);
      const samples=[];
      for(const river of (request.riverFeatures || []).slice(0,2)) {
        if(!['LineString','MultiLineString'].includes(river?.geometry?.type))continue;
        const parts=river?.geometry?.type==='LineString'?[river.geometry.coordinates]:(river?.geometry?.coordinates || []);
        const part=parts[0] || [];
        samples.push({id:String(river.properties?.pandolab_id ?? river.id ?? ''),points:part.slice(0,2).map(function(point){return {input:point,meters:workspace.toMeters(point)};})});
      }
      result.push({donorId:donor.countryId,componentIndex:index,centerLongitude:workspace.centerLongitude,centerLatitude:workspace.centerLatitude,angle:angle,cosine:cosine,cosineScale:Math.max(1e-6,cosine),firstPoint:component?.[0]?.[0]?{input:component[0][0],meters:workspace.toMeters(component[0][0])}:null,riverSamples:samples});
    }
  }
  return result;
})
