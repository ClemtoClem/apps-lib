#version 450
// Rayons de lumière (god rays) — flou radial depuis la position écran du soleil.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform Params { vec4 p; }; // xy = soleil (écran), z = atténuation

void main() {
	vec2 dir = vUv - p.xy;
	vec3 acc = vec3(0.0);
	float w = 0.0;
	const int N = 32;
	for (int i = 0; i < N; ++i) {
		float t = (float(i) + 0.5) / float(N);
		vec3 s = texture(src, vUv - dir * t).rgb;
		float wt = pow(1.0 - t, p.z);
		acc += s * wt;
		w += wt;
	}
	vec3 c = acc / max(w, 1e-4);
	outColor = vec4(clamp(mix(c, vec3(0.0), vec3(isnan(c))), vec3(0.0), vec3(32000.0)), 1.0);
}
