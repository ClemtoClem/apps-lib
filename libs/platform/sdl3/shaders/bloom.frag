#version 450
// Bloom — passe descendante : filtre à 13 échantillons (Jimenez), seuil de luminosité et,
// au premier niveau, moyenne pondérée de Karis : un pixel isolé très brillant (bulle,
// particule) ne produit pas de tache carrée.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Params { vec4 p; }; // x = seuil (0 : niveaux suivants), yz = texel source

vec3 S(vec2 o) {
	vec3 c = texture(src, vUv + o * p.yz).rgb;
	return clamp(mix(c, vec3(0.0), vec3(isnan(c))), vec3(0.0), vec3(32000.0)); // source assainie
}
float KarisWeight(vec3 c) { return 1.0 / (1.0 + max(c.r, max(c.g, c.b))); }

void main() {
	vec3 a = S(vec2(-2, -2)), b = S(vec2(0, -2)), c = S(vec2(2, -2));
	vec3 d = S(vec2(-1, -1)), e = S(vec2(1, -1));
	vec3 f = S(vec2(-2, 0)), g = S(vec2(0, 0)), h = S(vec2(2, 0));
	vec3 i = S(vec2(-1, 1)), j = S(vec2(1, 1));
	vec3 k = S(vec2(-2, 2)), l = S(vec2(0, 2)), m = S(vec2(2, 2));
	vec3 g0 = (d + e + i + j) * 0.25, g1 = (a + b + f + g) * 0.25, g2 = (b + c + g + h) * 0.25, g3 = (f + g + k + l) * 0.25, g4 = (g + h + l + m) * 0.25;
	vec3 col;
	if (p.x > 0.0) {
		float w0 = KarisWeight(g0) * 0.5, w1 = KarisWeight(g1) * 0.125, w2 = KarisWeight(g2) * 0.125, w3 = KarisWeight(g3) * 0.125, w4 = KarisWeight(g4) * 0.125;
		col = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
		float lum = max(col.r, max(col.g, col.b));
		col *= smoothstep(p.x, p.x * 1.6 + 1e-4, lum);
	} else {
		col = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
	}
	outColor = vec4(col, 1.0);
}
