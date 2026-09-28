#!/usr/bin/env node
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {dirname,resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const web=await import(pathToFileURL(resolve(root,'tests/fixtures/web-m6/source/historical-library.js')));
const serviceModule=await import(pathToFileURL(resolve(root,'tests/fixtures/web-m6/source/historical-library-service.js')));
const geometry={type:'Polygon',coordinates:[[[0,0],[1,0],[1,1],[0,1],[0,0]]]};
const version=(id,validFrom,validTo)=>({id,validFrom,validTo,geometry});
const entity={libraryId:'historical-subunit:example',type:'territory',
  canonicalName:'Example',displayNames:{ko:'예시'},alternateNames:['Former Example'],
  startDate:'1918',endDate:'2003',metadata:{geographicRegion:'Europe'},
  geometryVersions:[version('early','1918','1941'),
                    version('middle','1945','1992'),
                    version('late','1992-04-27','2003')],
  instantiation:{mode:'country-territory-priority'}};
const snapshot={id:'pilot',referenceDate:'1991',entityRefs:[entity.libraryId,'missing']};
const catalog=web.createHistoricalLibrary({schemaVersion:2,entities:[entity],snapshots:[snapshot]});
const actual=catalog.get(entity.libraryId);
const lines=['type|'+actual.type];
for(const date of ['1991','2000','1943','1900',''])
  lines.push('version|'+web.selectGeometryVersion(actual,date).id);
lines.push('search|'+catalog.search({query:'Former'}).length+'|'+
  catalog.search({type:'subunit',status:'past',referenceDate:'1991',geographicRegion:'Europe'}).length+'|'+
  catalog.search({type:'subunit',status:'current'}).length);
const instantiated=web.instantiateLibraryEntity(actual,'1991','early');
instantiated.geometry.coordinates[0][0][0]=42;
assert.equal(actual.geometryVersions[0].geometry.coordinates[0][0][0],0);
lines.push('instantiate|'+instantiated.geometryVersionId+'|'+instantiated.name+'|'+
  instantiated.instantiation.mode+'|'+catalog.getSnapshot('pilot').entityRefs.length);
const child={...entity,libraryId:'child',parentLibraryId:entity.libraryId,instantiation:{mode:'independent'}};
const grandchild={...entity,libraryId:'grandchild',parentLibraryId:'child',instantiation:{mode:'independent'}};
const service=serviceModule.createHistoricalLibraryService({dataUrl:'pilot',
  fetchJson:async()=>({schemaVersion:2,entities:[entity,child,grandchild],snapshots:[snapshot]}),
  getCountriesData:()=>({features:[]}),displayName:()=>'',
  combineGeometries:()=>geometry,currentYear:()=>2026});
await service.load();
for(const depth of ['none','level1','all'])
  lines.push('children|'+depth+'|'+service.entityRefsWithChildren([entity.libraryId],depth).join(','));
const expected=lines.join('\n')+'\n';
if(process.argv.includes('--web-only'))process.stdout.write(expected);
else {
  const probe=process.argv.at(-1);
  assert.ok(probe&&!probe.startsWith('--'));
  assert.equal(execFileSync(probe,{encoding:'utf8'}).replace(/\r\n/g,'\n'),expected);
  console.log('pinned web historical catalog parity passed');
}
