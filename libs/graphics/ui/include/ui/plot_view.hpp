#pragma once
/**
 * @file plot_view.hpp
 * @brief Graphique 2D interactif de l'interface immédiate : séries (ligne, aire, nuage,
 *        barres, tiges, barres d'erreur, chandeliers OHLC), axe Y secondaire, légende,
 *        déplacement, zoom, ajustement automatique et infobulle.
 *
 * @code{.cpp}
 * ui::PlotView plot;                       // garde l'état de la vue (bornes, glissés)
 * plot.title = "Températures";
 * ui::PlotLayer &t = plot.Series("Paris", ui::PlotStyle::Line);
 * t.x = {...}; t.y = {...};
 * plot.Draw(Ui(), rect);                    // chaque image
 * @endcode
 *
 * Souris : glisser = déplacer, molette = zoom (sur un seul axe si la souris est sur sa
 * bande de graduation), clic droit glissé = zoom rectangle, double-clic = ajustement
 * automatique ; survol de la légende = mise en avant, clic = masquer / afficher.
 */
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <sdl3/render/canvas.hpp>

namespace ui {

class Context;

enum class PlotStyle : uint8_t { Line, Area, Scatter, Bars, Stems, ErrorBars, Candles };

/// Série d'un PlotView. Selon le style : `y` (valeurs), `err` (barres d'erreur, ±),
/// `open/high/low/close` (chandeliers ; `x` donne les abscisses).
struct PlotLayer {
	std::string name;
	PlotStyle style = PlotStyle::Line;
	sdl3::Color color{0, 0, 0, 0}; ///< alpha nul : couleur de la palette
	float width = 2.f;			  ///< épaisseur de trait / rayon des points / largeur relative des barres
	bool secondaryAxis = false;	  ///< graduée sur l'axe Y de droite
	bool visible = true;
	std::vector<float> x, y, err;
	std::vector<float> open, high, low, close;
};

class PlotView {
public:
	std::string title, xLabel, yLabel, y2Label;
	bool legend = true;

	/// Ajoute une série ; la référence reste valable après d'autres ajouts.
	PlotLayer &Series(std::string name, PlotStyle style, sdl3::Color color = {0, 0, 0, 0});
	[[nodiscard]] std::deque<PlotLayer> &Layers() noexcept { return m_layers; }
	void Clear() noexcept { m_layers.clear(); }

	/// Dessine dans `r` et traite la souris. Rend vrai si la vue a changé (zoom, déplacement…).
	bool Draw(Context &u, const sdl3::FRect &r);
	/// Ajuste les bornes aux données visibles (aussi au premier affichage).
	void AutoFit();
	/// Bornes courantes (axe principal, axe secondaire).
	float xMin = 0.f, xMax = 1.f, yMin = 0.f, yMax = 1.f, y2Min = 0.f, y2Max = 1.f;

private:
	enum class Drag : uint8_t { None, Pan, Box };
	int m_highlight = -1;
	bool m_fitted = false;
	Drag m_drag = Drag::None;
	float m_startX = 0.f, m_startY = 0.f, m_lastX = 0.f, m_lastY = 0.f;
	double m_lastClick = -1.0, m_time = 0.0;
	std::deque<PlotLayer> m_layers; ///< deque : références stables aux séries
};

} // namespace ui
