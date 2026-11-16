#version 450
#extension GL_GOOGLE_include_directive : require
// Écran cathodique : courbure, aberration chromatique, lignes de balayage, vignettage.
#include "effect.glsl"

void main() {
    vec2 l = Local() * 2.0 - 1.0;
    l *= 1.0 + dot(l.yx, l.yx) * 0.09 * params.y;
    l = l * 0.5 + 0.5;
    if (any(lessThan(l, vec2(0.0))) || any(greaterThan(l, vec2(1.0)))) {
        outColor = vec4(0.0);
        return;
    }
    vec2 uv = ToLayer(l);
    float shift = params.z * 2.5 * params.y;
    vec4 c = Sample(uv);
    vec3 rgb = vec3(Sample(uv + vec2(shift, 0.0)).r, c.g, Sample(uv - vec2(shift, 0.0)).b);
    float pixelY = uv.y / params.w;
    float scan = 1.0 - params.y * 0.22 * (0.5 + 0.5 * sin(pixelY * PI * 0.666 + params.x * 4.0));
    vec2 edge = smoothstep(vec2(0.0), vec2(0.08), l) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - l);
    outColor = vec4(min(rgb, vec3(c.a)) * scan * mix(1.0, edge.x * edge.y, params.y), c.a) * vColor;
}
