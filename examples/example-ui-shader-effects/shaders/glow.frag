#version 450
#extension GL_GOOGLE_include_directive : require
// Halo : la couverture voisine (deux anneaux de 16 échantillons) éclaire le pourtour.
#include "effect.glsl"

void main() {
    vec4 c = Sample(vUv);
    float cover = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 d = vec2(cos(float(i) * PI / 8.0), sin(float(i) * PI / 8.0));
        cover += Sample(vUv + d * 7.0 * params.zw).a + Sample(vUv + d * 15.0 * params.zw).a;
    }
    cover /= 32.0;
    float pulse = 0.75 + 0.25 * sin(params.x * 3.0);
    float g = cover * params.y * pulse * (1.0 - c.a);
    vec3 tint = vec3(0.35, 0.7, 1.0);
    outColor = (c + vec4(tint * g, g)) * vColor;
}
