// Interface des shaders de fragments personnalisés du renderer 2D « gpu » de SDL
// (SDL_GPURenderState) : couleur et UV interpolées, texture dessinée en emplacement 0,
// uniformes de l'effet en emplacement 0 du jeu 3. Couleurs prémultipliées par l'alpha.
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D layer;

layout(set = 3, binding = 0) uniform Effect {
    vec4 params; // temps (s), intensité 0..1, taille d'un texel (u, v)
    vec4 region; // rectangle UV de la carte dans la couche : u0, v0, u1, v1
};

const float PI = 3.14159265;

// Coordonnées dans la carte (0..1) et retour vers la couche.
vec2 Local() { return (vUv - region.xy) / (region.zw - region.xy); }
vec2 ToLayer(vec2 local) { return region.xy + local * (region.zw - region.xy); }

// Échantillon de la couche limité à la carte : transparent au-delà (jamais les cartes voisines).
vec4 Sample(vec2 uv) {
    vec2 inside = step(region.xy, uv) * step(uv, region.zw);
    return texture(layer, uv) * (inside.x * inside.y);
}
