#version 450
// Segments colorés (grilles, axes, repères), couleurs sRGB telles quelles.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 lightViewProj;
};

layout(location = 0) out vec4 vColor;

void main() {
    vColor = inColor;
    gl_Position = viewProj * vec4(inPosition, 1.0);
}
