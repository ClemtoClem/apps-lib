#version 450
// Modèle texturé (joueur, skin 64×64) : sommets en coordonnées locales (origine aux
// pieds), transformés par une matrice de modèle (translation + rotation autour de Y).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv; // 0..1 dans la texture de skin

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 fog;
};
layout(set = 1, binding = 1) uniform ModelData {
    mat4 model;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vShade;
layout(location = 2) out float vFog;
layout(location = 3) out float vDist;

void main() {
    vec4 world = model * vec4(inPos, 1.0);
    gl_Position = viewProj * world;
    vUv = inUv;
    vShade = 0.65 + 0.35 * max(dot(inNormal, normalize(vec3(0.4, 1.0, 0.25))), 0.0);
    float d = length(world.xyz - cameraPos.xyz);
    vFog = clamp((d - fog.x) / max(fog.y - fog.x, 1.0), 0.0, 1.0);
    vDist = d;
}
