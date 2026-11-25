#pragma once
/**
 * @file config.hpp
 * @brief Constantes du monde d'Abyssale et niveaux de qualité.
 *
 * Le monde est un carré de 2 × kHalfExtent mètres, fermé par des falaises qui montent
 * jusque sous la surface : la zone de nage (rayon kSwimRadius) n'en voit jamais le bord,
 * noyé dans le brouillard et caché par les parois des canyons.
 */
#include <array>
#include <string_view>

namespace abyssale {

inline constexpr float kTau = 6.2831853f;
inline constexpr float kHalfExtent = 150.f; ///< demi-côté d'un chunk (m)
inline constexpr float kChunkSize = 2.f * kHalfExtent; ///< côté d'un chunk (m) : la carte actuelle = un chunk
inline constexpr float kSwimRadius = 98.f;	///< rayon de la zone de nage de la carte d'entrée (m)
inline constexpr float kSurfaceY = 0.f;		///< surface de l'eau
inline constexpr float kCeilingY = -1.5f;	///< le nageur reste sous la surface

/// Nombre d'éléments par niveau de qualité.
struct Quality {
	int kelp;	   ///< plants de varech
	int fronds;	   ///< algues violettes / dorées
	int tufts;	   ///< touffes du tapis magenta
	int rocks;	   ///< blocs posés
	int sponges;   ///< éponges (tonneaux et tubes)
	int corals;	   ///< coraux branchus
	int seagrass;  ///< touffes d'herbier (en taches de 3 à 6)
	int benthos;   ///< petits invertébrés fixés : coraux-fouets, champignons, pennatules, serpules, balanes
	int fish;	   ///< poissons (tous bancs confondus)
	int motes;	   ///< particules en suspension
	int glints;	   ///< reflets scintillants du varech
	int bubbles;   ///< bulles
	int shafts;	   ///< faisceaux de lumière
	bool shadows;  ///< ombre du soleil
};

inline constexpr std::array<Quality, 3> kQualities = {{
	{150, 120, 9000, 140, 220, 260, 600, 240, 520, 1400, 900, 220, 34, true}, // Élevée
	{100, 80, 5500, 100, 150, 170, 380, 160, 340, 900, 600, 150, 24, true},	  // Moyenne
	{60, 50, 1600, 70, 90, 100, 140, 70, 200, 300, 220, 80, 14, false},		  // Basse
}};
inline constexpr std::array<std::string_view, 3> kQualityNames = {"Élevée", "Moyenne", "Basse"};

/// Qualité ramenée à une fraction `factor` (un chunk peuplé de quelques chunks voisins
/// doit rester plus léger que le monde d'origine, qui était un unique chunk).
[[nodiscard]] inline Quality ScaleQuality(const Quality &q, float factor) {
	Quality out = q;
	out.kelp = int(float(q.kelp) * factor);
	out.fronds = int(float(q.fronds) * factor);
	out.tufts = int(float(q.tufts) * factor);
	out.rocks = int(float(q.rocks) * factor);
	out.sponges = int(float(q.sponges) * factor);
	out.corals = int(float(q.corals) * factor);
	out.seagrass = int(float(q.seagrass) * factor);
	out.benthos = int(float(q.benthos) * factor);
	out.fish = int(float(q.fish) * factor);
	out.bubbles = int(float(q.bubbles) * factor);
	return out;
}

} // namespace abyssale
