// Qt 6.8.3 lacks Array.prototype.at. Only standard array-like index lookup is
// supplied here; no geometry, projection or arithmetic function is overridden.
(function () {
  'use strict';
  if (typeof Array.prototype.at === 'function') return;
  Object.defineProperty(Array.prototype, 'at', {
    configurable: true,
    writable: true,
    value: function at(index) {
      if (this === null || this === undefined) throw new TypeError('Array.at requires an object');
      const object = Object(this);
      const length = Math.max(0, Math.min(9007199254740991, Math.floor(+object.length) || 0));
      const number = +index;
      const relative = Number.isNaN(number) || number === 0 ? 0 : Math.trunc(number);
      const position = relative >= 0 ? relative : length + relative;
      if (position < 0 || position >= length) return undefined;
      return object[position];
    },
  });
})();
