#version 450
// Post-traitement final : scène HDR (anticrénelage FXAA) + bloom + rayons → exposition,
// mappage de tons (ACES), saturation, vignettage, gamma, tramage (supprime les bandes des
// dégradés sombres).
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D scene;
layout(set = 2, binding = 1) uniform sampler2D bloom;
layout(set = 2, binding = 2) uniform sampler2D rays;
// a = exposition, force du bloom, force des rayons, saturation ; b = vignettage, gamma ;
// c = taille d'un texel (xy), FXAA (0/1), tramage (0/1)
layout(set = 3, binding = 0) uniform Params { vec4 a; vec4 b; vec4 c; };

// Luminance perçue d'une couleur HDR compressée (c / (1 + c)) : les bords très lumineux ne
// dominent pas la détection.
float Luma(vec3 rgb) {
	rgb *= a.x;
	return dot(rgb / (1.0 + rgb), vec3(0.299, 0.587, 0.114));
}

// FXAA (variante « console » de Lottes) : flou le long du bord détecté par les diagonales.
vec3 SceneFxaa(vec2 uv) {
	vec2 px = c.xy;
	vec3 rgbM = texture(scene, uv).rgb;
	if (c.z < 0.5)
		return rgbM;
	float lNW = Luma(texture(scene, uv + vec2(-px.x, -px.y)).rgb);
	float lNE = Luma(texture(scene, uv + vec2(px.x, -px.y)).rgb);
	float lSW = Luma(texture(scene, uv + vec2(-px.x, px.y)).rgb);
	float lSE = Luma(texture(scene, uv + vec2(px.x, px.y)).rgb);
	float lM = Luma(rgbM);
	float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
	float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
	if (lMax - lMin < max(0.04, lMax * 0.125)) // pas de bord : rien à faire
		return rgbM;
	vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
	float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
	float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
	dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * px;
	vec3 rgbA = 0.5 * (texture(scene, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(scene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(scene, uv - dir * 0.5).rgb + texture(scene, uv + dir * 0.5).rgb);
	float lB = Luma(rgbB);
	return (lB < lMin || lB > lMax) ? rgbA : rgbB;
}

// Bruit triangulaire (−1..1) par pixel, stable d'une image à l'autre.
float Dither(vec2 frag) {
	float r1 = fract(sin(dot(frag, vec2(12.9898, 78.233))) * 43758.5453);
	float r2 = fract(sin(dot(frag, vec2(39.3468, 11.135))) * 24634.6345);
	return r1 + r2 - 1.0;
}

void main() {
	vec3 col = SceneFxaa(vUv) * a.x;
	col += texture(bloom, vUv).rgb * a.y;
	col += texture(rays, vUv).rgb * a.z;
	col = clamp(mix(col, vec3(0.0), vec3(isnan(col))), vec3(0.0), vec3(32000.0));
	// Mappage de tons ACES (Narkowicz).
	col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), 0.0, 1.0);
	// Saturation.
	float l = dot(col, vec3(0.299, 0.587, 0.114));
	col = mix(vec3(l), col, a.w);
	// Vignettage.
	vec2 d = vUv - 0.5;
	col *= 1.0 - b.x * dot(d, d);
	// Gamma, puis tramage d'un demi-niveau sur 8 bits.
	col = pow(col, vec3(b.y));
	if (c.w > 0.5)
		col += Dither(gl_FragCoord.xy) / 255.0;
	outColor = vec4(col, 1.0);
}
