#pragma once
/**
 * @file layout.hpp
 * @brief Placement dynamique des widgets : ancres, découpes, rangées flexibles, grilles,
 *        piles ancrées et échelle adaptée à la taille de l'écran.
 *
 * Fonctions pures (aucun état global) : le HUD et les menus calculent leurs rectangles à
 * chaque image à partir de la taille courante de la fenêtre, si bien qu'ils restent bien
 * placés à toutes les résolutions et au redimensionnement.
 *
 * @code
 * const float s = ui::FitScale(c.Width(), c.Height());                     // 1 à 1280×720
 * const sdl3::FRect screen{0, 0, c.Width(), c.Height()};
 * const sdl3::FRect hotbar = ui::Place(screen, 9 * 44 * s, 44 * s, ui::Anchor::Bottom, 0, 10 * s);
 * ui::AnchorStack above(hotbar, ui::Anchor::Top, 4 * s);                    // empile vers le haut
 * const sdl3::FRect hearts = above.Push(hotbar.w * 0.5f, 18 * s, ui::Align::Start);
 *
 * sdl3::FRect body = ui::Place(screen, 760 * s, 520 * s, ui::Anchor::Center); // boîte de dialogue
 * const sdl3::FRect title = ui::CutTop(body, 32 * s, 8 * s);                   // bande retirée de body
 * const ui::GridLayout grid = ui::Grid(body, 36, 40 * s, 4 * s);            // 36 cases
 * @endcode
 */
#include <span>
#include <vector>

#include <sdl3/render/canvas.hpp>

namespace ui {

enum class Anchor : uint8_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
enum class Align : uint8_t { Start, Center, End, Stretch };

/// Rectangle `w`×`h` placé contre l'ancre de `parent`, décalé vers l'intérieur de
/// (`marginX`, `marginY`) ; les dimensions sont bornées à celles du parent.
[[nodiscard]] sdl3::FRect Place(const sdl3::FRect &parent, float w, float h, Anchor anchor, float marginX = 0.f, float marginY = 0.f) noexcept;

/// Échelle d'interface : rapport à la résolution de référence (le plus petit des deux axes),
/// bornée à [`minScale`, `maxScale`].
[[nodiscard]] float FitScale(float width, float height, float refWidth = 1280.f, float refHeight = 720.f, float minScale = 0.6f,
							 float maxScale = 2.f) noexcept;

/// Plus grand rectangle de rapport `aspect` (largeur / hauteur) centré dans `parent`.
[[nodiscard]] sdl3::FRect FitAspect(const sdl3::FRect &parent, float aspect) noexcept;

// ── Découpes : retirent une bande de `r` et la renvoient ───────────────────
sdl3::FRect CutTop(sdl3::FRect &r, float h, float gap = 0.f) noexcept;
sdl3::FRect CutBottom(sdl3::FRect &r, float h, float gap = 0.f) noexcept;
sdl3::FRect CutLeft(sdl3::FRect &r, float w, float gap = 0.f) noexcept;
sdl3::FRect CutRight(sdl3::FRect &r, float w, float gap = 0.f) noexcept;

/// Élément d'une rangée : taille fixe `size` + part `grow` de l'espace restant, bornée.
struct FlexItem {
	float size = 0.f;
	float grow = 0.f;
	float min = 0.f, max = 1e9f;
};

/// Rangée (horizontale) ou colonne : tailles fixes puis répartition de l'espace restant
/// selon `grow` ; s'il n'y a rien à faire grandir, le bloc est aligné selon `justify`.
/// Si la place manque, les éléments rétrécissent proportionnellement jusqu'à `min`.
/// La dimension transverse reprend celle de `r`.
[[nodiscard]] std::vector<sdl3::FRect> Row(const sdl3::FRect &r, std::span<const FlexItem> items, float gap = 0.f, Align justify = Align::Start);
[[nodiscard]] std::vector<sdl3::FRect> Column(const sdl3::FRect &r, std::span<const FlexItem> items, float gap = 0.f,
											Align justify = Align::Start);

/// Grille de cases carrées de taille `cell` : nombre de colonnes déduit de la largeur
/// (ou imposé par `columns` > 0), bloc aligné horizontalement selon `align`.
struct GridLayout {
	sdl3::FRect bounds; // emprise totale des cases
	int columns = 1, rows = 0;
	float cell = 0.f, gap = 0.f;
	[[nodiscard]] sdl3::FRect Cell(int index) const noexcept;
	/// Indice de la case sous (x, y), −1 sinon.
	[[nodiscard]] int HitTest(float x, float y, int count) const noexcept;
};
[[nodiscard]] GridLayout Grid(const sdl3::FRect &r, int count, float cell, float gap, int columns = 0, Align align = Align::Center) noexcept;

/// Pile d'éléments ancrée sur un bord : chaque `Push` place l'élément suivant à côté du
/// précédent (vers le haut pour `Anchor::Top`, etc.) sans chevauchement, aligné selon `align`.
class AnchorStack {
public:
	/// `from` : rectangle de départ ; la pile grandit depuis son bord `side`
	/// (Top : au-dessus, Bottom : en dessous, Left/Right : à gauche/droite).
	AnchorStack(const sdl3::FRect &from, Anchor side, float gap = 0.f) noexcept : m_edge(from), m_side(side), m_gap(gap) {}
	sdl3::FRect Push(float w, float h, Align align = Align::Center) noexcept;
	/// Emprise occupée par les éléments empilés.
	[[nodiscard]] const sdl3::FRect &Used() const noexcept { return m_used; }

private:
	sdl3::FRect m_edge;
	Anchor m_side;
	float m_gap;
	sdl3::FRect m_used{};
	bool m_any = false;
};

/// Garde `r` dans `bounds` (le déplace sans le redimensionner si possible).
[[nodiscard]] sdl3::FRect KeepInside(const sdl3::FRect &r, const sdl3::FRect &bounds) noexcept;

/// Piste d'un curseur horizontal : rentrée du rayon du bouton à gauche et à droite, pour
/// que le bouton reste entier (jamais rogné) aux deux extrémités. Sert au dessin et à la
/// conversion position de la souris → valeur, qui restent ainsi cohérents.
struct SliderTrack {
	float x0 = 0.f, width = 1.f; ///< début et longueur de la course du centre du bouton
	float radius = 0.f;			 ///< rayon du bouton
	/// Abscisse du centre du bouton pour la fraction `t` ∈ [0, 1].
	[[nodiscard]] float Position(float t) const noexcept { return x0 + width * t; }
	/// Fraction ∈ [0, 1] correspondant à l'abscisse `x` (bornée aux extrémités).
	[[nodiscard]] float Fraction(float x) const noexcept;
};
[[nodiscard]] SliderTrack MakeSliderTrack(const sdl3::FRect &area, float thumbRadius) noexcept;

} // namespace ui
