// Generates the importer fixture using pinned, unmodified web code.
import fs from 'node:fs';
import {createProjectSerializer} from '../tests/fixtures/web-properties/source/project-serializer.js';
import {normalizeTerritorialUnits} from '../tests/fixtures/web-properties/source/territorial-units.js';
import {normalizeLayerPresentation} from '../tests/fixtures/web-properties/source/layer-presentation.js';
const geometry={type:'Polygon',coordinates:[[[0,0],[8,0],[8,8],[0,8],[0,0]]]};
const sid='00000000-0000-4000-8000-000000000003',rid='00000000-0000-4000-8000-000000000004';
const unit=(id,type,parent)=>({type:'Feature',id,geometry,properties:{schemaVersion:2,unitType:type,name:type==='subunit'?'Child':'Region',notes:'',parentId:parent,sovereignId:'A',coverageMode:type==='subunit'?'partition':'explicit',style:{},locked:false,validFrom:null,validTo:null}});
const units=normalizeTerritorialUnits([unit(sid,'subunit','A'),unit(rid,'region',sid)],{countryExists:id=>id==='A'});
const snapshot={countriesData:{type:'FeatureCollection',features:[{type:'Feature',id:'A',geometry,properties:{name:'Alpha'}}]},projectFields:{
 countryOverrides:{},territorialUnits:units,territorialRelations:[],labels:[],hydroEdits:[],genericFeatures:[],distributionLayers:[],distributionEntries:[],
 layerVisibility:{countries:true,subunits:true,regions:true},itemVisibility:{},layerPresentation:normalizeLayerPresentation({}),labelSettings:{},physicalSettings:{},distributionSettings:{renderMode:'dominant',boundaryVisible:true},sourceInfo:{}}};
const serializer=createProjectSerializer({appVersion:'fixture',baseDataset:'fixture-base',distributionTypes:['language','ethnicity','religion'],distributionModes:['territorial','geometry'],terrainDataset:'fixture-terrain',hydroDataset:'fixture-hydro',readSnapshot:()=>snapshot,now:()=>new Date('2026-09-19T00:00:00Z')});
fs.writeFileSync(new URL('../tests/fixtures/web-properties/clean-v5.input.json',import.meta.url),JSON.stringify(serializer.buildProject(),null,2)+'\n');
console.log('Generated clean v5 with original serializer, territory normalizer and presentation normalizer.');
