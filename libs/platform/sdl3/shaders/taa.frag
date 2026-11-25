#version 450
// Anticrénelage temporel : mélange l'image courante (décalée d'une fraction de pixel) avec
// l'historique reprojeté. Tout le filtrage se fait sur des couleurs compressées
// (c / (1 + max(c)), Karis) : un reflet spéculaire très intense ne domine pas la moyenne et ne
// scintille pas. L'historique est borné par la boîte de couleurs du voisinage 3×3 courant
// (moyenne ± gamma × écart-type, en YCoCg) : pas de traînée derrière ce qui bouge ou apparaît.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D current;	 // scène HDR de l'image (décalée)
layout(set = 2, binding = 1) uniform sampler2D history;	 // résultat HDR de l'image précédente
layout(set = 2, binding = 2) uniform sampler2D depthTex; // profondeur de l'image (0..1)
// reproj : NDC + profondeur courantes → espace de découpage de l'image précédente.
// params : part de l'historique, gamma de la boîte, 1 = ignorer l'historique, accentuation.
// texel : taille d'un texel (xy), exposition (z).
layout(set = 3, binding = 0) uniform Params {
    mat4 reproj;
    vec4 params;
    vec4 texel;
};

vec3 Compress(vec3 c) {
    c = clamp(mix(c, vec3(0.0), vec3(isnan(c))), vec3(0.0), vec3(32000.0)) * texel.z;
    return c / (1.0 + max(c.r, max(c.g, c.b)));
}

vec3 Uncompress(vec3 c) {
    c = min(c, vec3(0.999));
    return c / (1.0 - max(c.r, max(c.g, c.b))) / max(texel.z, 1e-4);
}

vec3 ToYCoCg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}

vec3 FromYCoCg(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

vec3 History(vec2 uv) { return Compress(texture(history, uv).rgb); }

// Historique filtré en Catmull-Rom (5 lectures bilinéaires) : le rééchantillonnage
// d'une image à l'autre ne floute pas l'accumulation.
vec3 HistoryCatmullRom(vec2 uv) {
    vec2 size = 1.0 / texel.xy;
    vec2 pos = uv * size;
    vec2 c = floor(pos - 0.5) + 0.5;
    vec2 f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (c - 1.0) * texel.xy, t3 = (c + 2.0) * texel.xy, t12 = (c + w2 / w12) * texel.xy;
    float a = w12.x * w0.y, b = w0.x * w12.y, m = w12.x * w12.y, d = w3.x * w12.y, e = w12.x * w3.y;
    vec3 r = History(vec2(t12.x, t0.y)) * a + History(vec2(t0.x, t12.y)) * b + History(t12) * m +
             History(vec2(t3.x, t12.y)) * d + History(vec2(t12.x, t3.y)) * e;
    return max(r / (a + b + m + d + e), vec3(0.0));
}

// Ramène q dans la boîte [lo, hi] le long du segment vers son centre (moins de décalage de
// teinte qu'un simple bornage composante par composante).
vec3 ClipBox(vec3 lo, vec3 hi, vec3 q) {
    vec3 center = 0.5 * (hi + lo), extent = 0.5 * (hi - lo) + 1e-5;
    vec3 v = q - center;
    vec3 a = abs(v / extent);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? center + v / m : q;
}

void main() {
    vec2 px = texel.xy;
    vec3 m1 = vec3(0.0), m2 = vec3(0.0), cross = vec3(0.0), cur = vec3(0.0);
    float closest = 1.0;
    vec2 closestUv = vUv;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = vUv + vec2(x, y) * px;
            vec3 c = Compress(texture(current, uv).rgb);
            vec3 ycc = ToYCoCg(c);
            m1 += ycc;
            m2 += ycc * ycc;
            if (x == 0 && y == 0)
                cur = c;
            else if (x == 0 || y == 0)
                cross += c;
            // Profondeur la plus proche du voisinage : le bord d'un objet garde son propre
            // mouvement plutôt que celui du fond.
            float d = texture(depthTex, uv).r;
            if (d < closest) {
                closest = d;
                closestUv = uv;
            }
        }
    // Accentuation (masque flou sur la croix) de l'image courante seule : l'historique
    // n'accumule pas l'effet.
    cur = clamp(cur + (cur - cross * 0.25) * params.w, vec3(0.0), vec3(0.999));
    if (params.z > 0.5) {
        outColor = vec4(Uncompress(cur), 1.0);
        return;
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));

    // Reprojection : où ce point (à la profondeur la plus proche) était-il à l'image précédente ?
    vec4 prev = reproj * vec4(closestUv.x * 2.0 - 1.0, 1.0 - closestUv.y * 2.0, closest, 1.0);
    vec2 motion = vec2(0.0);
    bool valid = prev.w > 1e-6;
    if (valid) {
        vec2 ndc = prev.xy / prev.w;
        motion = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) - closestUv;
    }
    vec2 histUv = vUv + motion;
    valid = valid && all(greaterThanEqual(histUv, vec2(0.0))) && all(lessThanEqual(histUv, vec2(1.0)));
    if (!valid) {
        outColor = vec4(Uncompress(cur), 1.0);
        return;
    }
    vec3 hist = FromYCoCg(ClipBox(mean - params.y * sigma, mean + params.y * sigma, ToYCoCg(HistoryCatmullRom(histUv))));
    // En mouvement rapide, l'historique rééchantillonné perd en netteté : on lui fait moins confiance.
    float speed = length(motion / px);
    float feedback = params.x * mix(1.0, 0.85, clamp(speed / 8.0, 0.0, 1.0));
    outColor = vec4(Uncompress(mix(cur, hist, feedback)), 1.0);
}
