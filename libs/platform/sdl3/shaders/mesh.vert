#version 450
// Maillages instanciés : sommet (position, normale, UV) + instance (matrice monde, teinte).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inModel0;
layout(location = 4) in vec4 inModel1;
layout(location = 5) in vec4 inModel2;
layout(location = 6) in vec4 inModel3;
layout(location = 7) in vec4 inTint;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 lightViewProj;
};

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vTint;
layout(location = 4) out vec4 vLight;

void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 world = model * vec4(inPosition, 1.0);
    vWorld = world.xyz;
    vNormal = transpose(inverse(mat3(model))) * inNormal; // échelles non uniformes comprises
    vUv = inUv;
    vTint = inTint;
    vLight = lightViewProj * world;
    gl_Position = viewProj * world;
}
