import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
import {verifySources} from './contract.mjs';
const base=new URL('../../tests/fixtures/web-m97/',import.meta.url);
export async function loadOracle() {
  const manifest=JSON.parse(readFileSync(new URL('manifest.json',base),'utf8'));
  const sourceRoot=new URL('source/',base);
  assert.deepEqual(verifySources(fileURLToPath(sourceRoot),manifest),[], 'Web source provenance');
  // Evaluate the original UMD build in its own realm; no source rewriting.
  const context=vm.createContext({});
  vm.runInContext(readFileSync(new URL('assets/js/vendor/polygon-clipping.min.js',sourceRoot),'utf8'),context);
  const clipper=context.polygonClipping;
  const [{planDrawnTerritoryAnnex},{createCountryCommandCalculator},{resolveSnap}]=await Promise.all([
    import(new URL('assets/js/modules/annex-geometry.js',sourceRoot)),
    import(new URL('assets/js/modules/map-edit-country-commands.js',sourceRoot)),
    import(new URL('assets/js/modules/geometry-snap.js',sourceRoot)),
  ]);
  const calculator=createCountryCommandCalculator(clipper);
  function calculate(row) {
    if(!['drawn-annex','annex','annex-batch','merge','new-country','snap'].includes(row.operation)) throw Error(`Unsupported corpus operation: ${row.operation}`);
    const input=structuredClone(row);
    try {
      if(row.operation==='snap') {
        const [sx,sy,ox,oy]=row.projection??[1,1,0,0];
        return {ok:true,result:resolveSnap({...input,project:([x,y])=>[x*sx+ox,y*sy+oy]})};
      }
      let message=input;
      if(row.operation==='drawn-annex') {
        const selected=planDrawnTerritoryAnnex({drawnGeometry:input.drawnGeometry,
          donorFeatures:input.features.filter(f=>input.donorIds.includes(f.id)),targetFeature:input.features.find(f=>f.id===input.targetId),clipper});
        if(!selected)return {ok:false,error:{stage:'selection',message:'No transferable selection'}};
        message={...input,operation:'annex',transferredGeometry:selected.transferGeometry};
      }
      // Actual production calculator owns the private working map and patch.
      const output=calculator.calculate(message,new Map(input.features.map(f=>[f.id,f])));
      return {ok:true,...structuredClone(output)};
    }catch(error){return {ok:false,error:{stage:'calculation',message:error.message}};}
  }
  return {manifest,calculate,clipper};
}
