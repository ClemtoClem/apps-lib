#version 450
// #extension GL_GOOGLE_include_directive : require
// Éclairage physique (GGX, Smith, Schlick) : soleil avec ombre PCF, lumières ponctuelles
// et spots, environnement analytique (diffus + réflexions selon la rugosité).
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vTint;
layout(location = 4) in vec4 vLight;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 1) uniform sampler2D albedoMap;

layout(set = 3, binding = 0) uniform Material {
    vec4 baseColor; // rgb linéaire, opacité
    vec4 params;    // métal, rugosité, sans éclairage (0/1), reçoit les ombres (0/1)
    vec4 emissive;  // rgb
    vec4 uvScale;   // xy
    vec4 effects;   // x : surface d'eau vue de dessous, y : reçoit les caustiques, z : triplanaire, w : translucidité
    vec4 effects2;  // x : mélange additif (le brouillard l'éteint au lieu de le teinter), y : s'efface vu par la tranche, z : liseré (bulle), w : fondu de proximité (m)
    vec4 effects3;  // rgb : couleur du couvert végétal du terrain (vUv.x = couverture)
};

#include "lights.glsl"

const float PI = 3.14159265;

float DistributionGgx(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Visibilité de Smith (G / (4 n·l n·v)).
float VisibilitySmith(float nv, float nl, float a) {
    float k = a * 0.5;
    float gv = nv / (nv * (1.0 - k) + k);
    float gl = nl / (nl * (1.0 - k) + k);
    return gv * gl / max(4.0 * nv * nl, 1e-4);
}

vec3 FresnelSchlick(vec3 f0, float c) { return f0 + (1.0 - f0) * pow(1.0 - c, 5.0); }

vec3 Brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float metal, float rough) {
    float nl = dot(n, l);
    if (nl <= 0.0)
        return vec3(0.0);
    vec3 h = normalize(v + l);
    float nv = max(dot(n, v), 1e-3), nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
    float a = max(rough * rough, 0.002);
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 f = FresnelSchlick(f0, vh);
    vec3 specular = DistributionGgx(nh, a) * VisibilitySmith(nv, nl, a) * f;
    vec3 diffuse = (1.0 - f) * (1.0 - metal) * albedo / PI;
    return (diffuse + specular) * nl;
}

// Approximation analytique de la BRDF d'environnement (Karis, « mobile »).
vec2 EnvBrdf(float rough, float nv) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

float SunShadow(float nl) {
    if (sunColor.w < 0.5 || params.w < 0.5)
        return 1.0;
    vec3 p = vLight.xyz / vLight.w;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0 || p.z >= 1.0)
        return 1.0;
    float bias = mix(0.0025, 0.0006, clamp(nl, 0.0, 1.0));
    float texel = skyHorizon.w, sum = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sum += texture(shadowMap, vec3(uv + vec2(x, y) * texel, p.z - bias));
    return sum / 9.0;
}

// ── Caustiques : réseau de cellules animé (distance aux deux germes les plus proches) ──
vec2 Hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// Lignes lumineuses là où deux cellules se touchent (F2 − F1 petit) ; `width` élargit les
// lignes au loin (dérivées écran) pour éviter le scintillement.
float CausticLayer(vec2 p, float t, float width) {
    vec2 cell = floor(p), f = fract(p);
    float f1 = 8.0, f2 = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 o = vec2(x, y);
            vec2 h = Hash2(cell + o);
            vec2 seed = o + 0.5 + 0.42 * sin(t * (0.6 + 0.5 * h) + 6.2831 * h);
            float d = length(seed - f);
            if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
        }
    return 1.0 - smoothstep(0.0, width, f2 - f1);
}

float Caustics(vec3 world) {
    float t = water0.w;
    vec2 p = world.xz * water1.y;
    // Déformation lente : les bords de cellules deviennent des filaments sinueux.
    p += 0.32 * vec2(sin(p.y * 1.7 + t * 0.55), sin(p.x * 1.5 - t * 0.47));
    float px = length(fwidth(p));
    float width = 0.08 + 1.8 * px;
    float a = CausticLayer(p, t * 0.8, width);
    float b = CausticLayer(p * 1.37 + vec2(5.2, 1.3), t * 1.1, width * 1.2);
    // Deux réseaux qui interfèrent : lignes fines, nœuds plus brillants.
    float c = 0.45 * a + 0.3 * b + 0.8 * a * b;
    c *= 0.7 + 0.6 * smoothstep(-0.4, 0.6, sin(p.x * 0.37 + t * 0.21) * sin(p.y * 0.29 - t * 0.17)); // intensité irrégulière
    // Au loin, le motif se fond en valeur moyenne (pas de moiré ni de scintillement).
    float fade = clamp(1.0 - px * 4.0, 0.0, 1.0);
    return mix(0.3, c, fade);
}

// ── Surface de l'eau vue de dessous : vagues, fenêtre de Snell, réflexion totale ──
vec3 WaveNormal(vec2 xz, float t) {
    // Somme de vagues de 3 à 12 m : pentes faibles, la fenêtre de Snell ondule sans se rompre.
    vec2 g = vec2(0.0);
    const vec2 d0 = vec2(0.82, 0.57), d1 = vec2(-0.45, 0.89), d2 = vec2(0.97, -0.24), d3 = vec2(-0.71, -0.7);
    g += 0.07 * d0 * cos(dot(xz, d0) * 0.55 + t * 1.1);
    g += 0.06 * d1 * cos(dot(xz, d1) * 0.9 + t * 1.5);
    g += 0.05 * d2 * cos(dot(xz, d2) * 1.4 - t * 1.9);
    g += 0.04 * d3 * cos(dot(xz, d3) * 2.1 + t * 2.4);
    return normalize(vec3(-g.x, 1.0, -g.y));
}

vec3 SurfaceFromBelow() {
    vec3 ray = normalize(vWorld - cameraPos.xyz); // vers le haut, de la caméra à la surface
    vec3 n = WaveNormal(vWorld.xz, water0.w);
    float cosI = dot(ray, n);
    // Fenêtre de Snell (angle critique ≈ 48,6°) : le ciel lumineux ; au-delà, réflexion totale
    // de l'eau sombre.
    float window = smoothstep(0.6, 0.76, cosI);
    vec3 refracted = refract(ray, -n, 1.0 / 1.33);
    if (dot(refracted, refracted) < 1e-4)
        refracted = vec3(0.0, 1.0, 0.0);
    vec3 sky = mix(skyHorizon.rgb, skyZenith.rgb, clamp(refracted.y, 0.0, 1.0)) * 0.8;
    float toSun = max(dot(refracted, sunDir.xyz), 0.0);
    sky += sunColor.rgb * sunDir.w * (pow(toSun, 900.0) * 0.9 + pow(toSun, 40.0) * 0.06); // disque compact + halo léger
    vec3 below = WaterColor(reflect(ray, n)) * 0.75;
    return mix(below, sky, window);
}

// Échantillon d'une moitié d'atlas (0 : gauche, 1 : droite) avec des dérivées continues :
// pas de couture au raccord des tuiles malgré le `fract`.
vec3 AtlasHalf(vec2 uv, float half_) {
    vec2 tile = vec2((fract(uv.x) + half_) * 0.5, fract(uv.y));
    return textureGrad(albedoMap, tile, dFdx(uv) * vec2(0.5, 1.0), dFdy(uv) * vec2(0.5, 1.0)).rgb;
}

// Projection selon les trois axes du monde. Mode terrain (effects.z = 2) : le dessus
// (normale vers le haut) prend la moitié gauche de l'atlas (sable), les parois la droite (roche).
vec3 Triplanar(vec3 world, vec3 n) {
    vec2 s = uvScale.xy * 0.25;
    if (effects.z > 1.5) {
        vec3 w = pow(abs(n), vec3(6.0));
        float top = n.y > 0.0 ? w.y : 0.0;
        float side = w.x + w.z + (n.y > 0.0 ? 0.0 : w.y);
        float sand = smoothstep(0.35, 0.75, top / max(top + side, 1e-4));
        vec3 rock = (AtlasHalf(world.zy * s, 1.0) * w.x + AtlasHalf(world.xy * s, 1.0) * w.z + AtlasHalf(world.xz * s, 1.0) * (n.y > 0.0 ? 0.0 : w.y))
                  / max(w.x + w.z + (n.y > 0.0 ? 0.0 : w.y), 1e-4);
        return mix(rock, AtlasHalf(world.xz * s * 1.6, 0.0), sand);
    }
    vec3 w = pow(abs(n), vec3(4.0));
    w /= max(w.x + w.y + w.z, 1e-4);
    return texture(albedoMap, world.zy * s).rgb * w.x + texture(albedoMap, world.xz * s).rgb * w.y
         + texture(albedoMap, world.xy * s).rgb * w.z;
}

vec3 ApplyFog(vec3 color) {
    float dist = length(cameraPos.xyz - vWorld);
    if (effects2.x > 0.5) { // additif : s'efface dans le brouillard (sinon il éclaircirait le fond)
        float density = Underwater() || fogColor.w > 0.0 ? fogColor.w : 0.0;
        return color * exp(-density * dist);
    }
    if (Underwater()) {
        float f = 1.0 - exp(-fogColor.w * dist);
        return mix(color, WaterColor(normalize(vWorld - cameraPos.xyz)), f);
    }
    if (fogColor.w > 0.0)
        return mix(color, fogColor.rgb, 1.0 - exp(-fogColor.w * dist));
    return color;
}

void main() {
    if (effects.x > 0.5) { // surface de l'eau (sans lumière propre : elle EST la source)
        outColor = vec4(SafeHdr(ApplyFog(SurfaceFromBelow())), 1.0);
        return;
    }
    vec4 tex = texture(albedoMap, vUv * uvScale.xy);
    if (effects.z > 0.5)
        tex.rgb = Triplanar(vWorld, normalize(vNormal));
    vec3 albedo = baseColor.rgb * tex.rgb * vTint.rgb;
    float alpha = baseColor.a * tex.a * vTint.a;
    if (effects.z > 1.5) { // terrain : couvert végétal (tapis) sur les surfaces horizontales
        float cover = clamp(vUv.x, 0.0, 1.0) * smoothstep(0.55, 0.85, normalize(vNormal).y);
        float breakup = smoothstep(0.35, 0.75, tex.g + 0.25 * sin(vWorld.x * 1.7) * sin(vWorld.z * 1.3));
        albedo = mix(albedo, effects3.rgb * (0.7 + 0.5 * tex.g), cover * (0.35 + 0.4 * breakup));
    }
    if (effects2.w > 0.0) { // fondu de proximité par tramage (pas de feuille coupée par le plan proche)
        float d = length(cameraPos.xyz - vWorld);
        float keep = smoothstep(effects2.w * 0.35, effects2.w, d);
        // counts.z : index d'image du TAA (0 sans) — le motif tourne et l'accumulation le lisse.
        vec2 cell = floor(gl_FragCoord.xy) + counts.z * 5.588238;
        float threshold = fract(52.9829189 * fract(dot(cell, vec2(0.06711056, 0.00583715))));
        if (keep < threshold)
            discard;
    }
    if (params.z > 0.5) { // sans éclairage : couleur telle quelle
        outColor = vec4(pow(albedo, vec3(1.0 / 2.2)), alpha);
        return;
    }
    vec3 n = normalize(vNormal);
    vec3 v = normalize(cameraPos.xyz - vWorld);
    if (dot(n, v) < 0.0) // faces arrière des matériaux double face
        n = -n;
    float metal = params.x, rough = clamp(params.y, 0.08, 1.0); // en dessous, le pic spéculaire dépasse le flottant 16 bits

    vec3 color = emissive.rgb * tex.rgb * vTint.rgb; // émissif teinté par la texture et l'instance
    float nlSun = dot(n, sunDir.xyz);
    vec3 sun = sunColor.rgb * sunDir.w * SunShadow(nlSun);
    vec3 ambientTint = vec3(1.0);
    if (Underwater()) {
        vec3 transmit = WaterTransmittance(vWorld.y);
        sun *= transmit;
        ambientTint = sqrt(transmit); // lumière diffuse : chemin moyen plus court
        if (effects.y > 0.5 && water1.x > 0.0) {
            float up = smoothstep(0.35, 0.85, n.y);
            sun *= mix(1.0, 0.25 + 2.2 * Caustics(vWorld), water1.x * up);
        }
    }
    color += Brdf(n, v, sunDir.xyz, albedo, metal, rough) * sun;
    if (effects.w > 0.0) { // translucidité : feuilles éclairées par derrière, vues vers le soleil
        float back = pow(max(dot(-v, sunDir.xyz), 0.0), 3.0) * max(-nlSun, 0.0) + 0.25 * max(-nlSun, 0.0);
        // La lumière diffusée dans la feuille ne dépend qu'en partie de l'ombre portée.
        vec3 unshadowed = sunColor.rgb * sunDir.w * (Underwater() ? WaterTransmittance(vWorld.y) : vec3(1.0));
        color += albedo * mix(unshadowed, sun, 0.5) * back * effects.w;
    }
    for (int i = 0; i < int(groundColor.w); ++i) {
        vec3 d = pointPos[i].xyz - vWorld;
        float dist = length(d), range = pointPos[i].w;
        float fall = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        float att = fall * fall / (dist * dist + 1.0);
        color += Brdf(n, v, d / dist, albedo, metal, rough) * pointColor[i].rgb * att;
    }
    for (int i = 0; i < int(counts.x); ++i) {
        vec3 d = spotPos[i].xyz - vWorld;
        float dist = length(d), range = spotPos[i].w;
        vec3 l = d / dist;
        float cone = smoothstep(spotDir[i].w, spotColor[i].w, dot(-l, spotDir[i].xyz));
        float fall = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        float att = cone * fall * fall / (dist * dist + 1.0);
        color += Brdf(n, v, l, albedo, metal, rough) * spotColor[i].rgb * att;
    }

    // Environnement : diffus hémisphérique et réflexion floutée selon la rugosité.
    float nv = max(dot(n, v), 1e-3);
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec2 ab = EnvBrdf(rough, nv);
    vec3 r = reflect(-v, n);
    vec3 reflection = mix(SkyColor(r, false), Irradiance(r), rough * rough);
    color += ((1.0 - metal) * albedo * Irradiance(n) + reflection * (f0 * ab.x + ab.y)) * ambientTint;

    color = ApplyFog(color);
    if (effects2.y > 0.5) // plans croisés (faisceaux) : vus par la tranche, ils deviendraient des traits
        color *= smoothstep(0.2, 0.75, abs(dot(normalize(vNormal), v)));
    if (effects2.z > 0.5) { // bulle : liseré brillant (Fresnel) et cœur transparent
        float facing = abs(dot(normalize(vNormal), v));
        color *= 0.08 + 1.4 * pow(1.0 - facing, 2.5);
    }
    if (effects2.x > 0.5) // additif (ONE, ONE) : l'opacité dose l'apport
        color *= alpha;
    outColor = vec4(SafeHdr(color), clamp(alpha, 0.0, 1.0)); // linéaire (le post-traitement fait le mappage de tons)
}
