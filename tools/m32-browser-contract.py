#!/usr/bin/env python3
"""Run unchanged web controllers in Chromium DOM fixtures, not the deployed site."""
from pathlib import Path
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from functools import partial
from threading import Thread
from contextlib import contextmanager
import json, sys, shutil
from playwright.sync_api import sync_playwright
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'web-browser-evidence'
OUT.mkdir(parents=True, exist_ok=True)
class Quiet(SimpleHTTPRequestHandler):
    def log_message(self, *_): pass
server = ThreadingHTTPServer(('127.0.0.1', 0), partial(Quiet, directory=str(ROOT)))
thread = Thread(target=server.serve_forever, daemon=True); thread.start()
records=[]
try:
  with sync_playwright() as pw:
    browser=pw.chromium.launch(headless=True,executable_path=shutil.which('chromium'),args=['--no-sandbox'])
    for width in (1100,360):
      page=browser.new_page(viewport={'width':width,'height':760 if width==1100 else 640})
      page.goto(f'http://127.0.0.1:{server.server_port}/tests/fixtures/web-properties/')
      page.evaluate('''async () => {
        document.body.innerHTML='<main></main>';
        document.head.insertAdjacentHTML('beforeend','<style>body{font:14px sans-serif}main{max-width:340px}.hidden,[hidden]{display:none!important}.ui-custom-color-plane{display:block;width:300px;height:120px}.ui-custom-color-channels{display:flex}.ui-custom-color-channels input{width:65px}.ui-custom-color-formats,.ui-custom-color-actions{display:flex;gap:8px;margin:8px}.ui-custom-color-preview{display:inline-block;width:24px;height:24px}</style>');
        const {createCustomColorControl}=await import('./source/custom-color-control.js');
        window.applied=[];window.cancelled=0;
        window.control=createCustomColorControl({documentRef:document,onApply:v=>{applied.push(v)},onCancel:()=>{cancelled++;control.close()}});
        document.querySelector('main').appendChild(control.element);control.open('#336699');
      }''')
      hexfield=page.locator('.ui-custom-color-hex input')
      hexfield.fill('#xyz');assert page.locator('[data-custom-apply]').is_disabled()
      hexfield.fill('abc');assert page.evaluate('applied.length')==0
      page.locator('[data-custom-apply]').click();assert page.evaluate('applied')==['#aabbcc']
      page.evaluate("control.open('#336699')")
      page.locator('[data-custom-format="hsl"]').click()
      channels=page.locator('.ui-custom-color-channels input')
      channels.nth(0).fill('123');channels.nth(1).fill('45');channels.nth(2).fill('67')
      assert channels.all_text_contents()==['','','']
      assert [channels.nth(i).input_value() for i in range(3)]==['123','45','67']
      channels.nth(0).fill('361');assert page.locator('[data-custom-apply]').is_disabled()
      channels.nth(0).fill('123');assert not page.locator('[data-custom-apply]').is_disabled()
      page.locator('[data-custom-apply]').focus();page.keyboard.press('Tab')
      assert page.locator('[data-custom-cancel]').first.evaluate('(e)=>e===document.activeElement')
      hexfield.focus();page.keyboard.press('Escape');assert page.evaluate('cancelled')==1
      assert page.evaluate('applied.length')==1
      page.screenshot(path=str(OUT/f'web-custom-fixture-{width}.png'))
      # Actual field-binding module: unlisted dates must not become new features.
      bindings=page.evaluate('''async () => {
        const {createPropertyEditorBindings}=await import('./source/property-editor-bindings.js');
        const {createCountryPropertyController}=await import('./source/country-property-controller.js');
        document.body.innerHTML='<input id="countryName"><textarea id="countryNotes"></textarea><input id="subunitNameInput"><textarea id="subunitNotesInput"></textarea><input id="regionNameInput"><textarea id="regionNotesInput"></textarea><input id="regionValidFromInput"><input id="regionValidToInput"><input id="subunitValidFromInput"><div id="territorialTypeModal"><div class="confirm-modal-dim"></div></div>';
        const calls=[];const country=createCountryPropertyController({window,document,elements:{name:document.querySelector('#countryName'),notes:document.querySelector('#countryNotes')},commitField:(f,v)=>calls.push(['country',f,v])});country.bind();
        const fields=createPropertyEditorBindings({getElement:id=>document.getElementById(id),document,bindColorPickers(){},commitTerritorialUnitMeta:(f,v)=>calls.push(['territorial',f,v])});fields.bind();
        window.fieldCalls=calls;window.sourceBindings=fields;
      }''')
      for selector,value in [('#countryName','  Name  '),('#countryNotes','  raw\nnotes  '),('#subunitNameInput','  Subunit  '),('#subunitNotesInput','  raw sub  '),('#regionValidFromInput',' -0001 '),('#subunitValidFromInput','2000')]:
        page.locator(selector).fill(value);page.locator(selector).dispatch_event('change')
      calls=page.evaluate('fieldCalls')
      assert calls==[['country','name','Name'],['country','notes','  raw\nnotes  '],['territorial','name','Subunit'],['territorial','notes','  raw sub  '],['territorial','validFrom','-0001']],calls
      # Unchanged toolbar controller exposes direct-lock gating and mobile occlusion.
      result=page.evaluate('''async () => {
        const {createSelectionToolbarPresentation}=await import('./source/selection-toolbar-presentation.js');
        sourceBindings.dispose();document.body.innerHTML='<div id="selectionToolbar"><div data-selection-kind="country"><input id="countryNameInput"><button id="countryColorTrigger">color</button></div><div data-selection-notes="country"><textarea></textarea></div><button id="selectionToolbarEditBtn"></button><button id="selectionToolbarTypeBtn"></button></div>';
        const ref={domain:'territorial',type:'country',id:'A',key:'A'};let locked=true;
        const controller=createSelectionToolbarPresentation({window,document,getElement:id=>document.getElementById(id),getSelection:()=>({items:[ref],primaryKey:'A'}),getView:()=>({ref,name:'A'}),isMutationBlocked:()=>locked,getLayout:()=>innerWidth<800?'mobile':'wide'});
        controller.sync();const blocked={name:document.getElementById('countryNameInput').disabled,color:document.getElementById('countryColorTrigger').disabled,notes:document.querySelector('textarea').readOnly};
        locked=false;controller.sync();const unblocked=!document.getElementById('countryNameInput').disabled&&!document.querySelector('textarea').readOnly;
        const sheet=document.createElement('div');sheet.className='workspace-surface mobile-open';sheet.style='position:absolute;left:0;top:0;width:100vw;height:100vh';document.body.appendChild(sheet);
        controller.syncOcclusion();return {blocked,unblocked,covered:document.getElementById('selectionToolbar').inert};
      }''')
      assert result=={'blocked':{'name':True,'color':True,'notes':True},'unblocked':True,'covered':width==360},result
      records.append({'width':width,'customApplyCancelAndValidation':True,'hslInputStability':True,'customFocusTrap':True,'originalFieldBindings':calls,'toolbar':result})
      page.close()
    browser.close()
  (OUT/'browser-contract.json').write_text(json.dumps(records,ensure_ascii=False,indent=2))
  print('PASS: unchanged pinned web DOM controllers at 1100px and 360px. Fixture-only, not full website or Android.')
finally:
  server.shutdown();server.server_close();thread.join(timeout=3)
