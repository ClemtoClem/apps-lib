#version 450
// Triangle plein écran : 3 sommets sans tampon. vUv couvre [0..1] avec (0,0)
// en haut à gauche (origine des textures SDL_GPU), comme un sommet de maillage.
layout(location = 0) out vec2 vUv;
void main() {
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
	vUv = p * 0.5 + 0.5; // y = 0 en bas
	vUv.y = 1.0 - vUv.y; // y = 0 en haut
	gl_Position = vec4(p, 0.0, 1.0);
}
