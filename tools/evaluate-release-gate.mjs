#!/usr/bin/env node
// Fail closed: no missing report, skip, drift review, or unmeasured platform
// can be promoted to a final migration PASS.
import {existsSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {validateApprovedReport} from './parity/report.mjs';

function option(flag) {const i=process.argv.indexOf(flag);return i<0?null:process.argv[i+1];}
function read(path) {return path&&existsSync(resolve(path))?JSON.parse(readFileSync(resolve(path),'utf8')):null;}
try {
  const root=resolve(option('--root')??'.');
  const manifest=read(`${root}/docs/parity-manifest.json`);
  const oracle=read(option('--oracle-report'));
  const assets=read(option('--asset-report'));
  const ctestPath=option('--ctest-report');
  const platforms=read(`${root}/docs/platform-validation.json`);
  const budget=read(`${root}/docs/performance-budget.json`);
  const build=read(`${root}/docs/build-validation.json`);
  if(!manifest||manifest.schema!=='pandoeditor-final-parity')throw Error('missing parity manifest');
  const blockers=[];
  for(const [name,domain] of Object.entries(manifest.domains))
    if(domain.status!=='synced')blockers.push(`${name}: ${domain.status}`);
  if(manifest.unclassifiedPaths?.length)blockers.push('unclassified upstream paths');
  if(!manifest.pandoEditorMainHead)blockers.push('PandoEditor main identity not observed');
  if(!ctestPath||!existsSync(resolve(ctestPath)))blockers.push('fresh full CTest report NOT RUN');
  else {
    const junit=readFileSync(resolve(ctestPath),'utf8');
    const cases=(junit.match(/<testcase\b/g)??[]).length;
    const failures=(junit.match(/<(?:failure|error|skipped)\b/g)??[]).length;
    if(!cases||failures)blockers.push(`full CTest has ${cases} cases and ${failures} failures/errors/skips`);
  }
  if(build?.release?.status!=='PASS')blockers.push('Release build/smoke NOT RUN/failed');
  if(build?.asanUbsan?.status!=='PASS')blockers.push('ASan/UBSan NOT RUN/failed');
  try {
    validateApprovedReport(oracle ?? {}, read(`${root}/docs/platform-portability-pin.json`) ?? {});
    if (!oracle.sources.web.clean || !oracle.sources.app.clean) blockers.push('Parity evidence uses dirty checkouts');
    if (oracle.selected.length !== oracle.matrix.length || oracle.matrix.some(row => !row.selected)) blockers.push('Parity report is partial');
    if (oracle.cases.some(row => !['PASS','N/A'].includes(row.status)) ||
        oracle.matrix.some(row => Object.values(row.axes).some(axis => !['PASS','N/A'].includes(axis.status))))
      blockers.push('Behavioral parity contains differences, unsupported cases or missing evidence');
    if (oracle.problems.length || oracle.unclassifiedPaths.length || oracle.gates.behavioral !== 'PASS') blockers.push('Behavioral parity gate is blocked');
  } catch (error) { blockers.push('Current parity report invalid: ' + error.message); }
  if(!assets||assets.schema!=='pandoeditor-release-assets')blockers.push('asset report NOT RUN');
  else if(assets.status!=='source-verified'||assets.windowsDeployedPackage!=='PASS'||
          assets.androidApkOrAab!=='PASS')blockers.push('deployed asset integrity NOT RUN/failed');
  for(const name of ['windows','android']) {
    const evidence=platforms?.[name];
    if(evidence?.status!=='PASS'||!evidence.device||!evidence.graphicsApis?.length||
       !evidence.scenarios?.length||!evidence.screenshots?.length||!evidence.logs?.length)
      blockers.push(`${name} physical validation NOT RUN/failed/incomplete`);
  }
  if(budget?.status!=='ACCEPTED'||!budget.acceptedBy||!budget.devices?.length||
     !budget.thresholds||!Object.keys(budget.thresholds).length||!budget.measurementReports?.length)
    blockers.push('measured performance budget not accepted');
  const result={schema:'pandoeditor-final-release-gate',version:1,
    generatedAt:new Date().toISOString(),worldMapHead:manifest.worldMapHead,
    pandoEditorFeatureHead:manifest.pandoEditorFeatureHead,
    status:blockers.length?'BLOCKED':'PASS',blockers};
  const output=option('--out');
  if(output)writeFileSync(resolve(output),JSON.stringify(result,null,2)+'\n');
  console.log(`${result.status}: ${blockers.length} blocker(s)`);
  if(blockers.length)process.exitCode=1;
}catch(error){console.error(`Release gate evaluation failed: ${error.message}`);process.exitCode=1;}
