import assert from 'node:assert/strict';
export const collinearTargets={SRB:{river:14,boundary:732},HRV:{river:589,boundary:402},MDA:{river:221,boundary:67}};
function nextUp(value){const v=new DataView(new ArrayBuffer(8));v.setFloat64(0,value);v.setBigUint64(0,v.getBigUint64(0)+1n);return v.getFloat64(0);}
export function stressCases(original) {
  assert.equal(original.length,24);const output=[];
  const pair=original.find(c=>c.name==='crossing-pair');
  function shifted(name,latitude,halfHeight=.5){
    const row=structuredClone(pair);row.name=name;delete row.expectedCount;
    const visit=value=>Array.isArray(value)&&typeof value[0]==='number'?[value[0],latitude+(value[1]-.5)*(halfHeight*2)]:value.map(visit);
    row.request.donors[0].geometry.coordinates=visit(row.request.donors[0].geometry.coordinates);
    for(const river of row.request.riverFeatures)river.geometry.coordinates=visit(river.geometry.coordinates);
    row.components[0].geometry=structuredClone(row.request.donors[0].geometry);
    row.stressProvenance={kind:'translated-crossing-pair',nominalLatitude:latitude,halfHeight};output.push(row);return row;
  }
  const centers=[44.23100202768906,45.018160304698945,47.13193946527778];
  for(const latitude of centers)for(const sign of [1,-1])shifted('latitude-'+String(sign*latitude),sign*latitude);
  for(const gapM of [.4999999,.5,.5000001]){
    const row=shifted('node-gap-'+gapM,centers[0]),gap=gapM/(Math.PI/180*6371008.8);
    row.request.riverFeatures=[{type:'Feature',id:'gap',properties:{pandolab_id:'gap',category:'river'},geometry:{type:'LineString',coordinates:[[.5,centers[0]-.5+gap],[.5,centers[0]+.5-gap]]}}];
    row.stressProvenance={kind:'node-gap-metres',gapM,constructionRadius:6371008.8,nominalLatitude:centers[0]};
  }
  // Approved bounded fallback: three next-representable center cases. The three
  // original full-country cases separately receive actual instrumented branch
  // observations; these shifted cases are NOT labelled mini-collinear fixtures.
  for(const [i,latitude] of centers.entries())shifted('next-latitude-'+['SRB','HRV','MDA'][i],nextUp(latitude));
  shifted('cosine-floor-below',89.9999427041,1e-7);shifted('cosine-floor-above',89.9999427044,1e-7);
  for(const name of ['SRB-raw','HRV-raw']){const row=structuredClone(original.find(c=>c.name===name));row.name=name+'-reversed-features';row.request.riverFeatures.reverse();row.stressProvenance={kind:'reverse-feature-order',base:name};output.push(row);}
  for(const name of ['MDA-raw','SRB-live']){const row=structuredClone(original.find(c=>c.name===name));row.name=name+'-reversed-traversal';for(const f of row.request.riverFeatures){if(f.geometry.type==='LineString')f.geometry.coordinates.reverse();else for(const part of f.geometry.coordinates)part.reverse();}row.stressProvenance={kind:'reverse-line-traversal',base:name};output.push(row);}
  assert.equal(output.length,18);return output;
}
export function instrumentRiverSource(source) {
  const pairs=[
    ['const workspace = createRiverPartitionWorkspace(component);','globalThis.__riverTraceContext={donorId,componentIndex};\n  const workspace = createRiverPartitionWorkspace(component);'],
    ['for (const hit of segmentIntersections(river, boundary)) {',`const __riverHits=segmentIntersections(river,boundary);
      const __target=globalThis.__riverTraceTargets[globalThis.__riverTraceContext.donorId];
      if(__target && globalThis.__riverTraceContext.componentIndex===0 && boundaryIndex===__target.boundary && riverSegments.indexOf(river)===__target.river) {
        const rx=river.b[0]-river.a[0],ry=river.b[1]-river.a[1],sx=boundary.b[0]-boundary.a[0],sy=boundary.b[1]-boundary.a[1];
        globalThis.__riverBranchTrace.push({donorId:globalThis.__riverTraceContext.donorId,componentIndex:0,riverIndex:__target.river,boundaryIndex,sourceRiverIds:Array.from(river.sourceIds),riverEdge:[river.a,river.b],boundaryEdge:[boundary.a,boundary.b],denominator:rx*sy-ry*sx,hits:__riverHits});
      }
      for (const hit of __riverHits) {`],
  ];
  for(const [before,after] of pairs){assert.equal(source.split(before).length,2,'Diagnostic instrumentation count');source=source.replace(before,after);}
  return {source,replacements:pairs.map(([before,after])=>({before,after}))};
}
