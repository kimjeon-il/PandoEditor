#version 440
layout(location=0) in vec2 geographic;
layout(location=0) out float frontness;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    vec4 flat0; vec4 flat1; vec4 globe0; vec4 globe1; vec4 color; vec4 effects;
} u;
const float rad=0.01745329251994329577;
void main() {
    vec2 screen;
    frontness=1.0;
    if(u.flat1.w<0.5) {
        screen=vec2(u.flat0.x+(geographic.x+u.flat1.z)*u.flat0.z-u.flat1.x*u.flat0.w,
                    u.flat0.y+(u.flat1.y-geographic.y)*u.flat0.w);
    } else {
        float lon=geographic.x*rad-u.globe0.x, lat=geographic.y*rad;
        float h=cos(lat)*sin(lon);
        float v=sin(lat)*cos(u.globe0.y)-cos(lat)*sin(u.globe0.y)*cos(lon);
        frontness=sin(lat)*sin(u.globe0.y)+cos(lat)*cos(u.globe0.y)*cos(lon);
        float roll=u.globe0.z;
        screen=vec2(u.globe1.x+u.globe0.w*(cos(roll)*h-sin(roll)*v),
                    u.globe1.y-u.globe0.w*(sin(roll)*h+cos(roll)*v));
    }
    gl_Position=u.qt_Matrix*vec4(screen,0.0,1.0);
}
