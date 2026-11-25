#version 450
// Triangle plein écran (3 sommets, sans tampon) pour le post-traitement.
// L'axe V est inversé : l'espace de clip Vulkan a le Y vers le bas.
layout(location = 0) out vec2 vUv;

void main() {
    vec2 pos[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 p = pos[gl_VertexIndex];
    vUv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    gl_Position = vec4(p, 0.0, 1.0);
}
