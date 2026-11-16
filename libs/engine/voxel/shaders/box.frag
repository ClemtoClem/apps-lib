#version 450
layout(location = 0) in vec4 vColor;
layout(location = 1) in float vFog;
layout(set = 3, binding = 0) uniform Params {
    vec4 fogColor;
    vec4 params;
};
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(mix(vColor.rgb, fogColor.rgb, vFog), vColor.a);
}
