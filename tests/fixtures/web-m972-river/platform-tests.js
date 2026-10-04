(function () {
  'use strict';
  const tests = {};
  function check(name, condition) { if (!condition) throw new Error(name + ' ' + JSON.stringify(tests)); tests[name] = true; }
  const inherited = { inherited: 9 };
  const source = Object.create(inherited);
  source.b = 2; source['2'] = 'numeric'; source.a = 1;
  Object.defineProperty(source, 'hidden', { value: 'no', enumerable: false });
  Object.defineProperty(source, '__proto__', { value: { unexpected: true }, enumerable: true });
  let getterCalls = 0;
  Object.defineProperty(source, 'getter', { enumerable: true, get: function () { getterCalls++; return 7; } });
  const symbol = Symbol('test'); source[symbol] = 11;
  const merged = __riverOwnDataMerge({}, source, { a: 10 });
  check('spread-own-enumerable-only', !('inherited' in merged) && !('hidden' in merged));
  check('spread-property-order', JSON.stringify(Object.keys(merged)) === JSON.stringify(['2', 'b', 'a', '__proto__', 'getter']));
  check('spread-proto-is-own-data', Object.getPrototypeOf(merged) === Object.prototype && Object.prototype.hasOwnProperty.call(merged, '__proto__') && merged.__proto__.unexpected);
  check('spread-later-value-overrides', merged.a === 10);
  check('spread-getter-read-once', getterCalls === 1 && Object.getOwnPropertyDescriptor(merged, 'getter').value === 7);
  check('spread-enumerable-symbols', merged[symbol] === 11);
  check('spread-null-undefined', Object.keys(__riverOwnDataMerge({}, null, undefined)).length === 0);
  const seen = [];
  const sparse = [1, , 3];
  const mapped = sparse.flatMap(function (x, i, array) {
    seen.push([i, this.multiplier, array === sparse]);
    if (i === 0) array.push(9);
    return [x * this.multiplier, , i];
  }, { multiplier: 2 });
  check('flatMap-holes-order-thisArg', JSON.stringify(mapped) === '[2,0,6,2]' && JSON.stringify(seen) === '[[0,2,true],[2,2,true]]');
  check('flatMap-one-level', JSON.stringify([1].flatMap(() => [[2], 3])) === '[[2],3]');
  check('flatMap-scalars', JSON.stringify([1, 2].flatMap(x => x)) === '[1,2]');
  check('flatMap-nonenumerable', !Object.prototype.propertyIsEnumerable.call(Array.prototype, 'flatMap'));
  const original = JSON.parse('{"coordinates":[[1,-0.3],[0,2]],"empty":null,"label":"river🌊","__proto__":{"safe":true}}');
  const copy = structuredClone(original);
  check('clone-json-equality', JSON.stringify(copy) === JSON.stringify(original));
  copy.coordinates[0][0] = 8;
  check('clone-independence', original.coordinates[0][0] === 1);
  check('clone-proto-is-own-data', Object.getPrototypeOf(copy) === Object.prototype && Object.prototype.hasOwnProperty.call(copy, '__proto__'));
  check('clone-signed-zero', 1 / structuredClone([-0])[0] === -Infinity);
  for (const [label, value] of [['date', new Date()], ['map', new Map()], ['undefined', undefined], ['nonfinite', Infinity]]) {
    let rejected = false; try { structuredClone(value); } catch (error) { rejected = error instanceof TypeError; }
    check('clone-rejects-' + label, rejected);
  }
  const cyclic = {}; cyclic.self = cyclic;
  let rejected = false; try { structuredClone(cyclic); } catch (error) { rejected = error instanceof TypeError; }
  check('clone-rejects-cycle', rejected);
  check('js-json-number-format', JSON.stringify([1e-7,1e-6,1e20,1e21,-0]) === '[1e-7,0.000001,100000000000000000000,1e+21,0]');
  check('globalThis-identity', globalThis.globalThis === globalThis);
  check('performance-fallback', typeof globalThis.performance === 'undefined' && Number.isFinite(globalThis.performance?.now?.() ?? Date.now()));
  check('localeCompare-ascii-hash-order', ['square:river-cell:aa000000','square:river-cell:2a000000','square:river-cell:09a00000'].sort((a,b)=>a.localeCompare(b)).join('|') === 'square:river-cell:09a00000|square:river-cell:2a000000|square:river-cell:aa000000');
  return tests;
})();
