#pragma once
/**
 * @file theme.hpp
 * @brief Thèmes de couleur de Scribe : thème des widgets (`ui::Theme`, bibliothèque app) et
 *        palette des surfaces propres à l'application (espace de travail, panneaux, sélection,
 *        guides, fenêtres surgissantes), feuille de style de l'interface retenue.
 */
#include <span>
#include <string>
#include <string_view>

#include <ui/ui.hpp>

namespace scribe {

/// Couleurs des surfaces de l'application (le papier des pages reste blanc).
struct Palette {
	ui::Theme ui;			 ///< widgets, barre de titre, notifications
	ui::Color workspace;	 ///< fond autour des pages
	ui::Color panel;		 ///< panneaux latéraux, accueil
	ui::Color sidebar;		 ///< colonne de l'accueil
	ui::Color card;			 ///< vignettes des modèles
	ui::Color text, strong;	 ///< texte courant, texte mis en avant
	ui::Color muted, faint;	 ///< texte secondaire, indications
	ui::Color border;		 ///< filets, séparateurs, bords des nuanciers
	ui::Color hover;		 ///< voile de survol
	ui::Color selection;	 ///< sélection, poignées, repères (avec opacité selon l'usage)
	ui::Color accentSoft;	 ///< icônes actives
	ui::Color popup, popupBorder;
	ui::Color guide;		 ///< guides et cotes pendant un glisser
	ui::Color rulerFrame;	 ///< coin et cadre des règles
	ui::Color caption;		 ///< icônes des légendes (panneau Plan)
	ui::Color status, statusText;
};

struct ThemeInfo {
	std::string_view key;  ///< identifiant enregistré dans les réglages
	std::string_view name; ///< nom affiché (langue des sources : traduit par l'interface)
	Palette (*make)();
};

/// Thèmes proposés : Nuit (défaut), Clair, Contraste élevé.
[[nodiscard]] std::span<const ThemeInfo> Themes() noexcept;
/// Indice du thème `key` (0 si inconnu).
[[nodiscard]] size_t ThemeIndex(std::string_view key) noexcept;

/// Palette du thème actif (Nuit tant qu'aucun n'est choisi).
[[nodiscard]] const Palette &P() noexcept;
void SetPalette(const Palette &palette);

/// Feuille de style de l'interface retenue (ruban, barre d'état) pour une palette.
[[nodiscard]] std::string StyleSheetFor(const Palette &palette);

} // namespace scribe
