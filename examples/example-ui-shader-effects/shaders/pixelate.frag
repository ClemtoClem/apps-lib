#version 450
// #extension GL_GOOGLE_include_directive : require
// Mosaïque : blocs de 1 à 12 pixels selon l'intensité.
#include "effect.glsl"

void main() {
    float block = mix(1.0, 8.0, params.y);
    vec2 px = vUv / params.zw;
    vec2 snapped = (floor(px / block) + 0.5) * block;
    outColor = Sample(snapped * params.zw) * vColor;
}
