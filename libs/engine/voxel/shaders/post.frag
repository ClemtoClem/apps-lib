#version 450
// Post-traitement plein écran : effets atmosphériques et de statut du joueur.
//   effects.x  = sous l'eau (teinte bleue + ondulation)
//   effects.y  = chaleur (déformation thermique / réfraction)
//   effects.z  = éblouissement (halo face à une forte source)
//   effects.w  = étourdissement (poison : distorsion + teinte verte)
//   effects2.x = asphyxie (noyade / bloc : vignette sombre + teinte rouge)
//   effects2.y = temps (secondes) ; z/w = dimensions de l'écran
layout(location = 0) in vec2 vUv;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;
layout(set = 3, binding = 0) uniform PostParams {
    vec4 effects;
    vec4 effects2;
};

layout(location = 0) out vec4 outColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = vUv;

    // Sous l'eau : ondulation lente.
    if (effects.x > 0.001) {
        float t = effects2.y * 1.6;
        uv += vec2(sin(uv.y * 22.0 + t), cos(uv.x * 22.0 + t)) * 0.0035 * effects.x;
    }
    // Chaleur : réfraction qui monte.
    if (effects.y > 0.001) {
        float t = effects2.y * 3.2;
        uv += vec2(sin(uv.y * 42.0 + t), cos(uv.x * 42.0 + t * 0.9)) * 0.008 * effects.y;
    }
    // Étourdissement : distorsion chaotique.
    if (effects.w > 0.001) {
        float t = effects2.y * 5.0;
        uv += vec2(hash(uv * 13.0 + t) - 0.5, hash(uv * 17.0 + t) - 0.5) * 0.025 * effects.w;
    }

    vec3 col = texture(sceneTex, uv).rgb;

    // Éblouissement : surexposition + halo central (regard vers une source forte).
    if (effects.z > 0.001) {
        float d = length(vUv - 0.5);
        col += col * effects.z * 0.55;
        col += vec3(1.0, 0.92, 0.72) * exp(-d * d * 14.0) * effects.z * 0.55;
    }

    // Teintes de statut.
    col = mix(col, col * vec3(0.34, 0.55, 0.95), effects.x);   // bleu sous-marin
    col = mix(col, col * vec3(0.55, 1.0, 0.45), effects.w);     // vert poison
    col = mix(col, col * vec3(1.0, 0.35, 0.25), effects2.x);    // rouge asphyxie

    // Vignette d'asphyxie : le bord de l'écran s'assombrit.
    if (effects2.x > 0.001) {
        float d = length(vUv - 0.5);
        col *= 1.0 - smoothstep(0.25, 0.75, d) * effects2.x * 0.85;
    }

    outColor = vec4(col, 1.0);
}
