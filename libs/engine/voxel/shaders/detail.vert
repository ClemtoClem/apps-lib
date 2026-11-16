#version 450
// Formes détaillées (nodebox, maillages .obj/.b3d) : position flottante locale au chunk,
// UV en unités bloc (texture répétée), couche, face (ombrage) et lumière émise.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in uvec2 inLayerFace; // x = couche, y = face | lumière << 8

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 fog;
};
layout(set = 1, binding = 1) uniform ChunkData {
    vec4 origin;
};

layout(location = 0) out vec3 vUv;
layout(location = 1) out float vShade;
layout(location = 2) out float vFog;
layout(location = 3) out float vLight;
layout(location = 4) out vec3 vWorld;
layout(location = 5) out vec3 vNormal;
layout(location = 6) out float vReflect;

const float kFaceShade[7] = float[](0.82, 0.82, 1.0, 0.55, 0.92, 0.92, 0.9);
const vec3 kFaceNormal[7] = vec3[](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0),
    vec3(0.0, 1.0, 0.0)
);

void main() {
    vec3 world = origin.xyz + inPos;
    gl_Position = viewProj * vec4(world, 1.0);
    vUv = vec3(inUv, float(inLayerFace.x));
    uint face = min(inLayerFace.y & 255u, 6u);
    vShade = kFaceShade[face];
    vLight = float(inLayerFace.y >> 8) / 15.0;
    vWorld = world;
    vNormal = kFaceNormal[face];
    vReflect = 0.0; // formes détaillées : pas de réflectance
    float d = length(world - cameraPos.xyz);
    vFog = clamp((d - fog.x) / max(fog.y - fog.x, 1.0), 0.0, 1.0);
}
