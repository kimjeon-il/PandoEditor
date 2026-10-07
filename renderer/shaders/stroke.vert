#version 440
#extension GL_EXT_control_flow_attributes : require
layout(location=0) in vec4 segment;
layout(location=1) in vec2 corner;
layout(location=2) in float endpointWidth;
layout(location=3) in vec4 adjacent;
layout(location=4) in vec3 meta;
layout(location=0) out float frontness;
layout(location=1) out float along;
layout(location=2) out vec2 local;
layout(location=3) out vec2 incoming;
layout(location=4) out vec2 outgoing;
layout(location=5) out float halfWidth;
layout(location=6) out float kind;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0;vec4 flat1;vec4 globe0;vec4 globe1;vec4 color;vec4 effects;vec4 strokeOptions;
} u;
const float rad=0.01745329251994329577;
vec3 projectGeo(vec2 geo) {
    if(u.flat1.w<0.5)return vec3(u.flat0.x+(geo.x+u.flat1.z)*u.flat0.z-u.flat1.x*u.flat0.w,u.flat0.y+(u.flat1.y-geo.y)*u.flat0.w,1.0);
    float lon=geo.x*rad-u.globe0.x,lat=geo.y*rad;
    float h=cos(lat)*sin(lon),v=sin(lat)*cos(u.globe0.y)-cos(lat)*sin(u.globe0.y)*cos(lon);
    float f=sin(lat)*sin(u.globe0.y)+cos(lat)*cos(u.globe0.y)*cos(lon),roll=u.globe0.z;
    return vec3(u.globe1.x+u.globe0.w*(cos(roll)*h-sin(roll)*v),u.globe1.y-u.globe0.w*(sin(roll)*h+cos(roll)*v),f);
}
// Horizon search needs only depth. Keeping the full projection (including roll
// and screen coordinates) in this loop overflows the D3D11 compiler stack.
float globeDepth(vec2 geo) {
    float lon=geo.x*rad-u.globe0.x,lat=geo.y*rad;
    return sin(lat)*sin(u.globe0.y)+cos(lat)*cos(u.globe0.y)*cos(lon);
}
vec2 direction(vec2 d){float n=length(d);return n>0.0001?d/n:vec2(1.0,0.0);}
vec2 normal(vec2 d){return vec2(-d.y,d.x);}
bool flag(float f,float b){return mod(floor(f/b),2.0)>0.5;}
// Literal pinned-Web miter predicate/limit. Qt applies DPR exactly once.
vec2 miter(vec2 a,vec2 b,vec2 fallback,float side,float width) {
    vec2 n=normal(direction(a+b));float denominator=dot(n,fallback);
    if(abs(denominator)<0.08)return side*fallback*width;
    float scale=width/denominator;
    if(abs(scale)>width*u.strokeOptions.w)return side*fallback*width;
    return side*n*scale;
}
void main() {
    kind=meta.z;halfWidth=max(0.1,endpointWidth+u.effects.x)*0.5;
    float outer=halfWidth+u.strokeOptions.z;vec2 screen;
    vec2 sourceA=segment.xy,sourceB=segment.zw;
    vec3 a=projectGeo(sourceA),b=projectGeo(sourceB);
    // Keep the loop outside the node/body branch: the D3D11 optimizer expands
    // its loop-carried values recursively when nested beneath that branch.
    if(u.flat1.w>0.5&&a.z*b.z<0.0) {
        vec2 visible=a.z>0.0?sourceA:sourceB,hidden=a.z>0.0?sourceB:sourceA;
        [[dont_unroll]] for(int i=0;i<16;++i){vec2 mid=(visible+hidden)*0.5;if(globeDepth(mid)>=0.0)visible=mid;else hidden=mid;}
        if(a.z<0.0)a=projectGeo(visible);else b=projectGeo(visible);
    }
    if(kind>0.5) {
        vec3 p=projectGeo(segment.xy),previous=projectGeo(adjacent.xy),next=projectGeo(adjacent.zw);
        local=corner*outer;incoming=direction(p.xy-previous.xy);outgoing=direction(next.xy-p.xy);
        screen=p.xy+local;frontness=p.z;along=0.0;
    }else {
        vec2 d=direction(b.xy-a.xy),n=normal(d),offset=corner.x*n*outer;
        if(u.strokeOptions.x>0.5) {
            if(corner.y<0.5&&flag(meta.y,1.0))offset=miter(direction(a.xy-projectGeo(adjacent.xy).xy),d,n,corner.x,outer);
            if(corner.y>=0.5&&flag(meta.y,2.0))offset=miter(d,direction(projectGeo(adjacent.zw).xy-b.xy),n,corner.x,outer);
        }
        screen=mix(a.xy,b.xy,corner.y)+offset;frontness=min(a.z,b.z);
        local=vec2(corner.x*outer,0.0);incoming=d;outgoing=d;
        float phaseScale=u.flat1.w<0.5?u.flat0.w:u.globe0.w*rad;
        along=meta.x*phaseScale+corner.y*length(b.xy-a.xy);
    }
    gl_Position=u.qt_Matrix*vec4(screen,0.0,1.0);
}
