import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {normalizeLayerPresentation} from '../tests/fixtures/web-presentation/source/layer-presentation.js';
import {setScopedItemVisibility} from '../tests/fixtures/web-presentation/source/layer-list-model.js';
import {createTerritorialFillResolver} from '../tests/fixtures/web-presentation/source/territorial-fill-style.js';
const hashes={'layer-presentation.js':'9a820bf235ec8bd1995ae5a70499c5f6aef149e1','layer-list-model.js':'d91574f0e25863e2087fa856a0d8fc8a8b65c185','territorial-fill-style.js':'cac7b90d5b07d303daf073016100b3ff8b011e43'};
for(const [name,hash] of Object.entries(hashes)){
    const bytes=fs.readFileSync(new URL('../tests/fixtures/web-presentation/source/'+name,import.meta.url));
    assert.equal(crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex'),hash,name);
}
const cases=[];
for(const master of [true,false])for(const scoped of [undefined,true,false])for(const countryOpacity of [0,.6,1])for(const groupOpacity of [1,.5])for(const objectOpacity of [undefined,1,.3])for(const countryBlend of ['normal','multiply'])cases.push({master,scoped,countryOpacity,groupOpacity,objectOpacity,countryBlend});
const probe=process.argv[2];assert.ok(probe,'pass presentation_parity_probe.exe');
const run=spawnSync(probe,[],{input:cases.map(x=>JSON.stringify(x)).join('\n')+'\n',encoding:'utf8'});
assert.equal(run.status,0,run.stderr||run.error?.message);
const actual=run.stdout.trim().split(/\r?\n/).map(JSON.parse);assert.equal(actual.length,cases.length);
for(let i=0;i<cases.length;i++){
    const c=cases[i];const unit=id=>({id,properties:{unitType:'subunit',parentId:'A',sovereignId:'A'}});
    const override=c.objectOpacity===undefined?{}:{opacity:c.objectOpacity};
    const state={layerVisibility:{subunits:c.master},itemVisibility:{},territorialUnits:[unit('S'),unit('T')],countriesData:{features:[{id:'A'}]},layerPresentation:normalizeLayerPresentation({styles:{countries:{opacity:c.countryOpacity,blendMode:c.countryBlend},subunits:{opacity:c.groupOpacity}},objectStyles:{'territorial:subunit:S':override}})};
    if(c.scoped!==undefined)setScopedItemVisibility({...state,group:'subunits',allIds:['S','T'],ids:['S'],visible:c.scoped});
    const resolved=createTerritorialFillResolver({state,countryColor:()=> '#112233',defaultColor:'#cccccc'})(state.territorialUnits[0]);
    assert.deepEqual(actual[i],{master:state.layerVisibility.subunits,S:state.itemVisibility.subunits?.S!==false,T:state.itemVisibility.subunits?.T!==false,opacity:resolved.opacity,blendMode:resolved.blendMode},JSON.stringify(c));
}
console.log(`M3.4 scoped visibility and fill inheritance: ${cases.length} actual web/native comparisons passed`);
