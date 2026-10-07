import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
// esbuild is a development tool only. Production uses the generated bundle in Qt.
execFileSync(process.platform==='win32'?'npm.cmd':'npm',['exec','--yes','--package=esbuild@0.25.10','--','esbuild','app/reference-web/adapter.js','--bundle','--target=es2016','--format=iife','--global-name=ReferenceWeb','--outfile=app/reference-web/runtime.js'],{cwd:fileURLToPath(new URL('../../',import.meta.url)),stdio:'inherit',shell:process.platform==='win32'});
