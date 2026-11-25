#pragma once
/**
 * @file rulers.hpp
 * @brief Règles de la vue du document : graduations en centimètres, aimantation et affichage des
 *        longueurs, dessin d'une règle (horizontale ou verticale) avec ses repères : marges de
 *        la page, bords des colonnes ou des lignes du tableau courant.
 */
#include <string>
#include <vector>

#include <sdl3/render/canvas.hpp>

namespace scribe {

inline constexpr double kPointsPerCm = 72.0 / 2.54;
inline constexpr float kRulerThickness = 22.f; ///< pixels

/// Graduation : position (points depuis l'origine de la page), niveau (0 : millimètre, 1 : demi-
/// centimètre, 2 : centimètre) et numéro du centimètre (niveau 2).
struct RulerMark {
	double position = 0;
	int level = 0;
	int centimeter = 0;
};

/// Graduations de 0 à `length` points ; les millimètres sont omis si leur pas fait moins de
/// `minPixels` pixels à l'échelle `scale` (pixels par point), les demi-centimètres aussi si besoin.
[[nodiscard]] std::vector<RulerMark> RulerMarks(double length, double scale, double minPixels = 4);
/// Longueur arrondie au demi-millimètre (au dixième de millimètre si `fine`), ≥ 0.
[[nodiscard]] double SnapLength(double points, bool fine);
/// « 2,54 cm » (deux décimales).
[[nodiscard]] std::string FormatLength(double points);

/// Repère d'une règle : marge (début / fin), bord de colonne / ligne de tableau, ou espace avant /
/// après le paragraphe du curseur (règle verticale : haut du paragraphe, fin de son espace après).
struct RulerHandle {
	enum Kind : uint8_t { MarginStart, MarginEnd, TableBorder, SpaceBefore, SpaceAfter } kind = MarginStart;
	double position = 0; ///< points depuis l'origine de la page
	size_t index = 0;	 ///< bord du tableau
};

/// Nouvel espace avant d'un paragraphe dont le haut (avec l'espace actuel `current`) est à `top`,
/// quand son repère est amené à `position` ; aimanté, ≥ 0.
[[nodiscard]] double DraggedSpaceBefore(double top, double current, double position, bool fine);
/// Nouvel espace après d'un paragraphe dont le bas est à `bottom`, repère amené à `position`.
[[nodiscard]] double DraggedSpaceAfter(double bottom, double position, bool fine);

/// Dessine une règle dans `area` (pixels) : `origin` = pixel de l'origine de la page sur l'axe,
/// `scale` = pixels par point, `length` = longueur de la page (points), zones hors marges
/// assombries, repères ; `active` : indice du repère survolé ou glissé (−1 : aucun).
void DrawRuler(sdl3::Canvas &canvas, const sdl3::FRect &area, bool horizontal, float origin, float scale, double length, double marginStart,
			   double marginEnd, const std::vector<RulerHandle> &handles, int active);

/// Indice du repère proche de `pixel` sur l'axe (± `tolerance` pixels), −1 sinon.
[[nodiscard]] int HandleAt(const std::vector<RulerHandle> &handles, float origin, float scale, float pixel, float tolerance);

} // namespace scribe
