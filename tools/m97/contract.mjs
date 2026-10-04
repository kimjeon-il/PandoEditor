import {readFileSync, realpathSync} from 'node:fs';
import {resolve, relative, isAbsolute, dirname} from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';

export const stages = Object.freeze(['before','preview','cancel','confirm','undo','redo']);
const blob = bytes => createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
export function verifySources(root, manifest) {
  const failures=[], seen=new Set(), base=realpathSync(root);
  const pinned=new Set((manifest.sources??[]).map(source=>source.path));
  if (!Array.isArray(manifest.sources) || !manifest.sources.length) return ['No pinned sources'];
  for (const source of manifest.sources) {
    try {
      if (typeof source.path !== 'string' || isAbsolute(source.path) || source.path.split(/[\\/]/).includes('..')) throw Error('Unsafe path');
      if (seen.has(source.path)) throw Error('Duplicate source');
      seen.add(source.path);
      const path=realpathSync(resolve(base,source.path)), rel=relative(base,path);
      if (rel.startsWith('..') || isAbsolute(rel)) throw Error('Source escapes root');
      if (!/^[a-f0-9]{40}$/.test(source.blob)) throw Error('Invalid Git blob');
      const bytes=readFileSync(path);
      if (blob(bytes) !== source.blob) throw Error('Source hash mismatch');
      if (/\.m?js$/.test(source.path)) {
        const imports=bytes.toString().matchAll(/(?:\bfrom\s*|\bimport\s*(?:\(\s*)?)["']([^"']+)["']/g);
        for(const match of imports) if(match[1].startsWith('.')) {
          const dependency=relative(base,resolve(dirname(path),match[1])).split('\\').join('/');
          if(!pinned.has(dependency)) throw Error(`Unpinned dependency: ${dependency}`);
        }
      }
    } catch(error) { failures.push(`${source.path}: ${error.message}`); }
  }
  return failures;
}
const escape=value=>String(value).replaceAll('~','~0').replaceAll('/','~1');
const absent=Object.freeze({$absent:true});
function walk(expected, actual, path, differences) {
  if (isDeepStrictEqual(expected,actual)) return;
  if (Array.isArray(expected) && Array.isArray(actual)) {
    if(expected.length!==actual.length) differences.push({path:`${path}/length`,expected:expected.length,actual:actual.length});
    for(let i=0;i<Math.max(expected.length,actual.length);i++) walk(i<expected.length?expected[i]:absent,i<actual.length?actual[i]:absent,`${path}/${i}`,differences);
    return;
  }
  if (expected && actual && typeof expected==='object' && typeof actual==='object' && !Array.isArray(expected) && !Array.isArray(actual)) {
    const keys=[...new Set([...Object.keys(expected),...Object.keys(actual)])].sort();
    for(const key of keys) walk(Object.hasOwn(expected,key)?expected[key]:absent,Object.hasOwn(actual,key)?actual[key]:absent,`${path}/${escape(key)}`,differences);
    return;
  }
  differences.push({path,expected:expected===undefined?absent:expected,actual:actual===undefined?absent:actual});
}
const record=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
function completeState(state) {
  if(!record(state)||!record(state.history)||!['undo','redo'].every(key=>Number.isInteger(state.history[key])&&state.history[key]>=0))return false;
  if(Object.hasOwn(state,'document')) {
    const document=state.document;
    return record(document)&&['entities','identities','distributionLayers','distributionEntries'].every(key=>Array.isArray(document[key]))
      &&record(document.timelineRecords)&&['lifetimes','geometryBindings','parentRelations'].every(key=>Array.isArray(document.timelineRecords[key]))
      &&Array.isArray(document.geometryVersions)
      &&record(state.presentation)&&['itemVisibility','labelSettings','layerPresentation'].every(key=>record(state.presentation[key]))
      &&Object.hasOwn(state,'preview')&&(state.preview===null||record(state.preview));
  }
  return ['objects','retainedRefs','selection'].every(key=>Array.isArray(state[key]));
}
export function compareObservations(expected, actual) {
  const differences=[];
  if(expected.case!==actual.case) differences.push({path:'/case',expected:expected.case,actual:actual.case});
  for(const stage of stages) {
    const e=expected.stages?.[stage], a=actual.stages?.[stage], path=`/stages/${stage}`;
    if(e?.observed!==true || a?.observed!==true) {
      differences.push({path:`${path}/observed`,expected:e?.observed??absent,actual:a?.observed??absent});
      continue;
    }
    if(!completeState(e.state) || !completeState(a.state)) {differences.push({path:`${path}/state`,expected:e.state??absent,actual:a.state??absent});continue;}
    walk(e.state,a.state,`${path}/state`,differences);
    // Outcomes distinguish rejection from successful no-op, even if state agrees.
    walk(e.outcome??null,a.outcome??null,`${path}/outcome`,differences);
  }
  return structuredClone(differences);
}
export function checkBaseline(expected, actual, baseline = []) {
  const differences=compareObservations(expected,actual);
  const incomplete=stages.some(stage=>expected.stages?.[stage]?.observed!==true||actual.stages?.[stage]?.observed!==true||!completeState(expected.stages?.[stage]?.state)||!completeState(actual.stages?.[stage]?.state));
  const status=incomplete?'incomplete':!differences.length ? (baseline.length?'unexpected-match':'matched')
    : isDeepStrictEqual(differences,baseline)?'known-mismatch':'unexpected-mismatch';
  return {status,differences};
}
