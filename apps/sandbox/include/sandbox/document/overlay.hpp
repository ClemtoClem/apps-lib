#pragma once
/**
 * @file overlay.hpp
 * @brief Calque 2D du document : éléments nommés (rectangle, cercle, ligne, texte, image)
 *        dessinés par-dessus la vue du mode Jeu — HUD, menus, jauges, messages.
 *
 * Repère : l'écran de RÉFÉRENCE (1280×720 par défaut), origine en haut à gauche, y vers le bas,
 * en pixels de référence. Il est mis à l'échelle dans la vue réelle en gardant ses proportions
 * (`OverlayView::Fit`, bandes sur les côtés si besoin) : un HUD se place une fois pour toutes
 * les tailles de fenêtre.
 *
 * Partie pure (ni fenêtre ni GPU) : géométrie, sélection au point, couleurs ; le dessin vit dans
 * l'application (`app/overlay_view.cpp`). Les scripts du jeu modifient une copie du calque
 * (`hud.*`) et dessinent des éléments éphémères (`draw2d.*`).
 */
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <core/core.hpp>
#include <math/math.hpp>

#include "sandbox/modeling/model.hpp"

namespace sandbox {

enum class Shape2D : uint8_t { Rect, Circle, Line, Text, Image };

struct Item2D {
	uint32_t id = 0;
	String name;
	Shape2D shape = Shape2D::Rect;
	/// Coin haut-gauche (rectangle, image, texte), centre (cercle) ou départ (ligne).
	math::FVector2 position{40.f, 40.f};
	/// Largeur × hauteur (rectangle, image), diamètre en x (cercle), vecteur jusqu'à l'arrivée (ligne).
	math::FVector2 size{200.f, 60.f};
	modeling::Color fill{0.08f, 0.09f, 0.12f, 0.75f};
	modeling::Color stroke{1.f, 1.f, 1.f, 0.f}; ///< alpha nul : sans contour
	float strokeWidth = 2.f;
	float cornerRadius = 8.f;
	String text;		  ///< texte (Text)
	float fontSize = 24.f;
	String image;		  ///< fichier image relatif au document (Image)
	int z = 0;			  ///< ordre de dessin (croissant), puis ordre de la liste
	bool visible = true;

	[[nodiscard]] bool operator==(const Item2D &) const = default;
};

/// Mesure d'un texte (largeur) à une taille donnée ; estimée si absente.
using MeasureText2D = float (*)(std::string_view text, float size, void *user);

/// Calque de tuiles : grille de terrains colorés, posée sur l'écran de référence.
struct TilemapLayer {
	math::FVector2 origin{0.f, 0.f}; ///< coin haut-gauche (pixels de référence)
	float tileSize = 32.f;			 ///< côté d'une tuile (pixels de référence)
	int width = 0, height = 0;
	std::vector<int8_t> terrain;		   ///< terrain par cellule, ligne par ligne (0 = vide)
	std::vector<modeling::Color> palette; ///< couleur par terrain (index 0 = inutilisé)

	[[nodiscard]] bool Empty() const noexcept { return terrain.empty() || palette.empty(); }
	[[nodiscard]] int8_t At(int x, int y) const noexcept {
		return x >= 0 && y >= 0 && x < width && y < height ? terrain[size_t(y) * size_t(width) + size_t(x)] : 0;
	}
	void Set(int x, int y, int8_t t) {
		if (x >= 0 && y >= 0 && x < width && y < height)
			terrain[size_t(y) * size_t(width) + size_t(x)] = t;
	}
	[[nodiscard]] modeling::Color ColorOf(int8_t t) const noexcept {
		return size_t(t) < palette.size() ? palette[size_t(t)] : modeling::Color{};
	}
};

struct Layer2D {
	math::FVector2 reference{1280.f, 720.f};
	std::vector<Item2D> items;
	TilemapLayer tilemap; ///< calque de tuiles (vide tant qu'aucune tuile n'est posée)

	[[nodiscard]] Item2D *Find(uint32_t id) noexcept;
	[[nodiscard]] const Item2D *Find(uint32_t id) const noexcept;
	[[nodiscard]] Item2D *FindByName(std::string_view name) noexcept;
	/// Nouvel élément (identifiant et nom uniques) ; référence valable jusqu'au prochain ajout.
	Item2D &Add(Shape2D shape, const math::FVector2 &position);
	bool Remove(uint32_t id);
	/// Éléments dans l'ordre de dessin (z croissant, puis ordre de la liste).
	[[nodiscard]] std::vector<const Item2D *> DrawOrder() const;
	[[nodiscard]] bool operator==(const Layer2D &) const = default;
};

/// Boîte d'un élément dans le repère de référence (texte : largeur estimée ou mesurée).
struct Box2D {
	math::FVector2 min, max;
	[[nodiscard]] bool Contains(const math::FVector2 &p, float slack = 0.f) const noexcept {
		return p.x >= min.x - slack && p.x <= max.x + slack && p.y >= min.y - slack && p.y <= max.y + slack;
	}
};
[[nodiscard]] Box2D BoundsOf(const Item2D &item, MeasureText2D measure = nullptr, void *user = nullptr);
/// Élément visible le plus en avant sous `p` (repère de référence) ; 0 sinon.
[[nodiscard]] uint32_t Pick(const Layer2D &layer, const math::FVector2 &p, float slack = 4.f, MeasureText2D measure = nullptr, void *user = nullptr);

/// Passage de l'écran de référence à la vue réelle (échelle uniforme, centré).
struct OverlayView {
	float scale = 1.f;
	math::FVector2 origin; ///< coin haut-gauche de l'écran de référence dans la vue
	[[nodiscard]] static OverlayView Fit(float x, float y, float w, float h, const math::FVector2 &reference) noexcept;
	[[nodiscard]] math::FVector2 ToScreen(const math::FVector2 &p) const noexcept { return origin + p * scale; }
	[[nodiscard]] math::FVector2 ToReference(const math::FVector2 &p) const noexcept { return (p - origin) / scale; }
};

/// Couleur « #rgb », « #rrggbb » ou « #rrggbbaa » (scripts, champs) ; vide si invalide.
[[nodiscard]] std::optional<modeling::Color> ParseColor(std::string_view text);
[[nodiscard]] String FormatColor(const modeling::Color &c);

} // namespace sandbox
