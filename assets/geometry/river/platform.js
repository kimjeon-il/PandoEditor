// Only application-owned plain-data adapters. No geometry operations occur here.
// globalThis is installed as QJSEngine::globalObject() by the host.
(function () {
  'use strict';
  function ownDataMerge(target) {
    for (let i = 1; i < arguments.length; ++i) {
      const source = arguments[i];
      if (source == null) continue;
      const boxed = Object(source);
      const keys = Object.getOwnPropertyNames(boxed).filter(function (key) {
        return Object.getOwnPropertyDescriptor(boxed, key).enumerable;
      });
      if (typeof Object.getOwnPropertySymbols === 'function') {
        for (const key of Object.getOwnPropertySymbols(boxed)) {
          if (Object.prototype.propertyIsEnumerable.call(boxed, key)) keys.push(key);
        }
      }
      for (const key of keys) Object.defineProperty(target, key, {
        value: boxed[key], writable: true, enumerable: true, configurable: true,
      });
    }
    return target;
  }
  function plainClone(value, ancestors) {
    if (value === null || typeof value === 'string' || typeof value === 'boolean') return value;
    if (typeof value === 'number' && Number.isFinite(value)) return value;
    if (typeof value !== 'object') throw new TypeError('RIVER_CLONE_REQUIRES_JSON_DATA');
    if (ancestors.indexOf(value) !== -1) throw new TypeError('RIVER_CLONE_REQUIRES_ACYCLIC_DATA');
    const prototype = Object.getPrototypeOf(value);
    if (!Array.isArray(value) && prototype !== Object.prototype && prototype !== null) {
      throw new TypeError('RIVER_CLONE_REQUIRES_PLAIN_DATA');
    }
    ancestors.push(value);
    const result = Array.isArray(value) ? new Array(value.length) : {};
    for (const key of Object.keys(value)) Object.defineProperty(result, key, {
      value: plainClone(value[key], ancestors), writable: true, enumerable: true, configurable: true,
    });
    ancestors.pop();
    return result;
  }
  function flatMap(callback, thisArg) {
    if (this == null) throw new TypeError('flatMap called on null or undefined');
    const source = Object(this);
    const length = Math.max(0, Math.min(9007199254740991, Math.floor(Number(source.length)) || 0));
    if (typeof callback !== 'function') throw new TypeError('flatMap callback is not callable');
    const output = [];
    for (let i = 0; i < length; ++i) {
      if (!(i in source)) continue;
      const mapped = callback.call(thisArg, source[i], i, source);
      if (Array.isArray(mapped)) {
        const mappedLength = mapped.length;
        for (let j = 0; j < mappedLength; ++j) if (j in mapped) output.push(mapped[j]);
      } else output.push(mapped);
    }
    return output;
  }
  Object.defineProperty(globalThis, '__riverOwnDataMerge', { value: ownDataMerge });
  Object.defineProperty(globalThis, 'structuredClone', { value: function (value) { return plainClone(value, []); } });
  Object.defineProperty(Array.prototype, 'flatMap', { value: flatMap, writable: true, configurable: true });
})();
