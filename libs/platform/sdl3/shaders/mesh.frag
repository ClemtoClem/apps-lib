#version 450
#extension GL_GOOGLE_include_directive : require
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

void main() {
    vec4 tex = texture(albedoMap, vUv * uvScale.xy);
    vec3 albedo = baseColor.rgb * tex.rgb * vTint.rgb;
    float alpha = baseColor.a * tex.a * vTint.a;
    if (params.z > 0.5) { // sans éclairage : couleur telle quelle
        outColor = vec4(pow(albedo, vec3(1.0 / 2.2)), alpha);
        return;
    }
    vec3 n = normalize(vNormal);
    vec3 v = normalize(cameraPos.xyz - vWorld);
    if (dot(n, v) < 0.0) // faces arrière des matériaux double face
        n = -n;
    float metal = params.x, rough = clamp(params.y, 0.04, 1.0);

    vec3 color = emissive.rgb;
    float nlSun = dot(n, sunDir.xyz);
    color += Brdf(n, v, sunDir.xyz, albedo, metal, rough) * sunColor.rgb * sunDir.w * SunShadow(nlSun);
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
    color += (1.0 - metal) * albedo * Irradiance(n) + reflection * (f0 * ab.x + ab.y);

    if (fogColor.w > 0.0)
        color = mix(color, fogColor.rgb, 1.0 - exp(-fogColor.w * length(cameraPos.xyz - vWorld)));
    outColor = vec4(ToneMap(color), alpha);
}
