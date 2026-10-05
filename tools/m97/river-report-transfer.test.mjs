import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {createHash,webcrypto} from 'node:crypto';
import {writeBoundedJsonArtifact,exportBrowserReport} from './river/browser/report-transfer.mjs';
const sha256=value=>createHash('sha256').update(value).digest('hex');
const metadata=text=>({characters:text.length,bytes:Buffer.byteLength(text),sha256:sha256(text)});
const chunks=text=>async({offset,maximum})=>{let end=Math.min(offset+maximum,text.length);if(end<text.length&&text.charCodeAt(end-1)>=0xd800&&text.charCodeAt(end-1)<=0xdbff)end--;return {offset,text:text.slice(offset,end),next:end};};

test('bounded report transfer preserves every JSON byte across Unicode chunk boundaries',async()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'river-transfer-'));
  try{
    const text=JSON.stringify({nested:[[1,-0,1.2345678901234567]],unicode:'abc😀한국어'.repeat(100),allFields:{yes:true,no:false,empty:null}}),file=path.join(dir,'report.json');let largest=0,calls=0;
    const receipt=await writeBoundedJsonArtifact(metadata(text),async request=>{calls++;const row=await chunks(text)(request);largest=Math.max(largest,row.text.length);return row;},file,{chunkCharacters:7});
    assert.equal(fs.readFileSync(file,'utf8'),text);assert.equal(receipt.sha256,sha256(text));assert.ok(calls>1);assert.ok(largest<=7);assert.equal(fs.existsSync(file+'.partial'),false);
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

test('truncated, corrupted, replayed, or oversized chunks never publish a complete report',async()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'river-transfer-invalid-')),text='{"value":"'+'x'.repeat(100)+'"}';
  try{
    for(const [label,read] of [
      ['truncated',async({offset})=>({offset,text:'',next:offset})],
      ['corrupted',async request=>{const row=await chunks(text)(request);return {...row,text:row.text.replace(/x/g,'y')};}],
      ['replayed',async()=>({offset:0,text:'x',next:1})],
      ['oversized',async({offset})=>({offset,text,next:offset+text.length})],
    ]){const file=path.join(dir,label+'.json');await assert.rejects(writeBoundedJsonArtifact(metadata(text),read,file,{chunkCharacters:8}));assert.equal(fs.existsSync(file),false);}
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

test('browser export sends bounded text only and persists complete evidence before parsing',async()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'river-browser-export-'));
  const report={schema:'test-report',identity:{commit:'test'},runtime:{userAgent:'browser'},controllerAnnex:[{name:'one',expectedTransferAreaKm2:7,transferAreaDiagnostic:{raw:{productKm2:7}}}],controllerLifecycle:[],observations:Array.from({length:1000},(_,i)=>({name:'row-'+i,nested:[[i,i/3],['😀',null]]}))};
  const context=vm.createContext({__riverReport:structuredClone(report),TextEncoder,crypto:webcrypto});let largest=0;
  const page={async evaluate(fn,arg){context.__argument=arg;const result=await vm.runInContext('('+fn.toString()+')(__argument)',context);largest=Math.max(largest,JSON.stringify(result)?.length||0);return structuredClone(result);}};
  try{
    const full=path.join(dir,'browser-report.json'),diagnostics=path.join(dir,'browser-area-diagnostics.json');
    const receipt=await exportBrowserReport(page,{reportFile:full,diagnosticsFile:diagnostics,runtime:{playwright:'pin'},chunkCharacters:128});
    assert.deepEqual(JSON.parse(fs.readFileSync(full)),{...report,runtime:{...report.runtime,playwright:'pin'}});
    assert.equal(JSON.parse(fs.readFileSync(diagnostics)).controllerAnnex[0].transferAreaDiagnostic.raw.productKm2,7);
    assert.equal(receipt.report.sha256,sha256(fs.readFileSync(full)));assert.ok(largest<4096,'Never pass the nested full report through evaluate');
    assert.equal(context.__riverReportExport,undefined);
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

test('64 MiB synthetic transfer completes under a 64 MiB Node heap without accumulating chunks',async()=>{
  const {spawnSync}=await import('node:child_process');
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'river-transfer-memory-'));
  try{
    const helper=new URL('./river/browser/report-transfer.mjs',import.meta.url).href;
    const script=`import fs from 'node:fs';import {createHash} from 'node:crypto';import {writeBoundedJsonArtifact} from ${JSON.stringify(helper)};
      const block='x'.repeat(1024*1024),bodyBytes=64*1024*1024,hash=createHash('sha256');hash.update('"');for(let i=0;i<64;i++)hash.update(block);hash.update('"');
      const metadata={characters:bodyBytes+2,bytes:bodyBytes+2,sha256:hash.digest('hex')};let peakHeap=0;
      const receipt=await writeBoundedJsonArtifact(metadata,async({offset,maximum})=>{const next=Math.min(offset+maximum,metadata.characters);let text='x'.repeat(next-offset);if(offset===0)text='"'+text.slice(1);if(next===metadata.characters)text=text.slice(0,-1)+'"';peakHeap=Math.max(peakHeap,process.memoryUsage().heapUsed);return {offset,text,next};},process.argv[1]);
      console.log(JSON.stringify({bytes:receipt.bytes,chunks:receipt.chunks,peakHeap,maxRssKiB:process.resourceUsage().maxRSS}));`;
    const child=spawnSync(process.execPath,['--max-old-space-size=64','--input-type=module','-e',script,path.join(dir,'synthetic.json')],{encoding:'utf8',timeout:60000});
    assert.equal(child.status,0,child.stderr);const measurement=JSON.parse(child.stdout);assert.equal(measurement.bytes,64*1024*1024+2);assert.ok(measurement.chunks>200);assert.ok(measurement.peakHeap<64*1024*1024);console.log('Synthetic transfer only: '+JSON.stringify(measurement));
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

test('failed full report preserves completed diagnostics and clears frozen browser export',async()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'river-export-failure-'));
  const report={identity:{commit:'test'},runtime:{},controllerAnnex:[],controllerLifecycle:[],payload:'original'.repeat(100)};
  const context=vm.createContext({__riverReport:report,TextEncoder,crypto:webcrypto});
  const page={async evaluate(fn,arg){context.__argument=arg;const result=await vm.runInContext('('+fn.toString()+')(__argument)',context);
    if(arg?.name==='report'&&result?.text)result.text=result.text.replace(/original/g,'modified');
    return structuredClone(result);}};
  try{
    const full=path.join(dir,'report.json'),diagnostics=path.join(dir,'diagnostics.json');
    await assert.rejects(exportBrowserReport(page,{reportFile:full,diagnosticsFile:diagnostics,runtime:{},chunkCharacters:128}),/digest/);
    assert.equal(fs.existsSync(full),false);
    assert.equal(JSON.parse(fs.readFileSync(diagnostics,'utf8')).schema,'river-browser-area-diagnostics-v1');
    assert.equal(context.__riverReportExport,undefined);
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});
