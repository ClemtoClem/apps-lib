#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D albedo;

void main() {
    vec3 n = normalize(vNormal);
    float light = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.8, 0.6))), 0.0);
    outColor = vec4(texture(albedo, vUv).rgb * light, 1.0);
}
