// Node discovery only. This command cannot produce an authoritative browser artifact.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import {createSourceOrderSuite,runSourceOrderCase,sourceOrderObservationLimits,verifySourceOrderSuite} from './source-order-v2.mjs';
import {loadNodeSources} from './sources.mjs';
export async function runSourceOrderDiscovery(options){
 const suite=await createSourceOrderSuite(options);verifySourceOrderSuite(suite);const loaded=await loadNodeSources();
 try{const cases=[];for(const definition of suite.cases)cases.push(await runSourceOrderCase(loaded,definition));
  return {schema:'pando-m974-node-source-order-discovery',version:2,state:'complete',authoritativeBrowserObservation:false,identity:suite.identity,runtime:{node:process.version,v8:process.versions.v8},cases,observationLimits:sourceOrderObservationLimits};
 }finally{loaded.cleanup();}
}
async function main(){
 const root=fileURLToPath(new URL('../../',import.meta.url)),commit=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
 const report=await runSourceOrderDiscovery({commit,runId:'node-discovery-only'}),text=JSON.stringify(report,null,2)+'\n';
 if(process.argv[2]){fs.mkdirSync(path.dirname(path.resolve(process.argv[2])),{recursive:true});fs.writeFileSync(process.argv[2],text);}else process.stdout.write(text);
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error(error);process.exitCode=1;});
