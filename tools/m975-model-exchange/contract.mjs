import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
export const sha256=value=>createHash('sha256').update(value).digest('hex');
export function rawRecord(raw){assert.equal(typeof raw,'string');return {raw,bytes:Buffer.byteLength(raw),sha256:sha256(raw)};}
export function verifyRawRecord(row){assert.deepEqual(Object.keys(row).sort(),['bytes','raw','sha256']);assert.deepEqual(row,rawRecord(row.raw),'exact UTF-8 checkpoint transfer');return row.raw;}
// There is intentionally no blanket property deletion, array sorting, ID rewriting,
// geometry rounding or numeric tolerance. Additions require focused source evidence/tests.
export const permittedNormalizations=Object.freeze([{format:'native',path:'/documentId',reason:'decodeWeb recreates documentId from canonical web bytes; native v9 otherwise exact; native v10 requires explicit per-row/opaque-slot accounting receipts.'}]);
export function assertExchangeEqual(before,after,label='whole-file exchange'){assert.deepEqual(after,before,label);}
export function verifyInventory(definitions,rows){assert.deepEqual(rows.map(row=>row.case),definitions.map(row=>row.id),'exact ordered case inventory');for(const [i,row]of rows.entries())assert.deepEqual(Object.keys(row.stages),definitions[i].stages,'exact ordered stage inventory '+row.case);}
export function verifyCI(env,head){assert.equal(env.GITHUB_ACTIONS,'true','Actual browser capture requires authorized exact-commit GitHub CI');assert.match(env.GITHUB_SHA||'',/^[a-f0-9]{40}$/);assert.match(env.GITHUB_RUN_ID||'',/^\d+$/);assert.equal(head,env.GITHUB_SHA,'CI checkout must match GITHUB_SHA');}

export function assertNativeExchangeEqual(before,after,label='native whole-file exchange'){assert.equal(before.version,9,'legacy comparator only supports native v9');assert.equal(after.version,9,'native v10 requires explicit row accounting');assert.equal(typeof before.documentId,'string');assert.equal(typeof after.documentId,'string');const a=structuredClone(before),b=structuredClone(after);a.documentId=b.documentId='[new-import-document-identity]';assert.deepEqual(b,a,label);}
