// Node discovery only. This command cannot produce an authoritative browser artifact.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import {createSourceHistorySuite,sourceHistoryObservationLimits,verifySourceHistorySuite} from './source-history-suite.mjs';
import {loadSourceHistoryNodeSources} from './source-history-sources.mjs';
import {runSourceHistoryCase} from './source-history-runtime.mjs';
import {sha256} from './sources.mjs';
export async function runSourceHistoryDiscovery(options){
 const suite=await createSourceHistorySuite(options);verifySourceHistorySuite(suite);const loaded=await loadSourceHistoryNodeSources();
 try{loaded.selectionRuntime=(0,eval)(suite.runtime.source).selectionRuntime;const cases=[];for(const definition of suite.cases)cases.push(await runSourceHistoryCase(loaded,definition));
  return {schema:'pando-m974-node-source-history-discovery',version:1,state:'complete',authoritativeBrowserObservation:false,identity:suite.identity,sourceHashes:Object.fromEntries(Object.entries(loaded.sourceTexts).map(([name,source])=>[name,sha256(source)])),runtime:{node:process.version,v8:process.versions.v8},cases,observationLimits:sourceHistoryObservationLimits};
 }finally{loaded.cleanup();}
}
async function main(){
 const root=fileURLToPath(new URL('../../',import.meta.url)),commit=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
 const report=await runSourceHistoryDiscovery({commit,runId:'node-discovery-only'}),text=JSON.stringify(report,null,2)+'\n';
 if(process.argv[2]){fs.mkdirSync(path.dirname(path.resolve(process.argv[2])),{recursive:true});fs.writeFileSync(process.argv[2],text);}else process.stdout.write(text);
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
