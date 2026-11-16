#pragma once
/**
 * @file window_frame.hpp
 * @brief Encadrement de fenêtre dessiné par l'application : barre de titre avec icône,
 *        titre, boutons personnalisés (à gauche et à droite) et boutons réduire /
 *        agrandir / fermer — pour une fenêtre SDL sans décoration du système.
 *
 * Trois niveaux, du plus pur au plus concret :
 *  - `ComputeFrameLayout` et `HitTestFrame` : géométrie et zones (déplacement,
 *    redimensionnement), sans dessin ni SDL — testables ;
 *  - `WindowFrame` : dessine la barre avec un `ui::Context` et rapporte l'action demandée ;
 *  - `WindowChrome` : relie le tout à une `SDL_Window` (zones de saisie du système via
 *    `SDL_SetWindowHitTest`, réduire / agrandir / restaurer / fermer).
 *
 * @code
 * SDL_Window *window = SDL_CreateWindow("App", 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
 * ui::WindowChrome chrome(window);
 * // … à chaque image :
 * const ui::FrameButton tools[] = {{.id = "open", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), .tooltip = "Ouvrir"},
 *                                  {.id = "help", .label = "Aide"}};
 * ui::WindowFrameOptions frame;
 * frame.title = "App";
 * frame.left = tools;
 * frame.maximized = chrome.Maximized();
 * const ui::WindowFrameResult result = ui::WindowFrame(u, {0, 0, canvas.Width(), canvas.Height()}, frame);
 * chrome.Update(result.layout, canvas.Scale());
 * if (result.action == ui::FrameAction::Custom && result.custom == "open") { … }
 * if (chrome.Apply(result.action)) running = false;     // vrai : fermeture demandée
 * DessinerContenu(result.layout.content);
 * @endcode
 */
#include <span>
#include <string_view>
#include <vector>

#include "ui.hpp"

struct SDL_Window;

namespace ui {

/// Bouton personnalisé de la barre : icône, texte, ou les deux.
struct FrameButton {
	std::string_view id{};	  ///< rapporté dans `WindowFrameResult::custom`
	char32_t icon = 0;		  ///< 0 : pas d'icône
	std::string_view label{};  ///< vide : bouton d'icône seule
	std::string_view tooltip{};
	bool enabled = true;
	bool active = false;	  ///< état enfoncé (bascule)
	Color tint{0, 0, 0, 0};	  ///< couleur de l'icône (alpha nul : couleur du thème)
	bool separator = false;	  ///< trait de séparation avant ce bouton
};

struct WindowFrameOptions {
	std::string_view title;
	float height = 40.f;
	char32_t icon = 0;				 ///< icône de l'application (police d'icônes)
	sdl3::Texture *image = nullptr;	 ///< ou image de l'application (prioritaire sur `icon`)
	Color iconColor{0, 0, 0, 0};
	std::span<const FrameButton> left;	///< après l'icône
	std::span<const FrameButton> right; ///< avant les boutons de la fenêtre
	bool maximized = false;
	bool focused = true;
	bool minimizeButton = true, maximizeButton = true, closeButton = true;
};

/// Rectangles de la barre de titre (coordonnées logiques du canvas).
struct FrameLayout {
	Rect window;	///< fenêtre entière
	Rect bar;		///< barre de titre
	Rect content;	///< zone de l'application, sous la barre
	Rect icon;		///< vide (w = 0) s'il n'y a pas d'icône
	Rect title;		///< zone libre où le titre est centré
	std::vector<Rect> left, right;
	Rect minimize, maximize, close; ///< vides si le bouton est absent
};

/// Géométrie de la barre. `leftWidths`/`rightWidths` : largeur de chaque bouton personnalisé.
[[nodiscard]] FrameLayout ComputeFrameLayout(const Rect &window, float height, std::span<const float> leftWidths,
											 std::span<const float> rightWidths, bool hasIcon, bool minimizeButton = true,
											 bool maximizeButton = true, bool closeButton = true);

/// Zone de la fenêtre sous un point : contenu, barre (déplacement) ou bord (redimensionnement).
enum class FrameHit : uint8_t { Client, Caption, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
/// `border` : épaisseur des bords de redimensionnement (aucun si la fenêtre est agrandie).
/// Les boutons de la barre sont « contenu » : ils reçoivent leurs clics.
[[nodiscard]] FrameHit HitTestFrame(const FrameLayout &layout, float x, float y, bool maximized, float border = 6.f) noexcept;

enum class FrameAction : uint8_t { None, Close, Minimize, ToggleMaximize, Custom };

struct WindowFrameResult {
	FrameAction action = FrameAction::None;
	std::string_view custom; ///< `id` du bouton personnalisé cliqué (`action == Custom`)
	FrameLayout layout;
};

/// Dessine la barre de titre et traite ses boutons. À appeler avant le contenu.
WindowFrameResult WindowFrame(Context &u, const Rect &window, const WindowFrameOptions &options);

/// Liaison à une fenêtre SDL sans décoration : zones de déplacement et de redimensionnement
/// gérées par le système, actions des boutons de la fenêtre.
class WindowChrome {
public:
	explicit WindowChrome(SDL_Window *window);
	~WindowChrome();
	WindowChrome(const WindowChrome &) = delete;
	WindowChrome &operator=(const WindowChrome &) = delete;

	/// À appeler à chaque image avec la disposition courante ; `scale` : pixels de fenêtre
	/// par unité logique du canvas.
	void Update(const FrameLayout &layout, float scale);
	/// Applique l'action ; vrai si la fermeture est demandée (à l'application de quitter).
	bool Apply(FrameAction action);
	[[nodiscard]] bool Maximized() const;
	[[nodiscard]] bool Focused() const;
	/// Zone sous un point en pixels de fenêtre (ce que voit le système).
	[[nodiscard]] FrameHit HitTest(int x, int y) const noexcept;

private:
	SDL_Window *m_window; // non possédée
	FrameLayout m_layout;
	float m_scale = 1.f;
};

} // namespace ui
