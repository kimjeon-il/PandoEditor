// Browser Worker platform adapter only. The worker application is imported unchanged.
import { parentPort, workerData } from 'node:worker_threads';
import { readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { runInThisContext } from 'node:vm';
import { verifyLifecycleSources } from './web-lifecycle.mjs';
const { root } = verifyLifecycleSources(workerData);
const workerUrl = pathToFileURL(resolve(root, 'assets/js/workers/map-edit-worker.js'));
globalThis.self = globalThis;
self.location = { href: workerUrl.href };
self.postMessage = message => parentPort.postMessage(message);
globalThis.importScripts = (...urls) => {
  for (const url of urls) {
    const path = fileURLToPath(url);
    runInThisContext(readFileSync(path, 'utf8'), { filename: path });
  }
};
await import(workerUrl.href);
parentPort.on('message', data => self.onmessage({ data }));
