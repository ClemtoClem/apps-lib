#version 450
layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(location = 2) in float vFog;
layout(location = 3) in float vDist;

layout(set = 2, binding = 0) uniform sampler2D skinTexture;
layout(set = 3, binding = 0) uniform Params {
    vec4 fogColor;
    vec4 params; // w = interval LOD (blocs)
};

layout(location = 0) out vec4 outColor;

void main() {
    float lod = floor(vDist / max(params.w, 1.0));
    vec4 c = textureLod(skinTexture, vUv, lod);
    if (c.a < 0.5)
        discard;
    vec3 rgb = c.rgb * vShade;
    rgb = mix(rgb, fogColor.rgb, vFog);
    outColor = vec4(rgb, c.a);
}
