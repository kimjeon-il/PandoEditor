// Local development evidence only. This report cannot satisfy the Chromium gate.
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
import {loadPendingNodeSources,sha256,pendingProjection} from './sources.mjs';
import {runPendingInputCase,verifyWebCase} from './runtime.mjs';
const loaded=await loadPendingNodeSources();
try{
 const cases=[];for(const definition of loaded.bundle.corpus.cases)cases.push(verifyWebCase(definition,await runPendingInputCase(loaded,definition,loaded.bundle.corpus),loaded.bundle.corpus,pendingProjection(loaded.bundle,definition)));
 const report={schema:'pando-m977-node-pending-input',version:1,gateAcceptance:false,applicationCommit:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),runtime:{node:process.version,v8:process.versions.v8},sourceManifestSha256:sha256(JSON.stringify(loaded.bundle.manifest)),addendumSha256:sha256(JSON.stringify(loaded.bundle.addendum)),corpusSha256:loaded.bundle.addendum.corpusSha256,cases};
 const text=JSON.stringify(report,null,2)+'\n';if(process.argv[2])fs.writeFileSync(process.argv[2],text);else process.stdout.write(text);
 console.error(JSON.stringify({state:'complete',cases:cases.length,stages:cases.reduce((n,c)=>n+Object.keys(c.stages).length,0),gateAcceptance:false,sha256:sha256(text)}));
}finally{loaded.cleanup();}
