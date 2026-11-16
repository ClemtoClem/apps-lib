#version 450
#extension GL_GOOGLE_include_directive : require
// Fond : ciel analytique avec disque solaire.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform Sky {
    vec4 right;   // axe droit × tan(fov/2) × aspect
    vec4 up;      // axe haut × tan(fov/2)
    vec4 forward; // axe de visée
};

#include "lights.glsl"

void main() {
    vec3 dir = normalize(forward.xyz + right.xyz * vNdc.x + up.xyz * vNdc.y);
    vec3 c = SkyColor(dir, true);
    if (fogColor.w > 0.0)
        c = mix(c, fogColor.rgb, clamp(1.0 - dir.y * 4.0, 0.0, 1.0));
    outColor = vec4(ToneMap(c), 1.0);
}
