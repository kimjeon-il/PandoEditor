#!/usr/bin/env node
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {dirname,resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const web=await import(pathToFileURL(resolve(root,'tests/fixtures/web-m6/source/temporal.js')));
const lines=[];
for(const input of ['1945','1945-08-15','-0001','0001','+12000',
                    '-0400-02-29','0000','0','1945-2-1','12000',
                    '2023-02-29',' 1945 ','\u00a01945\u00a0','']) {
  try {
    const p=web.parseTemporal(input,{nullable:false});
    lines.push(['P',p.canonical,p.precision,p.startKey.join(','),p.endKey.join(',')].join('|'));
  } catch {lines.push('P|ERR');}
}
const contains=[
  ['1945','1945','1945-08-15'],
  ['1945-08-15','1945-08-15','1945'],
  ['1945-08-15','1945-08-15','1945-08-16'],
  ['-0001','0001','0001-01-01'],
  ['', '1945','1946'],
];
for(const [from,to,point] of contains)
  lines.push('C|'+Number(web.temporalContains(web.normalizeTemporalInterval(from,to),point)));
for(const [from,to,point] of [
  ['1945','1945','1945-12-31'],
  ['1945-08-15','1945-08-15','1945-08-16'],
])
  lines.push('O|'+Number(web.temporalIntervalsOverlap(
    web.normalizeTemporalInterval(from,to),web.normalizeTemporalInterval(point,point))));
lines.push('R|'+web.compareTemporal('1945','1945-08-15')+'|'+
  web.compareTemporal('1945','1945-08-15',{leftBoundary:'end',rightBoundary:'end'}));
const expected=lines.join('\n')+'\n';
if(process.argv.includes('--web-only'))process.stdout.write(expected);
else {
  const probe=process.argv.at(-1);
  assert.ok(probe&&!probe.startsWith('--'),'C++ probe path is required');
  assert.equal(execFileSync(probe,{encoding:'utf8'}),expected);
  console.log('pinned web temporal parity passed');
}
