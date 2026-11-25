#version 450
// #extension GL_GOOGLE_include_directive : require
// Ondulation : déplacement sinusoïdal animé des coordonnées de texture.
#include "effect.glsl"

void main() {
    vec2 l = Local();
    vec2 offset = vec2(sin(l.y * 18.0 + params.x * 4.0) * 5.0, cos(l.x * 14.0 + params.x * 3.0) * 3.0) * params.y;
    outColor = Sample(vUv + offset * params.zw) * vColor;
}
