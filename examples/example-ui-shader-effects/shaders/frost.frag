#version 450
#extension GL_GOOGLE_include_directive : require
// Verre dépoli : flou à 25 échantillons, légère teinte froide et grain.
#include "effect.glsl"

float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    vec4 sum = vec4(0.0);
    float radius = 4.0 * params.y;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            sum += Sample(vUv + vec2(x, y) * radius * params.zw);
    vec4 c = sum / 25.0;
    float grain = (Hash(floor(vUv / params.zw)) - 0.5) * 0.06 * params.y;
    vec3 tint = mix(vec3(1.0), vec3(0.85, 0.93, 1.05), params.y);
    outColor = vec4(clamp(c.rgb * tint + grain * c.a, 0.0, c.a), c.a) * vColor;
}
