import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';

export async function writeBoundedJsonArtifact(metadata,readChunk,destination,{chunkCharacters=256*1024}={}){
  assert.ok(Number.isSafeInteger(metadata?.characters)&&metadata.characters>0,'Report character count required');
  assert.ok(Number.isSafeInteger(metadata.bytes)&&metadata.bytes>0,'Report byte count required');
  assert.match(metadata.sha256,/^[0-9a-f]{64}$/,'Browser report digest required');
  assert.ok(Number.isSafeInteger(chunkCharacters)&&chunkCharacters>=2&&chunkCharacters<=1024*1024,'Bounded chunk size required');
  assert.equal(fs.existsSync(destination),false,'Do not confuse a prior artifact with this capture');
  const partial=destination+'.partial',fd=fs.openSync(partial,'wx'),hash=createHash('sha256');let offset=0,bytes=0,chunks=0,open=true;
  try{
    while(offset<metadata.characters){
      const chunk=await readChunk({offset,maximum:Math.min(chunkCharacters,metadata.characters-offset)});
      assert.equal(chunk?.offset,offset,'Ordered report chunk required');assert.equal(typeof chunk.text,'string');
      assert.ok(chunk.text.length>0&&chunk.text.length<=chunkCharacters,'Nonempty bounded report chunk required');
      assert.equal(chunk.next,offset+chunk.text.length,'Exact report chunk progress required');assert.ok(chunk.next<=metadata.characters,'Report chunk exceeds capture');
      const data=Buffer.from(chunk.text,'utf8');let written=0;
      while(written<data.length)written+=fs.writeSync(fd,data,written,data.length-written);
      hash.update(data);bytes+=data.length;offset=chunk.next;chunks++;
    }
    assert.equal(bytes,metadata.bytes,'Complete report byte count');const sha256=hash.digest('hex');assert.equal(sha256,metadata.sha256,'Complete report digest');
    fs.fsyncSync(fd);fs.closeSync(fd);open=false;fs.renameSync(partial,destination);
    return {characters:offset,bytes,sha256,chunks,chunkCharacters};
  }catch(error){if(open)fs.closeSync(fd);throw error;}
}

export async function exportBrowserReport(page,{reportFile,diagnosticsFile,runtime,chunkCharacters=256*1024,onPhase=()=>{}}){
  try{
    onPhase('browser-stringify');
    const metadata=await page.evaluate(async runtime=>{
      const report=globalThis.__riverReport;if(!report||typeof report!=='object')throw new Error('Missing completed browser report');
      report.runtime={...report.runtime,...runtime};
      const checkpoint=point=>point?{name:point.name,previewReady:point.previewReady,transferAreaKm2:point.transferAreaKm2,transferredGeometry:point.transferredGeometry,transferAreaDiagnostic:point.transferAreaDiagnostic}:null;
      const row=value=>({name:value.name,selection:value.selection,expectedTransferAreaKm2:value.expectedTransferAreaKm2,transferredGeometry:value.result?.transferredGeometry,transferAreaDiagnostic:value.transferAreaDiagnostic,
        initialCheckpoint:checkpoint(value.initialCheckpoint),lifecycleActions:value.lifecycleActions?.map(action=>({op:action.op,name:action.name,checkpoint:checkpoint(action.checkpoint)}))});
      globalThis.__riverReportExport={report:JSON.stringify(report),diagnostics:JSON.stringify({schema:'river-browser-area-diagnostics-v1',identity:report.identity,runtime:report.runtime,sourceHashes:report.sourceHashes,controllerAnnex:report.controllerAnnex.map(row),controllerLifecycle:report.controllerLifecycle.map(row)})};
      const result={};
      for(const [name,text] of Object.entries(globalThis.__riverReportExport)){
        const bytes=new TextEncoder().encode(text),hash=await crypto.subtle.digest('SHA-256',bytes);
        result[name]={characters:text.length,bytes:bytes.length,sha256:Array.from(new Uint8Array(hash),byte=>byte.toString(16).padStart(2,'0')).join('')};
      }
      return result;
    },runtime);
    const exported={};
    for(const [name,destination] of [['diagnostics',diagnosticsFile],['report',reportFile]]){
      onPhase('transfer-'+name,{...metadata[name],chunkCharacters});
      exported[name]=await writeBoundedJsonArtifact(metadata[name],request=>page.evaluate(({name,offset,maximum})=>{
        const text=globalThis.__riverReportExport[name];if(typeof text!=='string')throw new Error('Missing frozen browser JSON');
        let next=Math.min(offset+maximum,text.length);
        // UTF-16 slices must not split a valid surrogate pair before UTF-8 encoding.
        if(next<text.length&&text.charCodeAt(next-1)>=0xd800&&text.charCodeAt(next-1)<=0xdbff)next--;
        return {offset,text:text.slice(offset,next),next};
      },{name,...request}),destination,{chunkCharacters});
      onPhase('persisted-'+name,exported[name]);
    }
    return exported;
  }finally{
    await page.evaluate(()=>{delete globalThis.__riverReportExport;}).catch(()=>{});
  }
}
