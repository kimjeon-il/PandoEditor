#version 440
layout(location=0) in float frontness;
layout(location=1) in float along;
layout(location=2) in vec2 local;
layout(location=3) in vec2 incoming;
layout(location=4) in vec2 outgoing;
layout(location=5) in float halfWidth;
layout(location=6) in float kind;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0;vec4 flat1;vec4 globe0;vec4 globe1;vec4 color;vec4 effects;vec4 strokeOptions;
} u;
float cross2(vec2 a,vec2 b){return a.x*b.y-a.y*b.x;}
void main() {
    if(kind<-.5||frontness<0.0)discard;
    bool dashed=u.effects.y>0.0&&u.effects.z>0.0;float edge;
    if(kind>0.5) {
        // Pinned Web omits all solid join/cap patches for dashed lines.
        if(dashed)discard;
        bool interior=kind<1.5;
        if(interior&&u.strokeOptions.x>0.5)discard;
        if(!interior&&u.strokeOptions.y<0.5)discard;
        if(interior) {
            bool insideA=dot(local,incoming)<=0.0&&abs(cross2(incoming,local))<=halfWidth;
            bool insideB=dot(local,outgoing)>=0.0&&abs(cross2(outgoing,local))<=halfWidth;
            if(insideA||insideB)discard;
        }else if(kind<2.5){if(dot(local,outgoing)>=0.0)discard;}
        else {if(dot(local,incoming)<=0.0)discard;}
        edge=halfWidth-length(local);
    }else {
        float period=u.effects.y+u.effects.z;
        if(dashed&&mod(along,max(1.0,period))>u.effects.y)discard;
        edge=halfWidth-abs(local.x);
    }
    float aa=u.strokeOptions.z;
    float coverage=aa<=0.0?step(0.0,edge):smoothstep(-aa,aa,edge);
    if(coverage<=0.001)discard;
    fragColor=vec4(u.color.rgb*(u.effects.w>0.5?coverage:1.0),u.color.a*coverage);
}
