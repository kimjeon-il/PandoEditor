import {createStoredAssetLoader} from './stored-asset-loader.js';
import {normalizeTerritorialLibraryEntity,normalizeTerritorialLibraryIndex,selectGeometryVersion} from './territorial-library.js';

export function createTerritorialEntityLoader({indexUrl,indexSpec,dataRevision,fetchFn=globalThis.fetch,cacheStorage=globalThis.caches}) {
  if(!indexUrl || !indexSpec?.sha256 || !dataRevision)throw new Error('Territorial index URL, integrity metadata and data revision are required');
  const base=new URL(indexUrl);
  const assets=createStoredAssetLoader({dataRevision,fetchFn,cacheStorage,resolveUrl:spec=>{const url=new URL(spec.url,base);url.searchParams.set('v',dataRevision);return url;}});
  const entities=new Map(), pending=new Map();
  let index=null,indexPromise=null;
  const parse=bytes=>JSON.parse(new TextDecoder().decode(bytes));
  const comparable=value=>Array.isArray(value)?value.map(comparable):value && typeof value==='object'
    ? Object.fromEntries(Object.keys(value).sort().map(key=>[key,comparable(value[key])])):value;
  async function loadIndex(){
    if(index)return index;
    if(!indexPromise)indexPromise=assets.loadAsset({...indexSpec,url:base.href},'catalog','index','Territorial index',parse).then(({value})=>{
      index=normalizeTerritorialLibraryIndex(value);return index;
    }).catch(error=>{indexPromise=null;throw error;});
    return indexPromise;
  }
  async function loadEntity(entityId){
    if(entities.has(entityId))return entities.get(entityId);
    if(!pending.has(entityId))pending.set(entityId,(async()=>{
      const catalog=await loadIndex();const entry=catalog.entities.find(e=>e.entityId===entityId);
      if(!entry)throw new Error(`Unknown territorial entity: ${entityId}`);
      const {value}=await assets.loadAsset({...entry,url:entry.file,encoding:'gzip'},'catalog',entityId,entityId,bytes=>normalizeTerritorialLibraryEntity(parse(bytes)));
      const metadataKeys=['schemaVersion','entityId','lineageId','entityKind','names','alternateNames','lifetime','parentEntityId','instantiation','metadata'];
      const chunkMetadata=Object.fromEntries(metadataKeys.map(key=>[key,value[key]]));
      const indexMetadata=Object.fromEntries(metadataKeys.map(key=>[key,entry[key]]));
      chunkMetadata.geometryVersions=value.geometryVersions.map(({geometry,...version})=>version);
      indexMetadata.geometryVersions=entry.geometryVersions;
      if(JSON.stringify(comparable(chunkMetadata))!==JSON.stringify(comparable(indexMetadata)))throw new Error('Territorial chunk identity/metadata mismatch');
      entities.set(entityId,value);return value;
    })().finally(()=>pending.delete(entityId)));
    return pending.get(entityId);
  }
  return Object.freeze({loadIndex,loadEntity,peek:id=>entities.get(id)||null,
    loadGeometryVersion:async(id,date)=>selectGeometryVersion(await loadEntity(id),date)});
}
