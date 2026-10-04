import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fixture,canonical,sha256,outputHash} from './oracle.mjs';
export function browserBaseline(){const b=JSON.parse(fs.readFileSync(path.join(fixture,'browser-baseline.json')));assert.equal(b.schema,'actual-chromium-river-baseline-v1');assert.equal(b.cases.length,24);assert.equal(new Set(b.cases.map(c=>c.name)).size,24);return b;}
export function assertCapturedBrowser(row,observation,name=row.name){const pin=browserBaseline().cases.find(c=>c.name===name);assert.ok(pin,'Missing actual Chromium baseline: '+name);assert.equal(sha256(JSON.stringify(canonical({...row,name}))),pin.inputSha256,name+' input');assert.equal(outputHash({...observation,name}),pin.outputSha256,name+' complete actual Chromium output');}
