import test from 'node:test';import assert from 'node:assert/strict';
test('browser bundle explicitly imports serializer and binds source, input, runtime and native identities',async()=>{
 const m=await import('./suite.mjs').catch(()=>null);assert.ok(m,'M975 exact-identity suite required');
 const suite=m.createExchangeSuite({commit:'a'.repeat(40),runId:'diagnostic-only',native:{commit:'a'.repeat(40),sha256:'b'.repeat(64)}});m.verifyExchangeSuite(suite);assert.ok(suite.entrypoints.includes('project-serializer'));assert.equal(suite.cases.length,10);const runtime=(0,eval)(suite.runtime.source);assert.equal(typeof runtime.runExchangeCase,'function');assert.equal(typeof runtime.reopenExchange,'function');
 for(const mutate of [s=>s.cases.pop(),s=>s.cases[0].inputRaw+=' ',s=>s.sources[Object.keys(s.sources)[0]]+=' ',s=>s.runtime.source+=' ',s=>s.identity.native.sha256='c'.repeat(64)]){const s=structuredClone(suite);mutate(s);assert.throws(()=>m.verifyExchangeSuite(s));}
});
test('native action recipes carry explicit complete stages and real edit entrypoints',async()=>{
 const m=await import('./suite.mjs').catch(()=>null);assert.ok(m,'M975 exact-identity suite required');const suite=m.createExchangeSuite({commit:'a'.repeat(40),runId:'diagnostic-only',native:{commit:'a'.repeat(40),sha256:'b'.repeat(64)}});
 for(const row of suite.cases){const request=m.nativeRequest(row);assert.equal(request.inputSha256,row.sha256);if(row.operation!=='storage')assert.deepEqual(request.actions.filter(a=>a.stage).map(a=>a.stage),request.expectedStages);if(row.operation!=='storage')assert.ok(request.actions.some(a=>a.op==='begin'));}
});
