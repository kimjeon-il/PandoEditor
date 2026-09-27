#version 440
layout(location=0) in float frontness;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0; vec4 flat1; vec4 globe0; vec4 globe1; vec4 color; vec4 effects;
} u;
void main() {
    if(frontness<0.0) discard;
    fragColor=u.color;
}
