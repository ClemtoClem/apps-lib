#version 450
// Sommet compact de chunk (12 octets) : position locale, face, uv, AO, lumière, couche.
layout(location = 0) in uvec4 inPosFace;   // UBYTE4 : x, y, z, face
layout(location = 1) in uvec4 inUvAoLight; // UBYTE4 : u, v, ao, light
layout(location = 2) in uvec2 inLayer;     // USHORT2 : couche de texture, réflectance (0..255)

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 cameraPos;  // xyz
    vec4 fog;        // x = début, y = fin
};
layout(set = 1, binding = 1) uniform ChunkData {
    vec4 origin;     // origine monde du chunk
};

layout(location = 0) out vec3 vUv;
layout(location = 1) out float vShade;
layout(location = 2) out float vFog;
layout(location = 3) out float vLight;
layout(location = 4) out vec3 vWorld;   // éclairage dynamique (lumières ponctuelles)
layout(location = 5) out vec3 vNormal;  // normale de la face (réflectance)
layout(location = 6) out float vReflect; // réflectance du matériau (0..1)

const float kFaceShade[7] = float[](0.82, 0.82, 1.0, 0.55, 0.92, 0.92, 0.9);
const vec3 kFaceNormal[7] = vec3[](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0),
    vec3(0.0, 1.0, 0.0) // 6 = végétation en croix
);

void main() {
    vec3 world = origin.xyz + vec3(inPosFace.xyz);
    gl_Position = viewProj * vec4(world, 1.0);
    vUv = vec3(float(inUvAoLight.x), float(inUvAoLight.y), float(inLayer.x));
    float ao = float(inUvAoLight.z) / 3.0;
    vShade = kFaceShade[min(inPosFace.w, 6u)] * (0.42 + 0.58 * ao);
    vLight = float(inUvAoLight.w) / 15.0;
    vWorld = world;
    vNormal = kFaceNormal[min(inPosFace.w, 6u)];
    vReflect = float(inLayer.y) / 255.0;
    float d = length(world - cameraPos.xyz);
    vFog = clamp((d - fog.x) / max(fog.y - fog.x, 1.0), 0.0, 1.0);
}
