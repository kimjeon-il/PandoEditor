import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
test('full CTest registers the M9.7 oracle tests when Node is available',()=>{
 const cmake=readFileSync(new URL('../../CMakeLists.txt',import.meta.url),'utf8');
 assert.match(cmake,/add_test\(NAME m971_editing_oracle_contract/);
 for(const name of ['contract','calculations','corpus','native-baseline','web-lifecycle'])assert.ok(cmake.includes(`/tools/m97/${name}.test.mjs`));
});
