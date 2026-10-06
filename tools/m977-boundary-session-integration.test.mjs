import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
const read=file=>readFileSync(new URL('../'+file,import.meta.url),'utf8');

test('replacement probe and non-skipping contract are registered as required coverage',()=>{
 const cmake=read('app/CMakeLists.txt');assert.match(cmake,/add_executable\(m977_boundary_session_replacement_probe\s+\.\.\/tests\/m977_boundary_session_replacement_probe\.cpp\)/);assert.match(cmake,/target_link_libraries\(m977_boundary_session_replacement_probe PRIVATE pandoeditor_editor\)/);
 const contract=cmake.match(/add_test\(NAME m977_boundary_session_replacement_contract[\s\S]*?set_tests_properties\(m977_boundary_session_replacement_contract[\s\S]*?\nendif\(\)/)?.[0];assert.ok(contract);
 for(const name of ['runtime','suite','browser-runner','native','gate'])assert.ok(contract.includes(`/tools/m977-boundary-session-replacement/${name}.test.mjs`),name);
 assert.ok(contract.includes('/tools/m977-boundary-session-integration.test.mjs'));assert.ok(contract.includes('M977_SESSION_REPLACEMENT_PROBE=$<TARGET_FILE:m977_boundary_session_replacement_probe>'));
 assert.ok(read('tools/m6-regression-audit.py').includes('"m977_boundary_session_replacement_contract"'));
});

test('exact CI captures new sessions and preserves raw false legacy report under explicit coverage gate',()=>{
 const workflow=read('.github/workflows/m97-editing-parity.yml');
 assert.match(workflow,/ln build\/app\/m974_snap_probe[^\n]*build\/app\/m977_boundary_session_replacement_probe[^\n]*evidence\/m974-runtime/);
 assert.match(workflow,/sha256sum m974_snap_probe[^\n]*m977_boundary_session_replacement_probe[^\n]*> SHA256SUMS/);
 assert.match(workflow,/node --max-old-space-size=512 tools\/m977-boundary-session-replacement\/browser-runner\.mjs evidence\/boundary-session-chromium/);
 assert.match(workflow,/m974-runtime\/m977_boundary_session_replacement_probe < evidence\/boundary-session-chromium\/cases\.json > evidence\/native-boundary\/session-observations\.json/);
 const step=workflow.match(/- name: Validate boundary observable contracts and matched replacement sessions[\s\S]*?(?=\n      - name:)/)?.[0];assert.ok(step,'explicit changed gate scope');assert.ok(step.includes('node tools/m977-boundary-session-replacement/gate.mjs'));
 for(const option of ['--legacy-browser-directory','--legacy-native-file','--session-suite','--session-browser-report','--session-binding','--session-native-file','--output-file','--legacy-output-file','--session-output-file','--expected-commit','--expected-run-id','--native-commit','--legacy-native-binary-sha256','--session-native-binary-sha256'])assert.ok(step.includes(option),option);
 assert.ok(step.includes('--legacy-output-file evidence/native-boundary/comparison.json'));assert.ok(step.includes('--output-file evidence/native-boundary/matched-coverage.json'));
 assert.doesNotMatch(step,/continue-on-error|\|\| true|set \+e|--exclude|--ignore/);
});
