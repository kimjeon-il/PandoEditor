#!/usr/bin/env node
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {dirname,resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const web=await import(pathToFileURL(resolve(root,'tests/fixtures/web-m6/source/exchange-adapter-registry.js')));
const plans=await import(pathToFileURL(resolve(root,'tests/fixtures/web-m6/source/gis-import-plan.js')));
const lines=[];
for(const target of ['admin','territory','administrative','country','unknown']) {
  const descriptor=web.exchangeTargetDescriptor(target);
  lines.push(['target',target,web.normalizeExchangeTarget(target),descriptor.domain,
              Number(descriptor.replaceOnly),Number(descriptor.fallback)].join('|'));
}
const plan=plans.createGisImportPlan({id:'gis-import:1',kind:'territorial',projectGeneration:7,
  source:{fileName:'test.geojson',sourceKind:'geojson'},affectedIds:['A','A','B']});
lines.push(['plan',plan.version,plan.kind,plan.source.fileName,plan.source.sourceKind,
            plan.affectedIds.join(',')].join('|'));
plans.assertCurrentGisImportPlan(plan,7);
let stale=false;
try {plans.assertCurrentGisImportPlan(plan,8);} catch(error){stale=error.code==='PL-GIS-STALE-PLAN-001';}
lines.push('stale|'+Number(stale));
const expected=lines.join('\n')+'\n';
if(process.argv.includes('--web-only'))process.stdout.write(expected);
else {
  const probe=process.argv.at(-1);
  assert.ok(probe&&!probe.startsWith('--'),'C++ probe path is required');
  assert.equal(execFileSync(probe,{encoding:'utf8'}),expected);
  console.log('pinned web GIS exchange plan parity passed');
}
