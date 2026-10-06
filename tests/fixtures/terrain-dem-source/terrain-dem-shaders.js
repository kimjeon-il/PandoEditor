const common = (sample) => `
  float elevation(vec4 encoded) {
    return floor(encoded.r * 255.0 + 0.5) * 256.0 + floor(encoded.g * 255.0 + 0.5) - 12000.0;
  }
  vec4 demSample(vec2 pixel) {
    return ${sample}(uTerrain, (pixel + vec2(0.5)) / uTextureSize);
  }
  float derivedShade(vec2 uv) {
    vec2 point = uv * uTextureSize - vec2(0.5);
    vec2 cell = floor(point);
    vec2 fraction = fract(point);
    float h00 = elevation(demSample(cell));
    float h10 = elevation(demSample(cell + vec2(1.0, 0.0)));
    float h01 = elevation(demSample(cell + vec2(0.0, 1.0)));
    float h11 = elevation(demSample(cell + vec2(1.0, 1.0)));
    // Central differences of decoded, bilinear heights at point +/- 0.5
    // texel. The four original corners plus one outer column and row share
    // all eight samples; the existing one-pixel gutter covers the footprint.
    vec2 side = step(vec2(0.5), fraction);
    vec2 outerOffset = mix(vec2(-1.0), vec2(2.0), side);
    float hx0 = elevation(demSample(cell + vec2(outerOffset.x, 0.0)));
    float hx1 = elevation(demSample(cell + vec2(outerOffset.x, 1.0)));
    float hy0 = elevation(demSample(cell + vec2(0.0, outerOffset.y)));
    float hy1 = elevation(demSample(cell + vec2(1.0, outerOffset.y)));
    vec2 centeredFraction = fract(fraction + vec2(0.5));
    vec2 xPrevious = mix(vec2(hx0, hx1), vec2(h00, h01), side.x);
    vec2 xMiddle = mix(vec2(h00, h01), vec2(h10, h11), side.x);
    vec2 xNext = mix(vec2(h10, h11), vec2(hx0, hx1), side.x);
    vec2 eastDifferences = mix(xMiddle - xPrevious, xNext - xMiddle, centeredFraction.x);
    vec2 yPrevious = mix(vec2(hy0, hy1), vec2(h00, h10), side.y);
    vec2 yMiddle = mix(vec2(h00, h10), vec2(h01, h11), side.y);
    vec2 yNext = mix(vec2(h01, h11), vec2(hy0, hy1), side.y);
    vec2 southDifferences = mix(yMiddle - yPrevious, yNext - yMiddle, centeredFraction.y);
    float eastMeters = 40030228.884 * max(0.0001, cos(radians(vLonLat.y))) / uLevelSize.x;
    float northMeters = 20015114.442 / uLevelSize.y;
    float riseEast = mix(eastDifferences.x, eastDifferences.y, fraction.y) / eastMeters;
    float riseNorth = -mix(southDifferences.x, southDifferences.y, fraction.x) / northMeters;
    vec3 normal = normalize(vec3(-riseEast, -riseNorth, 1.0));
    vec3 light = vec3(-0.5, 0.5, 0.70710678);
    return 0.42 + 0.58 * max(0.0, dot(normal, light));
  }
  vec3 seaColor(float heightMeters) {
    float depth = pow(clamp(-heightMeters / 8000.0, 0.0, 1.0), 0.6);
    return mix(vec3(0.42, 0.66, 0.82), vec3(0.10, 0.24, 0.39), depth);
  }
  vec3 terrainColor() {
    vec4 encodedPixel = ${sample}(uTerrain, vUv);
    float shade = encodedPixel.b;
    if (uShadeBlend > 0.001 && abs(vLonLat.y) < 89.5) {
      shade = mix(shade, derivedShade(vUv), uShadeBlend);
    }
    if (uPhysicalStyle < 0.5) return vec3(shade);
    vec3 baseColor = uLandPass > 0.5
      ? ${sample}(uTint, vec2((vLonLat.x + 180.0) / 360.0, (90.0 - vLonLat.y) / 180.0)).rgb
      : seaColor(elevation(encodedPixel));
    float flatSurfaceShade = 0.42 + 0.58 * 0.70710678;
    return baseColor * clamp(shade / flatSurfaceShade, 0.5, 1.2);
  }
`;

export function terrainDemFragmentSource(glVersion) {
  const webGl2 = glVersion === 2;
  // The tint sampler is referenced explicitly below; height data always comes
  // from uTerrain. At texel centers RGBA is exact even with LINEAR filtering.
  const source = `
    precision highp float;
    precision ${webGl2 ? 'highp' : 'mediump'} int;
    ${webGl2 ? 'in' : 'varying'} vec2 vUv;
    ${webGl2 ? 'in' : 'varying'} vec2 vLonLat;
    ${webGl2 ? 'in' : 'varying'} float vDepth;
    uniform sampler2D uTerrain;
    uniform sampler2D uTint;
    uniform vec2 uTextureSize;
    uniform vec2 uLevelSize;
    uniform int uMode;
    uniform float uPhysicalStyle;
    uniform float uLandPass;
    uniform float uShadeBlend;
    uniform float uDarkTheme;
    ${webGl2 ? 'out vec4 outColor;' : ''}
    ${common(webGl2 ? 'texture' : 'texture2D')}
    void main() {
      if (uMode == 0 && vDepth < 0.0) discard;
      vec3 color = terrainColor();
      color = mix(color, color * vec3(0.60, 0.68, 0.76), uDarkTheme * 0.48);
      ${webGl2 ? 'outColor' : 'gl_FragColor'} = vec4(color, 1.0);
    }
  `;
  return webGl2 ? `#version 300 es\n${source}` : source;
}
