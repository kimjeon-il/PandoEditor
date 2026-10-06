// Node-only diagnostic. Never an actual Chromium or clean native CI receipt.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import {loadSourceHistoryNodeSources} from '../m974-snap-boundary/source-history-sources.mjs';
import {sha256} from '../m974-snap-boundary/sources.mjs';
import {boundarySessionReplacementCases,runBoundarySessionReplacementCase,verifyBoundarySessionReplacementCase} from './runtime.mjs';
export async function runSessionReplacementDiscovery(){
 const loaded=await loadSourceHistoryNodeSources();
 try{const cases=[];for(const definition of boundarySessionReplacementCases())cases.push(verifyBoundarySessionReplacementCase(definition,await runBoundarySessionReplacementCase(loaded,definition)));
  return {schema:'pando-m977-boundary-session-replacement-node-diagnostic',version:1,authoritativeBrowserObservation:false,commit:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),runtime:{node:process.version,v8:process.versions.v8},sourceManifestSha256:sha256(JSON.stringify(loaded.manifest)),sourceHashes:Object.fromEntries(Object.entries(loaded.sourceTexts).map(([name,source])=>[name,sha256(source)])),cases};
 }finally{loaded.cleanup();}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const report=await runSessionReplacementDiscovery(),output=process.argv[2];if(output){fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'node-report.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(output,'cases.json'),JSON.stringify(boundarySessionReplacementCases(),null,2)+'\n');console.log(JSON.stringify({cases:report.cases.length,authoritativeBrowserObservation:false}));}else console.log(JSON.stringify(report,null,2));
}
