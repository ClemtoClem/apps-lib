#version 450
// Bloom — passe montante : filtre en tente 3×3 (pas de blocs carrés à l'agrandissement),
// combinée additivement par l'état de mélange du pipeline. Texel nul : simple copie.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Params { vec4 p; }; // xy = texel source

void main() {
	vec2 t = p.xy;
	vec3 c = texture(src, vUv).rgb * 4.0;
	c += (texture(src, vUv + vec2(-t.x, 0)).rgb + texture(src, vUv + vec2(t.x, 0)).rgb + texture(src, vUv + vec2(0, -t.y)).rgb + texture(src, vUv + vec2(0, t.y)).rgb) * 2.0;
	c += texture(src, vUv + vec2(-t.x, -t.y)).rgb + texture(src, vUv + vec2(t.x, -t.y)).rgb + texture(src, vUv + vec2(-t.x, t.y)).rgb + texture(src, vUv + vec2(t.x, t.y)).rgb;
	outColor = vec4(c / 16.0, 1.0);
}
