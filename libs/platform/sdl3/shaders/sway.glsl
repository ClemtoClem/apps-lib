// Ondulation au sommet (mesh.vert, shadow.vert) : plantes dans le courant, nage des poissons,
// battement des ailes d'une raie. Paramètres par dessin (sdl3::Material) :
//   sway.x amplitude (m), sway.y fréquence (rad/s), sway.z mode, sway.w longueur de référence
//   (plante : hauteur du sommet ; marche : bord du corps |z| ; crevette : longueur de l'abdomen).
// La phase dépend de la position de l'instance : deux plantes voisines ne battent pas ensemble.

layout(set = 1, binding = 1) uniform Draw {
    vec4 sway;
};

// Position locale déformée (modes nage, battement, marche, crevette) ; le mode plante se fait
// dans le monde. sway.z : 0 plante, 1 nage, 2 battement, 3 marche, 4 crevette.
vec3 SwayLocal(vec3 p, float phase, float t) {
    float amp = sway.x, freq = sway.y;
    if (sway.z > 0.5 && sway.z < 1.5) { // nage : la queue (−x) ondule de côté
        float tail = clamp((0.35 - p.x) / 0.9, 0.0, 1.0);
        p.z += sin(t * freq + phase - p.x * 7.0) * amp * tail;
    } else if (sway.z > 1.5 && sway.z < 2.5) { // battement : les pointes des ailes (|z|) montent et descendent
        float span = abs(p.z);
        p.y += sin(t * freq + phase - span * 1.6) * amp * span * span;
    } else if (sway.z > 2.5 && sway.z < 3.5) { // marche : pattes au-delà du bord du corps
        float edge = max(sway.w, 1e-3);
        float leg = clamp((abs(p.z) - edge) / edge, 0.0, 1.0);
        // Tripode : pattes voisines en opposition, côtés en opposition.
        float gait = t * freq + phase + p.x * 14.0 + (p.z > 0.0 ? 0.0 : 3.14159);
        p.y += max(sin(gait), 0.0) * amp * leg;         // la patte se lève…
        p.x += cos(gait) * amp * 0.7 * leg;             // …et avance pendant qu'elle est levée
    } else if (sway.z > 3.5) { // crevette : abdomen qui fouette, pattes nageuses qui battent
        float legs = clamp((-p.y - 0.024) / 0.012, 0.0, 1.0); // sous le corps
        float tail = clamp(-p.x / max(sway.w, 1e-3), 0.0, 1.0);
        p.x += sin(t * freq * 2.6 + phase + p.x * 70.0) * amp * 0.35 * legs;
        p.y += sin(t * freq + phase) * amp * tail * tail;
    }
    return p;
}

// Décalage dans le monde du mode plante (courbure croissante vers le sommet).
vec3 SwayWorld(vec3 local, float phase, float t) {
    if (sway.z > 0.5 || sway.x <= 0.0)
        return vec3(0.0);
    float h = clamp(local.y / max(sway.w, 1e-3), 0.0, 1.0);
    float bend = h * h;
    float freq = sway.y;
    return vec3(sin(t * freq + phase + local.y * 0.35) + 0.12 * sin(t * freq * 2.3 + phase * 1.7),
                0.0,
                cos(t * freq * 0.83 + phase * 1.31 + local.y * 0.3)) * (sway.x * bend);
}

vec3 SwayedWorld(mat4 model, vec3 local, float t) {
    vec3 origin = model[3].xyz;
    float phase = dot(origin, vec3(0.21, 0.07, 0.17)) * 3.0;
    vec3 p = sway.x > 0.0 ? SwayLocal(local, phase, t) : local;
    return (model * vec4(p, 1.0)).xyz + SwayWorld(local, phase, t);
}
