import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import {gunzipSync} from 'node:zlib';
import { sha256, containedFile } from './provenance.mjs';

const load = (root, path) => import(pathToFileURL(resolve(root, path)).href);
const json = path => JSON.parse(readFileSync(path, 'utf8'));
const run = (binary, input) => execFileSync(binary, [], { input: input === undefined ? undefined : JSON.stringify(input), encoding: 'utf8', timeout:120000, maxBuffer: 32 * 1024 * 1024 });
export const nativeTargets = { 'library-queries': 'portability_library_probe', 'async-lifecycle': 'portability_async_probe', 'geometry-containment': 'portability_geometry_probe', 'command-history': 'portability_command_probe', selection: 'selection_probe', 'timeline-records': 'timeline_records_tests', 'timeline-storage': 'timeline_storage_tests' };

nativeTargets['file-exchange'] = 'timeline_project_tests';
nativeTargets['property-commands'] = 'portability_command_probe';
nativeTargets['structure-commands'] = 'portability_command_probe';
nativeTargets['geometry-clipping'] = 'portability_geometry_probe';
nativeTargets['library-loading'] = 'portability_library_probe';
nativeTargets['distribution-scale'] = 'content_probe';

export async function observeCurrent(adapter, { webRoot, appRoot, binary, evidenceRoot }) {
  if(adapter==='distribution-scale') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/distribution-scale.json'));
    const {distributionTrace}=await load(webRoot,'tools/parity/distribution.mjs');
    return corpus.map(row=>({id:row.id,input:row.input,web:distributionTrace(row.input),
      app:JSON.parse(run(binary,row.input)),expected:row.expected}));
  }
  if(adapter==='library-loading') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/library-loading.json'));
    const {index}=json(resolve(webRoot,'tests/fixtures/portability/library-queries.json'));
    const indexBytes=readFileSync(containedFile(webRoot,index.webPath));
    const nativeIndex=readFileSync(containedFile(appRoot,index.appPath));
    if(sha256(indexBytes)!==index.sha256||sha256(nativeIndex)!==index.sha256)throw new Error('Loader catalog bytes changed');
    const bytes=readFileSync(containedFile(webRoot,corpus.webPath)),appBytes=readFileSync(containedFile(appRoot,corpus.appPath));
    if(sha256(bytes)!==corpus.sha256||sha256(appBytes)!==corpus.sha256)throw new Error('Loader chunk bytes changed');
    const source=JSON.parse(gunzipSync(bytes));
    const {libraryLoadingTrace}=await load(webRoot,'tools/parity/library.mjs');
    const web=await libraryLoadingTrace(corpus,index,indexBytes,bytes);
    const app=JSON.parse(run(binary,{...corpus,loading:true,indexText:nativeIndex.toString('utf8'),sha256:index.sha256,entityBytes:appBytes.toString('base64')}));
    if(app.length!==corpus.operations.length)throw new Error('Incomplete loading observations');
    return corpus.operations.map((row,i)=>({id:row.id,input:row,web:web[i],app:app[i],expected:{id:row.id,...row.expected,value:row.expected.value==='source'?source:null}}));
  }
  if(adapter==='geometry-clipping') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/geometry-clipping.json'));
    const {clippingTrace}=await load(webRoot,'tools/parity/geometry.mjs');
    const web=clippingTrace(corpus),app=JSON.parse(run(binary,corpus));
    if(app.length!==corpus.length)throw new Error('Incomplete clipping observations');
    return corpus.map((row,index)=>{if(app[index].id!==row.id)throw new Error('Clipping case identity mismatch');return {id:row.id,input:row,web:web[index],app:app[index],expected:row.expected};});
  }
  if(adapter==='structure-commands') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/structure-commands.json'));
    const {structureTrace}=await load(webRoot,'tools/parity/structure.mjs');
    const web=structureTrace(corpus),app=JSON.parse(run(binary,corpus));
    if(web.length!==corpus.operations.length||app.length!==corpus.operations.length)throw new Error('Incomplete structural observations');
    return corpus.operations.map((row,index)=>({id:row.id,step:index,input:row,web:web[index],app:app[index],expected:row.expected}));
  }
  if(adapter==='property-commands') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/property-commands.json'));
    const {propertyTrace}=await load(webRoot,'tools/parity/properties.mjs');
    const web=propertyTrace(corpus),app=JSON.parse(run(binary,corpus));
    if(web.length!==corpus.operations.length||app.length!==corpus.operations.length)throw new Error('Incomplete property observations');
    return corpus.operations.map((row,index)=>({id:row.id,step:index,input:row,web:web[index],app:app[index],expected:row.expected}));
  }
  if (adapter === 'file-exchange') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/file-exchange.json'));
    const {exchangeTrace,exchangeContent,exchangeDocument,storageVerdict}=await load(webRoot,'tools/parity/exchange.mjs');
    const directory=mkdtempSync(resolve(evidenceRoot,'exchange-'));
    const observations=[];
    for(const row of corpus.cases) {
      const base=resolve(directory,String(observations.length));mkdirSync(base);
      const inputPath=containedFile(webRoot,row.fixture);
      const input=row.format==='gpkg'?readFileSync(inputPath):json(inputPath);
      const wholeDocument=row.contentMode==='document',observe=wholeDocument?exchangeDocument:exchangeContent;
      const web=await exchangeTrace(input,row.format,(name,bytes)=>writeFileSync(resolve(base,name),bytes),wholeDocument);
      const result=spawnSync(binary,['--trace-file',row.format==='gpkg'?'gpkg':'web',inputPath,resolve(base,'native')],{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
      writeFileSync(resolve(base,'native.log'),(result.stdout??'')+'\n'+(result.stderr??''));
      if(result.error)throw result.error;
      if(result.status!==0||json(resolve(base,'native/trace-status.json')).complete!==true)throw new Error('Native exchange trace incomplete: '+row.id);
      const app={stages:['read.web.json','reopened.web.json','package-reopened.web.json'].map(name=>observe(json(resolve(base,'native',name)))),activation:json(resolve(base,'native/activation.json')).result};
      const expectedContent=observe(json(containedFile(webRoot,row.expected)));
      observations.push({id:row.id,web,app,expected:{stages:[expectedContent,expectedContent,expectedContent],activation:row.activation},input:row});
    }
    for(const row of corpus.rejections) {
      const input=json(resolve(webRoot,'tests/fixtures/timeline-exchange/calendar-boundaries.json'));
      const record=Object.values(input.timelineRecords).filter(Array.isArray).flat().find(value=>value.id===row.record);
      if(!record)throw new Error('Missing rejection record');record[row.field]=row.value;
      const stem=String(observations.length);
      const source=resolve(directory,stem+'.json'),output=resolve(directory,stem+'.output.json');writeFileSync(source,JSON.stringify(input));
      const result=spawnSync(binary,[source,output],{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
      writeFileSync(resolve(directory,stem+'.log'),(result.stdout??'')+'\n'+(result.stderr??''));
      if(result.error)throw result.error;
      const codes=(result.stderr??'').match(/TIMELINE_[A-Z_]+/g)??[];
      observations.push({id:row.id,input:row,web:{verdict:storageVerdict(input),published:false},app:{verdict:result.status===1&&new Set(codes).size===1?codes[0]:'UNEXPECTED_EXIT:'+result.status,published:existsSync(output)},expected:{verdict:row.expected,published:false}});
    }
    return observations;
  }
  if(adapter==='library-queries') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/library-queries.json'));
    const webBytes=readFileSync(resolve(webRoot,corpus.index.webPath)),appBytes=readFileSync(resolve(appRoot,corpus.index.appPath));
    for(const bytes of [webBytes,appBytes])if(bytes.length!==corpus.index.compressedBytes||sha256(bytes)!==corpus.index.sha256)throw new Error('Library source bytes/hash drift');
    const {libraryTrace}=await load(webRoot,'tools/parity/library.mjs');
    const web=await libraryTrace(corpus,webBytes),app=JSON.parse(run(binary,{indexText:appBytes.toString('utf8'),sha256:corpus.index.sha256,cases:corpus.cases}));
    if(app.length!==corpus.cases.length)throw new Error('Incomplete native catalog observations');
    return corpus.cases.map((row,index)=>{
      if(app[index].id!==row.id)throw new Error('Native catalog case identity mismatch');
      return {id:row.id,web:web[index],app:app[index],expected:row.expected,input:row};
    });
  }
  if (adapter === 'async-lifecycle') {
    const corpus=json(resolve(webRoot,'tests/fixtures/portability/async-lifecycle.json'));
    const {asyncTrace}=await load(webRoot,'tools/parity/async.mjs');
    const web=await asyncTrace(corpus),app=JSON.parse(run(binary,corpus));
    if(app.length!==corpus.length)throw new Error('Incomplete native async observations');
    return corpus.map((row,index)=>{
      if(app[index].id!==row.id)throw new Error('Native async case identity mismatch');
      return {id:row.id,web:web[index],app:app[index],expected:row.expected,input:row};
    });
  }
  if (adapter === 'geometry-containment') {
    const corpus = json(resolve(webRoot, 'tests/fixtures/portability/geometry-containment.json'));
    const {geometryTrace} = await load(webRoot, 'tools/parity/geometry.mjs');
    const web=geometryTrace(corpus),app=JSON.parse(run(binary,corpus));
    if (app.length!==corpus.length) throw new Error('Incomplete native geometry observations');
    return corpus.map((row,index)=>{
      if(app[index].id!==row.id)throw new Error('Native geometry case identity mismatch');
      return {id:row.id,web:web[index],app:app[index],expected:row.expected,input:row};
    });
  }
  if (adapter === 'command-history') {
    const corpus = json(resolve(webRoot, 'tests/fixtures/portability/command-history.json'));
    const { commandHistoryTrace } = await load(webRoot, 'tools/parity/command-history.mjs');
    const web = commandHistoryTrace(corpus), app = JSON.parse(run(binary, corpus));
    if (web.length !== corpus.operations.length || app.length !== corpus.operations.length) throw new Error('Incomplete command/history trace');
    return corpus.operations.map((op, index) => ({ id: op.id, step: index, input: op,
      web: web[index], app: app[index], expected: op.expected }));
  }
  if (adapter === 'selection') {
    const corpus = json(resolve(webRoot, 'tests/fixtures/portability/selection.json'));
    const { selectionTrace } = await load(webRoot, 'tools/check-platform-portability.mjs');
    const { createObjectSelectionController } = await load(webRoot, 'assets/js/modules/object-selection-controller.js');
    const web = selectionTrace(createObjectSelectionController, corpus), app = JSON.parse(run(binary, corpus));
    if (web.length !== corpus.operations.length || app.length !== corpus.operations.length) throw new Error('Incomplete selection trace');
    return corpus.operations.map((op, index) => ({ id: `selection:${index}:${op.op}`, step: index,
      web: web[index], app: app[index], expected: corpus.expected[index], input: op }));
  }
  if (adapter === 'timeline-records' || adapter === 'timeline-storage') {
    const name = adapter === 'timeline-records' ? 'timeline-records' : 'timeline-storage';
    // The native fixtures are compiled from these files. A matching name is not
    // enough: enforce identical inputs before comparing executable output.
    for (const file of [`${name}.json`, `${name}-cases.mjs`]) {
      const left = readFileSync(resolve(webRoot, 'tests/fixtures', file)).toString().replaceAll('\r\n', '\n');
      const right = readFileSync(resolve(appRoot, 'tests/fixtures', file)).toString().replaceAll('\r\n', '\n');
      if (sha256(left) !== sha256(right)) throw new Error(`Native/Web fixture drift: ${file}`);
    }
    const corpus = await load(webRoot, `tests/fixtures/${name}-cases.mjs`);
    const module = await load(webRoot, `assets/js/modules/${name}.js`);
    const all = adapter === 'timeline-records' ? corpus.timelineCases() : corpus.timelineStorageCases();
    const active = all.filter(row => row.native !== false);
    const lines = run(binary).trim().split(/\r?\n/);
    if (lines.length !== active.length) throw new Error(`Incomplete native timeline output: ${lines.length}/${active.length}`);
    const observations = active.map((row, index) => {
      let verdict = 'OK', snapshot = null;
      try {
        if (adapter === 'timeline-records') module.normalizeTimelineRecords(row.input, corpus.timelineContext(row.context));
        else {
          const state = module.restoreTimelineStorage(row.input, row.entities);
          snapshot = module.snapshotTimelineStorage(state.records, state.geometries, row.entities);
        }
      } catch (error) { verdict = error.code ?? 'UNEXPECTED'; }
      if (adapter === 'timeline-records') {
        const [name, nativeVerdict, ...extra] = lines[index].split('\t');
        if (extra.length || name !== row.name) throw new Error(`Native case identity mismatch: ${row.name}`);
        return { id: row.name, input: row.input, web: { verdict }, app: { verdict: nativeVerdict }, expected: { verdict: row.expected } };
      }
      const observed = JSON.parse(lines[index]);
      if (observed.name !== row.name) throw new Error(`Native case identity mismatch: ${row.name}`);
      let expectedSnapshot = null;
      if (row.expected === 'OK') {
        // Independently specified storage contract: preserve every input field,
        // trim interval endpoints and emit the documented UTF-8 ID/version order.
        expectedSnapshot = structuredClone(row.input);
        for (const key of ['lifetimes', 'geometryBindings', 'parentRelations']) {
          for (const record of expectedSnapshot.records[key]) for (const endpoint of ['validFrom', 'validTo']) {
            if (typeof record[endpoint] === 'string') record[endpoint] = record[endpoint].trim();
          }
        }
        expectedSnapshot.geometries.sort((a, b) => Buffer.compare(Buffer.from(a.id), Buffer.from(b.id)) || a.version - b.version);
      }
      return { id: row.name, input: row.input, web: { verdict, snapshot },
        app: { verdict: observed.verdict, snapshot: observed.snapshot }, expected: { verdict: row.expected, snapshot: expectedSnapshot } };
    });
    return [...observations, ...all.filter(row => row.native === false).map(row => ({ id: row.name,
      status: 'UNSUPPORTED', reason: 'Existing native fixture generator excludes this input; not counted as compared.', input: row.input }))];
  }
  throw new Error(`Unknown current observation adapter: ${adapter}`);
}
