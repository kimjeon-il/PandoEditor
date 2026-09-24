.pragma library
// Ported unchanged math from world-map@58e4087/custom-color-control.js.
// Presentation-only color editing. The caller owns committing the final HEX value.
const clamp = (value, max = 1) => Math.max(0, Math.min(max, value));
const wrapHue = value => ((value % 360) + 360) % 360;

function parseColorHex(value) {
  const hex = String(value).trim().replace(/^#/, '');
  if (/^[\da-f]{3}$/i.test(hex)) return `#${[...hex].map(c => c + c).join('')}`.toLowerCase();
  return /^[\da-f]{6}$/i.test(hex) ? `#${hex.toLowerCase()}` : null;
}

function rgbToHex(rgb) {
  return `#${rgb.map(channel => Math.round(clamp(channel, 255)).toString(16).padStart(2, '0')).join('')}`;
}

function hexToRgb(hex) {
  return [1, 3, 5].map(index => Number.parseInt(hex.slice(index, index + 2), 16));
}

function rgbToHsv(rgb, previousHue = 0) {
  const [r, g, b] = rgb.map(channel => channel / 255);
  const max = Math.max(r, g, b), min = Math.min(r, g, b), delta = max - min;
  const hue = !delta ? previousHue : max === r ? 60 * ((g - b) / delta)
    : max === g ? 60 * (2 + (b - r) / delta) : 60 * (4 + (r - g) / delta);
  return [wrapHue(hue), max ? delta / max : 0, max];
}

function hsvToRgb([h, s, v]) {
  const c = v * s, x = c * (1 - Math.abs((wrapHue(h) / 60) % 2 - 1)), m = v - c;
  const sector = Math.floor(wrapHue(h) / 60);
  return [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][sector]
    .map(channel => Math.round((channel + m) * 255));
}

function rgbToHsl(rgb) {
  const [h, s, v] = rgbToHsv(rgb);
  const l = v * (1 - s / 2);
  return [h, !l || l === 1 ? 0 : (v - l) / Math.min(l, 1 - l) * 100, l * 100];
}

function hslToRgb([h, s, l]) {
  s /= 100; l /= 100;
  const v = l + s * Math.min(l, 1 - l);
  return hsvToRgb([h, v ? 2 * (1 - l / v) : 0, v]);
}
