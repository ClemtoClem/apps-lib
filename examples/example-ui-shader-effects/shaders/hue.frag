#version 450
#extension GL_GOOGLE_include_directive : require
// Teinte animée : rotation de la teinte dans l'espace YIQ (linéaire : reste prémultiplié).
#include "effect.glsl"

void main() {
    vec4 c = Sample(vUv);
    float a = params.x * 1.2 * params.y + Local().x * 2.0 * params.y;
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c.rgb;
    yiq.yz = mat2(cos(a), sin(a), -sin(a), cos(a)) * yiq.yz;
    outColor = vec4(clamp(toRgb * yiq, 0.0, c.a), c.a) * vColor;
}
