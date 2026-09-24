export function parseGpuColor(value) {
  const match = /^#([0-9a-f]{6})$/i.exec(String(value || ''));
  if (!match) return [0, 0, 0];
  const packed = Number.parseInt(match[1], 16);
  return [(packed >> 16 & 255) / 255, (packed >> 8 & 255) / 255, (packed & 255) / 255];
}

export function applyGpuBlendMode(gl, mode = 'normal') {
  if (!gl) return false;
  gl.enable(gl.BLEND);
  if (mode === 'multiply') {
    gl.blendFuncSeparate(gl.DST_COLOR, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
  } else {
    gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
  }
  return true;
}

export function resetGpuNormalBlend(gl) {
  return applyGpuBlendMode(gl, 'normal');
}
