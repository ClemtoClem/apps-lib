#version 450
// Passe de profondeur vue du soleil (carte d'ombre).
layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec4 inModel0;
layout(location = 4) in vec4 inModel1;
layout(location = 5) in vec4 inModel2;
layout(location = 6) in vec4 inModel3;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 lightViewProj;
};

void main() {
    gl_Position = lightViewProj * (mat4(inModel0, inModel1, inModel2, inModel3) * vec4(inPosition, 1.0));
}
