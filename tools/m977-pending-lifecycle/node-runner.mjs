// Diagnostic only. Cannot substitute for exact-commit Chromium evidence.
import fs from 'node:fs';
import {loadLifecycleSources,sha256} from './sources.mjs';
import {runPendingLifecycleCase} from './runtime.mjs';
import {verifyWebLifecycleCase} from './contract.mjs';
const loaded=await loadLifecycleSources();try{
 const cases=[];for(const def of loaded.bundle.corpus.cases)cases.push(verifyWebLifecycleCase(loaded.bundle,def,await runPendingLifecycleCase(loaded,def,loaded.bundle.corpus)));
 const report={schema:'pando-m977-node-pending-lifecycle',version:1,gateAcceptance:false,runtime:{node:process.version,v8:process.versions.v8},corpusSha256:loaded.bundle.corpusSha256,sourceHashes:Object.fromEntries(Object.entries(loaded.bundle.sources).map(([p,s])=>[p,sha256(s)])),cases};const text=JSON.stringify(report,null,2)+'\n';if(process.argv[2])fs.writeFileSync(process.argv[2],text);else process.stdout.write(text);
}finally{loaded.cleanup();}
