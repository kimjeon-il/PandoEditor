#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 texCoord;
layout(location=0) out vec2 vUv;
layout(location=1) out vec2 vLonLat;
layout(location=2) out float vDepth;
layout(location=3) out vec2 vMaskScreen;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0; vec4 flat1; vec4 globe0; vec4 globe1;
    vec4 bounds; vec4 uvBounds; vec4 dimensions; vec4 options; vec4 effects;
    vec4 maskTransform; vec4 maskMetrics; vec4 maskState;
} u;
const float rad=0.01745329251994329577;
void main() {
    vec2 localUv=texCoord;
    vec2 geographic=mix(u.bounds.xy,u.bounds.zw,localUv);
    vec2 screen=position;
    if(u.effects.y>0.5) {
        geographic=position;
        localUv=(geographic-u.bounds.xy)/(u.bounds.zw-u.bounds.xy);
        // This projection is the same geographic route as fill.vert.
        if(u.flat1.w<0.5) {
            screen=vec2(u.flat0.x+(geographic.x+u.flat1.z)*u.flat0.z-u.flat1.x*u.flat0.w,
                        u.flat0.y+(u.flat1.y-geographic.y)*u.flat0.w);
        } else {
            float lon=geographic.x*rad-u.globe0.x,lat=geographic.y*rad;
            float h=cos(lat)*sin(lon);
            float v=sin(lat)*cos(u.globe0.y)-cos(lat)*sin(u.globe0.y)*cos(lon);
            float roll=u.globe0.z;
            screen=vec2(u.globe1.x+u.globe0.w*(cos(roll)*h-sin(roll)*v),
                        u.globe1.y-u.globe0.w*(sin(roll)*h+cos(roll)*v));
        }
    }
    float lon=geographic.x*rad-u.globe0.x,lat=geographic.y*rad;
    vDepth=u.flat1.w<0.5?1.0:sin(lat)*sin(u.globe0.y)+cos(lat)*cos(u.globe0.y)*cos(lon);
    vLonLat=geographic;
    // One constant world-copy offset per tile preserves interpolation and
    // keeps the original -180/+180 tint endpoints (no per-vertex wrap seam).
    vLonLat.x-=u.effects.w;
    vUv=mix(u.uvBounds.xy,u.uvBounds.zw,localUv);
    vMaskScreen=screen*u.maskMetrics.xy;
    gl_Position=u.qt_Matrix*vec4(screen,0.0,1.0);
}
