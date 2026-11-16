#version 450
// Triangle : sommets 2D colorés, rotation et rapport d'aspect en uniformes.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(set = 1, binding = 0) uniform Params {
    vec4 rotation; // cos, sin, 1 / aspect, échelle
};

layout(location = 0) out vec3 vColor;

void main() {
    vec2 p = vec2(inPosition.x * rotation.x - inPosition.y * rotation.y, inPosition.x * rotation.y + inPosition.y * rotation.x);
    vColor = inColor;
    gl_Position = vec4(p.x * rotation.z * rotation.w, p.y * rotation.w, 0.0, 1.0);
}
