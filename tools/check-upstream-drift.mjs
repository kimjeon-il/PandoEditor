#!/usr/bin/env node
// Freeze observed upstream revision and classify changed paths. A changed file
// is REVIEW until its behavior is inspected; a matching SHA alone is SYNCED.
import {spawnSync} from 'node:child_process';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const initial = '58e4087f85aa51884bc4ab80959e05010d94d7d5';
const m5 = 'c0bd31d13dc8495593d78cf51f7cc195de7c9469';
const domains = {
  selection: {baseline:initial, sources:['assets/js/modules/object-selection-controller.js','assets/js/modules/selection-pass.js']},
  properties: {baseline:initial, sources:['assets/js/modules/object-property-controller.js','assets/js/modules/property-editor-bindings.js']},
  structure: {baseline:initial, sources:['assets/js/modules/territorial-edit-plan.js','assets/js/modules/territorial-units.js']},
  presentation: {baseline:initial, sources:['assets/js/modules/layer-presentation.js','assets/js/modules/label-layout.js']},
  interaction: {baseline:initial, sources:['assets/js/modules/map-interaction-style.js','assets/js/modules/territorial-interaction-policy.js']},
  geometry: {baseline:initial, sources:['assets/js/modules/country-geometry.js','assets/js/workers/map-edit-worker.js']},
  content: {baseline:m5, sources:['assets/js/modules/distribution-model.js','assets/js/modules/label-layout.js']},
  historyGis: {baseline:m5, sources:['assets/js/modules/temporal.js','assets/js/modules/historical-library.js']},
  renderer: {baseline:m5, sources:['assets/js/modules/render-scene.js','assets/js/modules/gpu-map-renderer.js']},
  mobileUi: {baseline:m5, sources:['assets/js/modules/mobile-sheet-controller.js']},
  storage: {baseline:initial, sources:['assets/js/modules/project-serializer.js','assets/js/modules/project-state.js']},
  crossCutting: {baseline:initial, sources:[]},
};

function run(command,args,cwd) {
  const result=spawnSync(command,args,{cwd,encoding:'utf8',maxBuffer:32*1024*1024});
  if(result.error||result.status!==0)
    throw Error(`${command} ${args.join(' ')}: ${result.error?.message??result.stderr.trim()}`);
  return result.stdout.trim();
}
function option(flag) {
  const i=process.argv.indexOf(flag);
  if(i<0)return null;
  if(!process.argv[i+1]||process.argv[i+1].startsWith('--'))throw Error(`${flag} requires a value`);
  return process.argv[i+1];
}
function classify(path) {
  const name=path.toLowerCase();
  if(/\.github\/|^docs\//.test(name))return 'crossCutting';
  if(/assets\/css\//.test(name)&&!/reference|mobile|touch|sheet|map-viewport|map-search|content|editor|view-menu/.test(name))return 'presentation';
  if(/app-(capability|domain-assembly|environment|lifecycle|runtime-dependenc|service-assembly|readiness)|bootstrap\.js|build-meta|notification-copy|user-preferences/.test(name))return 'crossCutting';
  if(/preview|startup/.test(name))return 'renderer';
  if(/reference-image/.test(name))return 'geometry';
  if(/physical|country-flags/.test(name))return 'content';
  if(/file-bindings|project-session|project-domain/.test(name))return 'storage';
  if(/object-commands|map-edit-country/.test(name))return 'structure';
  if(/navigation|global-input/.test(name))return 'interaction';
  if(/workspace|surface|editor-shell|editor-workspace/.test(name))return 'mobileUi';
  if(/assets\/data\//.test(name))return 'renderer';
  if(/selection|chooser|object-search/.test(name))return 'selection';
  if(/propert|color-picker|color-adapter/.test(name))return 'properties';
  if(/geometr|polygon|boundary|coast|mesh-edit/.test(name))return 'geometry';
  if(/territor|subunit|region|relation|structure/.test(name))return 'structure';
  if(/interaction|hover|map-input|selection-style/.test(name))return 'interaction';
  if(/distribution|hydro|generic|country-flags/.test(name))return 'content';
  if(/histor|temporal|gis|gpkg|geopackage|exchange/.test(name))return 'historyGis';
  if(/render|gpu|terrain|mesh|scene|lod|shader|quality|cull/.test(name))return 'renderer';
  if(/mobile|touch|sheet/.test(name))return 'mobileUi';
  if(/serializ|migration|persist|project-state|preview-cache|storage/.test(name))return 'storage';
  if(/present|label|layer|visibility|display|accent|flag/.test(name))return 'presentation';
  return 'crossCutting';
}
function blob(repo,sha,path) {
  const line=run('git',['ls-tree',sha,'--',path],repo);
  return line ? line.split(/\s+/)[2] : null;
}

try {
  const upstream=option('--upstream-root');
  const output=option('--out');
  if(!upstream||!output)throw Error('usage: check-upstream-drift.mjs --upstream-root <git checkout> --out <manifest> [--feature-root <repo>]');
  const repo=resolve(upstream),feature=resolve(option('--feature-root')??'.');
  const latest=run('git',['rev-parse','HEAD'],repo);
  const remote=run('git',['ls-remote','https://github.com/kimjeon-il/world-map.git','refs/heads/main'],repo).split(/\s+/)[0];
  if(latest!==remote)throw Error(`local upstream ${latest} differs from remote main ${remote}`);
  const result={schema:'pandoeditor-final-parity',version:1,
    observedAt:new Date().toISOString(),worldMapBaseline:m5,worldMapHead:latest,
    worldMapRemote:'https://github.com/kimjeon-il/world-map.git',
    pandoEditorFeatureHead:run('git',['rev-parse','HEAD'],feature),
    pandoEditorMainHead:null,
    pandoEditorMainNote:'No local main ref; private GitHub main cannot be read without credentials.',
    domains:{},unclassifiedPaths:[]};
  for(const [name,{baseline,sources}] of Object.entries(domains)) {
    run('git',['merge-base','--is-ancestor',baseline,latest],repo);
    const changes=run('git',['diff','--name-only',`${baseline}..${latest}`],repo)
      .split('\n').filter(Boolean);
    const paths=changes.filter(path=>classify(path)===name);
    const sourceBlobs=Object.fromEntries(sources.map(path=>
      [path,{baseline:blob(repo,baseline,path),head:blob(repo,latest,path)}]));
    if(Object.values(sourceBlobs).some(pair=>!pair.baseline||!pair.head))
      throw Error(`${name}: configured source missing from baseline or head`);
    result.domains[name]={syncedSha:baseline,status:paths.length?'review':'synced',
      changedPaths:paths,sourceBlobs};
  }
  result.unclassifiedPaths=[];
  writeFileSync(resolve(output),JSON.stringify(result,null,2)+'\n');
  console.log(`Observed world-map/main ${latest}; ${Object.values(result.domains).filter(d=>d.status==='review').length} domains need review; ${result.unclassifiedPaths.length} paths unclassified.`);
}catch(error){console.error(`Upstream drift freeze failed: ${error.message}`);process.exitCode=1;}
