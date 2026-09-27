#!/usr/bin/env node
// One report for registered web/native differential suites. A missing suite is
// SKIPPED and blocks the gate; a web-only calculation is never native parity.
import {spawnSync} from 'node:child_process';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const suites = [
  ['selection','M3.1','selection_web_parity'],
  ['properties','M3.2','property_web_parity'],
  ['structure','M3.3','m33_structure_web_parity'],
  ['presentation','M3.4','presentation_web_parity'],
  ['interaction','M3.5',null],
  ['geometry','M4','m4_geometry_web_parity'],
  ['content','M5 content','m5_content_web_parity'],
  ['content','M5 hydro index','m5_hydro_index_parity'],
  ['content','M5 hydro pack','m5_hydro_pack_parity'],
  ['content','M5 hydro merge','m5_hydro_logical_merge_parity'],
  ['content','M5 viewport','m5_hydro_viewport_parity'],
  ['content','M5 draw/pick','m5_render_pick_order_parity'],
  ['content','M5 safe area','m5_label_safe_area_web'],
  ['historyGis','M6 temporal','m6_temporal_web_parity'],
  ['historyGis','M6 historical','m6_historical_catalog_web_parity'],
  ['historyGis','M6 GIS','m6_gis_exchange_web_parity'],
  ['historyGis','M6 GIS export','m6_gis_export_sqlite_oracle'],
  ['historyGis','M6 project GPKG','m6_project_gpkg_sqlite_oracle'],
  ['renderer','M7.1 corpus','world_corpus_tests'],
  ['renderer','M7.2 projection','m72_projection_web_oracle'],
  ['renderer','M7.2 packet','m72_packet_corpus'],
  ['renderer','M7.3 GPU parity',null],
  ['renderer','M7.4 full-world parity',null],
  ['renderer','M7.5 quality native parity',null],
  ['geometry','M7.6 large-world edit parity',null],
];
function arg(name) {
  const i=process.argv.indexOf(name);
  return i<0?null:process.argv[i+1];
}
function command(binary,args,cwd) {
  const result=spawnSync(binary,args,{cwd,encoding:'utf8',maxBuffer:32*1024*1024,
    env:{...process.env,QT_QPA_PLATFORM:process.env.QT_QPA_PLATFORM??'offscreen',
      QT_QUICK_BACKEND:process.env.QT_QUICK_BACKEND??'software'}});
  return {code:result.status,error:result.error?.message??null,
    stdout:result.stdout,
    output:(result.stdout+'\n'+result.stderr).trim().slice(-6000)};
}
try {
  const root=resolve(arg('--root')??'.');
  const manifest=JSON.parse(readFileSync(resolve(root,'docs/parity-manifest.json'),'utf8'));
  if(manifest.schema!=='pandoeditor-final-parity'||manifest.version!==1)throw Error('parity manifest schema mismatch');
  if(process.argv.includes('--list')) {
    process.stdout.write(JSON.stringify(suites.map(([domain,name,test])=>
      ({domain,name,test,sourceSha:manifest.domains[domain]?.syncedSha??null})),null,2)+'\n');
    process.exit(0);
  }
  const build=arg('--build-dir'),output=arg('--out');
  if(!build||!output)throw Error('usage: run-final-oracles.mjs --build-dir <built CMake dir> --out <report.json> [--root <repo>]');
  const buildDir=resolve(build);
  const listing=command('ctest',['--test-dir',buildDir,'--show-only=json-v1'],root);
  if(listing.code!==0||listing.error)throw Error(`Cannot enumerate CTest: ${listing.error??listing.output}`);
  const registered=new Set(JSON.parse(listing.stdout).tests.map(test=>test.name));
  const cases=[];
  for(const [domain,name,test] of suites) {
    const sourceSha=manifest.domains[domain]?.syncedSha??null;
    if(!test||!registered.has(test)) {
      cases.push({domain,name,sourceSha,worldMapHead:manifest.worldMapHead,
        cases:1,passed:0,failed:0,skipped:1,status:'skipped',
        reason:test?`CTest target ${test} is not registered`:'No web/native parity suite exists'});
      continue;
    }
    const result=command('ctest',['--test-dir',buildDir,'-R',`^${test}$`,
      '--output-on-failure'],root);
    const passed=result.code===0&&!result.error;
    cases.push({domain,name,sourceSha,worldMapHead:manifest.worldMapHead,
      cases:1,passed:passed?1:0,failed:passed?0:1,skipped:0,
      status:passed?'passed':'failed',test,exitCode:result.code,
      output:result.output,error:result.error});
  }
  const totals={cases:cases.length,passed:cases.reduce((n,c)=>n+c.passed,0),
    failed:cases.reduce((n,c)=>n+c.failed,0),skipped:cases.reduce((n,c)=>n+c.skipped,0)};
  const currentHead=command('git',['rev-parse','HEAD'],root);
  if(currentHead.code!==0)throw Error(`Cannot identify feature HEAD: ${currentHead.output}`);
  const report={schema:'pandoeditor-final-oracle-report',version:1,generatedAt:new Date().toISOString(),
    worldMapHead:manifest.worldMapHead,pandoEditorHead:currentHead.stdout.trim(),
    unit:'suite invocation, not individual assertions',totals,cases};
  writeFileSync(resolve(output),JSON.stringify(report,null,2)+'\n');
  console.log(`Final Oracle suites: ${totals.passed} passed, ${totals.failed} failed, ${totals.skipped} skipped`);
  if(totals.failed||totals.skipped)process.exitCode=1;
}catch(error){console.error(`Final Oracle runner failed: ${error.message}`);process.exitCode=1;}
