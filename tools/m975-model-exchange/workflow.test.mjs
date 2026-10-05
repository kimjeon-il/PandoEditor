import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const text=()=>readFileSync(new URL('../../.github/workflows/m97-editing-parity.yml',import.meta.url),'utf8');
test('model exchange receives the exact compiled native probe without a second native build',()=>{
 const workflow=text();
 assert.match(workflow,/ln build\/app\/m975_model_exchange_probe evidence\/m975-runtime\//);
 assert.match(workflow,/qmake -query QT_VERSION > evidence\/m975-runtime\/QT_VERSION/);
 assert.match(workflow,/name: m975-runtime-\$\{\{ github.sha \}\}/);
 const job=workflow.split('\n  model-exchange-parity:\n')[1];
 assert.ok(job,'missing M9.7.5 exchange job');
 assert.match(job,/needs: linux-regression/);
 assert.match(job,/test "\$\(cat m975-runtime\/COMMIT\)" = "\$GITHUB_SHA"/);
 assert.match(job,/sha256sum -c SHA256SUMS/);
 assert.match(job,/cat m975-runtime\/QT_VERSION/);
 assert.match(job,/QT_VERSION\)" = '6\.8\.3'/);
 assert.match(job,/node tools\/m975-model-exchange\/browser-runner\.mjs m975-runtime evidence\/model-exchange/);
 assert.doesNotMatch(job,/cmake --build/,'exchange must reuse authenticated native build');
 assert.match(job,/if: always\(\)/,'failure evidence must remain downloadable');
 assert.match(job,/name: m975-model-exchange-\$\{\{ github.sha \}\}/);
});
test('model exchange diagnostic tests cannot be silently omitted by an empty inventory',()=>{
 assert.match(text(),/node --test tools\/m975-model-exchange\/\*\.test\.mjs/);
});
test('pre-run failures preserve job identity and terminal status evidence',()=>{
 const job=text().split('\n  model-exchange-parity:\n')[1];
 const init=job.indexOf('mkdir -p evidence/model-exchange');assert.ok(init>=0&&init<job.indexOf('actions/download-artifact@v4'));
 assert.match(job,/JOB_STATUS: \$\{\{ job.status \}\}/);
 assert.match(job,/printf '%s\\n' "\$JOB_STATUS" > evidence\/model-exchange\/job-status.txt/);
 assert.match(job,/name: Record terminal exchange job status\n        if: always\(\)/);
});
