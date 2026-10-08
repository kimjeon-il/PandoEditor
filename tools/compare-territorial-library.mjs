#!/usr/bin/env node
// Read-only Web/App historical catalog audit. No application asset is written.
import {readFileSync,readdirSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,join,dirname} from 'node:path';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {fileURLToPath} from 'node:url';

export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const ensure = (ok,message) => {if(!ok)throw Error(message);};
const naturalFile = id => id.replace(':','-')+'.json.gz';
function catalog(root) {
  root=resolve(root);
  const raw=readFileSync(join(root,'index.json'));
  const index=JSON.parse(raw.toString('utf8'));
  ensure(index.schemaVersion===2 && Array.isArray(index.entities) &&
    Array.isArray(index.lineages) && Array.isArray(index.snapshots),
    'Invalid territorial index: '+root);
  const entries=new Map(),files=new Set(['index.json']);
  for(const e of index.entities){
    ensure(typeof e.entityId==='string' && /^[a-z]+:[A-Za-z0-9_-]+$/.test(e.entityId)
      && e.file===naturalFile(e.entityId),'Invalid file/entity ID: '+e.entityId);
    ensure(!entries.has(e.entityId) && !files.has(e.file),'Duplicate catalog ID or file: '+e.entityId);
    ensure(/^[0-9a-f]{64}$/.test(e.sha256) && Number.isSafeInteger(e.compressedBytes)
      && e.compressedBytes>0 && Number.isSafeInteger(e.decodedBytes) && e.decodedBytes>0,
      'Invalid catalog integrity metadata: '+e.file);
    ensure(Array.isArray(e.geometryVersions) && e.geometryVersions.length===e.geometryVersionCount,
      'Invalid catalog geometry metadata: '+e.file);
    entries.set(e.entityId,e);files.add(e.file);
  }
  const actual=new Set(readdirSync(root).filter(n=>n==='index.json'||n.endsWith('.json.gz')));
  ensure(isDeepStrictEqual(files,actual),'Catalog missing/extra files: '+root);
  for(const group of [...index.lineages,...index.snapshots]) {
    ensure(Array.isArray(group.entityRefs) && group.entityRefs.every(id=>entries.has(id)),
      'Unresolved catalog entity reference');
  }
  return {root,index,raw,entries,indexSha256:sha256(raw)};
}
function chunk(source,e) {
  const stored=readFileSync(join(source.root,e.file));
  ensure(stored.length===e.compressedBytes,'Stored size mismatch: '+e.file);
  ensure(sha256(stored)===e.sha256,'SHA-256 mismatch: '+e.file);
  const decoded=gunzipSync(stored,{maxOutputLength:64*1024*1024});
  ensure(decoded.length===e.decodedBytes,'Decoded length mismatch: '+e.file);
  const json=JSON.parse(decoded.toString('utf8'));
  ensure(json.schemaVersion===2 && json.entityId===e.entityId,'Entity identity mismatch: '+e.file);
  ensure(json.lifetime && json.lifetime.validFrom===e.validFrom
    && json.lifetime.validTo===e.validTo,'Index / entity lifetime mismatch: '+e.file);
  ensure(Array.isArray(json.geometryVersions)
    && json.geometryVersions.length===e.geometryVersions.length,'Geometry version count mismatch: '+e.file);
  for(let i=0;i<json.geometryVersions.length;i++){
    const {geometry,...metadata}=json.geometryVersions[i];
    ensure(geometry && typeof geometry==='object' &&
      isDeepStrictEqual(metadata,e.geometryVersions[i]),
      'Index / entity geometry metadata mismatch: '+e.file+' : '+i);
  }
  return {stored,decoded,json};
}
function classify(w,a) {
  if(w.stored.equals(a.stored))return 'byte-identical';
  if(w.decoded.equals(a.decoded))return 'gzip-only';
  if(isDeepStrictEqual(w.json,a.json))return 'json-format-only';
  if(w.json.lifetime && a.json.lifetime && w.json.lifetime.validFrom!==a.json.lifetime.validFrom){
    const patched={...w.json,lifetime:{...w.json.lifetime,validFrom:a.json.lifetime.validFrom}};
    if(isDeepStrictEqual(patched,a.json))return 'lifetime-start-only';
  }
  const wg=w.json.geometryVersions,ag=a.json.geometryVersions;
  if(!isDeepStrictEqual(wg,ag))return 'geometry-or-version-drift';
  return 'other-semantic-drift';
}
function normalizedRow(e){
  const {validFrom,compressedBytes,decodedBytes,sha256,...rest}=e;
  return {...rest,lifetime:{...rest.lifetime,validFrom:null}};
}
export function compare(webRoot,appRoot,webRef=null,appRef=null) {
  const web=catalog(webRoot),app=catalog(appRoot);
  const report={schema:'pandoeditor-territorial-stage5',version:1,
    source:{webRef,appRef,webIndexSha256:web.indexSha256,appIndexSha256:app.indexSha256},
    indexBytesEqual:web.raw.equals(app.raw),
    lineagesEqual:isDeepStrictEqual(web.index.lineages,app.index.lineages),
    snapshotsEqual:isDeepStrictEqual(web.index.snapshots,app.index.snapshots),
    counts:{webEntities:web.entries.size,appEntities:app.entries.size,
      'byte-identical':0,'gzip-only':0,'json-format-only':0,
      'lifetime-start-only':0,'geometry-or-version-drift':0,
      'other-semantic-drift':0,'missing-app':0,'missing-web':0},
    indexUnexpectedChanges:[],files:[]};
  for(const [id,we] of web.entries){
    const w=chunk(web,we),ae=app.entries.get(id);
    if(!ae){report.counts['missing-app']++;report.files.push({entityId:id,status:'missing-app'});continue;}
    const a=chunk(app,ae),status=classify(w,a);
    report.counts[status]++;
    if(!isDeepStrictEqual(normalizedRow(we),normalizedRow(ae))){
      report.indexUnexpectedChanges.push(id);
    }
    report.files.push({entityId:id,file:we.file,status,
      webStoredSha256:we.sha256,appStoredSha256:ae.sha256,
      webDecodedBytes:w.decoded.length,appDecodedBytes:a.decoded.length,
      webStart:w.json.lifetime.validFrom,appStart:a.json.lifetime.validFrom});
  }
  for(const [id,ae] of app.entries)if(!web.entries.has(id)){
    chunk(app,ae);report.counts['missing-web']++;report.files.push({entityId:id,status:'missing-web'});
  }
  return report;
}
export function gateReport(r,mode) {
  const errors=[];
  if(!r.lineagesEqual || !r.snapshotsEqual)errors.push('Lineage or snapshot mismatch');
  if(r.counts['missing-app'] || r.counts['missing-web'])errors.push('Missing entities');
  if(mode==='exact' && (!r.indexBytesEqual || r.counts['byte-identical']!==r.counts.webEntities))
    errors.push('App differs from its exact pinned Web source');
  if(mode==='lifetime-only') {
    if(r.indexUnexpectedChanges.length)errors.push('Unexpected index metadata drift');
    if(!r.indexBytesEqual && !r.files.some(x=>x.status!=='byte-identical'))
      errors.push('Unexplained index byte drift');
    if(r.counts['geometry-or-version-drift'] || r.counts['other-semantic-drift'])
      errors.push('Unexpected geometry or other semantic drift');
  }
  return errors;
}
const called=process.argv[1] && resolve(process.argv[1])===fileURLToPath(import.meta.url);
if(called){
  const args=new Map();
  for(let i=2;i<process.argv.length;i+=2){
    const key=process.argv[i],value=process.argv[i+1];
    if(!['--web-root','--app-root','--web-ref','--app-ref','--output','--gate'].includes(key)
      ||!value||value.startsWith('--')||args.has(key)){
      console.error('Invalid parameter: '+key);process.exit(2);
    }
    args.set(key,value);
  }
  const mode=args.get('--gate')||'report';
  if(!args.get('--web-root') || !['report','exact','lifetime-only'].includes(mode)){
    console.error('Expected --web-root and --gate report|exact|lifetime-only');process.exit(2);
  }
  for(const key of ['--web-ref','--app-ref']){
    if(args.has(key)&&!/^[a-f0-9]{40}$/.test(args.get(key))){
      console.error('Expected pinned full commit SHA for '+key);process.exit(2);
    }
  }
  try{
    const r=compare(args.get('--web-root'),
      args.get('--app-root')||resolve(dirname(fileURLToPath(import.meta.url)),'../assets/territorial-library-v2'),
      args.get('--web-ref')||null,args.get('--app-ref')||null);
    const errors=gateReport(r,mode);
    r.gate={mode,passed:!errors.length,errors};
    if(args.has('--output')){
      const target=resolve(args.get('--output'));mkdirSync(dirname(target),{recursive:true});
      writeFileSync(target,JSON.stringify(r,null,2)+'\n');
    }
    console.log('Territorial crosscheck '+mode+': '+JSON.stringify(r.counts));
    console.log('Index: bytes='+r.indexBytesEqual+', lineages='+r.lineagesEqual+
      ', snapshots='+r.snapshotsEqual+', unexpected index entries='+r.indexUnexpectedChanges.length);
    if(errors.length){console.error('Stage 5 crosscheck FAILED: '+errors.join('; '));process.exitCode=1;}
    else console.log('Stage 5 gate passed; app assets unchanged.');
  }catch(e){console.error('Stage 5 verification failed: '+e.stack);process.exitCode=1;}
}
