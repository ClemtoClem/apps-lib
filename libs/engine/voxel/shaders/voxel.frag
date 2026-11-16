#version 450
layout(location = 0) in vec3 vUv;
layout(location = 1) in float vShade;
layout(location = 2) in float vFog;
layout(location = 3) in float vLight;
layout(location = 4) in vec3 vWorld;
layout(location = 5) in vec3 vNormal;
layout(location = 6) in float vReflect;

layout(set = 2, binding = 0) uniform sampler2DArray blockTextures;
// Grille d'opacité 3D (ombres portées) : 0 vide → 1 opaque.
layout(set = 2, binding = 1) uniform sampler3D shadowVolume;
layout(set = 3, binding = 0) uniform Params {
    vec4 fogColor;
    vec4 params; // x = seuil alpha (découpe), y = lumière du jour, z = opacité min, w = interval LOD (blocs)
    vec4 cameraPos; // xyz = position de la caméra
    vec4 extra;     // x = rebonds de réflexion (intensité spéculaire)
};
// Lumières dynamiques typées : ponctuelle, spot (cône) ou directionnelle (faisceau).
layout(set = 3, binding = 1) uniform Lights {
    vec4 lightCount;      // x = nombre (0..16)
    vec4 lightPosRange[16]; // xyz = position, w = portée
    vec4 lightColorInt[16]; // rgb = couleur, w = intensité
    vec4 lightDirType[16];  // xyz = direction, w = type (0 ponctuelle, 1 spot, 2 directionnelle)
    vec4 lightCone[16];     // x = cos intérieur, y = cos extérieur, z = rayon du faisceau
    vec4 shadowInfo;        // xyz = centre du volume d'ombres, w = taille (0 : désactivé)
};

layout(location = 0) out vec4 outColor;

const int kShadowSteps = 48;

// Transmittance du trajet `from` → `to` à travers la grille d'opacité : 1 = libre,
// 0 = entièrement occulté ; les blocs partiellement transparents (feuillage) ne
// bloquent qu'une partie de la lumière.
float ShadowAt(vec3 from, vec3 to, vec4 info) {
    if (info.w <= 0.0)
        return 1.0;
    vec3 center = info.xyz;
    float size = info.w;
    vec3 step = (to - from) / float(kShadowSteps);
    float transmittance = 1.0;
    for (int s = 0; s < kShadowSteps; ++s) {
        vec3 p = from + step * (float(s) + 0.5);
        vec3 uv = (p - center) / size + 0.5;
        if (any(lessThan(uv, vec3(0.0))) || any(greaterThan(uv, vec3(1.0))))
            continue; // hors du volume : rien de connu, pas d'occlusion
        float op = texture(shadowVolume, uv).r;
        transmittance *= 1.0 - op;
        if (transmittance < 0.02)
            break;
    }
    return transmittance;
}

void main() {
    // Niveau de détail : un mip de plus tous les `params.w` blocs de distance.
    float lod = floor(length(vWorld - cameraPos.xyz) / max(params.w, 1.0));
    vec4 c = textureLod(blockTextures, vUv, lod);
    if (c.a < params.x)
        discard;
    float light = max(params.y, vLight);
    vec3 viewDir = normalize(cameraPos.xyz - vWorld);
    vec3 dynamicLight = vec3(0.0);
    vec3 specular = vec3(0.0);
    int n = int(lightCount.x);
    for (int i = 0; i < n; ++i) {
        vec3 lp = lightPosRange[i].xyz;
        float range = max(lightPosRange[i].w, 0.01);
        vec3 lc = lightColorInt[i].rgb * lightColorInt[i].w;
        vec3 toLight = lp - vWorld;
        float d = length(toLight);
        float t = lightDirType[i].w;
        float att;
        if (t < 0.5) {
            // Ponctuelle : atténuation linéaire adoucie jusqu'à la portée.
            att = clamp(1.0 - d / range, 0.0, 1.0);
            att = att * att;
        } else if (t < 1.5) {
            // Spot : cône intérieur/extérieur lissé + atténuation.
            vec3 dir = normalize(lightDirType[i].xyz);
            float cosAng = dot(dir, toLight / max(d, 0.001));
            float cone = smoothstep(lightCone[i].y, lightCone[i].x, cosAng);
            att = cone * clamp(1.0 - d / range, 0.0, 1.0);
            att = att * att;
        } else {
            // Directionnelle : faisceau parallèle, sans atténuation de distance.
            vec3 dir = normalize(lightDirType[i].xyz);
            float side = length(toLight - dir * dot(toLight, dir));
            att = 1.0 - smoothstep(0.0, max(lightCone[i].z, 0.001), side);
        }
        att = max(att, 0.0);
        // Ombres portées : les blocs opaques arrêtent la lumière, le feuillage la filtre.
        float sh = (t < 1.5) ? ShadowAt(vWorld, lp, shadowInfo)
                             : ShadowAt(vWorld, vWorld - normalize(lightDirType[i].xyz) * shadowInfo.w, shadowInfo);
        dynamicLight += lc * att * sh;
        // Réflectance spéculaire (Blinn-Phong) : les matériaux réfléchissants
        // renvoient un reflet des sources, d'autant plus fort que `extra.x`.
        if (vReflect > 0.001) {
            vec3 ldir = toLight / max(d, 0.001);
            vec3 halfVec = normalize(ldir + viewDir);
            float spec = pow(max(dot(vNormal, halfVec), 0.0), 48.0) * vReflect * extra.x;
            specular += lc * (spec * att * sh);
        }
    }
    vec3 rgb = c.rgb * (vShade * (vec3(light) + dynamicLight) + specular);
    rgb = mix(rgb, fogColor.rgb, vFog);
    outColor = vec4(rgb, max(c.a, params.z));
}
