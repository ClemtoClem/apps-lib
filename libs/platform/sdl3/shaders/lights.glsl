// Bloc d'éclairage commun (sdl3::LightBlock, std140 : uniquement des vec4).
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 4

layout(set = 3, binding = 1) uniform Lights {
    vec4 cameraPos;   // xyz, exposition
    vec4 sunDir;      // xyz : vers la lumière ; w : intensité
    vec4 sunColor;    // rgb ; w : ombre active (0/1)
    vec4 skyZenith;   // rgb ; w : intensité de l'environnement
    vec4 skyHorizon;  // rgb ; w : taille d'un texel de la carte d'ombre
    vec4 groundColor; // rgb ; w : nombre de lumières ponctuelles
    vec4 fogColor;    // rgb ; w : densité du brouillard
    vec4 counts;      // x : nombre de spots ; y : soleil visible dans le ciel (0/1) ; z : image du TAA
    vec4 pointPos[MAX_POINT_LIGHTS];   // xyz, portée
    vec4 pointColor[MAX_POINT_LIGHTS]; // rgb × intensité
    vec4 spotPos[MAX_SPOT_LIGHTS];     // xyz, portée
    vec4 spotDir[MAX_SPOT_LIGHTS];     // xyz : sens du faisceau ; w : cos du demi-angle extérieur
    vec4 spotColor[MAX_SPOT_LIGHTS];   // rgb × intensité ; w : cos du demi-angle intérieur
    vec4 water0;      // x : milieu sous-marin actif (0/1), y : altitude de la surface, z : absorption (1/m), w : temps (s)
    vec4 water1;      // x : force des caustiques, y : échelle des caustiques (1/m), z : coefficient de profondeur du brouillard
};

// ── Milieu sous-marin ─────────────────────────────────────────────────────────
// Le rouge est absorbé le premier, puis le vert : la lumière bleuit avec la profondeur.
const vec3 kWaterAbsorption = vec3(2.6, 1.0, 0.62);

bool Underwater() { return water0.x > 0.5; }

// Transmission de la lumière venue de la surface jusqu'à l'altitude `y`.
vec3 WaterTransmittance(float y) {
    float depth = max(water0.y - y, 0.0);
    return exp(-kWaterAbsorption * water0.z * depth);
}

// Ciel analytique : dégradé zénith / horizon / sol et disque solaire.
vec3 SkyColor(vec3 d, bool withSun) {
    float t = d.y;
    vec3 c = t > 0.0 ? mix(skyHorizon.rgb, skyZenith.rgb, pow(t, 0.55))
                     : mix(skyHorizon.rgb, groundColor.rgb, pow(min(-t * 4.0, 1.0), 0.6));
    if (withSun && counts.y > 0.5) {
        float s = max(dot(d, sunDir.xyz), 0.0);
        c += sunColor.rgb * sunDir.w * (pow(s, 1200.0) * 24.0 + pow(s, 24.0) * 0.25);
    }
    return c * skyZenith.w;
}

// Couleur de l'eau vue dans la direction `d` depuis la caméra : dégradé de l'environnement
// assombri selon la profondeur de la caméra (fond et brouillard identiques : aucun raccord).
vec3 WaterColor(vec3 d) {
    // Dégradé large et continu selon la verticale : aucune ligne d'horizon visible.
    vec3 c = mix(groundColor.rgb, skyHorizon.rgb, smoothstep(-0.85, 0.15, d.y));
    c = mix(c, skyZenith.rgb, smoothstep(0.05, 1.0, d.y)) * skyZenith.w;
    float depth = max(water0.y - cameraPos.y, 0.0);
    // Vers le haut, la lumière traverse moins d'eau : plus claire et plus cyan.
    float upward = clamp(d.y, 0.0, 1.0);
    c *= exp(-kWaterAbsorption * water0.z * depth * water1.z * (1.0 - 0.6 * upward));
    return c;
}

// Éclairement diffus de l'environnement pour une normale (hémisphères ciel / sol).
vec3 Irradiance(vec3 n) {
    float t = n.y;
    vec3 c = t > 0.0 ? mix(skyHorizon.rgb, skyZenith.rgb, t) : mix(skyHorizon.rgb, groundColor.rgb, min(-t * 1.5, 1.0));
    return c * skyZenith.w;
}

// Valeur HDR exploitable : NaN remplacés par 0, infinis et excès bornés sous le maximum du
// flottant 16 bits (65 504). Sans cela, un seul pixel invalide est étalé par le bloom en
// grandes taches blanches ou noires.
vec3 SafeHdr(vec3 c) {
    c = mix(c, vec3(0.0), vec3(isnan(c)));
    return clamp(c, vec3(0.0), vec3(32000.0));
}

// Courbe de tons ACES (ajustement de Narkowicz) puis encodage gamma 2,2.
vec3 ToneMap(vec3 c) {
    c *= cameraPos.w;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}
