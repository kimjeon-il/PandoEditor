// Only this authenticated-byte path can label the web observation authoritative.
// Matching settled outcomes cannot establish full parity; clean native CI provenance is separate.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {gunzipSync} from 'node:zlib';
import {verifyCapture} from './suite.mjs';
import {compareTimingObservations} from './compare.mjs';
const [suitePath,reportPath,bindingPath,nativePath,outputPath]=process.argv.slice(2);
assert.ok(suitePath&&reportPath&&bindingPath&&nativePath&&outputPath,'suite.json.gz browser-report.json capture-verification.json native-report.json output.json required');
const suiteText=gunzipSync(fs.readFileSync(suitePath)).toString('utf8'),reportText=fs.readFileSync(reportPath,'utf8'),binding=JSON.parse(fs.readFileSync(bindingPath)),web=verifyCapture(suiteText,reportText,binding),native=JSON.parse(fs.readFileSync(nativePath));
const report=compareTimingObservations(web,native);
report.authoritativeBrowser=true;report.nativeBuildAuthenticated=false;report.identity=web.identity;
report.remaining[0]='Clean native exact-commit rebuild provenance and aggregate CI regression';
fs.writeFileSync(outputPath,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({cases:report.summary.cases,parityAccepted:report.parityAccepted,boundaryReadinessDivergences:report.summary.boundaryReadinessDivergences,previewDivergences:report.summary.previewDivergences}));
