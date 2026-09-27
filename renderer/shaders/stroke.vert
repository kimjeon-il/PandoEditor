#version 440
layout(location=0) in vec4 segment;
layout(location=1) in vec2 sideEnd;
layout(location=0) out float frontness;
layout(location=1) out float along;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0; vec4 flat1; vec4 globe0; vec4 globe1; vec4 color; vec4 effects;
} u;
const float rad=0.01745329251994329577;
vec3 projectGeo(vec2 geo) {
    if(u.flat1.w<0.5)
        return vec3(u.flat0.x+(geo.x+u.flat1.z)*u.flat0.z-u.flat1.x*u.flat0.w,
                    u.flat0.y+(u.flat1.y-geo.y)*u.flat0.w,1.0);
    float lon=geo.x*rad-u.globe0.x,lat=geo.y*rad;
    float h=cos(lat)*sin(lon);
    float v=sin(lat)*cos(u.globe0.y)-cos(lat)*sin(u.globe0.y)*cos(lon);
    float f=sin(lat)*sin(u.globe0.y)+cos(lat)*cos(u.globe0.y)*cos(lon);
    float roll=u.globe0.z;
    return vec3(u.globe1.x+u.globe0.w*(cos(roll)*h-sin(roll)*v),
                u.globe1.y-u.globe0.w*(sin(roll)*h+cos(roll)*v),f);
}
void main() {
    vec2 sourceA=segment.xy,sourceB=segment.zw;
    vec3 a=projectGeo(sourceA),b=projectGeo(sourceB);
    // Clip the segment in geographic space against the visible hemisphere.
    if(u.flat1.w>0.5 && a.z*b.z<0.0) {
        vec2 visible=a.z>0.0?sourceA:sourceB;
        vec2 hidden=a.z>0.0?sourceB:sourceA;
        for(int i=0;i<16;++i) {
            vec2 mid=(visible+hidden)*0.5;
            if(projectGeo(mid).z>=0.0)visible=mid; else hidden=mid;
        }
        if(a.z<0.0)a=projectGeo(visible); else b=projectGeo(visible);
    }
    vec2 delta=b.xy-a.xy;
    float lengthPx=length(delta);
    vec2 normal=lengthPx>0.0001?vec2(-delta.y,delta.x)/lengthPx:vec2(0.0);
    vec2 screen=mix(a.xy,b.xy,sideEnd.y)+sideEnd.x*normal*u.effects.x*0.5;
    frontness=min(a.z,b.z);
    along=sideEnd.y*lengthPx;
    gl_Position=u.qt_Matrix*vec4(screen,0.0,1.0);
}
