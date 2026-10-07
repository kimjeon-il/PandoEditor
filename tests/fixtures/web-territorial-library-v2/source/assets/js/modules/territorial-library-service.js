import {instantiateLibraryEntity, territorialEntityExistsAt, selectGeometryVersion} from './territorial-library.js';
import {parseTemporal} from './temporal.js';

export function createTerritorialLibraryService({loader, today = () => {
  const date=new Date();return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`;
}}) {
  if (!loader || !['loadIndex','loadEntity','peek'].every(key=>typeof loader[key] === 'function')) throw new Error('Territorial loader is required');
  let catalog = null;
  let pending = null;
  async function load() {
    if(catalog) return catalog;
    if(!pending) pending=loader.loadIndex().then(index=>{
      if(index.schemaVersion!==2)throw new Error('Territorial index schema mismatch');
      const ids=new Set();
      for(const e of index.entities){if(!e.entityId || ids.has(e.entityId))throw new Error('Duplicate catalog identity');ids.add(e.entityId);}
      catalog=index;return index;
    }).catch(error=>{pending=null;throw error;});
    return pending;
  }
  const list=()=>catalog?.entities || [];
  const get=id=>list().find(e=>e.entityId===String(id)) || null;
  function entityRefsWithChildren(rootIds, depth='none', referenceDate=today()) {
    const selected=new Set(rootIds.map(String));
    if(depth==='none')return [...selected];
    let frontier=[...selected];
    while(frontier.length){const parents=new Set(frontier);frontier=[];
      for(const e of list()) if(parents.has(e.parentEntityId) && !selected.has(e.entityId) && territorialEntityExistsAt(e,referenceDate)){selected.add(e.entityId);frontier.push(e.entityId);}
      if(depth==='level1')break;
    }
    return [...selected];
  }
  async function instantiateDescriptors(rootIds, referenceDate, depth='none') {
    if(!referenceDate)throw new Error('A reference date is required');
    parseTemporal(referenceDate,{nullable:false});
    await load();
    const entities=await Promise.all(entityRefsWithChildren(rootIds,depth,referenceDate).map(id=>loader.loadEntity(id)));
    return entities.map(e=>instantiateLibraryEntity(e,referenceDate));
  }
  return Object.freeze({load,get,list,loadEntity:loader.loadEntity, getLoadedEntity:loader.peek,entityRefsWithChildren,instantiateDescriptors,
    snapshots:()=>catalog?.snapshots || [],getSnapshot:id=>catalog?.snapshots.find(s=>s.id===id) || null,
    today,
    search({query='',referenceDate=today()}={}){
      parseTemporal(referenceDate,{nullable:false});
      const needle=String(query).trim().toLocaleLowerCase('ko');
      const matches=values=>values.some(name=>name.toLocaleLowerCase('ko').includes(needle));
      return (catalog?.lineages || []).map(lineage=>{
        const groupMatches=!needle || matches(Object.values(lineage.names));
        const entities=lineage.entityRefs.map(get).filter(e=>territorialEntityExistsAt(e,referenceDate)
          && (groupMatches || matches([...Object.values(e.names),...e.alternateNames])))
          .map(e=>({...e,selectedVersionId:selectGeometryVersion(e,referenceDate)?.versionId || null}));
        return {lineageId:lineage.lineageId,names:lineage.names,entities};
      }).filter(group=>group.entities.length);
    },
  });
}
