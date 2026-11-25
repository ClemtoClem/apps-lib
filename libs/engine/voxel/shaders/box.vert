#version 450
// Boîtes instanciées (entités, contour de sélection) : un cube unitaire [-1, 1]³
// par instance, mis à l'échelle par les demi-étendues.
layout(location = 0) in vec3 inCorner;   // sommet du cube unitaire
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inCenter;   // par instance
layout(location = 3) in vec3 inHalf;
layout(location = 4) in vec4 inColor;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 fog;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vFog;

void main() {
    vec3 world = inCenter + inCorner * inHalf;
    gl_Position = viewProj * vec4(world, 1.0);
    float shade = 0.6 + 0.4 * max(dot(inNormal, normalize(vec3(0.4, 1.0, 0.25))), 0.0);
    vColor = vec4(inColor.rgb * shade, inColor.a);
    float d = length(world - cameraPos.xyz);
    vFog = clamp((d - fog.x) / max(fog.y - fog.x, 1.0), 0.0, 1.0);
}
