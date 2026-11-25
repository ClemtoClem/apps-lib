#version 450
// Cube texturé : position, normale, UV ; matrices en uniformes poussés.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(set = 1, binding = 0) uniform Matrices {
    mat4 mvp;
    mat4 model;
    vec4 uvScale;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;

void main() {
    vNormal = mat3(model) * inNormal;
    vUv = inUv * uvScale.xy;
    gl_Position = mvp * vec4(inPosition, 1.0);
}
