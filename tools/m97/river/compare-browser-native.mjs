import assert from 'node:assert/strict';
import {assertExact,stable,outputHash} from './oracle.mjs';
import {assertControllerAnnexObservations,assertControllerLifecycleObservations} from './controller-suite.mjs';
import {verifySuite,canonicalHash,runtimePin} from './suite.mjs';
export function assertRuntimeHash(actual,expected,label){assert.equal(actual,expected,label+' exact runtime output');}
export function assertCompleteRows(caseIds,native,browser) {
  assert.equal(caseIds.length,42,'42 case identities required');assert.equal(new Set(caseIds).size,42,'Unique case identities required');
  assert.ok(native,'Missing compiled native evidence');assert.ok(browser,'Missing actual browser evidence');
  for(const field of ['observations','presentations','traces'])for(const [label,report] of [['native',native],['browser',browser]]) {
    assert.equal(report[field]?.length,42,'Missing '+label+' '+field);
    for(let i=0;i<42;i++)assert.equal(report[field][i].name,caseIds[i],label+' ordered '+field+' identity');
  }
}
export function compareBrowserNative(payload,native,browser,{commit,runId}={}) {
  assert.ok(payload,'Missing input payload');assert.ok(native,'Missing compiled native evidence');assert.ok(browser,'Missing actual browser evidence');verifySuite(payload);assertCompleteRows(payload.cases.map(c=>c.name),native,browser);
  assert.equal(native.schema,'river-native-observations-v2');assert.equal(browser.schema,'river-browser-observations-v2');
  assertExact(browser.sourceHashes,payload.identity.sourceHashes,'Observed browser source hashes');assertExact(browser.boundaryHashes,payload.identity.boundaryHashes,'Observed browser boundary hashes');
  assert.match(native.runtime.binarySha256,/^[0-9a-f]{64}$/,'Compiled native binary identity');
  assert.equal(payload.commit,commit);assert.equal(payload.runId,runId);
  assertExact(native.identity,payload.identity,'Native exact-commit/input/source identity');assertExact(browser.identity,payload.identity,'Browser exact-commit/input/source identity');
  assert.equal(native.runtime.qt,runtimePin.qt);assert.equal(browser.runtime.playwright,runtimePin.playwright);assert.equal(browser.runtime.browserVersion,runtimePin.chromium);assert.equal(browser.runtime.chromiumRevision,runtimePin.revision);assert.equal(browser.runtime.cdp.jsVersion,runtimePin.v8);assert.ok(['HeadlessChrome/'+runtimePin.chromium,'Chrome/'+runtimePin.chromium].includes(browser.runtime.cdp.product));
  const hashes={};
  for(const field of ['observations','presentations','traces']){
    assert.equal(native[field]?.length,42,'Missing native '+field);assert.equal(browser[field]?.length,42,'Missing browser '+field);
    hashes[field]=[];
    for(let i=0;i<42;i++){const name=payload.cases[i].name;assert.equal(native[field][i].name,name);assert.equal(browser[field][i].name,name);
      const a=field==='observations'?stable(native[field][i]):native[field][i],b=field==='observations'?stable(browser[field][i]):browser[field][i];
      assertRuntimeHash(canonicalHash(a),canonicalHash(b),name+'/'+field);assertExact(a,b,name+'/'+field);hashes[field].push({name,sha256:canonicalHash(a)});
    }
  }
  assert.equal(browser.branchObservations?.length,3,'Required actual near-collinear traces');assert.ok(browser.branchObservations.every(r=>r.outputExact&&r.segments.length===1));
  assert.equal(browser.annex?.length,payload.annex.scenarios.length,'Required browser annex observations');
  assert.deepEqual(browser.annex.map(r=>r.name),payload.annex.scenarios.map(r=>r.name));assert.ok(browser.annex.every(r=>r.inputUnchanged&&r.fullWorldFeatureCount===258&&r.result.autoIncludedSlivers));
  assert.equal(browser.controllerSourceCommit,payload.controllerSources.baseBehavioralCommit,'Controller source commit');assert.equal(browser.controllerBehavioralCommit,payload.controllerSources.behavioralCommit,'Controller corrected behavior commit');
  assertControllerAnnexObservations(payload.controllerAnnex,browser.controllerAnnex);
  assertControllerLifecycleObservations(payload.controllerLifecycle,browser.controllerLifecycle);
  return {state:'passed',commit,runId,total:42,exactRaw:42,exactPresentation:42,exactWorkspace:42,hashes,controllerLifecycle:{observations:browser.controllerLifecycle.map(row=>({name:row.name,sha256:canonicalHash(row),checkpoints:row.lifecycleActions.map(action=>action.name)}))},controllerAnnex:{role:payload.controllerAnnex.role,observations:browser.controllerAnnex.map(row=>({name:row.name,sha256:canonicalHash(row),selectedKeys:row.selectedCells.map(cell=>cell.key),autoIncludedSlivers:row.result.autoIncludedSlivers}))},annex:{role:payload.annex.role,observations:browser.annex.map(r=>({name:r.name,sha256:canonicalHash(r),selectedKeys:r.selectedCells.map(c=>c.key),autoIncludedSlivers:r.result.autoIncludedSlivers}))}};
}
