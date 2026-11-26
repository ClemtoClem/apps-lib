#pragma once
/**
 * @file dock.hpp
 * @brief Disposition ancrable (docking) du Studio : quatre régions
 *        gauche / centre / droite / bas, redimensionnables par des séparateurs
 *        (bordures doubles), masquables, et portant chacune une barre d'onglets
 *        dont les onglets se déplacent entre régions par glisser-déposer.
 *
 * Usage :
 * @code
 *   ui::DockLayout dock;
 *   dock.SetTabs(ui::DockLayout::Left, {{"outliner", "Outliner", icons::Codepoint(icons::MaterialIcons::LIST)}});
 *   dock.SetTabs(ui::DockLayout::Center, {{"viewport", "Vue 3D"}, {"script", "Script"}});
 *   ...
 *   dock.Draw(ctx, area, [](ui::DockLayout::Side s, const sdl3::FRect &r, std::string_view tab) {
 *       // dessine le contenu de l'onglet actif de la région `s` dans `r`
 *   });
 * @endcode
 */
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdl3/render/canvas.hpp>

namespace ui {

class Context;

/// Onglet d'un dock : identifiant stable + libellé + icône facultative.
struct DockTab {
	std::string id;
	std::string title;
	char32_t icon = 0;
	bool dirty = false;		///< non enregistré : teinte d'avertissement (thème)
	bool closable = true; ///< bouton de fermeture à droite de l'onglet
};

/// État d'une région (sérialisable : identifiants d'onglets seulement).
struct DockRegionState {
	std::vector<std::string> tabs;
	std::string active;
	bool visible = true;
	float size = 280.f;
};

/// Disposition complète d'un dock — un « espace de travail » du Studio.
struct DockState {
	std::array<DockRegionState, 4> regions; // gauche, centre, droite, bas
};

class DockLayout {
public:
	enum class Side : uint8_t { Left = 0, Center = 1, Right = 2, Bottom = 3, Count = 4 };

	/// Disposition courante (onglets, onglet actif, visibilité, taille de chaque région).
	[[nodiscard]] DockState Capture() const;
	/// Applique `state` ; `catalog` résout chaque identifiant en onglet (titre, icône).
	/// Les identifiants inconnus du catalogue sont ignorés.
	void Apply(const DockState &state, std::span<const DockTab> catalog);
	/// Région qui contient l'onglet `id`, ou `Side::Count`.
	[[nodiscard]] Side Find(std::string_view id) const;

	/// Remplace la liste d'onglets d'une région.
	void SetTabs(Side side, std::vector<DockTab> tabs);
	[[nodiscard]] const std::vector<DockTab> &Tabs(Side side) const;
	/// Active un onglet (s'il existe dans la région).
	void Activate(Side side, std::string_view id);
	/// Identifiant de l'onglet actif, ou "" si la région n'a aucun onglet.
	[[nodiscard]] std::string_view Active(Side side) const;
	void Show(Side side, bool visible = true);
	[[nodiscard]] bool Visible(Side side) const;
	/// Largeur (Left/Right) ou hauteur (Bottom) par défaut de la région.
	void SetSize(Side side, float size);
	[[nodiscard]] float Size(Side side) const;

	/// Dessine la disposition dans `area`, puis appelle `content` pour chaque
	/// région visible : `content(side, rect, activeTabId)`. `rect` est la zone
	/// utile de la région (sous sa barre d'onglets).
	void Draw(Context &ctx, const sdl3::FRect &area, std::function<void(Side, const sdl3::FRect &, std::string_view)> content);

	/// Identifiant de l'onglet dont le bouton « fermer » a été cliqué pendant le dernier
	/// `Draw` (vide sinon). À traiter après le dessin (l'appelant peut alors retirer l'onglet).
	[[nodiscard]] std::string TakeCloseRequest() { return std::exchange(m_closeRequested, {}); }

private:
	struct Region {
		std::vector<DockTab> tabs;
		int active = -1;
		bool visible = true;
		float size = 280.f;
	};
	[[nodiscard]] Region &RegionOf(Side side) noexcept { return m_regions[uint8_t(side)]; }
	[[nodiscard]] const Region &RegionOf(Side side) const noexcept { return m_regions[uint8_t(side)]; }

	Region m_regions[4];
	int m_dragSplitter = -1;  // 0 left|center, 1 center|right, 2 center|bottom
	Side m_dragFrom = Side::Left; // région source d'un onglet en cours de glissement
	int m_dragIndex = -1;	 // index de l'onglet glissé (-1 = aucun)
	std::string m_closeRequested; // onglet à fermer (demandé au dernier Draw)
};

} // namespace ui
