#version 440
// DEM arithmetic copied literally from fixed Web 5649c307da24d0965d63bc8c00206aed9b9d3438.
layout(location=0) in vec2 vUv;
layout(location=1) in vec2 vLonLat;
layout(location=2) in float vDepth;
layout(location=3) in vec2 vMaskScreen;
layout(location=0) out vec4 outColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0; vec4 flat1; vec4 globe0; vec4 globe1;
    vec4 bounds; vec4 uvBounds; vec4 dimensions; vec4 options; vec4 effects;
    vec4 maskTransform; vec4 maskMetrics; vec4 maskState;
} u;
layout(binding=1) uniform sampler2D uTerrain;
layout(binding=2) uniform sampler2D uTint;
layout(binding=3) uniform sampler2D uLandMask;
#define uTextureSize u.dimensions.xy
#define uLevelSize u.dimensions.zw
#define uPhysicalStyle effectivePhysicalStyle
#define uDarkTheme u.options.y
#define uShadeBlend u.options.z
#define uLandPass effectiveLandPass
float effectivePhysicalStyle;
float effectiveLandPass;
  float elevation(vec4 encoded) {
    return floor(encoded.r * 255.0 + 0.5) * 256.0 + floor(encoded.g * 255.0 + 0.5) - 12000.0;
  }
  vec4 demSample(vec2 pixel) {
    return texture(uTerrain, (pixel + vec2(0.5)) / uTextureSize);
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
    vec4 encodedPixel = texture(uTerrain, vUv);
    float shade = encodedPixel.b;
    if (uShadeBlend > 0.001 && abs(vLonLat.y) < 89.5) {
      shade = mix(shade, derivedShade(vUv), uShadeBlend);
    }
    if (uPhysicalStyle < 0.5) return vec3(shade);
    vec3 baseColor = uLandPass > 0.5
      ? texture(uTint, vec2((vLonLat.x + 180.0) / 360.0, (90.0 - vLonLat.y) / 180.0)).rgb
      : seaColor(elevation(encodedPixel));
    float flatSurfaceShade = 0.42 + 0.58 * 0.70710678;
    return baseColor * clamp(shade / flatSurfaceShade, 0.5, 1.2);
  }
void main() {
    if(u.flat1.w>0.5 && vDepth<0.0)discard;
    // Geographic world-mask triangles can extend beyond this tile.
    if(any(lessThan(vUv,u.uvBounds.xy))||any(greaterThan(vUv,u.uvBounds.zw)))discard;
    vec3 color;
    effectivePhysicalStyle=u.options.x;
    effectiveLandPass=u.options.w;
    if(u.effects.x>0.5||u.options.x<0.5) {
        // Never draw with an absent, pending, or previous-view mask.
        if(u.maskState.x<0.5)discard;
        if(any(lessThan(vMaskScreen,vec2(0.0)))||any(greaterThan(vMaskScreen,vec2(1.0))))discard;
        vec2 maskUv=vMaskScreen*u.maskTransform.xy+u.maskTransform.zw;
        vec2 low=min(u.maskTransform.zw,u.maskTransform.xy+u.maskTransform.zw)+u.maskMetrics.zw*0.5;
        vec2 high=max(u.maskTransform.zw,u.maskTransform.xy+u.maskTransform.zw)-u.maskMetrics.zw*0.5;
        maskUv=clamp(maskUv,low,high);
        effectiveLandPass=step(0.5,texture(uLandMask,maskUv).a);
        // Ocean physical shading is independent of the optional DEM land tint.
        if(u.effects.x>0.5&&effectiveLandPass>0.5&&u.maskState.y<0.5)effectivePhysicalStyle=0.0;
        if(uPhysicalStyle<0.5&&effectiveLandPass<0.5)discard;
    }
    if(u.effects.x>0.5)color=terrainColor();
    // Raster upload is explicitly opaque RGBX, already selected from rawRGB
    // or Gray Earth A. Its data-alpha never enters Qt's blend/premultiply path.
    else color=texture(uTerrain,vUv).rgb;
    color=mix(color,color*vec3(0.60,0.68,0.76),uDarkTheme*0.48);
    // Qt's default scene-graph blend uses premultiplied output.
    outColor=vec4(color*u.effects.z,u.effects.z);
}
