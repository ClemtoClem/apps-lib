#pragma once
/**
 * @file scene3d.hpp
 * @brief Description d'une scène 3D indépendante du GPU : couleurs, transformations,
 *        caméra (et contrôleur orbital), matériaux physiques, lumières, environnement.
 *
 * Conventions : repère main droite, Y vers le haut ; matrices colonne par colonne
 * (`math::FMatrix4::m[colonne][ligne]`), profondeur [0, 1] (SDL_GPU). Les couleurs des
 * matériaux et des lumières sont linéaires (`Srgb()` convertit une couleur d'écran).
 */
#include <cstdint>
#include <optional>
#include <vector>

#include <math/math.hpp>

namespace sdl3 {

/// Couleur linéaire (composantes 0..1, au-delà pour les intensités).
/// Distincte de `sdl3::Color`/`sdl3::FColor` (couleurs d'écran, sRGB) : ici les
/// composantes sont des valeurs radiométriques pour l'éclairage physique.
using LinearColor = math::FVector3;

/// Couleur d'écran 8 bits (sRGB) → couleur linéaire.
[[nodiscard]] LinearColor Srgb(uint8_t r, uint8_t g, uint8_t b) noexcept;
/// Couleur d'écran hexadécimale 0xRRGGBB → couleur linéaire.
[[nodiscard]] LinearColor Srgb(uint32_t rgb) noexcept;

/// Matrice de rotation d'un quaternion unitaire.
[[nodiscard]] math::FMatrix4 RotationMatrix(const math::FQuaternion &q) noexcept;

/// Position, orientation et échelle d'un objet (ordre d'application : échelle, rotation, translation).
struct Transform {
	math::FVector3 position{};
	math::FQuaternion rotation{};
	math::FVector3 scale{1.f, 1.f, 1.f};

	[[nodiscard]] math::FMatrix4 Matrix() const noexcept;
	/// Point du monde exprimé dans le repère de l'objet (inverse de `Matrix()`).
	[[nodiscard]] math::FVector3 InverseTransformPoint(const math::FVector3 &p) const noexcept;
	/// Direction du monde exprimée dans le repère de l'objet (non normalisée).
	[[nodiscard]] math::FVector3 InverseTransformDirection(const math::FVector3 &d) const noexcept;
};

/// Distance d'entrée d'un rayon dans une boîte (méthode des dalles) ; nullopt sans intersection.
[[nodiscard]] std::optional<float> IntersectRayAabb(const math::FRay &ray, const math::FAABB &box) noexcept;

/// Caméra de vue 3D. Distincte de `sdl3::Camera` (le périphérique vidéo de SDL).
struct Camera3D {
	math::FVector3 position{0.f, 2.f, 6.f};
	math::FVector3 target{};
	math::FVector3 up{0.f, 1.f, 0.f};
	float fovY = 50.f * math::DEG2RAD;
	float zNear = 0.1f;
	float zFar = 200.f;
	/// Projection parallèle : `orthoHeight` unités du monde sur la hauteur de l'image.
	bool orthographic = false;
	float orthoHeight = 10.f;

	[[nodiscard]] math::FMatrix4 View() const noexcept { return math::FMatrix4::LookAt(position, target, up); }
	[[nodiscard]] math::FMatrix4 Projection(float aspect) const noexcept {
		if (orthographic) {
			const float h = orthoHeight * 0.5f, w = h * aspect;
			return math::FMatrix4::Ortho(-w, w, -h, h, -zFar, zFar); // rien n'est coupé devant la caméra
		}
		return math::FMatrix4::Perspective(fovY, aspect, zNear, zFar);
	}
	[[nodiscard]] math::FMatrix4 ViewProjection(float aspect) const noexcept { return Projection(aspect) * View(); }
	/// Rayon du monde passant par le point (`u`, `v`) de l'image, 0..1 depuis le coin haut gauche.
	[[nodiscard]] math::FRay ScreenRay(float u, float v, float aspect) const noexcept;
};

/// Caméra tournant autour d'une cible : lacet, tangage, distance.
struct OrbitController {
	math::FVector3 target{};
	float yaw = 0.6f;	 // radians autour de Y
	float pitch = 0.45f; // radians au-dessus de l'horizon
	float distance = 8.f;
	float minDistance = 1.f, maxDistance = 100.f;

	/// Glissé de la souris (pixels) : rotation, tangage borné.
	void Rotate(float dx, float dy) noexcept;
	/// Molette : facteur > 1 éloigne, < 1 rapproche.
	void Zoom(float factor) noexcept;
	/// Déplacement de la cible dans le plan de l'image (pixels, échelle selon la distance).
	void Pan(float dx, float dy) noexcept;
	[[nodiscard]] math::FVector3 Eye() const noexcept;
	void Apply(Camera3D &camera) const noexcept;

	/// Souris d'une vue : glissé gauche = rotation, glissé droit ou milieu = déplacement,
	/// molette = zoom. Un glissé commence au-dessus de la vue (`over`) et se poursuit dehors
	/// tant que le bouton reste enfoncé. Rend vrai si la caméra a bougé.
	bool HandleMouse(float x, float y, bool left, bool pan, float wheel, bool over) noexcept;
	[[nodiscard]] bool Dragging() const noexcept { return m_drag != 0; }

private:
	int m_drag = 0; // 0 : aucun ; 1 : rotation ; 2 : déplacement
	float m_lastX = 0.f, m_lastY = 0.f;
};

using TextureId = uint32_t; // 0 : texture blanche (aucune)

struct Material {
	LinearColor color{0.8f, 0.8f, 0.8f};
	float opacity = 1.f; // < 1 : transparent (trié, sans écriture de profondeur)
	float metallic = 0.f;
	float roughness = 0.5f;
	LinearColor emissive{}; // multiplié par la texture et la teinte de l'instance
	TextureId texture = 0;			// multipliée par `color`
	math::FVector2 uvScale{1.f, 1.f}; // répétition de la texture
	bool unlit = false;				// couleur telle quelle, sans lumière
	bool doubleSided = false;
	bool castShadow = true;
	bool receiveShadow = true;
	/// Mélange additif (ONE, ONE) pour les transparents : rayons, bulles, reflets.
	bool additive = false;

	// ── Effets (milieu sous-marin, végétation, animaux) ─────────────────────
	/// Ondulation au sommet, calculée sur le GPU (le matériau ne change pas d'une image à l'autre).
	enum class Sway : uint8_t {
		None,  ///< immobile
		Plant, ///< courbure croissante vers le haut (varech, algues) ; `swayHeight` = sommet local
		Swim,  ///< la queue (−x local) ondule de côté (poissons)
		Flap,  ///< les pointes (|z| local) battent verticalement (raies)
		/// Marche à pattes : au-delà de |z| = `swayHeight` (bord du corps), les pattes se lèvent
		/// et avancent en alternance (tripode : phase selon x et le côté) — crabes, homards.
		Walk,
		/// Nage de crevette : l'abdomen (x < 0, longueur `swayHeight`) fouette verticalement et
		/// les pattes nageuses sous le corps (y < 0) battent d'avant en arrière.
		Paddle,
	};
	Sway sway = Sway::None;
	float swayAmplitude = 0.f;	///< mètres (plante) ou unités locales (nage, battement)
	float swayFrequency = 1.f;	///< radians par seconde
	float swayHeight = 1.f;		///< hauteur locale du sommet (plante), bord du corps (marche), longueur de l'abdomen (crevette)
	bool caustics = true;		///< reçoit les caustiques (si `Lighting::water.caustics` > 0)
	bool triplanar = false;		///< texture projetée selon les axes du monde (falaises sans étirement)
	/// Avec `triplanar` : la texture est un atlas, moitié gauche sur le dessus (sable), moitié
	/// droite sur les parois (roche) — un seul matériau pour un terrain.
	bool terrainAtlas = false;
	float translucency = 0.f;	///< lumière transmise par derrière (feuilles fines)
	bool waterSurface = false;	///< surface de l'eau vue de dessous (vagues, fenêtre de Snell)
	bool edgeFade = false;		///< s'efface vu par la tranche (faisceaux en plans croisés)
	bool rim = false;			///< seul le contour brille (bulles : cœur transparent)
	/// > 0 : disparaît progressivement (tramage) à moins de cette distance de la caméra —
	/// le feuillage qui frôle l'objectif ne masque plus l'image ni n'est coupé net.
	float nearFade = 0.f;
	/// Terrain (`terrainAtlas`) : couleur du couvert végétal, dont la proportion est portée par
	/// la coordonnée de texture u de chaque sommet (0 : sable ou roche nus, 1 : tapis).
	LinearColor groundCover{};

	[[nodiscard]] static Material Plastic(const LinearColor &c, float roughness = 0.45f) noexcept;
	[[nodiscard]] static Material Metal(const LinearColor &c, float roughness = 0.25f) noexcept;
	[[nodiscard]] static Material Pbr(const LinearColor &c, float metallic, float roughness) noexcept;
	[[nodiscard]] static Material Unlit(const LinearColor &c) noexcept;
	[[nodiscard]] static Material Glass(const LinearColor &c, float opacity = 0.35f) noexcept;
	[[nodiscard]] static Material Emissive(const LinearColor &c, float strength = 4.f) noexcept;
};

struct DirectionalLight {
	math::FVector3 direction{-0.4f, -1.f, -0.3f}; // sens de propagation (du soleil vers la scène)
	LinearColor color{1.f, 0.97f, 0.92f};
	float intensity = 2.5f;
	bool castShadow = true;
	math::FVector3 shadowCenter{}; // centre de la zone ombrée
	float shadowExtent = 12.f;	   // demi-côté de la zone ombrée (unités du monde)
};

struct PointLight {
	math::FVector3 position{};
	LinearColor color{1.f, 1.f, 1.f};
	float intensity = 10.f;
	float range = 12.f; // atténuation nulle au-delà
};

struct SpotLight {
	math::FVector3 position{0.f, 5.f, 0.f};
	math::FVector3 direction{0.f, -1.f, 0.f}; // sens du faisceau
	LinearColor color{1.f, 1.f, 1.f};
	float intensity = 20.f;
	float range = 20.f;
	float angle = 0.5f;	   // demi-angle extérieur (radians)
	float penumbra = 0.3f; // part adoucie du cône, 0..1
};

/// Environnement analytique : fond du ciel, éclairage diffus et réflexions.
struct Environment {
	LinearColor zenith = Srgb(0x3d6fb6);
	LinearColor horizon = Srgb(0xb9cde0);
	LinearColor ground = Srgb(0x4a4640);
	float intensity = 0.6f; // part de l'environnement dans l'éclairage
	bool drawSky = true;	// sinon fond uni `background`
	bool showSun = true;	// disque solaire dans le ciel
	LinearColor background = Srgb(0x14161c);
	LinearColor fogColor = Srgb(0xb9cde0);
	float fogDensity = 0.f; // 0 : sans brouillard
};

inline constexpr size_t kMaxPointLights = 8;
inline constexpr size_t kMaxSpotLights = 4;

/// Milieu sous-marin : lumière absorbée avec la profondeur (le rouge d'abord), brouillard
/// coloré par la direction du regard (identique au fond : aucun raccord), caustiques animées.
struct Water {
	bool enabled = false;
	float surfaceY = 0.f;	   ///< altitude de la surface
	float absorption = 0.02f;  ///< atténuation par mètre de la lumière venue de la surface
	float caustics = 0.f;	   ///< force des caustiques (0 : sans)
	float causticsScale = 0.12f; ///< fréquence spatiale (cellules par mètre)
	float depthFog = 1.f;	   ///< assombrissement du fond selon la profondeur de la caméra
};

/// Réglages du post-traitement (HDR → mappage de tons, bloom, rayons).
/// L'exposition est celle de `Lighting::exposure`.
/// Anticrénelage temporel (TAA). La projection est décalée d'une fraction de pixel à chaque
/// image (séquence de Halton 2,3) ; chaque pixel retrouve sa position dans l'image précédente
/// (profondeur + matrices vue-projection) et y mélange l'historique accumulé, borné par les
/// couleurs voisines de l'image courante (pas de traînées). Lisse le fourmillement des détails
/// plus fins qu'un pixel (feuilles, tiges, particules lointaines). Les objets animés sans
/// vecteur de mouvement (ondulation, poissons) se fient au bornage : un peu de flou en mouvement.
/// L'historique est propre à chaque `RenderTarget3D` ; `RenderTarget3D::ResetHistory()` après
/// une coupure de caméra. Le FXAA devient en général inutile (`Post::fxaa`).
struct TemporalAA {
	bool enabled = false;
	float feedback = 0.9f;	///< part de l'historique dans le mélange (0..0,98 ; plus haut : plus lisse, plus de flou)
	float jitter = 1.f;		///< amplitude du décalage sous-pixel (1 : un pixel complet ; 0 : aucun)
	float clipGamma = 1.f;	///< demi-largeur de la boîte de couleurs admises, en écarts-types du voisinage
	float sharpness = 0.2f; ///< accentuation de l'image courante (compense le flou du filtre ; 0 : aucune)
};

/// Décalage sous-pixel de l'image `frame` (−0,5..0,5 pixel) : Halton (2, 3) sur 8 images.
[[nodiscard]] math::FVector2 TemporalJitter(uint32_t frame) noexcept;
/// Matrice à appliquer après la vue-projection pour décaler l'image de `pixels` dans une cible
/// `width`×`height` (translation en espace de découpage, proportionnelle à w).
[[nodiscard]] math::FMatrix4 JitterMatrix(const math::FVector2 &pixels, int width, int height) noexcept;

struct Post {
	float bloomStrength = 0.35f; ///< intensité du bloom (0 : désactivé)
	float bloomThreshold = 1.0f; ///< seuil linéaire au-delà duquel la lumière fleurit
	float raysStrength = 0.55f;	 ///< intensité des rayons de lumière (0 : désactivé)
	float saturation = 1.1f;	 ///< saturation finale
	float vignette = 0.35f;		 ///< vignettage (0 : désactivé)
	float gamma = 1.f / 2.2f;	 ///< correction gamma
	bool fxaa = true;			 ///< anticrénelage (bords fins : feuilles, particules)
	bool dither = true;			 ///< tramage final (pas de bandes dans les dégradés sombres)
	TemporalAA taa;				 ///< anticrénelage temporel (désactivé par défaut)
};

struct Lighting {
	DirectionalLight sun;
	std::vector<PointLight> points; // au plus kMaxPointLights utilisées
	std::vector<SpotLight> spots;	// au plus kMaxSpotLights utilisées
	Environment environment;
	Water water;
	Post post;
	float exposure = 1.f;
	float time = 0.f; ///< secondes : animation des ondulations, caustiques, vagues
};

/// Matrice vue-projection orthographique du soleil couvrant sa zone d'ombre.
[[nodiscard]] math::FMatrix4 SunViewProjection(const DirectionalLight &sun) noexcept;

} // namespace sdl3
