// Node discovery platform adapter; never authoritative Chromium observations.
import {parentPort,workerData} from 'node:worker_threads';
import {readFileSync} from 'node:fs';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
import {runInThisContext} from 'node:vm';
const workerUrl=pathToFileURL(resolve(workerData.root,'assets/js/workers/map-edit-worker.js'));
globalThis.self=globalThis;self.location={href:workerUrl.href};self.postMessage=message=>parentPort.postMessage(message);
globalThis.importScripts=(...urls)=>{for(const url of urls){const path=fileURLToPath(url);runInThisContext(readFileSync(path,'utf8'),{filename:path});}};
await import(workerUrl.href);parentPort.on('message',data=>self.onmessage({data}));
