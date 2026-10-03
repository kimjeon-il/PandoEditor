import fs from 'node:fs';
import vm from 'node:vm';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { normalizeLayerPresentation } from '../tests/fixtures/web-current/source/layer-presentation.js';
const source=fs.readFileSync(new URL('../tests/fixtures/web-current/source/project-migrations.js.txt',import.meta.url),'utf8').replace(/\r\n/g,'\n');
const bytes=Buffer.from(source);
assert.equal(createHash('sha1').update('blob '+bytes.length+'\0').update(bytes).digest('hex'),'0a84b2849c201c176a5630e0cf785d5fc0e91c65');
const begin=source.indexOf('export function migrateProjectV5ToV6(');
const end=source.indexOf('\nexport const PROJECT_MIGRATIONS',begin);
assert.ok(begin>=0&&end>begin);
const migrate=vm.runInNewContext('('+source.slice(begin,end).replace('export function','function')+')',{clone:structuredClone,normalizeLayerPresentation,LEGACY_DISTRIBUTION_GROUPS:{language:'languages',ethnicity:'ethnicities',religion:'religions'},migrationError:message=>new Error(message)});
const results={};
for(const name of ['v3','v4','v5','scalars'])results[name]=JSON.parse(JSON.stringify(migrate(JSON.parse(fs.readFileSync(new URL('../tests/fixtures/web-import/'+name+'.expected.json',import.meta.url),'utf8')))));
if(process.argv.includes('--verify')) {
 for(const [name,expected] of Object.entries(results))assert.deepEqual(JSON.parse(fs.readFileSync(new URL('../tests/fixtures/web-current/'+name+'.expected.json',import.meta.url),'utf8')),expected);
 console.log('Current web v5 to v6 migration goldens verified');
} else console.log(JSON.stringify(results));
