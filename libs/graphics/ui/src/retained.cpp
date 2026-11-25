// Définitions de ui/retained.hpp — hôte de l'interface retenue.
#include "ui/retained.hpp"
#include "ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

#include <sdl3/core/events_manager.hpp>
#include <ui/icons.hpp>
#include <ui/ui.hpp> // ui::Context (feuilles `Custom`)

namespace ui {

namespace {

/// Fonte gfx correspondant au style calculé.
sdl3::CanvasFont FontOf(const ComputedStyle &c) {
	return c.IsBold() ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular;
}

/// Famille de widget (dispatch du rendu et de la saisie).
enum class WidgetKind : uint8_t {
	Box, Text, Label, RichText, Image, Separator,
	Button, Toggle, Radio, ToolButton, Checkbox, Badge,
	LineEdit, TextArea, CodeEditor, Slider, SpinBox, Knob, Progress,
	ComboBox, Panel, Expander, Scroll, Splitter, Modal, Custom,
	ListView, TreeView, TableView, TabBar, TabWidget, MenuBar, SubWindow, Popup, Plot, Pie, Heatmap,
	Selectable, Switch, MenuButton, SettingRow, ToolGroup
};

WidgetKind KindOf(const Node &n) {
	if (n.customPaint) return WidgetKind::Custom;
	if (n.type == wt::text) return WidgetKind::Text;
	if (n.type == wt::label) return WidgetKind::Label;
	if (n.type == wt::richtext) return WidgetKind::RichText;
	if (n.type == wt::image) return WidgetKind::Image;
	if (n.type == wt::separator) return WidgetKind::Separator;
	if (n.type == wt::button) return WidgetKind::Button;
	if (n.type == wt::toggle) return WidgetKind::Toggle;
	if (n.type == wt::radio) return WidgetKind::Radio;
	if (n.type == wt::toolbutton) return WidgetKind::ToolButton;
	if (n.type == wt::checkbox) return WidgetKind::Checkbox;
	if (n.type == wt::badge) return WidgetKind::Badge;
	if (n.type == wt::lineedit) return WidgetKind::LineEdit;
	if (n.type == wt::textarea) return WidgetKind::TextArea;
	if (n.type == wt::codeeditor) return WidgetKind::CodeEditor;
	if (n.type == wt::slider) return WidgetKind::Slider;
	if (n.type == wt::spinbox) return WidgetKind::SpinBox;
	if (n.type == wt::knob) return WidgetKind::Knob;
	if (n.type == wt::progress) return WidgetKind::Progress;
	if (n.type == wt::combobox) return WidgetKind::ComboBox;
	if (n.type == wt::panel) return WidgetKind::Panel;
	if (n.type == wt::expander) return WidgetKind::Expander;
	if (n.type == wt::scroll) return WidgetKind::Scroll;
	if (n.type == wt::splitter) return WidgetKind::Splitter;
	if (n.type == wt::modal) return WidgetKind::Modal;
	if (n.type == wt::listview) return WidgetKind::ListView;
	if (n.type == wt::treeview) return WidgetKind::TreeView;
	if (n.type == wt::tableview) return WidgetKind::TableView;
	if (n.type == wt::tabbar) return WidgetKind::TabBar;
	if (n.type == wt::tabwidget) return WidgetKind::TabWidget;
	if (n.type == wt::menubar) return WidgetKind::MenuBar;
	if (n.type == wt::subwindow) return WidgetKind::SubWindow;
	if (n.type == wt::popup) return WidgetKind::Popup;
	if (n.type == wt::plot) return WidgetKind::Plot;
	if (n.type == wt::pie) return WidgetKind::Pie;
	if (n.type == wt::heatmap) return WidgetKind::Heatmap;
	if (n.type == wt::selectable) return WidgetKind::Selectable;
	if (n.type == wt::switchbox) return WidgetKind::Switch;
	if (n.type == wt::menubutton) return WidgetKind::MenuButton;
	if (n.type == wt::settingrow) return WidgetKind::SettingRow;
	if (n.type == wt::toolgroup) return WidgetKind::ToolGroup;
	return WidgetKind::Box;
}

/// Un widget peut recevoir le focus clavier (et participer au cycle Tab).
[[nodiscard]] bool IsFocusable(const Node &n) noexcept {
	if (n.disabled || !n.IsVisible())
		return false;
	switch (KindOf(n)) {
	case WidgetKind::LineEdit:
	case WidgetKind::TextArea:
	case WidgetKind::CodeEditor:
	case WidgetKind::SpinBox:
	case WidgetKind::Button:
	case WidgetKind::Toggle:
	case WidgetKind::Radio:
	case WidgetKind::ToolButton:
	case WidgetKind::ComboBox:
	case WidgetKind::Knob:
	case WidgetKind::Selectable:
	case WidgetKind::Switch:
	case WidgetKind::MenuButton:
		return true;
	default:
		return false;
	}
}

/// Un modal visible (dans ce sous-arbre) gèle l'arrière-plan.
[[nodiscard]] bool HasVisibleModal(const Node &n) {
	if (KindOf(n) == WidgetKind::Modal && n.IsVisible())
		return true;
	for (const NodePtr &c : n.children)
		if (HasVisibleModal(*c))
			return true;
	return false;
}

/// Nombre de lignes d'un texte multi-lignes (`\n`).
[[nodiscard]] int LineCount(std::string_view s) noexcept {
	int n = 1;
	for (char c : s)
		if (c == '\n')
			++n;
	return n;
}

/// Découpe `text` en lignes tenant dans `maxWidth` : les retours `\n` sont
/// respectés, et un retour à la ligne est inséré aux espaces. Un mot plus large
/// que `maxWidth` reste sur sa propre ligne (pas de coupure au milieu d'un mot).
[[nodiscard]] std::vector<std::string> WrapText(sdl3::Canvas &canvas, std::string_view text, float maxWidth, float size,
												sdl3::CanvasFont font) {
	std::vector<std::string> out;
	std::string line;
	const auto flush = [&] {
		out.push_back(std::move(line));
		line.clear();
	};
	size_t i = 0;
	const size_t n = text.size();
	while (i <= n) {
		if (i == n || text[i] == '\n') {
			flush();
			++i;
			continue;
		}
		size_t j = i;
		while (j < n && text[j] != '\n' && text[j] != ' ')
			++j;
		const std::string_view word = text.substr(i, j - i);
		const std::string trial = line.empty() ? std::string(word) : line + " " + std::string(word);
		if (!line.empty() && canvas.MeasureText(trial, size, font) > maxWidth) {
			flush();
			line = std::string(word);
		} else {
			line = trial;
		}
		i = j;
		while (i < n && text[i] == ' ')
			++i;
	}
	flush();
	return out;
}

/// Tronque `text` en « … » pour tenir dans `maxWidth` (sinon le texte entier).
[[nodiscard]] std::string EllipsizeText(sdl3::Canvas &canvas, std::string_view text, float maxWidth, float size,
										sdl3::CanvasFont font) {
	if (canvas.MeasureText(text, size, font) <= maxWidth)
		return std::string(text);
	const std::string ellipsis = "\xE2\x80\xA6"; // « … »
	const float budget = std::max(0.f, maxWidth - canvas.MeasureText(ellipsis, size, font));
	size_t lo = 0, hi = text.size();
	while (lo < hi) {
		const size_t mid = (lo + hi + 1) / 2;
		if (canvas.MeasureText(text.substr(0, mid), size, font) <= budget)
			lo = mid;
		else
			hi = mid - 1;
	}
	return std::string(text.substr(0, lo)) + ellipsis;
}

[[nodiscard]] float ContentSize(sdl3::Canvas &canvas, const Node &n, const LengthBasis &b, bool horizontal);

/// Hauteur intrinsèque d'un nœud (panneau d'une modale, infobulle) : hauteur
/// explicite, sinon la hauteur calculée par `ContentSize` (point unique du
/// dimensionnement) ; un conteneur vide garde une hauteur minimale lisible.
[[nodiscard]] float IntrinsicHeight(sdl3::Canvas &canvas, const Node &n, const LengthBasis &b) {
	const ComputedStyle &c = n.computed;
	if (!c.height.IsAuto() && !c.height.IsFr())
		return c.height.Resolve(b);
	const float h = ContentSize(canvas, n, b, false);
	return h > 0.f ? h : 28.f;
}

/// Enfant positionné hors du flux (`position: absolute` / `fixed`).
[[nodiscard]] bool OutOfFlow(const Node &n) noexcept { return n.computed.position != Position::Relative; }

constexpr float kToolGroupCaption = 11.f; ///< corps de la légende d'un groupe d'outils

/// Taille du contenu d'un nœud pour la mise en page flex, sur l'axe horizontal
/// (`horizontal`) ou vertical : taille explicite, sinon contenu propre du widget
/// (texte, bouton, curseur…), sinon somme (axe du conteneur) ou maximum (axe
/// transversal) des enfants dans le flux, avec écarts, marges, padding et bordures.
/// Un conteneur vide mesure 0 (un espaceur `Box().Grow()` ne prend que le reste).
[[nodiscard]] float ContentSize(sdl3::Canvas &canvas, const Node &n, const LengthBasis &b, bool horizontal) {
	const ComputedStyle &c = n.computed;
	const Length &len = horizontal ? c.width : c.height;
	if (!len.IsAuto() && !len.IsFr() && !len.IsRelative())
		return len.Resolve(b);
	const sdl3::CanvasFont font = FontOf(c);
	const float line = canvas.LineHeight(c.fontSize, font);
	const float padH = c.paddingLeft + c.paddingRight, padV = c.paddingTop + c.paddingBottom;
	if (n.type == wt::text || n.type == wt::label || n.type == wt::richtext) {
		if (!horizontal) {
			// Retour à la ligne automatique : la hauteur dépend de la largeur (explicite
			// ou calculée par le parent) ; sans `wordWrap`, une ligne par `\n`.
			if (n.wordWrap && !c.width.IsAuto() && !c.width.IsFr()) {
				const float w = std::max(1.f, c.width.Resolve(b) - padH);
				return line * float(WrapText(canvas, n.text, w, c.fontSize, font).size());
			}
			return line * float(LineCount(n.text));
		}
		float w = 0.f;
		size_t start = 0;
		while (true) {
			const size_t nl = n.text.find('\n', start);
			w = std::max(w, canvas.MeasureText(std::string_view(n.text).substr(start, nl == std::string::npos ? std::string::npos : nl - start),
											   c.fontSize, font));
			if (nl == std::string::npos)
				break;
			start = nl + 1;
		}
		return w;
	}
	if (n.type == wt::button || n.type == wt::toggle || n.type == wt::radio || n.type == wt::checkbox || n.type == wt::combobox)
		return horizontal ? canvas.MeasureText(n.text, c.fontSize, font) + padH + ((n.type == wt::checkbox || n.type == wt::radio) ? 28.f : 20.f) : line + padV;
	if (n.type == wt::spinbox)
		return horizontal ? 100.f + padH : line + padV;
	if (n.type == wt::lineedit || n.type == wt::textarea)
		return horizontal ? 140.f + padH : line + padV;
	if (n.type == wt::toolbutton || n.type == wt::image)
		return line + padV;
	if (n.type == wt::badge) // pilule : texte + padding, un peu plus large que le texte
		return horizontal ? canvas.MeasureText(n.text, c.fontSize, font) + padH : line + padV;
	if (n.type == wt::selectable || n.type == wt::switchbox) // ligne de liste / interrupteur
		return horizontal ? canvas.MeasureText(n.text, c.fontSize, font) + padH + (n.type == wt::switchbox ? 50.f : (n.icon ? 28.f : 20.f)) : line + padV;
	if (n.type == wt::menubutton) { // icône + titre + sous-titre
		const float sub = n.placeholder.empty() ? 0.f : canvas.LineHeight(c.fontSize - 2.f, font);
		return horizontal ? canvas.MeasureText(n.text, c.fontSize, font) + padH + (n.icon ? 40.f : 24.f) : line + sub + padV;
	}
	if (n.type == wt::knob)
		return 40.f;
	if (n.type == wt::separator) // filet horizontal (en ligne) ou vertical (`.Vertical()`)
		return (horizontal == c.IsRow()) ? 0.f : 12.f;
	if (n.type == wt::slider)
		return horizontal ? 120.f : 24.f;
	if (n.type == wt::progress)
		return horizontal ? 120.f : 10.f;
	if (n.type == wt::tabbar || n.type == wt::menubar)
		return horizontal ? 0.f : line + padV;
	if (n.type == wt::listview || n.type == wt::treeview || n.type == wt::tableview)
		return horizontal ? 160.f : 200.f;
	if (n.type == wt::plot || n.type == wt::pie || n.type == wt::heatmap)
		return horizontal ? 220.f : 220.f;
	// Défilement : la taille vient du parent (ou d'une taille explicite), pas du contenu.
	if (n.type == wt::scroll)
		return 0.f;
	// Section dépliante : en-tête, puis le corps seulement si elle est ouverte.
	if (n.type == wt::expander && !horizontal) {
		float body = 0.f;
		int shown = 0;
		if (n.open)
			for (const NodePtr &ch : n.children)
				if (ch->IsVisible() && !ch->floating.active && !OutOfFlow(*ch)) {
					body += ContentSize(canvas, *ch, b, false) + ch->computed.marginTop + ch->computed.marginBottom;
					++shown;
				}
		if (shown > 1)
			body += c.gap * float(shown - 1);
		return c.fontSize + 12.f + body + padV + c.BorderBox().V();
	}
	// Onglets : une seule page visible → barre + plus grande page.
	if (n.type == wt::tabwidget) {
		float page = 0.f;
		for (const NodePtr &ch : n.children)
			if (ch->IsVisible())
				page = std::max(page, ContentSize(canvas, *ch, b, horizontal));
		return horizontal ? page : page + c.fontSize + 12.f;
	}
	// Conteneur : enfants dans le flux.
	const bool alongMain = c.IsRow() == horizontal;
	float size = 0.f;
	int count = 0;
	for (const NodePtr &ch : n.children) {
		if (!ch->IsVisible() || ch->floating.active || OutOfFlow(*ch))
			continue;
		const ComputedStyle &cc = ch->computed;
		const float margin = horizontal ? cc.marginLeft + cc.marginRight : cc.marginTop + cc.marginBottom;
		const float s = ContentSize(canvas, *ch, b, horizontal) + margin;
		size = alongMain ? size + s : std::max(size, s);
		++count;
	}
	if (alongMain && count > 1 && c.display == Display::Flex)
		size += c.gap * float(count - 1);
	if (c.display == Display::Grid && !horizontal && count > 0) {
		// Grille : lignes de `gridColumns` cellules, chacune aussi haute que sa plus haute cellule.
		const int cols = std::max(1, c.gridColumns);
		float total = 0.f, rowMax = 0.f;
		int index = 0;
		for (const NodePtr &ch : n.children) {
			if (!ch->IsVisible() || ch->floating.active || OutOfFlow(*ch))
				continue;
			rowMax = std::max(rowMax, ContentSize(canvas, *ch, b, false));
			if (++index % cols == 0) {
				total += rowMax + c.gap;
				rowMax = 0.f;
			}
		}
		size = total + rowMax - (index % cols == 0 ? c.gap : 0.f);
	}
	// Un conteneur vide (espaceur) mesure 0 ; sinon padding, bordure et titre éventuel.
	if (count == 0 && padH + padV <= 0.f && n.type != wt::panel)
		return 0.f;
	const math::Sides border = c.BorderBox();
	size += horizontal ? padH + border.H() : padV + border.V();
	if (n.type == wt::panel && !horizontal)
		size += c.fontSize + 10.f; // en-tête du panneau
	if (n.type == wt::toolgroup && horizontal) // légende sous les outils
		size = std::max(size, canvas.MeasureText(n.text, kToolGroupCaption, font) + padH + border.H());
	return size;
}

/// Collecte les widgets focusables dans l'ordre du document (cycle Tab).
void CollectFocusable(Node &n, std::vector<Node *> &out) {
	if (IsFocusable(n))
		out.push_back(&n);
	for (NodePtr &ch : n.children)
		CollectFocusable(*ch, out);
}

/// Texte masqué d'un champ « mot de passe » (un « • » par caractère).
[[nodiscard]] std::string MaskedText(std::string_view text) {
	std::string out;
	out.reserve(text.size() * 3);
	for (size_t i = 0; i < text.size(); ++i)
		out += "\xE2\x80\xA2"; // « • »
	return out;
}


/// Segment d'un texte enrichi (Markdown léger).
struct RichSegment {
	std::string text;
	sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
	bool underline = false;
	std::string url; ///< non vide = lien
};

/// Découpe un Markdown léger en segments stylés : `**gras**`, `*italique*`,
/// `_souligné_`, `` `code` ``, `[libellé](url)`.
[[nodiscard]] std::vector<RichSegment> ParseRichText(std::string_view text) {
	std::vector<RichSegment> out;
	sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
	bool underline = false;
	size_t i = 0;
	const size_t n = text.size();
	auto pushPlain = [&](std::string_view s) {
		if (!s.empty())
			out.push_back({std::string(s), font, underline, {}});
	};
	while (i < n) {
		const char ch = text[i];
		if (ch == '*' && i + 1 < n && text[i + 1] == '*') {
			font = font == sdl3::CanvasFont::Bold ? sdl3::CanvasFont::Regular : sdl3::CanvasFont::Bold;
			i += 2;
		} else if (ch == '*') {
			++i; // italique (pas de fonte italique disponible : neutre)
		} else if (ch == '_') {
			underline = !underline;
			++i;
		} else if (ch == '`') {
			font = font == sdl3::CanvasFont::Mono ? sdl3::CanvasFont::Regular : sdl3::CanvasFont::Mono;
			++i;
		} else if (ch == '[') {
			const size_t close = text.find(']', i + 1);
			if (close != std::string_view::npos && close + 1 < n && text[close + 1] == '(') {
				const size_t closeP = text.find(')', close + 2);
				if (closeP != std::string_view::npos) {
					out.push_back({std::string(text.substr(i + 1, close - i - 1)), font, true,
								   std::string(text.substr(close + 2, closeP - close - 2))});
					i = closeP + 1;
					continue;
				}
			}
			pushPlain(text.substr(i, 1));
			++i;
		} else {
			size_t j = i;
			while (j < n && text[j] != '*' && text[j] != '_' && text[j] != '`' && text[j] != '[')
				++j;
			pushPlain(text.substr(i, j - i));
			i = j;
		}
	}
	return out;
}

/// Hauteur d'une ligne d'un ListView / TreeView / TableView.
[[nodiscard]] float RowHeight(const Node &n) noexcept {
	return std::max(22.f, n.computed.fontSize + 8.f);
}

/// Ligne aplatie d'une arborescence (TreeView).
struct FlatRow {
	const TreeItem *item;
	int depth;
};

/// Aplatit récursivement une arborescence en lignes visibles (nœuds dépliés).
void Flatten(const std::vector<TreeItem> &roots, const std::set<std::string> &expanded, std::vector<FlatRow> &out, int depth = 0) {
	for (const TreeItem &it : roots) {
		out.push_back({&it, depth});
		if (it.isDir && expanded.count(it.id))
			Flatten(it.children, expanded, out, depth + 1);
	}
}

/// Entrée validée (Retour ou pavé numérique).
[[nodiscard]] bool EnterPressed(const sdl3::EventsManager &ev) {
	return ev.KeycodePressed(SDLK_RETURN) || ev.KeycodePressed(SDLK_KP_ENTER);
}

/// Bord/corner d'une fenêtre flottante sous (mx, my) : 0=HG,1=HD,2=BD,3=BG,
/// 4=Haut,5=Bas,6=Gauche,7=Droite ; -1 = aucun.
[[nodiscard]] int ResizeEdgeAt(const Node &w, float mx, float my) {
	const float m = 6.f;
	const float L = w.floating.x, T = w.floating.y, R = w.floating.x + w.floating.w, B = w.floating.y + w.floating.h;
	const bool l = std::fabs(mx - L) < m, r = std::fabs(mx - R) < m, t = std::fabs(my - T) < m, b = std::fabs(my - B) < m;
	if (t && l) return 0;
	if (t && r) return 1;
	if (b && r) return 2;
	if (b && l) return 3;
	if (t) return 4;
	if (b) return 5;
	if (l) return 6;
	if (r) return 7;
	return -1;
}

/// Pas « rond » pour ~`target` graduations sur l'intervalle `range`.
[[nodiscard]] float NiceStep(float range, int target) {
	if (range <= 0.f)
		return 1.f;
	const float rough = range / float(target);
	const float mag = std::pow(10.f, std::floor(std::log10(rough)));
	const float norm = rough / mag;
	const float step = norm < 1.5f ? 1.f : (norm < 3.f ? 2.f : (norm < 7.f ? 5.f : 10.f));
	return step * mag;
}

/// Couleur d'une palette (Jet) pour `t` (0..1).
[[nodiscard]] sdl3::Color Colormap(float t) {
	t = std::clamp(t, 0.f, 1.f);
	const auto c = [](float v) { return uint8_t(std::clamp(v, 0.f, 1.f) * 255.f); };
	const float r = std::clamp(std::min(4.f * t - 1.5f, -4.f * t + 4.5f), 0.f, 1.f);
	const float g = std::clamp(std::min(4.f * t - 0.5f, -4.f * t + 3.5f), 0.f, 1.f);
	const float b = std::clamp(std::min(4.f * t + 0.5f, -4.f * t + 2.5f), 0.f, 1.f);
	return sdl3::Rgba(c(r), c(g), c(b));
}

constexpr float kScrollbarW = 12.f; // épaisseur d'une barre de défilement

/// Un nœud défile-t-il son contenu (barres de défilement visibles) ?
[[nodiscard]] bool Scrollable(const Node &n) {
	return n.type == wt::scroll || n.type == wt::listview || n.type == wt::treeview || n.type == wt::tableview;
}

/// Piste de la barre verticale (ou horizontale) d'un conteneur défilant.
[[nodiscard]] sdl3::FRect ScrollTrack(const Node &n, bool vertical) {
	const float pad = 2.f;
	if (vertical)
		return {n.content.Right() - kScrollbarW - pad, n.content.y + pad, kScrollbarW, n.content.h - 2.f * pad};
	return {n.content.x + pad, n.content.Bottom() - kScrollbarW - pad, n.content.w - 2.f * pad, kScrollbarW};
}

/// Barre d'onglets (TabBar, bandeau d'un TabWidget) : onglets de largeur égale s'ils tiennent,
/// sinon à leur largeur naturelle, défilants entre deux flèches.
struct TabStrip {
	std::vector<sdl3::FRect> tabs; ///< défilement compris
	sdl3::FRect area{};			   ///< zone visible des onglets (hors flèches)
	sdl3::FRect back{}, forward{}; ///< flèches (débordement seulement)
	bool overflow = false;
	float total = 0.f; ///< largeur naturelle de tous les onglets
	[[nodiscard]] float MaxScroll() const noexcept { return overflow ? std::max(0.f, total - area.w) : 0.f; }
};

[[nodiscard]] TabStrip TabStripOf(sdl3::Canvas &canvas, const Node &n, const sdl3::FRect &strip) {
	TabStrip t;
	t.area = strip;
	const size_t count = n.tabs.size();
	if (count == 0)
		return t;
	const sdl3::CanvasFont font = FontOf(n.computed);
	std::vector<float> widths(count);
	for (size_t i = 0; i < count; ++i) {
		widths[i] = std::max(64.f, canvas.MeasureText(n.tabs[i], n.computed.fontSize, font) + 28.f);
		t.total += widths[i];
	}
	if (t.total <= strip.w) { // tout tient : largeurs égales sur toute la barre
		const float w = strip.w / float(count);
		for (size_t i = 0; i < count; ++i)
			t.tabs.push_back({strip.x + float(i) * w, strip.y, w, strip.h});
		return t;
	}
	t.overflow = true;
	const float arrow = std::min(24.f, strip.h);
	t.back = {strip.x, strip.y, arrow, strip.h};
	t.forward = {strip.Right() - arrow, strip.y, arrow, strip.h};
	t.area = {strip.x + arrow, strip.y, std::max(0.f, strip.w - 2.f * arrow), strip.h};
	float x = t.area.x - std::clamp(n.scroll.x, 0.f, t.MaxScroll());
	for (size_t i = 0; i < count; ++i) {
		t.tabs.push_back({x, strip.y, widths[i], strip.h});
		x += widths[i];
	}
	return t;
}

/// Bande des onglets d'une barre (son contenu) ou d'un TabWidget (bandeau du haut).
[[nodiscard]] sdl3::FRect TabStripRect(const Node &n, bool tabWidget) {
	return tabWidget ? sdl3::FRect{n.content.x, n.content.y, n.content.w, n.computed.fontSize + 12.f} : n.content;
}

/// Onglets qui débordent : défilement borné ; onglet actif amené dans la zone visible quand il
/// change (une fois : on peut ensuite faire défiler ailleurs).
TabStrip UpdateTabScroll(sdl3::Canvas &canvas, Node &n, const sdl3::FRect &strip) {
	TabStrip t = TabStripOf(canvas, n, strip);
	if (!t.overflow) {
		n.scroll.x = 0.f;
		n.scroll.revealed = n.selectedIndex;
		return t;
	}
	if (n.selectedIndex != n.scroll.revealed && n.selectedIndex >= 0 && size_t(n.selectedIndex) < t.tabs.size()) {
		const sdl3::FRect &tab = t.tabs[size_t(n.selectedIndex)];
		if (tab.x < t.area.x)
			n.scroll.x -= t.area.x - tab.x;
		else if (tab.Right() > t.area.Right())
			n.scroll.x += tab.Right() - t.area.Right();
		n.scroll.revealed = n.selectedIndex;
	}
	n.scroll.x = std::clamp(n.scroll.x, 0.f, t.MaxScroll());
	return TabStripOf(canvas, n, strip);
}

/// Dessine les onglets (`tabWidget` : onglet actif sur le fond, souligné d'accent) et les flèches.
void DrawTabStrip(sdl3::Canvas &canvas, const Theme &theme, const Node &n, const TabStrip &t, bool tabWidget, sdl3::Color text, sdl3::Color hover,
				  float mouseX, float mouseY) {
	const sdl3::CanvasFont font = FontOf(n.computed);
	canvas.PushClip(t.area);
	for (size_t i = 0; i < t.tabs.size(); ++i) {
		const sdl3::FRect &tab = t.tabs[i];
		const bool active = int(i) == n.selectedIndex;
		const bool hovered = tab.Contains(mouseX, mouseY) && t.area.Contains(mouseX, mouseY);
		if (active) {
			if (tabWidget)
				canvas.FillRoundRect(tab, 4.f, theme.background);
			else
				canvas.FillRect(tab, theme.background);
		} else if (hovered) {
			canvas.FillRect(tab, sdl3::WithAlpha(hover, 120));
		}
		canvas.TextCentered(tab, n.tabs[i], active && tabWidget ? theme.text : text, n.computed.fontSize, font);
		if (active && tabWidget) // onglet ouvert : bordure basse à la couleur d'accent
			canvas.FillRect({tab.x + 8.f, tab.y + tab.h - 3.f, tab.w - 16.f, 3.f}, theme.accent);
	}
	canvas.PopClip();
	if (!t.overflow)
		return;
	const float scroll = std::clamp(n.scroll.x, 0.f, t.MaxScroll());
	const auto arrow = [&](const sdl3::FRect &r, icons::MaterialIcons icon, bool enabled) {
		if (enabled && r.Contains(mouseX, mouseY))
			canvas.FillRect(r, sdl3::WithAlpha(hover, 160));
		canvas.Icon(r, icons::Codepoint(icon), enabled ? text : sdl3::WithAlpha(text, 70), 18.f);
	};
	arrow(t.back, icons::MaterialIcons::CHEVRON_LEFT, scroll > 0.f);
	arrow(t.forward, icons::MaterialIcons::CHEVRON_RIGHT, scroll < t.MaxScroll());
}

/// Parties d'une barre de défilement : boutons fléchés (début / fin) et piste restante.
struct ScrollParts {
	sdl3::FRect back, forward, track;
};
[[nodiscard]] ScrollParts ScrollPartsOf(const sdl3::FRect &track, bool vertical) {
	const float thick = vertical ? track.w : track.h;
	const float length = vertical ? track.h : track.w;
	const float arrow = std::min(thick, length / 3.f);
	ScrollParts p;
	if (vertical) {
		p.back = {track.x, track.y, track.w, arrow};
		p.forward = {track.x, track.Bottom() - arrow, track.w, arrow};
		p.track = {track.x, track.y + arrow, track.w, std::max(0.f, track.h - 2.f * arrow)};
	} else {
		p.back = {track.x, track.y, arrow, track.h};
		p.forward = {track.Right() - arrow, track.y, arrow, track.h};
		p.track = {track.x + arrow, track.y, std::max(0.f, track.w - 2.f * arrow), track.h};
	}
	return p;
}

/// Applique un pas de défilement (bouton fléché) à un conteneur.
void ScrollStep(Node &s, bool vertical, float delta) {
	if (vertical)
		s.scroll.y = std::clamp(s.scroll.y + delta, 0.f, std::max(0.f, s.scroll.contentH - s.content.h));
	else
		s.scroll.x = std::clamp(s.scroll.x + delta, 0.f, std::max(0.f, s.scroll.contentW - s.content.w));
}

/// Dessine la barre verticale (ou horizontale) d'un conteneur défilant, avec ses
/// boutons fléchés — même apparence que `Context::Scrollbar`.
void DrawScrollbar(sdl3::Canvas &c, const Theme &th, const sdl3::FRect &track, bool vertical, float offset, float content, float viewport,
				   float mouseX, float mouseY, int arrowDir) {
	if (content <= viewport)
		return;
	const ScrollParts p = ScrollPartsOf(track, vertical);
	const float thick = vertical ? track.w : track.h;
	c.FillRect(track, sdl3::WithAlpha(th.widget, 70));
	const auto drawArrow = [&](const sdl3::FRect &r, char32_t icon, int dir) {
		const bool held = arrowDir == dir;
		const bool over = r.Contains(mouseX, mouseY);
		if (over)
			c.FillRect(r, held ? th.widgetActive : th.widgetHover);
		c.Icon(r, icon, held ? th.accent : (over ? th.text : th.textDim), thick + 8.f);
	};
	if (vertical) {
		drawArrow(p.back, 0xE09A, -1);	 // arrow_drop_up
		drawArrow(p.forward, 0xE098, +1); // arrow_drop_down
	} else {
		drawArrow(p.back, 0xE09D, -1);	 // arrow_left
		drawArrow(p.forward, 0xE09E, +1); // arrow_right
	}
	// Curseur, dans la piste restante (hors des flèches).
	const float length = vertical ? p.track.h : p.track.w;
	const float thumb = std::max(20.f, length * (viewport / content));
	const float maxOff = content - viewport;
	const float t = maxOff > 0.f ? offset / maxOff : 0.f;
	const sdl3::FRect bar = vertical
		? sdl3::FRect{p.track.x, p.track.y + t * (length - thumb), p.track.w, thumb}
		: sdl3::FRect{p.track.x + t * (length - thumb), p.track.y, thumb, p.track.h};
	c.FillRoundRect(bar, std::min(bar.w, bar.h) * 0.5f, th.widgetHover);
}

/// Dessine les barres d'un conteneur défilant, par-dessus son contenu.
void DrawScrollbars(sdl3::Canvas &c, const Theme &th, const Node &n, float mouseX, float mouseY) {
	if (!Scrollable(n))
		return;
	if (n.scroll.contentH > n.content.h)
		DrawScrollbar(c, th, ScrollTrack(n, true), true, n.scroll.y, n.scroll.contentH, n.content.h, mouseX, mouseY, n.scroll.arrowDir);
	if (n.scroll.contentW > n.content.w)
		DrawScrollbar(c, th, ScrollTrack(n, false), false, n.scroll.x, n.scroll.contentW, n.content.w, mouseX, mouseY, n.scroll.arrowDir);
}

} // namespace

// ============================================================================
// Montage
// ============================================================================

void Ui::Root(WidgetBuilder builder) {
	m_root = builder.Build();
	m_i18nVersion = 0; // nouvel arbre : traductions à poser
	// L'ancien arbre (et ses pointeurs de survol/appui/focus) n'existe plus.
	m_pressed = nullptr;
	m_hovered = nullptr;
	m_focus = nullptr;
	m_prevHovered = nullptr;
	m_prevFocus = nullptr;
	m_openCombo = nullptr;
	m_openMenuBar = nullptr;
	m_bringToFront = nullptr;
	m_dragSource = nullptr;
	m_dragActive = false;
	m_scrollDrag = nullptr;
	m_lastClickNode = nullptr;
	m_selNode = nullptr;
	m_textClickNode = nullptr;
	m_selDragging = false;
	// Index identifiant → nœud (Find en O(1)) et chaînage des parents.
	m_index.clear();
	std::function<void(NodePtr &)> link = [&](NodePtr &n) {
		if (!n->id.empty())
			m_index[n->id] = n;
		// États de transition : initialisés pour ne pas déclencher OnHidden/…
		// dès la première image d'un widget construit masqué/désactivé.
		n->prevHidden = n->hidden;
		n->prevDisabled = n->disabled;
		for (NodePtr &c : n->children) {
			c->parent = n.get();
			link(c);
		}
	};
	if (m_root)
		link(m_root);
}

Widget Ui::Find(std::string_view id) const {
	const auto it = m_index.find(std::string(id));
	return it != m_index.end() ? Widget(it->second) : Widget();
}

bool Ui::IsBlocking() const {
	if (m_blocked || m_openCombo || m_openMenuBar)
		return true;
	return m_root && HasVisibleModal(*m_root);
}

// ============================================================================
// Résolution des styles (cascade)
// ============================================================================

namespace {

bool MatchSelector(const Node &node, const std::vector<const Node *> &ancestors, bool isRoot, bool scopeOwner,
				   const Selector &sel) {
	// Vérifie le sujet (dernier composé) contre le nœud.
	const CompoundSelector &subject = sel.Subject();
	{
		ElementData e;
		e.type = node.type;
		e.id = node.id;
		e.classes = &node.classes;
		e.state = node.state;
		e.isRoot = isRoot;
		e.isScopeOwner = scopeOwner;
		if (!MatchCompound(subject, e))
			return false;
	}
	// Remonte les combinateurs (descendant / enfant) sur la chaîne des ancêtres.
	// ancestors[i] est l'ancêtre au rang i (ancestors.back() = parent direct).
	size_t idx = ancestors.size();
	for (size_t k = sel.parts.size() - 1; k > 0; --k) {
		const Combinator comb = sel.combinators[k - 1];
		const CompoundSelector &part = sel.parts[k - 1];
		if (comb == Combinator::Child) {
			if (idx == 0)
				return false;
			const Node *anc = ancestors[--idx];
			ElementData e;
			e.type = anc->type;
			e.id = anc->id;
			e.classes = &anc->classes;
			e.state = anc->state;
			e.isRoot = false;
			e.isScopeOwner = scopeOwner;
			if (!MatchCompound(part, e))
				return false;
		} else { // Descendant
			bool found = false;
			while (idx > 0) {
				const Node *anc = ancestors[--idx];
				ElementData e;
				e.type = anc->type;
				e.id = anc->id;
				e.classes = &anc->classes;
				e.state = anc->state;
				e.isRoot = false;
				e.isScopeOwner = scopeOwner;
				if (MatchCompound(part, e)) {
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
	}
	return true;
}

void ApplySheet(ComputedStyle &style, const StyleSheet &sheet, const Node &node,
				const std::vector<const Node *> &ancestors, bool isRoot, bool scopeOwner) {
	struct Match {
		const StyleProps *props;
		uint32_t spec;
		size_t order;
	};
	std::vector<Match> matches;
	for (const StyleRule &rule : sheet.Rules())
		for (const Selector &sel : rule.selectors)
			if (MatchSelector(node, ancestors, isRoot, scopeOwner, sel))
				matches.push_back({&rule.props, sel.specificity, rule.order});
	// Ordre croissant de (spécificité, ordre) : le plus spécifique l'emporte.
	std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
		if (a.spec != b.spec)
			return a.spec < b.spec;
		return a.order < b.order;
	});
	for (const Match &m : matches)
		style.Apply(*m.props);
}

} // namespace

void Ui::ResolveStyle(Node &node, const Node *parent, const ComputedStyle *parentStyle, bool isRoot) {
	ComputedStyle c{};
	// Héritage (couleur, police, couleurs sémantiques).
	if (parentStyle) {
		c.color = parentStyle->color;
		c.fontSize = parentStyle->fontSize;
		c.fontWeight = parentStyle->fontWeight;
		c.fontStyle = parentStyle->fontStyle;
		c.underline = parentStyle->underline;
		c.textAlign = parentStyle->textAlign;
		c.accentColor = parentStyle->accentColor;
		c.widgetColor = parentStyle->widgetColor;
		c.widgetHoverColor = parentStyle->widgetHoverColor;
		c.widgetActiveColor = parentStyle->widgetActiveColor;
		c.titleColor = parentStyle->titleColor;
	}
	// Chaîne des ancêtres pour les sélecteurs combinés.
	std::vector<const Node *> ancestors;
	for (const Node *p = parent; p; p = p->parent)
		ancestors.push_back(p);
	std::reverse(ancestors.begin(), ancestors.end());
	// Ancêtre immédiat en dernier (pour les combinateurs enfant).

	// Feuilles globales.
	for (const StyleSheetPtr &sheet : m_sheets)
		ApplySheet(c, *sheet, node, ancestors, isRoot, /*scopeOwner=*/false);
	// Feuilles locales (portée : ce widget → `this` le désigne).
	for (const StyleSheetPtr &sheet : node.localSheets)
		ApplySheet(c, *sheet, node, ancestors, isRoot, /*scopeOwner=*/true);
	// Style inline (le plus fort).
	c.Apply(node.style);
	// Widget désactivé : couleurs estompées (le grisé est visuel, l'interaction
	// est déjà coupée par HitTest / IsFocusable).
	if (node.disabled) {
		const auto dim = [](sdl3::Color col) { return sdl3::WithAlpha(col, uint8_t(std::max(0, int(col.a) * 45 / 100))); };
		c.color = dim(c.color);
		c.backgroundColor = dim(c.backgroundColor);
	}
	node.computed = c;

	for (NodePtr &child : node.children)
		ResolveStyle(*child, &node, &node.computed, false);
}

// ============================================================================
// Mise en page (flex / grid)
// ============================================================================

void Ui::LayoutNode(Node &node, const sdl3::FRect &available, const LayoutContext &ctx) {
	if (!node.IsVisible())
		return;
	const ComputedStyle &c = node.computed;
	const math::Sides margin = c.Margin();
	const math::Sides border = c.BorderBox();
	const math::Sides padding = c.Padding();

	// Boîte de bordure : disponible moins marge.
	sdl3::FRect box = {available.x + margin.left, available.y + margin.top,
					 std::max(0.f, available.w - margin.H()), std::max(0.f, available.h - margin.V())};

	const LengthBasis b{available.w, c.fontSize, ctx.rootFontSize, ctx.viewportW, ctx.viewportH, 1.f};

	// Modal : le fond (scrim) recouvre TOUTE la zone disponible ; un panneau
	// centré (largeur/hauteur explicites, sinon par défaut) porte le titre, le
	// bouton croix et les enfants. Nœud à part entière : placé dans l'arbre, il
	// masque au hit-test tous les widgets déclarés avant lui (à la racine, tout).
	if (node.type == wt::modal) {
		node.bounds = box;
		const float pad = 12.f;
		const float titleH = c.fontSize + 16.f;
		const bool hasW = !c.width.IsAuto() && !c.width.IsFr();
		const bool hasH = !c.height.IsAuto() && !c.height.IsFr();
		float panelW = hasW ? c.width.Resolve(b) : 420.f;
		if (!c.minWidth.IsAuto())
			panelW = std::max(panelW, c.minWidth.Resolve(b));
		if (!c.maxWidth.IsAuto())
			panelW = std::min(panelW, c.maxWidth.Resolve(b));
		float bodyH = 0.f;
		for (NodePtr &child : node.children) {
			if (!child->IsVisible())
				continue;
			bodyH += IntrinsicHeight(*m_canvas, *child, b) + c.gap;
		}
		if (bodyH > 0.f)
			bodyH -= c.gap;
		float panelH = hasH ? c.height.Resolve(b) : (titleH + bodyH + 2.f * pad);
		if (!c.minHeight.IsAuto())
			panelH = std::max(panelH, c.minHeight.Resolve(b));
		if (!c.maxHeight.IsAuto())
			panelH = std::min(panelH, c.maxHeight.Resolve(b));
		panelW = std::max(0.f, std::min(panelW, box.w - 2.f * pad));
		panelH = std::max(0.f, std::min(panelH, box.h - 2.f * pad));
		sdl3::FRect panel{box.x + (box.w - panelW) * 0.5f, box.y + (box.h - panelH) * 0.5f, panelW, panelH};
		node.panel = panel;
		node.closeRect = {panel.Right() - titleH, panel.y, titleH, titleH};
		sdl3::FRect body{panel.x + pad, panel.y + titleH, std::max(0.f, panel.w - 2.f * pad),
					   std::max(0.f, panel.h - titleH - pad)};
		node.content = body;
		float y = body.y;
		for (NodePtr &child : node.children) {
			if (!child->IsVisible())
				continue;
			const float h = IntrinsicHeight(*m_canvas, *child, b);
			LayoutNode(*child, sdl3::FRect{body.x, y, body.w, h}, ctx);
			y += h + c.gap;
		}
		return;
	}

	const bool hasW = !c.width.IsAuto() && !c.width.IsFr();
	const bool hasH = !c.height.IsAuto() && !c.height.IsFr();
	if (hasW)
		box.w = c.width.Resolve(b);
	if (hasH)
		box.h = c.height.Resolve(b);
	// min/max.
	if (!c.minWidth.IsAuto())
		box.w = std::max(box.w, c.minWidth.Resolve(b));
	if (!c.maxWidth.IsAuto())
		box.w = std::min(box.w, c.maxWidth.Resolve(b));
	if (!c.minHeight.IsAuto())
		box.h = std::max(box.h, c.minHeight.Resolve(b));
	if (!c.maxHeight.IsAuto())
		box.h = std::min(box.h, c.maxHeight.Resolve(b));

	// Animation de glissé : décalage vertical pendant l'animation.
	if (node.animTime >= 0.f && node.animTime < node.animDuration) {
		const float t = Ease(node.animTime / node.animDuration, node.animEasing);
		box.y += (1.f - t) * node.animOffset;
	}

	node.bounds = box;

	// Boîte de contenu (intérieur padding + bordure).
	sdl3::FRect content = {box.x + border.left + padding.left, box.y + border.top + padding.top,
						 std::max(0.f, box.w - border.H() - padding.H()), std::max(0.f, box.h - border.V() - padding.V())};
	node.content = content;

	// Fenêtre flottante (SubWindow / Popup) : position et taille absolues,
	// hors du flux flex/grid ; les enfants s'empilent verticalement dans le corps.
	if (node.floating.active) {
		node.bounds = {node.floating.x, node.floating.y, node.floating.w, node.floating.h};
		sdl3::FRect body = node.bounds;
		if (node.type == wt::subwindow) {
			const float titleH = c.fontSize + 12.f;
			body.y += titleH;
			body.h = std::max(0.f, body.h - titleH);
		}
		body.x += border.left + padding.left;
		body.y += border.top + padding.top;
		body.w = std::max(0.f, body.w - border.H() - padding.H());
		body.h = std::max(0.f, body.h - border.V() - padding.V());
		node.content = body;
		float y = body.y;
		for (NodePtr &child : node.children) {
			if (!child->IsVisible())
				continue;
			const ComputedStyle &cc = child->computed;
			const float h = ContentSize(*m_canvas, *child, b, false) + cc.marginTop + cc.marginBottom;
			LayoutNode(*child, sdl3::FRect{body.x, y, body.w, h}, ctx);
			y = child->bounds.Bottom() + cc.marginBottom + c.gap;
		}
		return;
	}

	// Expander : en-tête cliquable puis corps (enfants). Fermé → pas d'enfants.
	if (node.type == wt::expander) {
		const float titleH = c.fontSize + 12.f;
		content.y += titleH;
		content.h = std::max(0.f, content.h - titleH);
		node.content = content;
		if (!node.open)
			return;
	}

	// TabWidget : barre d'onglets puis corps de l'onglet actif.
	if (node.type == wt::tabwidget) {
		const float barH = c.fontSize + 12.f;
		sdl3::FRect body = content;
		body.y += barH;
		body.h = std::max(0.f, body.h - barH);
		if (node.selectedIndex >= 0 && size_t(node.selectedIndex) < node.children.size())
			LayoutNode(*node.children[size_t(node.selectedIndex)], body, ctx);
		return;
	}

	// ListView / TreeView / TableView : hauteur du contenu (défilement virtuel).
	// Ces widgets n'ont pas d'enfants : leurs lignes viennent d'un modèle.
	if (node.type == wt::listview || node.type == wt::treeview || node.type == wt::tableview) {
		const float rowH = RowHeight(node);
		size_t n = 0;
		if (node.type == wt::listview)
			n = node.modelView.model.rowCount();
		else if (node.type == wt::tableview)
			n = node.modelView.model.rowCount(); // l'en-tête est fixe, seules les données défilent
		else {
			std::vector<FlatRow> rows;
			Flatten(node.modelView.tree, node.modelView.expanded, rows);
			n = rows.size();
		}
		node.scroll.contentH = float(n) * rowH;
		return;
	}

	// Enfants flottants : position absolue, hors du flux flex/grid.
	for (NodePtr &child : node.children)
		if (child->IsVisible() && child->floating.active)
			LayoutNode(*child, available, ctx);

	if (node.type == wt::panel) {
		// Corps sous le titre : le rendu dessine le titre juste au-dessus de `content`.
		const float titleH = c.fontSize + 10.f;
		content.y += titleH;
		content.h = std::max(0.f, content.h - titleH);
		node.content = content;
	}

	if (node.type == wt::settingrow) {
		// Corps sous l'en-tête (titre + description), hauteur identique au rendu.
		const float titleH = c.fontSize + 4.f;
		const float descH = node.placeholder.empty() ? 0.f : c.fontSize - 1.f;
		const float headH = titleH + descH;
		content.y += headH;
		content.h = std::max(0.f, content.h - headH);
		node.content = content;
	}

	// Enfants positionnés (`position: absolute`) : hors du flux, placés dans la boîte
	// de remplissage du parent par left/top/right/bottom, sinon par leur ancre.
	for (NodePtr &child : node.children)
		if (child->IsVisible() && !child->floating.active && OutOfFlow(*child))
			LayoutPositioned(*child, sdl3::FRect{content.x - padding.left, content.y - padding.top, content.w + padding.H(), content.h + padding.V()},
							 ctx);

	// Enfants.
	std::vector<Node *> kids;
	for (NodePtr &child : node.children)
		if (child->IsVisible() && !child->floating.active && !OutOfFlow(*child))
			kids.push_back(child.get());
	if (kids.empty())
		return;

	// Splitter : deux panneaux séparés par une poignée redimensionnable
	// (le ratio courant est stocké dans `value`, 0..1).
	if (node.type == wt::splitter && kids.size() >= 2) {
		const float ratio = std::clamp(node.value, 0.f, 1.f);
		const float grip = 6.f;
		if (c.IsRow()) {
			const float w0 = std::max(0.f, content.w * ratio - grip * 0.5f);
			LayoutNode(*kids[0], sdl3::FRect{content.x, content.y, w0, content.h}, ctx);
			LayoutNode(*kids[1], sdl3::FRect{content.x + w0 + grip, content.y, std::max(0.f, content.w - w0 - grip), content.h}, ctx);
		} else {
			const float h0 = std::max(0.f, content.h * ratio - grip * 0.5f);
			LayoutNode(*kids[0], sdl3::FRect{content.x, content.y, content.w, h0}, ctx);
			LayoutNode(*kids[1], sdl3::FRect{content.x, content.y + h0 + grip, content.w, std::max(0.f, content.h - h0 - grip)}, ctx);
		}
		return;
	}

	// Conteneur défilant : les enfants s'empilent verticalement sans bornes de
	// hauteur (chacun fixe la sienne), décalés du défilement courant ; le rendu
	// rogne via le clip (overflow hidden).
	if (node.type == wt::scroll) {
		float y = content.y - node.scroll.y;
		float maxW = content.w;
		for (Node *child : kids) {
			const ComputedStyle &cc = child->computed;
			const float h = ContentSize(*m_canvas, *child, b, false) + cc.marginTop + cc.marginBottom;
			const float w = ContentSize(*m_canvas, *child, b, true) + cc.marginLeft + cc.marginRight;
			maxW = std::max(maxW, w); // contenu plus large : barre horizontale
			LayoutNode(*child, sdl3::FRect{content.x - node.scroll.x, y, std::max(content.w, w), h}, ctx);
			y = child->bounds.Bottom() + cc.marginBottom + c.gap;
		}
		node.scroll.contentH = kids.back()->bounds.Bottom() + node.scroll.y - content.y;
		node.scroll.contentW = maxW;
		return;
	}

	if (c.display == Display::Stack) {
		for (Node *child : kids)
			LayoutNode(*child, content, ctx);
		return;
	}
	if (c.display == Display::Grid) {
		const int cols = std::max(1, c.gridColumns);
		const float cellW = (content.w - c.gap * float(cols - 1)) / float(cols);
		float y = content.y;
		for (size_t start = 0; start < kids.size(); start += size_t(cols)) {
			// Hauteur de la ligne : la plus haute cellule (taille du contenu).
			const size_t end = std::min(kids.size(), start + size_t(cols));
			float rowH = 0.f;
			for (size_t i = start; i < end; ++i)
				rowH = std::max(rowH, ContentSize(*m_canvas, *kids[i], b, false));
			for (size_t i = start; i < end; ++i)
				LayoutNode(*kids[i], sdl3::FRect{content.x + float(i - start) * (cellW + c.gap), y, cellW, rowH}, ctx);
			y += rowH + c.gap;
		}
		return;
	}

	// Flex (row / column).
	const bool row = c.IsRow();
	const bool reverse = c.flexDirection == FlexDirection::RowReverse || c.flexDirection == FlexDirection::ColumnReverse;
	const bool wrap = c.flexWrap == FlexWrap::Wrap;
	const float mainSpace = row ? content.w : content.h;
	const float crossSpace = row ? content.h : content.w;

	// Taille principale / transversale de chaque enfant (explicite sinon contenu).
	struct Kid {
		Node *node;
		float main = 0.f, cross = 0.f; // sans marge
		float marginMain = 0.f, marginCross = 0.f;
		float grow = 0.f, shrink = 1.f;
		float floor = 0.f; // taille minimale au rétrécissement
	};
	std::vector<Kid> items;
	items.reserve(kids.size());
	for (Node *child : kids) {
		const ComputedStyle &cc = child->computed;
		const math::Sides m = cc.Margin();
		// Taille intrinsèque (point unique : ContentSize).
		const float iw = ContentSize(*m_canvas, *child, b, true);
		const float ih = ContentSize(*m_canvas, *child, b, false);
		Kid k;
		k.node = child;
		k.grow = cc.flexGrow;
		k.shrink = cc.flexShrink;
		{
			const Length &minLen = row ? cc.minWidth : cc.minHeight;
			const Length &mainLen = row ? cc.width : cc.height;
			if (!mainLen.IsAuto() && !mainLen.IsFr())
				k.floor = mainLen.Resolve(b); // taille explicite : jamais rétrécie
			else if (!minLen.IsAuto())
				k.floor = minLen.Resolve(b);
			else if (!child->text.empty() && child->children.empty())
				k.floor = row ? m_canvas->MeasureText(child->text, cc.fontSize, FontOf(cc)) + 8.f : m_canvas->LineHeight(cc.fontSize, FontOf(cc));
		}
		const Length &mlen = row ? cc.width : cc.height;
		const Length &clen = row ? cc.height : cc.width;
		k.main = (mlen.IsAuto() || mlen.IsFr()) ? (row ? iw : ih) : mlen.Resolve(b);
		k.cross = clen.IsAuto() ? (row ? ih : iw) : clen.Resolve(b);
		if (row) {
			k.marginMain = m.left + m.right;
			k.marginCross = m.top + m.bottom;
		} else {
			k.marginMain = m.top + m.bottom;
			k.marginCross = m.left + m.right;
		}
		items.push_back(k);
	}

	// Répartit l'espace restant (grow) ou le manque (shrink) sur une ligne ; rend
	// l'espace libre restant (pour la justification).
	const auto settle = [&](std::vector<size_t> &idx, float space) -> float {
		float total = 0.f;
		for (size_t i : idx)
			total += items[i].main + items[i].marginMain;
		if (idx.size() > 1)
			total += c.gap * float(idx.size() - 1);
		float free = space - total;
		if (free > 0.f) {
			float growSum = 0.f;
			for (size_t i : idx)
				growSum += items[i].grow;
			if (growSum > 0.f)
				for (size_t i : idx)
					if (items[i].grow > 0.f)
						items[i].main += free * (items[i].grow / growSum);
		} else if (free < 0.f) {
			float deficit = -free;
			for (int pass = 0; pass < 4 && deficit > 0.01f; ++pass) {
				float weighted = 0.f;
				for (size_t i : idx)
					if (items[i].shrink > 0.f && items[i].main > items[i].floor)
						weighted += items[i].shrink * items[i].main;
				if (weighted <= 0.f)
					break;
				float taken = 0.f;
				for (size_t i : idx) {
					Kid &k = items[i];
					if (k.shrink <= 0.f || k.main <= k.floor)
						continue;
					const float cut = std::min(k.main - k.floor, deficit * (k.shrink * k.main / weighted));
					k.main -= cut;
					taken += cut;
				}
				deficit -= taken;
			}
			free = -deficit;
		}
		return free;
	};

	// Justification d'une ligne : rend (écart entre enfants, retrait du premier).
	const auto justifyLine = [&](size_t n, float free) -> std::pair<float, float> {
		float gap = c.gap, lead = 0.f;
		switch (c.justifyContent) {
		case Justify::Center: lead = std::max(0.f, free) * 0.5f; break;
		case Justify::End: lead = std::max(0.f, free); break;
		case Justify::SpaceBetween: if (n > 1) gap = std::max(0.f, free) / float(n - 1); break;
		case Justify::SpaceAround: if (n) { gap = std::max(0.f, free) / float(n); lead = gap * 0.5f; } break;
		case Justify::SpaceEvenly: if (n) { gap = std::max(0.f, free) / float(n + 1); lead = gap; } break;
		default: break;
		}
		return {gap, lead};
	};

	// Lignes : une seule sans retour à la ligne, sinon découpées à la largeur/hauteur.
	struct Line {
		std::vector<size_t> idx;
		float free = 0.f;   // espace libre après grow/shrink
		float cross = 0.f;  // taille transversale de la ligne
	};
	std::vector<Line> lines;
	if (!wrap) {
		Line l;
		l.idx.resize(items.size());
		for (size_t i = 0; i < items.size(); ++i)
			l.idx[i] = i;
		lines.push_back(std::move(l));
	} else {
		Line cur;
		float used = 0.f;
		for (size_t i = 0; i < items.size(); ++i) {
			const float add = items[i].main + items[i].marginMain + (cur.idx.empty() ? 0.f : c.gap);
			if (!cur.idx.empty() && used + add > mainSpace) {
				lines.push_back(std::move(cur));
				cur = Line{};
				used = 0.f;
			}
			cur.idx.push_back(i);
			used += add;
		}
		if (!cur.idx.empty())
			lines.push_back(std::move(cur));
	}

	// Répartition par ligne et taille transversale de chacune.
	for (Line &line : lines) {
		line.free = settle(line.idx, mainSpace);
		float crossMax = 0.f;
		for (size_t i : line.idx)
			crossMax = std::max(crossMax, items[i].cross + items[i].marginCross);
		line.cross = crossMax;
	}
	if (!wrap && !lines.empty()) // ligne unique : elle occupe tout l'axe transversal (comme avant)
		lines[0].cross = std::max(lines[0].cross, crossSpace);

	// Placement : lignes empilées sur l'axe transversal, enfants sur l'axe principal.
	float linePos = row ? content.y : content.x;
	for (Line &line : lines) {
		const auto [gap, lead] = justifyLine(line.idx.size(), line.free);
		float pos = (row ? content.x : content.y) + lead;
		if (reverse) {
			std::reverse(line.idx.begin(), line.idx.end());
			pos = (row ? content.Right() : content.Bottom()) - lead;
		}
		for (size_t i : line.idx) {
			Kid &k = items[i];
			// Boîte marge comprise sur les deux axes : `k.main` et `k.cross` excluent la marge,
			// que LayoutNode retranche lui-même (une seule fois).
			const float mainBox = k.main + k.marginMain;
			// Taille transversale : étirée à la ligne (pas à tout le conteneur).
			const bool stretch = k.cross <= 0.f || c.alignItems == Align::Stretch;
			const float crossBox = stretch ? line.cross : k.cross + k.marginCross;
			Align self = c.alignItems;
			if (k.node->computed.alignSelf)
				self = *k.node->computed.alignSelf;
			float crossPos = linePos;
			switch (self) {
			case Align::Start: break;
			case Align::Center: crossPos += (line.cross - crossBox) * 0.5f; break;
			case Align::End: crossPos += line.cross - crossBox; break;
			default: break;
			}
			sdl3::FRect childBox;
			if (row) {
				const float x = reverse ? pos - mainBox : pos;
				childBox = {x, crossPos, mainBox, crossBox};
			} else {
				const float y = reverse ? pos - mainBox : pos;
				childBox = {crossPos, y, crossBox, mainBox};
			}
			LayoutNode(*k.node, childBox, ctx);
			if (reverse)
				pos -= mainBox + gap;
			else
				pos += mainBox + gap;
		}
		linePos += line.cross + c.gap;
	}
}

void Ui::ApplyTranslations(Node &node, const Translator *tr) {
	if (!node.id.empty()) {
		Node::I18nSource &src = node.i18n;
		if (!src.captured) { // textes des sources, avant toute traduction
			src.captured = true;
			src.text = node.text;
			src.placeholder = node.placeholder;
			src.tooltip = node.tooltip ? node.tooltip->text : std::string();
			src.tabs = node.tabs;
			src.options = node.options;
			src.headers = node.modelView.headers;
			src.menus = node.menus;
		}
		const auto pick = [tr](const std::string &key, const std::string &fallback) -> std::string {
			const std::string *found = tr ? tr->Find(key) : nullptr;
			return found ? *found : fallback;
		};
		node.text = pick(node.id, src.text);
		node.placeholder = pick(node.id + ".placeholder", src.placeholder);
		if (node.tooltip)
			node.tooltip->text = pick(node.id + ".tooltip", src.tooltip);
		for (size_t i = 0; i < node.tabs.size() && i < src.tabs.size(); ++i)
			node.tabs[i] = pick(std::format("{}.{}", node.id, i), src.tabs[i]);
		for (size_t i = 0; i < node.options.size() && i < src.options.size(); ++i)
			node.options[i] = pick(std::format("{}.{}", node.id, i), src.options[i]);
		for (size_t i = 0; i < node.modelView.headers.size() && i < src.headers.size(); ++i)
			node.modelView.headers[i] = pick(std::format("{}.{}", node.id, i), src.headers[i]);
		for (size_t m = 0; m < node.menus.size() && m < src.menus.size(); ++m) {
			Menu &menu = node.menus[m];
			// Menu contextuel : un seul menu sans titre, items en `clé.N`.
			const std::string base = node.contextMenu ? node.id : std::format("{}.{}", node.id, m);
			if (!node.contextMenu)
				menu.title = pick(base, src.menus[m].title);
			for (size_t i = 0; i < menu.items.size() && i < src.menus[m].items.size(); ++i)
				if (!menu.items[i].separator)
					menu.items[i].label = pick(std::format("{}.{}", base, i), src.menus[m].items[i].label);
		}
	}
	for (NodePtr &child : node.children)
		ApplyTranslations(*child, tr);
}

Theme DefaultRetainedTheme() {
	// Couleurs historiques de l'interface retenue (avant les thèmes) : aucun changement
	// visuel pour les applications qui n'appellent pas Ui::SetTheme.
	Theme t;
	t.background = sdl3::Rgba(14, 16, 21);
	t.panel = sdl3::Rgba(24, 30, 46, 250);
	t.panelBorder = sdl3::Rgba(48, 58, 84);
	t.title = sdl3::Rgba(30, 34, 43);
	t.textDim = sdl3::Rgba(146, 154, 170);
	t.widget = sdl3::Rgba(40, 46, 66);
	t.widgetHover = sdl3::Rgba(48, 58, 86);
	t.widgetActive = sdl3::Rgba(62, 74, 108);
	t.info = sdl3::Rgba(86, 156, 255);
	return t;
}

void Ui::LayoutPositioned(Node &node, const sdl3::FRect &container, const LayoutContext &ctx) {
	const ComputedStyle &c = node.computed;
	const LengthBasis bw{container.w, c.fontSize, ctx.rootFontSize, ctx.viewportW, ctx.viewportH, 1.f};
	const LengthBasis bh{container.h, c.fontSize, ctx.rootFontSize, ctx.viewportW, ctx.viewportH, 1.f};
	const math::Sides margin = c.Margin();
	// Taille : explicite (pourcentages de la boîte du parent), sinon celle du contenu ;
	// étirée entre left et right (top et bottom) quand les deux sont donnés.
	float w = c.width.IsAuto() ? ContentSize(*m_canvas, node, bw, true) : c.width.Resolve(bw);
	float h = c.height.IsAuto() ? ContentSize(*m_canvas, node, bh, false) : c.height.Resolve(bh);
	if (c.width.IsAuto() && !c.left.IsAuto() && !c.right.IsAuto())
		w = container.w - c.left.Resolve(bw) - c.right.Resolve(bw) - margin.H();
	if (c.height.IsAuto() && !c.top.IsAuto() && !c.bottom.IsAuto())
		h = container.h - c.top.Resolve(bh) - c.bottom.Resolve(bh) - margin.V();
	if (!c.minWidth.IsAuto())
		w = std::max(w, c.minWidth.Resolve(bw));
	if (!c.maxWidth.IsAuto())
		w = std::min(w, c.maxWidth.Resolve(bw));
	if (!c.minHeight.IsAuto())
		h = std::max(h, c.minHeight.Resolve(bh));
	if (!c.maxHeight.IsAuto())
		h = std::min(h, c.maxHeight.Resolve(bh));
	w = std::max(0.f, w);
	h = std::max(0.f, h);
	const sdl3::FRect anchored = Place(container, w + margin.H(), h + margin.V(), c.anchor);
	float x = anchored.x, y = anchored.y;
	if (!c.left.IsAuto())
		x = container.x + c.left.Resolve(bw);
	else if (!c.right.IsAuto())
		x = container.Right() - c.right.Resolve(bw) - w - margin.H();
	if (!c.top.IsAuto())
		y = container.y + c.top.Resolve(bh);
	else if (!c.bottom.IsAuto())
		y = container.Bottom() - c.bottom.Resolve(bh) - h - margin.V();
	// La marge est retranchée par LayoutNode : on lui passe la boîte marge comprise,
	// avec la taille calculée imposée.
	Node &n = node;
	const Length savedW = n.computed.width, savedH = n.computed.height;
	n.computed.width = Length::Px(w);
	n.computed.height = Length::Px(h);
	LayoutNode(n, sdl3::FRect{x, y, w + margin.H(), h + margin.V()}, ctx);
	n.computed.width = savedW;
	n.computed.height = savedH;
}

// ============================================================================
// Rendu
// ============================================================================

size_t Ui::CodeHitOf(const Node &node, float x, float y) const {
	CodeEditorOptions options;
	options.syntax = node.syntax.get();
	return CodeHit(*m_canvas, LayoutCode(*m_canvas, node.content, node.text, node.textEdit, options, node.computed.fontSize), node.text, x, y);
}

size_t Ui::TextHit(const Node &node, std::string_view shown, float x, float y) const {
	const Node::TextView &v = node.textView;
	const auto measure = [&](std::string_view s) { return m_canvas->MeasureText(s, v.size, v.font); };
	// Ligne sous le point, puis position dans la ligne (abscisse propre à la ligne si alignée).
	size_t line = 0, index = 0;
	const int target = v.lineHeight > 0.f ? int(std::max(0.f, y - v.y) / v.lineHeight) : 0;
	for (int i = 0; i < target; ++i) {
		const size_t end = TextEdit::LineEnd(shown, line);
		if (end >= shown.size())
			break;
		line = end + 1;
		++index;
	}
	const float lx = index < v.lineX.size() ? v.lineX[index] : v.x;
	const std::string_view row = shown.substr(line, TextEdit::LineEnd(shown, line) - line);
	return line + TextEdit::HitTest(row, x - lx, 0.f, v.lineHeight, measure);
}

void Ui::DrawEditableText(Node &node, const ComputedStyle &c, float size, sdl3::CanvasFont font, bool multiline) {
	TextEdit &ed = node.textEdit;
	ed.multiline = multiline;
	ed.Clamp(node.text);
	const float lh = m_canvas->LineHeight(size, font);
	const float pad = c.paddingLeft + 4.f;
	const float top = multiline ? node.content.y : node.content.y + (node.content.h - lh) * 0.5f;
	const bool focused = &node == m_focus;
	// Mot de passe : affichage masqué, positions converties en caractères.
	const std::string shown = node.password ? MaskedText(node.text) : node.text;
	TextEdit view = ed;
	if (node.password) {
		view.cursor = TextEdit::ByteOfChar(shown, TextEdit::CharCount(node.text, ed.cursor));
		view.anchor = TextEdit::ByteOfChar(shown, TextEdit::CharCount(node.text, ed.anchor));
	}
	const auto measure = [&](std::string_view s) { return m_canvas->MeasureText(s, size, font); };
	if (focused) { // le curseur reste visible
		const auto [cx, cy] = view.CaretPosition(shown, lh, measure);
		const float viewW = node.content.w - 2.f * pad, viewH = node.content.h;
		ed.scrollX = std::clamp(ed.scrollX, std::max(0.f, cx - viewW + 2.f), cx);
		if (multiline)
			ed.scrollY = std::clamp(ed.scrollY, std::max(0.f, cy + lh - viewH), cy);
	} else if (!multiline) {
		ed.scrollX = 0.f;
	}
	const float x = node.content.x + pad - ed.scrollX, y = top - ed.scrollY;
	node.textView = {x, y, size, lh, font, {}};
	if (node.text.empty() && !node.placeholder.empty() && !focused) {
		m_canvas->Text(node.content.x + pad, top, node.placeholder, sdl3::WithAlpha(m_theme.textDim, 180), size, font);
		return;
	}
	const bool caretOn = focused && std::fmod(m_time, 1.0) < 0.6;
	view.Draw(*m_canvas, shown, x, y, size, font, c.color, sdl3::WithAlpha(c.accentColor.a > 0 ? c.accentColor : m_theme.accent, focused ? 110 : 60), c.color, caretOn);
}

sdl3::Texture *Ui::TextureFor(const sdl3::Surface &surf) {
	const SDL_Surface *key = surf.Get();
	auto it = m_textures.find(key);
	if (it == m_textures.end()) {
		auto tex = sdl3::Texture::CreateFromSurface(m_canvas->Renderer(), surf);
		if (tex.IsError()) {
			core::log::Warn("ui: {}", tex.Error());
			return nullptr;
		}
		it = m_textures.emplace(key, std::move(tex.Value())).first;
	}
	return &it->second;
}

void Ui::DrawWidgetIcon(const Node &node, const sdl3::FRect &r, Color color, float size) {
	// Icône image : mise à l'échelle dans `r` (rapport d'aspect conservé).
	if (node.iconImage && *node.iconImage && node.iconImage->Width() > 0 && node.iconImage->Height() > 0) {
		sdl3::Texture *tex = TextureFor(*node.iconImage->Surface());
		if (!tex)
			return;
		const int iw = node.iconImage->Width(), ih = node.iconImage->Height();
		const float scale = std::min(r.w / float(iw), r.h / float(ih));
		const float w = float(iw) * scale, h = float(ih) * scale;
		// Icône monochrome (SVG blanc) : teintée comme un glyphe.
		m_canvas->Image(*tex, {r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h}, node.tintIcon ? color : sdl3::Color{255, 255, 255, 255});
		return;
	}
	// Glyphe de la police d'icônes.
	if (node.icon)
		m_canvas->Icon(r, node.icon, color, size);
}

void Ui::RenderNode(Node &node) {
	if (!node.IsVisible())
		return;
	const ComputedStyle &c = node.computed;
	const sdl3::FRect &box = node.bounds;
	const sdl3::CanvasFont font = FontOf(c);
	const float size = c.fontSize;

	// Couleurs sémantiques résolues : CSS (`accent-color`, `widget-color`…) sinon thème.
	const sdl3::Color accentCol = c.accentColor.a > 0 ? c.accentColor : m_theme.accent;
	const sdl3::Color widgetCol = c.widgetColor.a > 0 ? c.widgetColor : m_theme.widget;
	const sdl3::Color widgetHoverCol = c.widgetHoverColor.a > 0 ? c.widgetHoverColor : m_theme.widgetHover;
	const sdl3::Color widgetActiveCol = c.widgetActiveColor.a > 0 ? c.widgetActiveColor : m_theme.widgetActive;
	const sdl3::Color titleCol = c.titleColor.a > 0 ? c.titleColor : m_theme.title;

	// Fond + bordure.
	if (c.backgroundColor.a > 0) {
		const float r = c.borderTopLeftRadius;
		if (r > 0.f)
			m_canvas->FillRoundRect(box, r, c.backgroundColor);
		else
			m_canvas->FillRect(box, c.backgroundColor);
	}
	if (c.HasBorder()) {
		const float r = c.borderTopLeftRadius;
		if (r > 0.f)
			m_canvas->StrokeRoundRect(box, r, c.borderColor, c.borderTopWidth);
		else
			m_canvas->StrokeRect(box, c.borderColor, c.borderTopWidth);
	}

	// Le contenu propre du widget (texte, icônes, titre) ne déborde pas de sa boîte ;
	// un conteneur en overflow hidden / scroll / auto rogne en plus à sa boîte de contenu.
	const bool clips = c.Clips();
	m_canvas->PushClip(box);
	if (clips)
		m_canvas->PushClip(node.content);

	switch (KindOf(node)) {
	case WidgetKind::Text: {
		const float y = node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f;
		const std::string shown = node.ellipsis ? EllipsizeText(*m_canvas, node.text, node.content.w, size, font) : node.text;
		float x = node.content.x;
		switch (c.textAlign) {
		case TextAlign::Center: x = node.content.x + (node.content.w - m_canvas->MeasureText(shown, size, font)) * 0.5f; break;
		case TextAlign::Right: x = node.content.Right() - m_canvas->MeasureText(shown, size, font); break;
		default: break;
		}
		m_canvas->Text(x, y, shown, c.color, size, font);
		break;
	}
	case WidgetKind::Label: {
		const float lh = m_canvas->LineHeight(size, font);
		float y = node.content.y;
		node.textView = {node.content.x, node.content.y, size, lh, font, {}};
		// Retour à la ligne automatique : une ligne par fragment enroulé (sans sélection).
		if (node.wordWrap) {
			for (const std::string &line : WrapText(*m_canvas, node.text, node.content.w, size, font)) {
				float x = node.content.x;
				if (c.textAlign == TextAlign::Center)
					x += (node.content.w - m_canvas->MeasureText(line, size, font)) * 0.5f;
				else if (c.textAlign == TextAlign::Right)
					x = node.content.Right() - m_canvas->MeasureText(line, size, font);
				node.textView.lineX.push_back(x);
				m_canvas->Text(x, y, line, c.color, size, font);
				if (c.underline) {
					const float w = m_canvas->MeasureText(line, size, font);
					m_canvas->Line(x, y + lh - 1.f, x + w, y + lh - 1.f, c.color, 1.f);
				}
				y += lh;
			}
			break;
		}
		// Libellé multi-lignes (retours `\n`) : couleurs, gras, italique, souligné, sélection.
		size_t start = 0;
		const bool selected = &node == m_selNode;
		if (selected)
			node.textEdit.Clamp(node.text);
		const auto [selA, selB] = node.textEdit.Range();
		while (true) {
			const size_t nl = node.text.find('\n', start);
			const std::string_view line = std::string_view(node.text).substr(start, nl == std::string::npos ? std::string::npos : nl - start);
			float x = node.content.x;
			if (c.textAlign == TextAlign::Center)
				x += (node.content.w - m_canvas->MeasureText(line, size, font)) * 0.5f;
			else if (c.textAlign == TextAlign::Right)
				x = node.content.Right() - m_canvas->MeasureText(line, size, font);
			node.textView.lineX.push_back(x);
			if (selected && selA < selB) { // fond de la sélection sur cette ligne
				const size_t s = std::max(selA, start), e = std::min(selB, start + line.size());
				if (s < e) {
					const float xa = m_canvas->MeasureText(line.substr(0, s - start), size, font), xb = m_canvas->MeasureText(line.substr(0, e - start), size, font);
					m_canvas->FillRect({x + xa, y, xb - xa, lh}, sdl3::WithAlpha(accentCol, 110));
				}
			}
			m_canvas->Text(x, y, line, c.color, size, font);
			if (c.underline) {
				const float w = m_canvas->MeasureText(line, size, font);
				m_canvas->Line(x, y + lh - 1.f, x + w, y + lh - 1.f, c.color, 1.f);
			}
			y += lh;
			if (nl == std::string::npos)
				break;
			start = nl + 1;
		}
		break;
	}
	case WidgetKind::Button: {
		const float s = std::min(node.content.h - 4.f, 22.f);
		if (node.iconImage || node.icon) {
			const sdl3::FRect iconRect{node.content.x + 6.f, node.content.y + (node.content.h - s) * 0.5f, s, s};
			DrawWidgetIcon(node, iconRect, c.color, s);
			const sdl3::FRect textRect{node.content.x + s + 6.f, node.content.y,
									  std::max(0.f, node.content.w - s - 6.f), node.content.h};
			m_canvas->TextCentered(textRect, node.text, c.color, size, font);
		} else {
			m_canvas->TextCentered(node.content, node.text, c.color, size, font);
		}
		break;
	}
	case WidgetKind::Slider: {
		// Course rentrée du rayon du bouton : il reste entier à 0 % comme à 100 %.
		const SliderTrack course = MakeSliderTrack(node.content, node.content.h * 0.5f);
		const float trackH = std::min(6.f, node.content.h * 0.5f), cy = node.content.y + node.content.h * 0.5f;
		const sdl3::FRect track{course.x0, cy - trackH * 0.5f, course.width, trackH};
		m_canvas->FillRoundRect(track, trackH * 0.5f, c.backgroundColor.a > 0 ? c.backgroundColor : widgetActiveCol);
		const float t = std::clamp((node.value - node.valueMin) / std::max(1e-6f, node.valueMax - node.valueMin), 0.f, 1.f);
		const float cx = course.Position(t);
		if (cx > track.x) // partie parcourue, en couleur d'accent
			m_canvas->FillRoundRect({track.x, track.y, cx - track.x, trackH}, trackH * 0.5f, accentCol);
		m_canvas->FillCircle(cx, cy, course.radius, c.color);
		break;
	}
	case WidgetKind::Checkbox: {
		const float s = std::min(node.content.h, 18.f);
		const sdl3::FRect checkboxRect{node.content.x, node.content.y + (node.content.h - s) * 0.5f, s, s};
		m_canvas->FillRoundRect(checkboxRect, 4.f, c.backgroundColor.a > 0 ? c.backgroundColor : widgetCol);
		m_canvas->StrokeRoundRect(checkboxRect, 4.f, c.borderColor.a > 0 ? c.borderColor : m_theme.textDim);
		if (node.checked)
			m_canvas->TextCentered(checkboxRect, "\xE2\x9C\x93", c.color, s * 0.75f, sdl3::CanvasFont::Bold);
		if (!node.text.empty())
			m_canvas->Text(checkboxRect.Right() + 8.f, node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f,
						   node.text, c.color, size, font);
		break;
	}
	case WidgetKind::Badge:
		// Pilule : le fond arrondi est déjà posé par le bloc générique (couleur de
		// fond + rayon de bordure), il ne reste que le texte centré.
		m_canvas->TextCentered(node.content, node.text, c.color, size, font);
		break;
	case WidgetKind::Selectable: {
		// Ligne d'une liste : pleine largeur, surlignée si sélectionnée ou survolée.
		if (node.checked)
			m_canvas->FillRect(node.content, c.backgroundColor.a > 0 ? c.backgroundColor : sdl3::WithAlpha(accentCol, 40));
		else if (HasState(node.state, State::Hover))
			m_canvas->FillRect(node.content, sdl3::WithAlpha(widgetHoverCol, 120));
		if (node.icon || node.iconImage) {
			const float s = std::min(node.content.h, 20.f);
			const sdl3::FRect iconRect{node.content.x + 8.f, node.content.y + (node.content.h - s) * 0.5f, s, s};
			DrawWidgetIcon(node, iconRect, c.color, s);
			m_canvas->Text(node.content.x + 8.f + s + 8.f, node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f,
						   node.text, c.color, size, font);
		} else {
			m_canvas->Text(node.content.x + 10.f, node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f,
						   node.text, c.color, size, font);
		}
		break;
	}
	case WidgetKind::Switch: {
		// Interrupteur à glissière : piste + pouce, libellé facultatif à droite.
		const float trackW = std::min(40.f, node.content.h * 2.f);
		const float trackH = std::min(20.f, node.content.h * 0.8f);
		const sdl3::FRect track{node.content.x, node.content.y + (node.content.h - trackH) * 0.5f, trackW, trackH};
		const float r = trackH * 0.5f;
		m_canvas->FillRoundRect(track, r, node.checked ? (c.backgroundColor.a > 0 ? c.backgroundColor : accentCol) : widgetActiveCol);
		const float thumb = trackH - 4.f;
		const float cx = node.checked ? track.Right() - r : track.x + r;
		m_canvas->FillCircle(cx, track.y + trackH * 0.5f, thumb * 0.5f, c.color.a ? c.color : sdl3::Rgba(255, 255, 255));
		if (!node.text.empty())
			m_canvas->Text(track.Right() + 10.f, node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f,
						   node.text, c.color, size, font);
		break;
	}
	case WidgetKind::MenuButton: {
		// Grande entrée de menu : icône à gauche, titre puis sous-titre (grisé).
		const bool hasIcon = node.icon || (node.iconImage && *node.iconImage);
		const float padX = hasIcon ? 10.f : 14.f;
		const float iconW = hasIcon ? std::min(node.content.h, 26.f) + 10.f : 0.f;
		const float lh = m_canvas->LineHeight(size, font);
		const float subH = node.placeholder.empty() ? 0.f : m_canvas->LineHeight(size - 2.f);
		const float total = lh + subH;
		float ty = node.content.y + (node.content.h - total) * 0.5f;
		if (hasIcon)
			DrawWidgetIcon(node, {node.content.x + 10.f, node.content.y + (node.content.h - std::min(node.content.h, 26.f)) * 0.5f,
								  std::min(node.content.h, 26.f), std::min(node.content.h, 26.f)},
						   c.color, std::min(node.content.h, 26.f));
		m_canvas->Text(node.content.x + padX + iconW, ty, node.text, c.color, size, font);
		ty += lh;
		if (!node.placeholder.empty())
			m_canvas->Text(node.content.x + padX + iconW, ty, node.placeholder, sdl3::WithAlpha(m_theme.textDim, 200), size - 2.f);
		break;
	}
	case WidgetKind::SettingRow: {
		// En-tête : titre (gras) puis description (grisée) — le contrôle est un enfant,
		// placé sous l'en-tête par la mise en page (hauteur réservée identique ici et
		// dans LayoutNode : titre `size + 4`, description `size − 1`).
		const float titleH = size + 4.f;
		const float descH = node.placeholder.empty() ? 0.f : size - 1.f;
		const sdl3::FRect head{node.content.x, node.content.y - titleH - descH, node.content.w, titleH + descH};
		m_canvas->Text(head.x, head.y, node.text, c.color, size, sdl3::CanvasFont::Bold);
		if (!node.placeholder.empty())
			m_canvas->Text(head.x, head.y + titleH, node.placeholder, sdl3::WithAlpha(m_theme.textDim, 200), size - 2.f);
		break;
	}
	case WidgetKind::Progress: {
		const float barH = std::min(8.f, node.content.h);
		const sdl3::FRect track{node.content.x, node.content.y + (node.content.h - barH) * 0.5f, node.content.w, barH};
		m_canvas->FillRoundRect(track, barH * 0.5f, c.backgroundColor.a > 0 ? c.backgroundColor : widgetCol);
		const float t = std::clamp((node.value - node.valueMin) / std::max(1e-6f, node.valueMax - node.valueMin), 0.f, 1.f);
		if (t > 0.f)
			m_canvas->FillRoundRect({track.x, track.y, track.w * t, barH}, barH * 0.5f, c.color);
		break;
	}
	case WidgetKind::Panel: {
		const float titleH = size + 10.f;
		const sdl3::FRect titleBar{node.content.x, node.content.y - titleH, node.content.w, titleH};
		if (!node.text.empty())
			m_canvas->Text(titleBar.x, titleBar.y + (titleBar.h - m_canvas->LineHeight(size, sdl3::CanvasFont::Bold)) * 0.5f,
						   node.text, c.color, size, sdl3::CanvasFont::Bold);
		break;
	}
	case WidgetKind::Modal: {
		// Fond sombre semi-transparent (scrim) : par défaut si aucun fond posé.
		if (c.backgroundColor.a == 0)
			m_canvas->FillRect(node.bounds, sdl3::Rgba(0, 0, 0, 128));
		// Panneau centré (titre + croix).
		const sdl3::FRect &panel = node.panel;
		m_canvas->FillRoundRect(panel, 10.f, sdl3::WithAlpha(m_theme.panel, 255));
		m_canvas->StrokeRoundRect(panel, 10.f, m_theme.panelBorder);
		const float titleH = size + 16.f;
		if (!node.text.empty())
			m_canvas->Text(panel.x + 12.f, panel.y + (titleH - m_canvas->LineHeight(size, sdl3::CanvasFont::Bold)) * 0.5f,
						   node.text, c.color, size, sdl3::CanvasFont::Bold);
		if (node.closeRect.w > 0.f) // bouton croix
			m_canvas->TextCentered(node.closeRect, "\xC3\x97", c.color, size + 2.f, sdl3::CanvasFont::Bold);
		break;
	}
	case WidgetKind::RichText: {
		const std::vector<RichSegment> segs = ParseRichText(node.text);
		const float lh = m_canvas->LineHeight(size, font);
		float x = node.content.x;
		const float y = node.content.y + (node.content.h - lh) * 0.5f;
		node.links.clear();
		for (const RichSegment &s : segs) {
			const sdl3::Color col = s.url.empty() ? c.color : m_theme.info;
			const float w = m_canvas->Text(x, y, s.text, col, size, s.font);
			if (s.underline || !s.url.empty())
				m_canvas->Line(x, y + lh - 1.f, x + w, y + lh - 1.f, col, 1.f);
			if (!s.url.empty())
				node.links.emplace_back(sdl3::FRect{x, y, w, lh}, s.url);
			x += w;
		}
		break;
	}
	case WidgetKind::Image:
		if (node.icon || node.iconImage)
			DrawWidgetIcon(node, node.content, c.color, std::min(node.content.h, 24.f));
		if (!node.text.empty())
			m_canvas->TextCentered(node.content, node.text, c.color, size, font);
		break;
	case WidgetKind::Separator: {
		// `Separator()` est en ligne (filet horizontal) ; `.Vertical()` le passe en colonne.
		if (!c.IsRow()) { // ligne verticale
			const float cx = node.content.x + node.content.w * 0.5f;
			m_canvas->Line(cx, node.content.y, cx, node.content.Bottom(), c.borderColor.a > 0 ? c.borderColor : m_theme.panelBorder, 1.f);
		} else { // ligne horizontale
			const float cy = node.content.y + node.content.h * 0.5f;
			m_canvas->Line(node.content.x, cy, node.content.Right(), cy, c.borderColor.a > 0 ? c.borderColor : m_theme.panelBorder, 1.f);
		}
		break;
	}
	case WidgetKind::Toggle:
		m_canvas->TextCentered(node.content, node.text, c.color, size, font);
		break;
	case WidgetKind::Radio: {
		const float s = std::min(node.content.h, 18.f);
		const sdl3::FRect circle{node.content.x, node.content.y + (node.content.h - s) * 0.5f, s, s};
		m_canvas->FillCircle(circle.x + s * 0.5f, circle.y + s * 0.5f, s * 0.5f, widgetCol);
		m_canvas->FillCircle(circle.x + s * 0.5f, circle.y + s * 0.5f, s * 0.35f, m_theme.background);
		if (node.checked)
			m_canvas->FillCircle(circle.x + s * 0.5f, circle.y + s * 0.5f, s * 0.22f, c.color);
		if (!node.text.empty())
			m_canvas->Text(circle.Right() + 8.f, node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f, node.text, c.color, size, font);
		break;
	}
	case WidgetKind::ToolButton: {
		// Fond : actif (bascule enfoncée), survolé, pressé — si le CSS n'en donne pas.
		if (c.backgroundColor.a == 0) {
			if (node.checked)
				m_canvas->FillRoundRect(box, 6.f, sdl3::WithAlpha(accentCol, 70));
			if (HasState(node.state, State::Active))
				m_canvas->FillRoundRect(box, 6.f, widgetActiveCol);
			else if (HasState(node.state, State::Hover))
				m_canvas->FillRoundRect(box, 6.f, sdl3::WithAlpha(widgetHoverCol, 200));
		}
		sdl3::FRect iconBox = node.content;
		if (node.swatch.a > 0) { // couleur courante sous l'icône
			iconBox.h = std::max(0.f, iconBox.h - 5.f);
			m_canvas->FillRect({node.content.x + 5.f, node.content.Bottom() - 4.f, std::max(0.f, node.content.w - 10.f), 4.f}, node.swatch);
		}
		if (node.icon || node.iconImage)
			DrawWidgetIcon(node, iconBox, node.checked ? accentCol : c.color, std::min(iconBox.h, 20.f));
		break;
	}
	case WidgetKind::ToolGroup: {
		// Légende dans le rembourrage du bas, filet vertical dans celui de droite.
		const sdl3::FRect caption{box.x, box.Bottom() - c.paddingBottom, box.w - c.paddingRight * 0.5f, c.paddingBottom};
		m_canvas->TextCentered(caption, node.text, sdl3::WithAlpha(c.color, 170), kToolGroupCaption, sdl3::CanvasFont::Regular);
		m_canvas->FillRect({box.Right() - 1.f, box.y + 6.f, 1.f, std::max(0.f, box.h - 12.f)}, m_theme.panelBorder);
		break;
	}
	case WidgetKind::LineEdit: {
		DrawEditableText(node, c, size, font, false);
		break;
	}
	case WidgetKind::TextArea: {
		DrawEditableText(node, c, size, font, true);
		break;
	}
	case WidgetKind::CodeEditor: {
		TextEdit &ed = node.textEdit;
		ed.multiline = ed.code = true;
		ed.Clamp(node.text);
		CodeEditorOptions options;
		options.syntax = node.syntax.get();
		options.readOnly = !node.editable;
		options.errorLine = node.errorLine;
		options.placeholder = node.placeholder;
		const bool focused = &node == m_focus;
		if (focused)
			ScrollCodeToCaret(*m_canvas, LayoutCode(*m_canvas, node.content, node.text, ed, options, size), node.text, ed);
		const CodeLayout layout = LayoutCode(*m_canvas, node.content, node.text, ed, options, size);
		node.textView = {layout.originX, layout.originY, size, layout.lineHeight, sdl3::CanvasFont::Mono, {}}; // mesure du clavier (↑ ↓, pages)
		CodePalette palette;
		palette.selection = sdl3::WithAlpha(accentCol, 90);
		palette.error = m_theme.danger;
		DrawCode(*m_canvas, layout, node.text, ed, options, palette, focused, focused && std::fmod(m_time, 1.0) < 0.6);
		break;
	}
	case WidgetKind::SpinBox: {
		const float bw = 22.f;
		const sdl3::FRect minus{node.content.x, node.content.y, bw, node.content.h};
		const sdl3::FRect plus{node.content.Right() - bw, node.content.y, bw, node.content.h};
		m_canvas->FillRect(minus, widgetCol);
		m_canvas->FillRect(plus, widgetCol);
		m_canvas->TextCentered(minus, "\xE2\x88\x92", c.color, size, font); // −
		m_canvas->TextCentered(plus, "+", c.color, size, font);
		m_canvas->TextCentered(node.content, String::From(node.value), c.color, size, font);
		break;
	}
	case WidgetKind::Knob: {
		const float cx = node.content.x + node.content.w * 0.5f;
		const float cy = node.content.y + node.content.h * 0.5f;
		const float r = std::min(node.content.w, node.content.h) * 0.5f - 2.f;
		m_canvas->FillCircle(cx, cy, r, widgetCol);
		const float t = std::clamp((node.value - node.valueMin) / std::max(1e-6f, node.valueMax - node.valueMin), 0.f, 1.f);
		const float angle = -2.35f + t * 4.7f; // -135° à +135°
		m_canvas->Line(cx, cy, cx + std::cos(angle) * r * 0.8f, cy + std::sin(angle) * r * 0.8f, c.color, 2.f);
		m_canvas->FillCircle(cx, cy, 3.f, c.color);
		break;
	}
	case WidgetKind::ComboBox: {
		const float pad = c.paddingLeft + 4.f;
		const std::string current = node.selectedIndex >= 0 && size_t(node.selectedIndex) < node.options.size()
									   ? node.options[size_t(node.selectedIndex)]
									   : std::string{};
		const float y = node.content.y + (node.content.h - m_canvas->LineHeight(size, font)) * 0.5f;
		if (current.empty() && !node.placeholder.empty())
			m_canvas->Text(node.content.x + pad, y, node.placeholder, sdl3::WithAlpha(m_theme.textDim, 180), size, font);
		else
			m_canvas->Text(node.content.x + pad, y, current, c.color, size, font);
		m_canvas->Icon({node.content.Right() - node.content.h, node.content.y, node.content.h, node.content.h}, 0xE098, c.color, size + 2.f);
		break;
	}
	case WidgetKind::Expander: {
		const float titleH = size + 12.f;
		// La mise en page place le corps sous l'en-tête : l'en-tête est juste au-dessus de `content`.
		const sdl3::FRect head{node.content.x, node.content.y - titleH, node.content.w, titleH};
		m_canvas->Icon({head.x, head.y, titleH, titleH}, node.open ? 0xE246 : 0xE15F, c.color, size);
		m_canvas->Text(head.x + titleH, head.y + (titleH - m_canvas->LineHeight(size, sdl3::CanvasFont::Bold)) * 0.5f, node.text, c.color, size, sdl3::CanvasFont::Bold);
		break;
	}
	case WidgetKind::Splitter: {
		const float grip = 6.f;
		const float ratio = std::clamp(node.value, 0.f, 1.f);
		if (c.IsRow()) {
			const float cx = node.content.x + node.content.w * ratio;
			m_canvas->FillRect({cx - grip * 0.5f, node.content.y, grip, node.content.h}, c.borderColor.a > 0 ? c.borderColor : m_theme.panelBorder);
		} else {
			const float cy = node.content.y + node.content.h * ratio;
			m_canvas->FillRect({node.content.x, cy - grip * 0.5f, node.content.w, grip}, c.borderColor.a > 0 ? c.borderColor : m_theme.panelBorder);
		}
		break;
	}
	case WidgetKind::ListView: {
		const float rowH = RowHeight(node);
		const size_t total = node.modelView.model.rowCount();
		const size_t first = size_t(std::max(0.f, node.scroll.y / rowH));
		const size_t visible = size_t(node.content.h / rowH) + 1;
		float y = node.content.y - (node.scroll.y - float(first) * rowH);
		for (size_t i = first; i < first + visible && i < total; ++i) {
			const sdl3::FRect row{node.content.x, y, node.content.w, rowH};
			const bool selected = node.modelView.multiSelect ? node.modelView.selectedRows.count(int(i)) > 0 : (int(i) == node.selectedIndex);
			const bool hovered = row.Contains(m_mouseX, m_mouseY);
			if (selected)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetActiveCol, 220));
			else if (hovered)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetHoverCol, 120));
			float x = row.x + 6.f;
			if (node.modelView.model.icon(i)) {
				m_canvas->Icon({x, row.y, rowH, rowH}, node.modelView.model.icon(i), c.color, size);
				x += rowH;
			}
			m_canvas->Text(x, row.y + (rowH - m_canvas->LineHeight(size, font)) * 0.5f, node.modelView.model.text(i, 0), c.color, size, font);
			y += rowH;
		}
		break;
	}
	case WidgetKind::TreeView: {
		std::vector<FlatRow> rows;
		Flatten(node.modelView.tree, node.modelView.expanded, rows);
		const float rowH = RowHeight(node);
		const float indent = rowH;
		const size_t first = size_t(std::max(0.f, node.scroll.y / rowH));
		const size_t visible = size_t(node.content.h / rowH) + 1;
		float y = node.content.y - (node.scroll.y - float(first) * rowH);
		for (size_t i = first; i < first + visible && i < rows.size(); ++i) {
			const FlatRow &fr = rows[i];
			const sdl3::FRect row{node.content.x, y, node.content.w, rowH};
			const bool selected = node.modelView.selectedId == fr.item->id;
			const bool hovered = row.Contains(m_mouseX, m_mouseY);
			if (selected)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetActiveCol, 220));
			else if (hovered)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetHoverCol, 120));
			float x = row.x + float(fr.depth) * indent;
			if (fr.item->isDir)
				m_canvas->Icon({x, row.y, rowH, rowH}, node.modelView.expanded.count(fr.item->id) ? 0xE246 : 0xE15F, c.color, size - 2);
			x += rowH;
			const char32_t icon = fr.item->icon ? fr.item->icon : (fr.item->isDir ? 0xE2A3 : 0xE1BF);
			m_canvas->Icon({x, row.y, rowH, rowH}, icon, fr.item->iconColor.a ? fr.item->iconColor : c.color, size);
			x += rowH;
			m_canvas->Text(x, row.y + (rowH - m_canvas->LineHeight(size, font)) * 0.5f, fr.item->label, c.color, size, font);
			y += rowH;
		}
		break;
	}
	case WidgetKind::TableView: {
		const float rowH = RowHeight(node);
		const size_t total = node.modelView.model.rowCount();
		const size_t cols = std::max<size_t>(1, node.modelView.model.columnCount());
		// Largeurs de colonnes (ajustables via le glissé des en-têtes).
		std::vector<float> cw(cols, node.content.w / float(cols));
		if (node.modelView.columnWidths.size() == cols)
			cw = node.modelView.columnWidths;
		// En-tête (fixe).
		const sdl3::FRect head{node.content.x, node.content.y, node.content.w, rowH};
		m_canvas->FillRect(head, titleCol);
		const float lh = m_canvas->LineHeight(size, sdl3::CanvasFont::Bold);
		float hx = head.x;
		for (size_t j = 0; j < cols; ++j) {
			const std::string hdr = j < node.modelView.headers.size() ? node.modelView.headers[j] : std::string{};
			std::string label = hdr;
			if (int(j) == node.modelView.sortColumn)
				label += node.modelView.sortDescending ? " ▼" : " ▲";
			m_canvas->Text(hx + 6.f, head.y + (rowH - lh) * 0.5f, label, c.color, size, sdl3::CanvasFont::Bold);
			hx += cw[j];
		}
		// Lignes de données (défilement virtuel).
		const float dataTop = head.Bottom();
		const size_t first = size_t(std::max(0.f, node.scroll.y / rowH));
		const size_t visible = size_t(std::max(0.f, node.content.h - rowH) / rowH) + 1;
		float y = dataTop - (node.scroll.y - float(first) * rowH);
		for (size_t i = first; i < first + visible && i < total; ++i) {
			const sdl3::FRect row{node.content.x, y, node.content.w, rowH};
			const bool selected = node.modelView.multiSelect ? node.modelView.selectedRows.count(int(i)) > 0 : (int(i) == node.selectedIndex);
			const bool hovered = row.Contains(m_mouseX, m_mouseY);
			if (selected)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetActiveCol, 220));
			else if (hovered)
				m_canvas->FillRect(row, sdl3::WithAlpha(widgetHoverCol, 120));
			float x = row.x + 6.f;
			for (size_t j = 0; j < cols; ++j) {
				m_canvas->Text(x, row.y + (rowH - m_canvas->LineHeight(size, font)) * 0.5f, node.modelView.model.text(i, j), c.color, size, font);
				x += cw[j];
			}
			y += rowH;
		}
		break;
	}
	case WidgetKind::TabBar:
	case WidgetKind::TabWidget: {
		// Onglets trop nombreux pour la largeur : largeur naturelle, défilement et flèches.
		const bool tabWidget = KindOf(node) == WidgetKind::TabWidget;
		const TabStrip strip = UpdateTabScroll(*m_canvas, node, TabStripRect(node, tabWidget));
		DrawTabStrip(*m_canvas, m_theme, node, strip, tabWidget, c.color, widgetHoverCol, m_mouseX, m_mouseY);
		break;
	}
	case WidgetKind::MenuBar: {
		float x = node.content.x;
		const float barH = node.content.h;
		for (size_t i = 0; i < node.menus.size(); ++i) {
			const std::string &title = node.menus[i].title;
			const float w = m_canvas->MeasureText(title, size, font) + 20.f;
			const sdl3::FRect item{x, node.content.y, w, barH};
			const bool open = int(i) == node.openMenu;
			const bool hovered = item.Contains(m_mouseX, m_mouseY);
			if (open || hovered)
				m_canvas->FillRect(item, sdl3::WithAlpha(open ? accentCol : widgetHoverCol, open ? 200 : 120));
			m_canvas->Text(x + 10.f, node.content.y + (barH - m_canvas->LineHeight(size, font)) * 0.5f, title, c.color, size, font);
			x += w;
		}
		break;
	}
	case WidgetKind::SubWindow:
	case WidgetKind::Popup: {
		// Cadre par défaut (si aucun fond / bordure CSS posés).
		if (c.backgroundColor.a == 0)
			m_canvas->FillRect(node.bounds, sdl3::WithAlpha(m_theme.panel, 250));
		if (!c.HasBorder())
			m_canvas->StrokeRect(node.bounds, m_theme.panelBorder);
		if (KindOf(node) == WidgetKind::SubWindow) {
			const float titleH = size + 12.f;
			const sdl3::FRect titleBar{node.bounds.x, node.bounds.y, node.bounds.w, titleH};
			m_canvas->FillRect(titleBar, titleCol);
			if (!node.text.empty())
				m_canvas->Text(titleBar.x + 10.f, titleBar.y + (titleH - m_canvas->LineHeight(size, sdl3::CanvasFont::Bold)) * 0.5f, node.text, c.color,
							   size, sdl3::CanvasFont::Bold);
			const sdl3::FRect close{titleBar.Right() - titleH, titleBar.y, titleH, titleH};
			m_canvas->TextCentered(close, "\xC3\x97", c.color, size + 2.f, sdl3::CanvasFont::Bold);
		}
		break;
	}
	case WidgetKind::Plot: {
		const PlotData &p = node.plot;
		const float leftM = 44.f, rightM = 12.f, topM = p.title.empty() ? 8.f : 24.f, bottomM = 26.f;
		const sdl3::FRect area{node.content.x + leftM, node.content.y + topM,
							 std::max(0.f, node.content.w - leftM - rightM), std::max(0.f, node.content.h - topM - bottomM)};
		float xMin = p.xMin, xMax = p.xMax, yMin = p.yMin, yMax = p.yMax;
		if (p.autoFit && !p.series.empty()) {
			bool first = true;
			for (const PlotSeries &s : p.series)
				for (const auto &[x, y] : s.points) {
					if (first) {
						xMin = xMax = x;
						yMin = yMax = y;
						first = false;
					} else {
						xMin = std::min(xMin, x);
						xMax = std::max(xMax, x);
						yMin = std::min(yMin, y);
						yMax = std::max(yMax, y);
					}
				}
			if (xMax <= xMin)
				xMax = xMin + 1.f;
			if (yMax <= yMin)
				yMax = yMin + 1.f;
		}
		if (!p.title.empty())
			m_canvas->TextCentered({node.content.x, node.content.y, node.content.w, topM}, p.title, c.color, size, sdl3::CanvasFont::Bold);
		if (!p.xLabel.empty())
			m_canvas->TextCentered({area.x, area.Bottom(), area.w, bottomM}, p.xLabel, c.color, size - 2);
		if (!p.yLabel.empty())
			m_canvas->TextCentered({node.content.x, area.y, leftM, area.h}, p.yLabel, c.color, size - 2);
		const auto sx = [&](float x) { return area.x + (x - xMin) / (xMax - xMin) * area.w; };
		const auto sy = [&](float y) { return area.Bottom() - (y - yMin) / (yMax - yMin) * area.h; };
		// Grille + graduations.
		const float xStep = NiceStep(xMax - xMin, 6);
		const float yStep = NiceStep(yMax - yMin, 6);
		const float lh = m_canvas->LineHeight(size - 3);
		for (float x = std::ceil(xMin / xStep) * xStep; x <= xMax + 1e-6f; x += xStep) {
			const float px = sx(x);
			m_canvas->Line(px, area.y, px, area.Bottom(), sdl3::WithAlpha(m_theme.panelBorder, 90), 1.f);
			m_canvas->Text(px - 12.f, area.Bottom() + 4.f, std::format("{:g}", x), m_theme.textDim, size - 3);
		}
		for (float y = std::ceil(yMin / yStep) * yStep; y <= yMax + 1e-6f; y += yStep) {
			const float py = sy(y);
			m_canvas->Line(area.x, py, area.Right(), py, sdl3::WithAlpha(m_theme.panelBorder, 90), 1.f);
			m_canvas->Text(area.x - 38.f, py - lh * 0.5f, std::format("{:g}", y), m_theme.textDim, size - 3);
		}
		// Axes.
		m_canvas->Line(area.x, area.y, area.x, area.Bottom(), sdl3::WithAlpha(m_theme.textDim, 150), 1.f);
		m_canvas->Line(area.x, area.Bottom(), area.Right(), area.Bottom(), sdl3::WithAlpha(m_theme.textDim, 150), 1.f);
		// Séries.
		for (const PlotSeries &s : p.series) {
			if (s.points.empty())
				continue;
			switch (s.kind) {
			case PlotKind::Line: {
				std::vector<float> xy;
				xy.reserve(s.points.size() * 2);
				for (const auto &[x, y] : s.points) {
					xy.push_back(sx(x));
					xy.push_back(sy(y));
				}
				m_canvas->Polyline(xy.data(), s.points.size(), s.color, s.width);
				break;
			}
			case PlotKind::Area: {
				const float baseY = sy(yMin);
				for (size_t i = 0; i + 1 < s.points.size(); ++i) {
					const float x0 = sx(s.points[i].first), y0 = sy(s.points[i].second);
					const float x1 = sx(s.points[i + 1].first), y1 = sy(s.points[i + 1].second);
					m_canvas->FillTriangle(x0, y0, x1, y1, x0, baseY, s.color);
					m_canvas->FillTriangle(x1, y1, x1, baseY, x0, baseY, s.color);
				}
				break;
			}
			case PlotKind::Scatter:
				for (const auto &[x, y] : s.points)
					m_canvas->FillCircle(sx(x), sy(y), std::max(1.5f, s.width), s.color);
				break;
			case PlotKind::Bar: {
				const float bw = area.w / float(s.points.size()) * 0.7f;
				for (const auto &[x, y] : s.points) {
					const float cx = sx(x);
					const float top = sy(std::max(0.f, y));
					const float bottom = sy(std::min(0.f, y));
					m_canvas->FillRect({cx - bw * 0.5f, top, bw, std::max(1.f, bottom - top)}, s.color);
				}
				break;
			}
			}
		}
		break;
	}
	case WidgetKind::Pie: {
		const float cx = node.content.x + node.content.w * 0.5f;
		const float cy = node.content.y + node.content.h * 0.5f;
		const float r = std::max(4.f, std::min(node.content.w, node.content.h) * 0.5f - 6.f);
		float total = 0.f;
		for (const PieSlice &s : node.pie)
			total += std::max(0.f, s.value);
		if (total <= 0.f)
			break;
		float angle = -1.5707963f;
		for (const PieSlice &s : node.pie) {
			const float a = std::max(0.f, s.value) / total * 6.2831853f;
			const int segs = std::max(2, int(a / 0.12f));
			for (int i = 0; i < segs; ++i) {
				const float t0 = angle + a * float(i) / float(segs);
				const float t1 = angle + a * float(i + 1) / float(segs);
				m_canvas->FillTriangle(cx, cy, cx + std::cos(t0) * r, cy + std::sin(t0) * r, cx + std::cos(t1) * r, cy + std::sin(t1) * r, s.color);
			}
			angle += a;
		}
		break;
	}
	case WidgetKind::Heatmap: {
		const size_t cols = std::max<size_t>(1, node.heatCols);
		const size_t rows = node.heat.empty() ? 0 : (node.heat.size() + cols - 1) / cols;
		if (rows == 0)
			break;
		const float cw = node.content.w / float(cols);
		const float ch = node.content.h / float(rows);
		const float range = std::max(1e-6f, node.heatMax - node.heatMin);
		for (size_t i = 0; i < node.heat.size(); ++i) {
			const size_t r = i / cols, col = i % cols;
			m_canvas->FillRect({node.content.x + float(col) * cw, node.content.y + float(r) * ch, cw + 1.f, ch + 1.f},
							   Colormap((node.heat[i] - node.heatMin) / range));
		}
		break;
	}
	default:
		break;
	}

	// Contenu personnalisé : le clip rect (boîte de contenu) est déjà posé.
	if (node.customPaint) {
		m_canvas->PushClip(node.content);
		node.customPaint(*m_canvas, node.content);
		m_canvas->PopClip();
	}
	// Feuille dessinée par le contexte immédiat (catalogue de widgets existant).
	if (node.customImmediate && m_immediate) {
		// Les widgets immédiats (placés par Next()) s'empilent dans la boîte de contenu.
		m_canvas->PushClip(node.content);
		m_immediate->BeginColumn(node.content);
		node.customImmediate(*m_immediate);
		m_immediate->EndColumn();
		m_canvas->PopClip();
	}

	// Enfants (rognés à la boîte du parent) : un Expander fermé cache son corps ; un
	// TabWidget n'affiche que l'onglet actif. Les fenêtres flottantes (SubWindow, Popup)
	// sont dessinées ensuite, sans hériter du clip du parent.
	if (KindOf(node) == WidgetKind::TabWidget) {
		if (node.selectedIndex >= 0 && size_t(node.selectedIndex) < node.children.size())
			RenderNode(*node.children[size_t(node.selectedIndex)]);
	} else if (!(KindOf(node) == WidgetKind::Expander && !node.open)) {
		for (NodePtr &child : node.children)
			if (!child->floating.active)
				RenderNode(*child);
	}

	// Barres de défilement d'un conteneur défilant, par-dessus son contenu.
	if (Scrollable(node))
		DrawScrollbars(*m_canvas, m_theme, node, m_mouseX, m_mouseY);

	if (clips)
		m_canvas->PopClip();
	m_canvas->PopClip();

	for (NodePtr &child : node.children)
		if (child->floating.active && child->IsVisible()) {
			m_canvas->PushClip(m_viewport, false);
			RenderNode(*child);
			m_canvas->PopClip();
		}
}

// ============================================================================
// Hit-test
// ============================================================================

void Ui::HitTest(Node &node, float x, float y, Node *&hit) {
	// Un widget désactivé n'est pas interactif : ni lui ni sa descendance.
	if (!node.IsVisible() || node.disabled)
		return;
	if (node.bounds.Contains(x, y) && !node.passThrough) // calque transparent au pointeur : ses enfants seulement
		hit = &node;
	for (NodePtr &child : node.children)
		HitTest(*child, x, y, hit);
}

void Ui::ClearTransient(Node &node) {
	node.clicked = false;
	node.state = node.state & ~(State::Hover | State::Active);
	for (NodePtr &child : node.children)
		ClearTransient(*child);
}

// ============================================================================
// Saisie des widgets
// ============================================================================

void Ui::HandleWidgetInput(Node &node, Node *hit, Node *pressed) {
	const float wheelY = m_events ? m_events->WheelY() : 0.f;
	// Curseur : glisser (appui maintenu sur un slider).
	if (pressed && KindOf(*pressed) == WidgetKind::Slider) {
		const float t = MakeSliderTrack(pressed->content, pressed->content.h * 0.5f).Fraction(m_mouseX); // même course que le dessin
		const float v = pressed->valueMin + t * (pressed->valueMax - pressed->valueMin);
		if (v != pressed->value) {
			pressed->value = v;
			if (pressed->onChanged)
				pressed->onChanged(v);
		}
	}
	// Cadran : glisser verticalement.
	if (pressed && KindOf(*pressed) == WidgetKind::Knob) {
		const float h = std::max(1.f, pressed->content.h);
		const float t = 1.f - std::clamp((m_mouseY - pressed->content.y) / h, 0.f, 1.f);
		const float v = pressed->valueMin + t * (pressed->valueMax - pressed->valueMin);
		if (v != pressed->value) {
			pressed->value = v;
			if (pressed->onChanged)
				pressed->onChanged(v);
		}
	}
	// Séparateur redimensionnable : glisser la poignée (ratio stocké dans `value`).
	if (pressed && KindOf(*pressed) == WidgetKind::Splitter && pressed->children.size() >= 2) {
		const sdl3::FRect &b = pressed->bounds;
		if (pressed->computed.IsRow())
			pressed->value = std::clamp((m_mouseX - b.x) / std::max(1.f, b.w), 0.1f, 0.9f);
		else
			pressed->value = std::clamp((m_mouseY - b.y) / std::max(1.f, b.h), 0.1f, 0.9f);
	}
	// TableView : redimensionnement d'une colonne (glissé de l'en-tête).
	if (pressed && KindOf(*pressed) == WidgetKind::TableView) {
		Node &t = *pressed;
		const float rowH = RowHeight(t);
		const size_t cols = std::max<size_t>(1, t.modelView.model.columnCount());
		if (t.modelView.columnWidths.size() != cols)
			t.modelView.columnWidths.assign(cols, t.content.w / float(cols));
		const bool onHeader = m_mouseY >= t.content.y && m_mouseY < t.content.y + rowH;
		if ((*m_events)[sdl3::MouseButton::Left].IsJustPressed() && onHeader) {
			float x = t.content.x;
			for (size_t j = 0; j < cols; ++j) {
				if (std::fabs(m_mouseX - (x + t.modelView.columnWidths[j])) < 5.f) {
					t.modelView.resizeColumn = int(j);
					t.modelView.resizeStartX = m_mouseX;
					t.modelView.resizeStartWidth = t.modelView.columnWidths[j];
					break;
				}
				x += t.modelView.columnWidths[j];
			}
		}
		if (t.modelView.resizeColumn >= 0) {
			if ((*m_events)[sdl3::MouseButton::Left].IsPressed())
				t.modelView.columnWidths[size_t(t.modelView.resizeColumn)] = std::max(20.f, t.modelView.resizeStartWidth + (m_mouseX - t.modelView.resizeStartX));
			else
				t.modelView.resizeColumn = -1;
		}
	}
	// SubWindow : glissé de la barre de titre + redimensionnement.
	if (pressed && KindOf(*pressed) == WidgetKind::SubWindow) {
		Node &w = *pressed;
		const float titleH = w.computed.fontSize + 12.f;
		const bool inTitle = m_mouseY >= w.bounds.y && m_mouseY < w.bounds.y + titleH;
		if ((*m_events)[sdl3::MouseButton::Left].IsJustPressed()) {
			if (inTitle) {
				w.floating.dragging = true;
				w.floating.dragOffsetX = m_mouseX - w.floating.x;
				w.floating.dragOffsetY = m_mouseY - w.floating.y;
			} else if (w.floating.resizable) {
				w.floating.resizeEdge = ResizeEdgeAt(w, m_mouseX, m_mouseY);
				if (w.floating.resizeEdge >= 0) {
					w.floating.resizeStartX = m_mouseX;
					w.floating.resizeStartY = m_mouseY;
					w.floating.resizeStartW = w.floating.w;
					w.floating.resizeStartH = w.floating.h;
				}
			}
		}
		if (w.floating.dragging && (*m_events)[sdl3::MouseButton::Left].IsPressed()) {
			w.floating.x = m_mouseX - w.floating.dragOffsetX;
			w.floating.y = m_mouseY - w.floating.dragOffsetY;
		}
		if (w.floating.resizeEdge >= 0) {
			if ((*m_events)[sdl3::MouseButton::Left].IsPressed()) {
				const float dx = m_mouseX - w.floating.resizeStartX, dy = m_mouseY - w.floating.resizeStartY;
				if (w.floating.resizeEdge == 0 || w.floating.resizeEdge == 3 || w.floating.resizeEdge == 6) {
					w.floating.x = std::min(w.floating.resizeStartX + dx, w.floating.resizeStartX + w.floating.resizeStartW - 80.f);
					w.floating.w = std::max(80.f, w.floating.resizeStartW - dx);
				}
				if (w.floating.resizeEdge == 1 || w.floating.resizeEdge == 2 || w.floating.resizeEdge == 7)
					w.floating.w = std::max(80.f, w.floating.resizeStartW + dx);
				if (w.floating.resizeEdge == 0 || w.floating.resizeEdge == 1 || w.floating.resizeEdge == 4) {
					w.floating.y = std::min(w.floating.resizeStartY + dy, w.floating.resizeStartY + w.floating.resizeStartH - 60.f);
					w.floating.h = std::max(60.f, w.floating.resizeStartH - dy);
				}
				if (w.floating.resizeEdge == 2 || w.floating.resizeEdge == 3 || w.floating.resizeEdge == 5)
					w.floating.h = std::max(60.f, w.floating.resizeStartH + dy);
			} else {
				w.floating.resizeEdge = -1;
			}
		}
	}
	// Barres de défilement : boutons fléchés (pas + répétition), puis glissé du curseur.
	if (m_scrollDrag && !(*m_events)[sdl3::MouseButton::Left].IsPressed())
		m_scrollDrag = nullptr; // relâché
	if (pressed && Scrollable(*pressed) && !m_scrollDrag) {
		Node &s = *pressed;
		const sdl3::Trigger left = (*m_events)[sdl3::MouseButton::Left];
		const bool just = left.IsJustPressed(), down = left.IsPressed();
		// Bouton fléché sous la souris (barre verticale puis horizontale).
		int dir = 0;
		bool vertical = false;
		for (const bool v : {true, false}) {
			if (v ? s.scroll.contentH <= s.content.h : s.scroll.contentW <= s.content.w)
				continue;
			const ScrollParts p = ScrollPartsOf(ScrollTrack(s, v), v);
			if (p.back.Contains(m_mouseX, m_mouseY)) {
				dir = -1;
				vertical = v;
				break;
			}
			if (p.forward.Contains(m_mouseX, m_mouseY)) {
				dir = +1;
				vertical = v;
				break;
			}
		}
		if (dir != 0) {
			if (just) { // premier appui : pas immédiat, puis répétition cadencée
				s.scroll.arrowDir = dir;
				s.scroll.arrowRepeat = 0.35f;
				ScrollStep(s, vertical, float(dir) * s.scroll.arrowStep);
			} else if (down && s.scroll.arrowDir == dir) {
				s.scroll.arrowRepeat -= m_dt;
				if (s.scroll.arrowRepeat <= 0.f) {
					s.scroll.arrowRepeat = 0.04f;
					ScrollStep(s, vertical, float(dir) * s.scroll.arrowStep);
				}
			}
		} else {
			s.scroll.arrowDir = 0; // souris hors des flèches : répétition arrêtée
		}
		// Glissé du curseur : appui sur la piste (hors des flèches).
		if (just && dir == 0) {
			if (s.scroll.contentH > s.content.h && ScrollTrack(s, true).Contains(m_mouseX, m_mouseY)) {
				m_scrollDrag = &s;
				m_scrollGrab = m_mouseY - ScrollPartsOf(ScrollTrack(s, true), true).track.y;
			} else if (s.scroll.contentW > s.content.w && ScrollTrack(s, false).Contains(m_mouseX, m_mouseY)) {
				m_scrollDrag = &s;
				m_scrollGrab = m_mouseX - ScrollPartsOf(ScrollTrack(s, false), false).track.x;
			}
		}
	}
	if (m_scrollDrag) {
		Node &s = *m_scrollDrag;
		const auto thumbOf = [](float length, float viewport, float content) { return std::max(20.f, length * (viewport / content)); };
		// Le curseur se déplace dans la piste restante (hors des flèches).
		if (s.scroll.contentH > s.content.h) {
			const sdl3::FRect t = ScrollPartsOf(ScrollTrack(s, true), true).track;
			const float travel = t.h - thumbOf(t.h, s.content.h, s.scroll.contentH);
			const float f = travel > 0.f ? std::clamp((m_mouseY - t.y - m_scrollGrab) / travel, 0.f, 1.f) : 0.f;
			s.scroll.y = f * (s.scroll.contentH - s.content.h);
		} else if (s.scroll.contentW > s.content.w) {
			const sdl3::FRect t = ScrollPartsOf(ScrollTrack(s, false), false).track;
			const float travel = t.w - thumbOf(t.w, s.content.w, s.scroll.contentW);
			const float f = travel > 0.f ? std::clamp((m_mouseX - t.x - m_scrollGrab) / travel, 0.f, 1.f) : 0.f;
			s.scroll.x = f * (s.scroll.contentW - s.content.w);
		}
	}
	// Défilement : molette sur une zone de défilement, une liste ou un tableau.
	const float wheelX = m_events ? m_events->WheelX() : 0.f;
	if (wheelY != 0.f || wheelX != 0.f) {
		for (Node *p = hit; p; p = p->parent) {
			const float along = wheelX != 0.f ? wheelX : wheelY; // molette verticale : en largeur s'il n'y a rien d'autre
			if (KindOf(*p) == WidgetKind::TabBar || KindOf(*p) == WidgetKind::TabWidget) {
				const TabStrip strip = TabStripOf(*m_canvas, *p, TabStripRect(*p, KindOf(*p) == WidgetKind::TabWidget));
				if (strip.overflow && strip.area.Contains(m_mouseX, m_mouseY)) {
					p->scroll.x = std::clamp(p->scroll.x - along * 48.f, 0.f, strip.MaxScroll());
					break; // la molette sert aux onglets, pas à la page en dessous
				}
			} else if (KindOf(*p) == WidgetKind::Scroll) {
				const float maxScroll = std::max(0.f, p->scroll.contentH - p->content.h);
				const float maxScrollX = std::max(0.f, p->scroll.contentW - p->content.w);
				if (wheelY != 0.f && maxScroll > 0.f)
					p->scroll.y = std::clamp(p->scroll.y - wheelY * 48.f, 0.f, maxScroll);
				else if (maxScrollX > 0.f) // seulement trop large (ruban…) : défilement en largeur
					p->scroll.x = std::clamp(p->scroll.x - along * 48.f, 0.f, maxScrollX);
			} else if (wheelY != 0.f && (KindOf(*p) == WidgetKind::ListView || KindOf(*p) == WidgetKind::TreeView || KindOf(*p) == WidgetKind::TableView)) {
				const float rowH = RowHeight(*p);
				const float viewportH = KindOf(*p) == WidgetKind::TableView ? p->content.h - rowH : p->content.h;
				const float maxScroll = std::max(0.f, p->scroll.contentH - viewportH);
				p->scroll.y = std::clamp(p->scroll.y - wheelY * rowH, 0.f, maxScroll);
			}
		}
	}
	// Clics : boutons, cases, radios, champs…
	std::function<void(Node &)> dispatch = [&](Node &n) {
		if (n.clicked) {
			if (n.disabled)
				return; // défense en profondeur (HitTest l'exclut déjà)
			switch (KindOf(n)) {
			case WidgetKind::Button:
			case WidgetKind::ToolButton:
				if (n.onClick)
					n.onClick();
				break;
			case WidgetKind::Toggle:
				n.checked = !n.checked;
				break;
			case WidgetKind::Switch:
				n.checked = !n.checked;
				if (n.onClick)
					n.onClick();
				break;
			case WidgetKind::Selectable:
				// Sélection pilotée par l'application (comme `Context::Selectable`) :
				// le clic notifie (`onClick` / `Bind(bool)`) sans basculer `checked`.
				if (n.onClick)
					n.onClick();
				break;
			case WidgetKind::MenuButton:
				if (n.onClick)
					n.onClick();
				break;
			case WidgetKind::Radio: {
				// Les radios d'un même groupe s'excluent mutuellement.
				if (!n.group.empty()) {
					std::function<void(Node &)> uncheck = [&](Node &x) {
						if (KindOf(x) == WidgetKind::Radio && x.group == n.group && &x != &n)
							x.checked = false;
						for (NodePtr &c : x.children)
							uncheck(*c);
					};
					uncheck(node);
				}
				n.checked = true;
				break;
			}
			case WidgetKind::Checkbox:
				n.checked = !n.checked;
				break;
			case WidgetKind::ComboBox:
				n.open = !n.open;
				m_openCombo = n.open ? &n : nullptr;
				break;
			case WidgetKind::Expander:
				n.open = !n.open;
				break;
			case WidgetKind::SpinBox: {
				const float bw = 22.f;
				const sdl3::FRect &b = n.bounds;
				const bool inc = m_mouseX >= b.Right() - bw;
				const bool dec = m_mouseX <= b.x + bw;
				if (inc || dec) {
					const float v = std::clamp(n.value + (inc ? n.step : -n.step), n.valueMin, n.valueMax);
					if (v != n.value) {
						n.value = v;
						if (n.onChanged)
							n.onChanged(v);
					}
				}
				break;
			}
			case WidgetKind::RichText:
				if (n.onLink)
					for (const auto &[rect, url] : n.links)
						if (rect.Contains(m_mouseX, m_mouseY)) {
							n.onLink(url);
							break;
						}
				break;
			case WidgetKind::ListView: {
				const float rowH = RowHeight(n);
				const size_t total = n.modelView.model.rowCount();
				const int idx = int((m_mouseY - n.content.y + n.scroll.y) / rowH);
				if (idx < 0 || size_t(idx) >= total)
					break;
				if (n.modelView.multiSelect) {
					if (m_events->Shift()) {
						if (n.modelView.selectedRows.count(idx)) n.modelView.selectedRows.erase(idx);
						else n.modelView.selectedRows.insert(idx);
					} else {
						n.modelView.selectedRows = {idx};
					}
				}
				n.selectedIndex = idx;
				if (n.modelView.onSelect)
					n.modelView.onSelect(idx);
				const bool dbl = (m_lastClickNode == &n) && (m_time - m_lastClickTime < 0.4);
				m_lastClickNode = &n;
				m_lastClickTime = m_time;
				if (dbl && n.modelView.onActivate)
					n.modelView.onActivate(idx);
				break;
			}
			case WidgetKind::TreeView: {
				std::vector<FlatRow> rows;
				Flatten(n.modelView.tree, n.modelView.expanded, rows);
				const float rowH = RowHeight(n);
				const int idx = int((m_mouseY - n.content.y + n.scroll.y) / rowH);
				if (idx < 0 || size_t(idx) >= rows.size())
					break;
				const FlatRow &fr = rows[size_t(idx)];
				const float arrowX = n.content.x + float(fr.depth) * rowH;
				const bool onArrow = fr.item->isDir && m_mouseX >= arrowX && m_mouseX < arrowX + rowH;
				if (onArrow) {
					if (n.modelView.expanded.count(fr.item->id)) n.modelView.expanded.erase(fr.item->id);
					else n.modelView.expanded.insert(fr.item->id);
				} else {
					n.modelView.selectedId = fr.item->id;
					if (n.modelView.onSelectId)
						n.modelView.onSelectId(fr.item->id);
					const bool dbl = (m_lastClickNode == &n) && (m_time - m_lastClickTime < 0.4);
					m_lastClickNode = &n;
					m_lastClickTime = m_time;
					if (dbl && fr.item->isDir) {
						if (n.modelView.expanded.count(fr.item->id)) n.modelView.expanded.erase(fr.item->id);
						else n.modelView.expanded.insert(fr.item->id);
					}
				}
				break;
			}
			case WidgetKind::TableView: {
				const float rowH = RowHeight(n);
				const size_t cols = std::max<size_t>(1, n.modelView.model.columnCount());
				const bool inHeader = m_mouseY < n.content.y + rowH;
				if (inHeader) {
					// Tri : clic au milieu d'une cellule d'en-tête.
					std::vector<float> cw(cols, n.content.w / float(cols));
					if (n.modelView.columnWidths.size() == cols)
						cw = n.modelView.columnWidths;
					float x = n.content.x;
					for (size_t j = 0; j < cols; ++j) {
						if (m_mouseX >= x && m_mouseX < x + cw[j]) {
							if (n.modelView.sortColumn == int(j))
								n.modelView.sortDescending = !n.modelView.sortDescending;
							else {
								n.modelView.sortColumn = int(j);
								n.modelView.sortDescending = false;
							}
							break;
						}
						x += cw[j];
					}
				} else {
					const size_t total = n.modelView.model.rowCount();
					const int idx = int((m_mouseY - (n.content.y + rowH) + n.scroll.y) / rowH);
					if (idx < 0 || size_t(idx) >= total)
						break;
					if (n.modelView.multiSelect) {
						if (m_events->Shift()) {
							if (n.modelView.selectedRows.count(idx)) n.modelView.selectedRows.erase(idx);
							else n.modelView.selectedRows.insert(idx);
						} else {
							n.modelView.selectedRows = {idx};
						}
					}
					n.selectedIndex = idx;
					if (n.modelView.onSelect)
						n.modelView.onSelect(idx);
					const bool dbl = (m_lastClickNode == &n) && (m_time - m_lastClickTime < 0.4);
					m_lastClickNode = &n;
					m_lastClickTime = m_time;
					if (dbl && n.modelView.onActivate)
						n.modelView.onActivate(idx);
				}
				break;
			}
			case WidgetKind::TabBar:
			case WidgetKind::TabWidget: {
				const TabStrip strip = TabStripOf(*m_canvas, n, TabStripRect(n, KindOf(n) == WidgetKind::TabWidget));
				if (strip.overflow && (strip.back.Contains(m_mouseX, m_mouseY) || strip.forward.Contains(m_mouseX, m_mouseY))) {
					const float step = strip.area.w * 0.6f * (strip.back.Contains(m_mouseX, m_mouseY) ? -1.f : 1.f);
					n.scroll.x = std::clamp(n.scroll.x + step, 0.f, strip.MaxScroll());
					break;
				}
				if (!strip.area.Contains(m_mouseX, m_mouseY))
					break;
				for (size_t i = 0; i < strip.tabs.size(); ++i)
					if (strip.tabs[i].Contains(m_mouseX, m_mouseY)) {
						n.selectedIndex = int(i);
						n.scroll.revealed = int(i); // déjà visible : pas de défilement
					}
				break;
			}
			case WidgetKind::MenuBar: {
				const float barH = n.content.h;
				const sdl3::CanvasFont f = FontOf(n.computed);
				float x = n.content.x;
				int clicked = -1;
				for (size_t i = 0; i < n.menus.size(); ++i) {
					const float w = m_canvas->MeasureText(n.menus[i].title, n.computed.fontSize, f) + 20.f;
					if (m_mouseY >= n.content.y && m_mouseY < n.content.y + barH && m_mouseX >= x && m_mouseX < x + w) {
						clicked = int(i);
						break;
					}
					x += w;
				}
				if (clicked >= 0) {
					n.openMenu = (n.openMenu == clicked) ? -1 : clicked;
					n.openSubMenu = -1;
					m_openMenuBar = n.openMenu >= 0 ? &n : nullptr;
				} else {
					n.openMenu = -1;
					n.openSubMenu = -1;
					m_openMenuBar = nullptr;
				}
				m_menuHover = m_menuItemHover = -1;
				m_menuHoverTime = m_menuItemTime = 0.f;
				m_subMenuPopup = {};
				break;
			}
			case WidgetKind::SubWindow: {
				const float titleH = n.computed.fontSize + 12.f;
				const sdl3::FRect close{n.bounds.Right() - titleH, n.bounds.y, titleH, titleH};
				if (close.Contains(m_mouseX, m_mouseY)) {
					n.hidden = true;
					if (n.onClose)
						n.onClose();
				} else {
					m_bringToFront = &n; // amenée au premier plan
				}
				break;
			}
			default: break;
			}
		}
		for (NodePtr &c : n.children)
			dispatch(*c);
	};
	dispatch(node);

	// Amener la fenêtre flottante cliquée au premier plan (fin de la liste des
	// enfants de son parent) — le hit-test et le rendu la traitent alors comme
	// la plus en avant.
	if (m_bringToFront && m_bringToFront->parent) {
		Node *p = m_bringToFront->parent;
		auto &vec = p->children;
		auto it = std::find_if(vec.begin(), vec.end(), [&](const NodePtr &c) { return c.get() == m_bringToFront; });
		if (it != vec.end()) {
			NodePtr moved = *it;
			vec.erase(it);
			vec.push_back(std::move(moved));
		}
	}
	m_bringToFront = nullptr;

	// Popup : un clic en dehors de ses bornes la ferme.
	if (m_events && (*m_events)[sdl3::MouseButton::Left].IsJustPressed()) {
		std::function<void(Node &)> closePopups = [&](Node &x) {
			if (KindOf(x) == WidgetKind::Popup && !x.bounds.Contains(m_mouseX, m_mouseY))
				x.hidden = true;
			for (NodePtr &c : x.children)
				closePopups(*c);
		};
		closePopups(node);
	}
}

// ============================================================================
// Present — affichage automatique
// ============================================================================

void Ui::Present(float dt) {
	Present(dt, sdl3::FRect{0, 0, m_canvas->Width(), m_canvas->Height()});
}

void Ui::Begin(float dt) {
	m_pendingDt = dt;
	// Le contexte immédiat (feuilles `Custom`) doit être « commencé » avant que
	// les feuilles ne dessinent ; il est clôturé dans End().
	if (m_immediate)
		m_immediate->Begin(dt);
}

void Ui::Render() {
	Present(m_pendingDt, sdl3::FRect{0, 0, m_canvas->Width(), m_canvas->Height()});
}

void Ui::End() {
	if (m_immediate)
		m_immediate->End();
}

void Ui::Present(float dt, const sdl3::FRect &viewport) {
	if (!m_root)
		return;
	// Traductions : posées quand le traducteur (ou l'arbre) a changé.
	if (m_translator && m_translator->Version() != m_i18nVersion) {
		ApplyTranslations(*m_root, m_translator);
		m_i18nVersion = m_translator->Version();
		m_i18nApplied = true;
	} else if (!m_translator && m_i18nApplied) {
		ApplyTranslations(*m_root, nullptr);
		m_i18nApplied = false;
	}
	m_time += double(dt);
	m_dt = dt;
	TickToasts(dt);
	const float scale = m_canvas->Scale() > 0.f ? m_canvas->Scale() : 1.f;
	m_mouseX = m_events->MouseX() / scale;
	m_mouseY = m_events->MouseY() / scale;
	const sdl3::Trigger left = (*m_events)[sdl3::MouseButton::Left];
	const bool pressed = left.IsJustPressed();
	const bool released = left.IsJustReleased();
	const bool down = left.IsPressed();
	const bool shift = m_events->Shift();

	// 0. Synchronise les liaisons (variables de l'application → état des widgets).
	std::function<void(Node &)> pull = [&](Node &n) {
		if (n.valueRef)
			n.value = *n.valueRef;
		if (n.checkedRef)
			n.checked = *n.checkedRef;
		if (n.indexRef)
			n.selectedIndex = *n.indexRef;
		if (n.textRef)
			n.text = *n.textRef;
		for (NodePtr &c : n.children)
			pull(*c);
	};
	pull(*m_root);

	// 0a. Transitions d'état (masqué/visible, activé/désactivé) → callbacks.
	//     `prevHidden`/`prevDisabled` sont initialisés au montage (Root) : seuls
	//     les changements faits par l'application déclenchent les callbacks.
	std::function<void(Node &)> transitions = [&](Node &n) {
		if (n.hidden != n.prevHidden) {
			n.prevHidden = n.hidden;
			if (n.hidden) { if (n.onHidden) n.onHidden(); }
			else if (n.onVisible) n.onVisible();
		}
		if (n.disabled != n.prevDisabled) {
			n.prevDisabled = n.disabled;
			if (n.disabled) { if (n.onDisable) n.onDisable(); }
			else if (n.onEnable) n.onEnable();
		}
		for (NodePtr &c : n.children)
			transitions(*c);
	};
	transitions(*m_root);

	// 0bis. Avance des animations (glissé) — indépendant du rendu.
	std::function<void(Node &)> animate = [&](Node &n) {
		if (n.animTime >= 0.f && n.animTime < n.animDuration)
			n.animTime = std::min(n.animDuration, n.animTime + float(dt));
		for (NodePtr &c : n.children)
			animate(*c);
	};
	animate(*m_root);

	// 0ter. Menu déroulant d'un ComboBox ouvert : il est traité en premier, pour
	// consommer les clics (choisir un item ou fermer en cliquant ailleurs).
	const bool comboOpen = m_openCombo != nullptr;
	if (comboOpen && !m_blocked) {
		const sdl3::FRect &b = m_openCombo->bounds;
		const float itemH = 26.f;
		const float h = std::min(float(m_openCombo->options.size()) * itemH, 280.f);
		m_comboPopup = {b.x, b.Bottom() + 2.f, b.w, h};
		if (pressed) {
			if (m_comboPopup.Contains(m_mouseX, m_mouseY)) {
				const int idx = int((m_mouseY - m_comboPopup.y) / itemH);
				if (idx >= 0 && idx < int(m_openCombo->options.size())) {
					m_openCombo->selectedIndex = idx;
					if (m_openCombo->onIndexChanged)
						m_openCombo->onIndexChanged(idx);
				}
			}
			m_openCombo->open = false;
			m_openCombo = nullptr;
		} else if (!m_openCombo->options.empty()) {
			// Échap ferme, flèches changent l'item, Entrée valide.
			if (m_events->KeycodePressed(SDLK_ESCAPE)) {
				m_openCombo->open = false;
				m_openCombo = nullptr;
			} else if (m_events->KeycodePressed(SDLK_DOWN)) {
				m_openCombo->selectedIndex = std::min(int(m_openCombo->options.size()) - 1, m_openCombo->selectedIndex + 1);
			} else if (m_events->KeycodePressed(SDLK_UP)) {
				m_openCombo->selectedIndex = std::max(0, m_openCombo->selectedIndex - 1);
			} else if (EnterPressed(*m_events) && m_openCombo->selectedIndex >= 0) {
				m_openCombo->open = false;
				m_openCombo = nullptr;
			}
		}
	}

	// 0ter. Menu déroulant d'une MenuBar ouverte : choisir un item, naviguer dans
	//      les sous-menus (délais d'intention + triangle de sécurité), ou fermer.
	const bool menuOpen = m_openMenuBar != nullptr;
	if (menuOpen && !m_blocked) {
		Node &bar = *m_openMenuBar;
		if (bar.openMenu >= 0 && size_t(bar.openMenu) < bar.menus.size()) {
			const float itemH = 26.f;
			Menu &menu = bar.menus[size_t(bar.openMenu)];
			m_menuPopup = MenuPopupRect(bar);
			const int hoverItem = m_menuPopup.Contains(m_mouseX, m_mouseY) ? MenuItemAt(menu.items, m_menuPopup, m_mouseY, itemH) : -1;

			// Point dans un triangle (a,b,c) — cf. triangle de sécurité du sous-menu.
			const auto inTriangle = [](float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
				const auto side = [](float x1, float y1, float x2, float y2, float x3, float y3) {
					return (x1 - x3) * (y2 - y3) - (x2 - x3) * (y1 - y3);
				};
				const float d1 = side(px, py, ax, ay, bx, by);
				const float d2 = side(px, py, bx, by, cx, cy);
				const float d3 = side(px, py, cx, cy, ax, ay);
				const bool neg = d1 < 0.f || d2 < 0.f || d3 < 0.f;
				const bool pos = d1 > 0.f || d2 > 0.f || d3 > 0.f;
				return !(neg && pos);
			};
			// Sous-menu ouvert : boîte, et « triangle de sécurité » entre la position
			// précédente de la souris et les coins supérieur/inférieur gauche du
			// sous-menu. Tant que la souris reste dans ce triangle (elle se dirige vers
			// le sous-menu), on gèle le changement d'état : le sous-menu reste ouvert.
			bool frozen = false;
			bool inSub = false;
			if (bar.openSubMenu >= 0 && size_t(bar.openSubMenu) < menu.items.size() && !menu.items[size_t(bar.openSubMenu)].children.empty()) {
				const MenuItem &parent = menu.items[size_t(bar.openSubMenu)];
				const float py = m_menuPopup.y + MenuItemY(menu.items, bar.openSubMenu, itemH);
				m_subMenuPopup = SubMenuRect(bar, parent, py);
				inSub = m_subMenuPopup.Contains(m_mouseX, m_mouseY);
				frozen = inTriangle(m_mouseX, m_mouseY, m_menuPrevX, m_menuPrevY, m_subMenuPopup.x, m_subMenuPopup.y,
									m_subMenuPopup.x, m_subMenuPopup.Bottom());
			} else if (bar.openSubMenu < 0) {
				m_subMenuPopup = {};
			}

			const auto applyItem = [&](MenuItem &mi, std::vector<MenuItem> &siblings) {
				if (mi.checkable) {
					if (mi.radioGroup.empty())
						mi.checked = !mi.checked;
					else
						for (MenuItem &o : siblings)
							if (o.radioGroup == mi.radioGroup)
								o.checked = (&o == &mi);
				}
				if (mi.action)
					mi.action();
			};
			const auto closeMenu = [&] {
				bar.openMenu = -1;
				bar.openSubMenu = -1;
				m_openMenuBar = nullptr;
				m_menuHover = m_menuItemHover = -1;
				m_menuHoverTime = m_menuItemTime = 0.f;
				m_subMenuPopup = {};
			};

			if (pressed) {
				if (inSub) { // clic dans le sous-menu ouvert
					MenuItem &pmi = menu.items[size_t(bar.openSubMenu)];
					const int si = MenuItemAt(pmi.children, m_subMenuPopup, m_mouseY, itemH);
					if (si >= 0 && !pmi.children[size_t(si)].separator && pmi.children[size_t(si)].enabled)
						applyItem(pmi.children[size_t(si)], pmi.children);
					closeMenu();
				} else if (hoverItem >= 0) { // clic sur un item du menu parent
					MenuItem &mi = menu.items[size_t(hoverItem)];
					if (!mi.separator && mi.enabled) {
						if (!mi.children.empty())
							bar.openSubMenu = (bar.openSubMenu == hoverItem) ? -1 : hoverItem; // bascule le sous-menu
						else {
							applyItem(mi, menu.items);
							closeMenu();
						}
					}
				} else {
					closeMenu(); // clic hors du menu
				}
			} else if (m_events->KeycodePressed(SDLK_ESCAPE)) {
				if (bar.openSubMenu >= 0) { // Échap ferme d'abord le sous-menu
					bar.openSubMenu = -1;
					m_subMenuPopup = {};
					m_menuItemHover = -1;
					m_menuItemTime = 0.f;
				} else {
					closeMenu();
				}
			} else if (!frozen) {
				// ── Survole : bascule de menu et ouverture de sous-menu, avec un délai
				//    d'intention pour ne pas changer d'état pendant un balayage rapide.
				float tx = bar.content.x;
				const sdl3::CanvasFont bf = FontOf(bar.computed);
				int hoverTitle = -1;
				for (size_t i = 0; i < bar.menus.size(); ++i) {
					const float w = m_canvas->MeasureText(bar.menus[i].title, bar.computed.fontSize, bf) + 20.f;
					if (m_mouseY >= bar.content.y && m_mouseY < bar.content.y + bar.content.h && m_mouseX >= tx && m_mouseX < tx + w) {
						hoverTitle = int(i);
						break;
					}
					tx += w;
				}
				if (hoverTitle >= 0 && hoverTitle != bar.openMenu) {
					m_menuHoverTime = (m_menuHover == hoverTitle) ? m_menuHoverTime + float(dt) : 0.f;
					if (m_menuHoverTime >= m_menuHoverDelay) {
						bar.openMenu = hoverTitle;
						bar.openSubMenu = -1;
						m_subMenuPopup = {};
					}
				} else if (m_menuHover != hoverTitle) {
					m_menuHoverTime = 0.f;
				}
				m_menuHover = hoverTitle;

				// Ouverture du sous-menu au survol d'un item à enfants ; fermeture
				// quand on survole un autre item (hors du sous-menu et du triangle).
				if (hoverItem >= 0 && !menu.items[size_t(hoverItem)].separator && menu.items[size_t(hoverItem)].enabled &&
					!menu.items[size_t(hoverItem)].children.empty()) {
					m_menuItemTime = (m_menuItemHover == hoverItem) ? m_menuItemTime + float(dt) : 0.f;
					if (m_menuItemTime >= m_submenuDelay)
						bar.openSubMenu = hoverItem;
				} else if (bar.openSubMenu >= 0 && hoverItem != bar.openSubMenu && !inSub) {
					bar.openSubMenu = -1;
					m_subMenuPopup = {};
				}
				m_menuItemHover = hoverItem;
			}
		}
	}

	// 1. Hit-test avec les bornes de l'image précédente.
	ClearTransient(*m_root);
	Node *hit = nullptr;
	if (!m_blocked && !comboOpen && !menuOpen) {
		HitTest(*m_root, m_mouseX, m_mouseY, hit);
		// Clic droit : menu contextuel du widget survolé ou de son plus proche ancêtre.
		if (hit && m_events->MouseButton(3).IsJustPressed())
			for (Node *n = hit; n; n = n->parent)
				if (n->contextMenu && !n->menus.empty()) {
					n->openMenu = 0;
					n->menuX = m_mouseX;
					n->menuY = m_mouseY;
					m_openMenuBar = n;
					break;
				}
		if (hit) {
			hit->state = hit->state | State::Hover;
			m_hovered = hit;
		} else {
			m_hovered = nullptr;
		}
		// Clic : relâchement sur le même widget que l'appui.
		if (pressed) {
			m_pressed = hit;
			m_focus = (hit && IsFocusable(*hit)) ? hit : nullptr; // clic → focus
			// Texte : curseur sous la souris (champs) ou début d'une sélection (libellés).
			const bool field = hit && (KindOf(*hit) == WidgetKind::LineEdit || KindOf(*hit) == WidgetKind::TextArea || KindOf(*hit) == WidgetKind::CodeEditor);
			const bool label = hit && KindOf(*hit) == WidgetKind::Label && !hit->text.empty();
			if (field || label) {
				const bool doubleClick = m_textClickNode == hit && m_time - m_textClickTime < 0.35;
				m_textClickNode = hit;
				m_textClickTime = doubleClick ? -1.0 : m_time;
				TextEdit &ed = hit->textEdit;
				const std::string shown = hit->password ? MaskedText(hit->text) : hit->text;
				size_t at = KindOf(*hit) == WidgetKind::CodeEditor ? CodeHitOf(*hit, m_mouseX, m_mouseY) : TextHit(*hit, shown, m_mouseX, m_mouseY);
				if (hit->password)
					at = TextEdit::ByteOfChar(hit->text, TextEdit::CharCount(shown, at));
				ed.readOnly = label || !hit->editable;
				ed.MousePress(hit->text, at, shift, doubleClick);
				m_selNode = label ? hit : nullptr;
				m_selDragging = true;
			} else {
				m_selNode = nullptr;
			}
			// Début d'un glisser-déposer (source draggable).
			if (hit && hit->dragDrop.draggable && !hit->dragDrop.payload.empty()) {
				m_dragActive = true;
				m_dragSource = hit;
			}
		}
		if (released) {
			if (m_pressed && hit == m_pressed) {
				m_pressed->clicked = true;
				// Modal : un relâchement sur la croix ferme (et n'active rien d'autre).
				if (KindOf(*m_pressed) == WidgetKind::Modal && m_pressed->closeRect.w > 0.f &&
					m_pressed->closeRect.Contains(m_mouseX, m_mouseY)) {
					m_pressed->clicked = false;
					if (m_pressed->onClose)
						m_pressed->onClose();
				}
			}
			// Fin d'un glisser-déposer : dépôt sur une cible.
			if (m_dragActive && hit && hit->dragDrop.dropTarget && hit->dragDrop.onDrop && m_dragSource && hit != m_dragSource)
				hit->dragDrop.onDrop(m_dragSource->dragDrop.payload);
			m_dragActive = false;
			m_dragSource = nullptr;
			m_pressed = nullptr;
		}
		if (m_pressed && down)
			m_pressed->state = m_pressed->state | State::Active;
	} else if (!m_blocked) {
		// Menu déroulant ouvert : le fond n'est pas interactif.
		m_hovered = nullptr;
		m_pressed = nullptr;
	} else {
		m_hovered = nullptr;
		m_pressed = nullptr;
	}

	// 1bis. Transitions de survol (souris entrée/sortie du widget).
	if (m_hovered != m_prevHovered) {
		if (m_prevHovered && m_prevHovered->onMouseLeave)
			m_prevHovered->onMouseLeave();
		if (m_hovered && m_hovered->onMouseEnter)
			m_hovered->onMouseEnter();
		m_prevHovered = m_hovered;
	}

	// 1bis. Cycle de focus clavier (Tab / Maj+Tab) puis saisie de texte.
	if (!m_blocked && m_events->KeycodePressed(SDLK_TAB)) {
		std::vector<Node *> order;
		CollectFocusable(*m_root, order);
		if (!order.empty()) {
			auto it = std::find(order.begin(), order.end(), m_focus);
			int idx = it == order.end() ? -1 : int(it - order.begin());
			idx += shift ? -1 : +1;
			const int n = int(order.size());
			if (idx < 0)
				idx = n - 1;
			if (idx >= n)
				idx = 0;
			m_focus = order[size_t(idx)];
		}
	}

	// 1ter. Transitions de focus (gain / perte du focus clavier).
	if (m_focus != m_prevFocus) {
		if (m_prevFocus && m_prevFocus->onLoseFocus)
			m_prevFocus->onLoseFocus();
		if (m_focus && m_focus->onGainFocus)
			m_focus->onGainFocus();
		m_prevFocus = m_focus;
	}

	// 1quater. Évènements de pointeur (boutons, molette) et gestes multi-doigts,
	//          délivrés au widget survolé (ou appuyé).
	if (!m_blocked && m_events) {
		Node *evt = m_hovered ? m_hovered : m_pressed;
		if (evt) {
			const uint16_t mods = m_events->Mods();
			const bool touch = !m_events->TouchFingers().empty() || !m_events->TouchpadFingers().empty();
			const auto emit = [&](PointerEvent pe) {
				pe.position = {m_mouseX, m_mouseY};
				pe.mods = mods;
				pe.touch = touch;
				if (touch) { if (evt->onTouchEvent) evt->onTouchEvent(pe); }
				else if (evt->onMouseEvent) evt->onMouseEvent(pe);
			};
			// Boutons : enfoncement (Press) / relâchement sur le widget (Click) + raccourcis.
			const struct {
				sdl3::MouseButton btn;
				std::function<void()> Node::*fn;
			} buttons[] = {
				{sdl3::MouseButton::Left, &Node::onButtonLeft},
				{sdl3::MouseButton::Right, &Node::onButtonRight},
				{sdl3::MouseButton::Middle, &Node::onButtonMiddle},
				{sdl3::MouseButton::X1, &Node::onButtonX1},
				{sdl3::MouseButton::X2, &Node::onButtonX2},
			};
			for (const auto &entry : buttons) {
				const sdl3::Trigger t = (*m_events)[entry.btn];
				if (t.IsJustPressed())
					emit(PointerEvent{PointerEvent::Kind::Press, entry.btn});
				if (t.IsJustReleased()) {
					emit(PointerEvent{PointerEvent::Kind::Release, entry.btn});
					// « Clic » : relâché sur le widget — le bouton gauche exige en
					// plus que l'appui ait commencé dessus (`clicked`, cf. plus haut).
					const bool clicked = entry.btn == sdl3::MouseButton::Left ? evt->clicked : true;
					if (clicked) {
						emit(PointerEvent{PointerEvent::Kind::Click, entry.btn});
						if ((evt->*entry.fn))
							(evt->*entry.fn)();
					}
				}
			}
			// Molette horizontale / verticale.
			const float wheelX = m_events->WheelX(), wheelY = m_events->WheelY();
			if (wheelX != 0.f) {
				emit(PointerEvent{PointerEvent::Kind::Wheel, sdl3::MouseButton::Left, {}, wheelX, 0.f, 0.f});
				if (evt->onWheelX)
					evt->onWheelX(wheelX);
			}
			if (wheelY != 0.f) {
				emit(PointerEvent{PointerEvent::Kind::Wheel, sdl3::MouseButton::Left, {}, 0.f, wheelY, 0.f});
				if (evt->onWheelY)
					evt->onWheelY(wheelY);
			}
		}
	}

	// 1quinquies. Gestes multi-doigts (pincement → zoom, rotation, balayage).
	if (!m_blocked && m_events) {
		const sdl3::Gesture &g = m_events->GestureState();
		Node *evt = m_hovered ? m_hovered : m_pressed;
		if (evt) {
			const float cw = m_canvas->Width(), ch = m_canvas->Height();
			const sdl3::FPoint center{g.center.x * cw, g.center.y * ch};
			if (evt->gestureRotate && g.active && g.rotate != 0.f) {
				if (evt->onRotate)
					evt->onRotate(g.rotate);
				if (evt->onGesture)
					evt->onGesture(GestureEvent{GestureEvent::Kind::Rotate, 1.f, g.rotate, 0.f, 0.f, center});
			}
			if (evt->gestureZoom && g.active && g.pinch != 1.f) {
				if (evt->onZoom)
					evt->onZoom(g.pinch);
				if (evt->onGesture)
					evt->onGesture(GestureEvent{GestureEvent::Kind::Pinch, g.pinch, 0.f, 0.f, 0.f, center});
			}
			// Balayage horizontal (précédent/suivant) : geste à deux doigts OU molette X.
			float swipe = 0.f;
			if (g.active && std::fabs(g.dx) > 1e-3f && std::fabs(g.dx) > std::fabs(g.dy))
				swipe = g.dx;
			else if (m_events->WheelX() != 0.f)
				swipe = m_events->WheelX();
			if (evt->gesturePrevNext && swipe != 0.f) {
				const int dir = swipe < 0.f ? +1 : -1; // glisser à gauche → suivant
				if (evt->onSwipe)
					evt->onSwipe(dir);
				if (evt->onGesture)
					evt->onGesture(GestureEvent{GestureEvent::Kind::Swipe, 1.f, 0.f, swipe, 0.f, center});
			}
		}
	}

	// 1sexies. Évènements clavier → widget au focus (sinon survolé).
	if (!m_blocked && m_events) {
		Node *k = m_focus ? m_focus : m_hovered;
		if (k) {
			const uint16_t mods = m_events->Mods();
			if (k->onKeyEvent) {
				for (sdl3::Keycode kc : m_events->PressedKeycodes()) {
					const KeyEvent ke{m_events->KeycodeJustPressed(kc) ? KeyEvent::Kind::Down : KeyEvent::Kind::Repeat, kc,
									  SDL_GetScancodeFromKey(kc, nullptr), mods};
					k->onKeyEvent(ke);
				}
				for (sdl3::Keycode kc : m_events->ReleasedKeycodes()) {
					const KeyEvent ke{KeyEvent::Kind::Up, kc, SDL_GetScancodeFromKey(kc, nullptr), mods};
					k->onKeyEvent(ke);
				}
			}
			if (k->onKeyEnter && (m_events->KeycodeJustPressed(SDLK_RETURN) || m_events->KeycodeJustPressed(SDLK_KP_ENTER)))
				k->onKeyEnter();
		}
	}
	// Glissé : la sélection suit la souris (champ pressé ou libellé sélectionné).
	if (!down)
		m_selDragging = false;
	if (m_selDragging && down) {
		Node *target = m_selNode ? m_selNode : (m_pressed && m_pressed == m_focus ? m_pressed : nullptr);
		if (target) {
			const std::string shown = target->password ? MaskedText(target->text) : target->text;
			size_t at = KindOf(*target) == WidgetKind::CodeEditor ? CodeHitOf(*target, m_mouseX, m_mouseY) : TextHit(*target, shown, m_mouseX, m_mouseY);
			if (target->password)
				at = TextEdit::ByteOfChar(target->text, TextEdit::CharCount(shown, at));
			target->textEdit.MouseDrag(at);
		}
	}
	if (!m_blocked && m_focus) {
		Node &f = *m_focus;
		if (KindOf(f) == WidgetKind::LineEdit || KindOf(f) == WidgetKind::TextArea || KindOf(f) == WidgetKind::CodeEditor) {
			TextEdit &ed = f.textEdit;
			ed.multiline = KindOf(f) != WidgetKind::LineEdit;
			ed.code = KindOf(f) == WidgetKind::CodeEditor;
			ed.readOnly = !f.editable;
			ed.secret = f.password;
			const Node::TextView &v = f.textView;
			const auto measure = [&](std::string_view s) { return m_canvas->MeasureText(s, v.size, v.font); };
			const int pageLines = std::max(1, int(f.content.h / std::max(1.f, v.lineHeight)));
			const TextEditResult res = ed.HandleKeys(f.text, *m_events, dt, measure, pageLines);
			if (res.changed && f.onEdited)
				f.onEdited();
			if (res.submitted && KindOf(f) == WidgetKind::LineEdit && f.onSubmit)
				f.onSubmit();
			if (res.cancelled)
				m_focus = nullptr;
		}
	} else if (!m_blocked && m_selNode && m_events->Ctrl()) {
		// Libellé sélectionné : Ctrl+C copie, Ctrl+A sélectionne tout.
		if (m_events->KeycodePressed(SDLK_C) || m_events->KeycodePressed(SDLK_INSERT))
			(void)m_selNode->textEdit.Copy(m_selNode->text);
		else if (m_events->KeycodePressed(SDLK_A))
			m_selNode->textEdit.SelectAll(m_selNode->text);
	}

	// 2. Entrée des widgets (curseur, cases, défilement) — utilise les bornes de
	//    l'image précédente, avant la mise en page qui applique le défilement.
	if (!m_blocked)
		HandleWidgetInput(*m_root, hit, m_pressed);

	// 2bis. États des widgets (pseudo-classes CSS) : Checked, Disabled, Focus, Open.
	std::function<void(Node &)> applyStates = [&](Node &n) {
		State s = n.state;
		if (n.checked) s = s | State::Checked;
		if (n.disabled) s = s | State::Disabled;
		if (&n == m_focus) s = s | State::Focus;
		if (n.open) s = s | State::Open;
		n.state = s;
		for (NodePtr &c : n.children)
			applyStates(*c);
	};
	applyStates(*m_root);

	// 3. Résolution des styles.
	ResolveStyle(*m_root, nullptr, nullptr, true);

	// 3a. Curseur — résolu APRÈS les styles (le curseur peut venir du CSS,
	//     ex. `button:hover { cursor: pointer }`). Priorité : override explicite,
	//     curseur image du widget, bord de redimensionnement, curseur du style,
	//     glissé, saisie de texte.
	if (!m_blocked) {
		Node *n = m_hovered;
		// Curseur image (le plus spécifique après l'override).
		const SDL_Surface *wantImage = nullptr;
		int hotX = 0, hotY = 0;
		sdl3::SystemCursor want = sdl3::SystemCursor::DEFAULT;
		if (m_cursorOverride.IsSome()) {
			want = *m_cursorOverride;
		} else if (n && n->cursorImage && *n->cursorImage) {
			wantImage = n->cursorImage->Surface()->Get();
			hotX = n->cursorHotX;
			hotY = n->cursorHotY;
		} else {
			// Fenêtre flottante redimensionnable : bord/coin sous la souris (ou en cours).
			if (n && KindOf(*n) == WidgetKind::SubWindow && n->floating.resizable) {
				const int edge = n->floating.resizeEdge >= 0 ? n->floating.resizeEdge : ResizeEdgeAt(*n, m_mouseX, m_mouseY);
				switch (edge) {
				case 0: case 2: want = sdl3::SystemCursor::SIZE_NWSE; break;
				case 1: case 3: want = sdl3::SystemCursor::SIZE_NESW; break;
				case 4: case 5: want = sdl3::SystemCursor::SIZE_NS; break;
				case 6: case 7: want = sdl3::SystemCursor::SIZE_EW; break;
				default: break;
				}
			}
			if (want == sdl3::SystemCursor::DEFAULT) {
				if (n && n->computed.cursor.has_value())
					want = *n->computed.cursor;
				else if (m_dragActive || (n && KindOf(*n) == WidgetKind::SubWindow && n->floating.dragging) || m_scrollDrag)
					want = sdl3::SystemCursor::MOVE;
				else if (n && (KindOf(*n) == WidgetKind::LineEdit || KindOf(*n) == WidgetKind::TextArea || KindOf(*n) == WidgetKind::CodeEditor))
					want = sdl3::SystemCursor::TEXT;
			}
		}
		if (wantImage) {
			// Curseur image personnalisé (créé et mis en cache à la demande).
			if (wantImage != m_lastCursorImage) {
				auto it = m_cursors.find(wantImage);
				if (it == m_cursors.end()) {
					auto cur = sdl3::Cursor::FromSurface(MakeRef(*n->cursorImage->Surface()), hotX, hotY);
					if (cur.IsOk())
						it = m_cursors.emplace(wantImage, std::move(cur.Value())).first;
				}
				if (it != m_cursors.end()) {
					it->second.Set();
					m_lastCursorImage = wantImage;
					m_lastCursor = -1; // le suivi du curseur système repart de zéro
				}
			}
		} else {
			m_lastCursorImage = nullptr;
			if (static_cast<int>(want) != m_lastCursor) {
				m_lastCursor = static_cast<int>(want);
				sdl3::mouse::SetSystemCursor(want);
			}
		}
	}

	// 4. Mise en page.
	LayoutContext ctx{viewport.w, viewport.h, m_rootFontSize};
	LayoutNode(*m_root, viewport, ctx);

	// 4bis. Redimensionnement : notifie les widgets dont la taille a changé.
	std::function<void(Node &)> resize = [&](Node &n) {
		if (n.onResize && n.hasPrevBounds && (n.bounds.w != n.prevBounds.w || n.bounds.h != n.prevBounds.h))
			n.onResize();
		n.prevBounds = n.bounds;
		n.hasPrevBounds = true;
		for (NodePtr &c : n.children)
			resize(*c);
	};
	resize(*m_root);

	// 5. Rendu, rogné à la zone de l'interface.
	m_viewport = viewport;
	// Un modal / menu ouvert gèle aussi le contexte immédiat lié (`SetImmediateContext`) :
	// les feuilles `Custom(immediate)` ne répondent plus pendant ce rendu. L'état
	// préexistant est restauré ensuite (la barre de titre, dessinée hors de Present,
	// n'est pas affectée).
	const bool imBlocked = m_immediate ? m_immediate->Blocked() : false;
	if (m_immediate && IsBlocking())
		m_immediate->SetBlocked(true);
	m_canvas->PushClip(viewport);
	RenderNode(*m_root);
	m_canvas->PopClip();
	if (m_immediate)
		m_immediate->SetBlocked(imBlocked);

	// 5bis. Menu déroulant du ComboBox ouvert (au premier plan).
	if (m_openCombo)
		RenderComboPopup(*m_openCombo, m_mouseX, m_mouseY);

	// 5bis2. Menu déroulant d'une MenuBar ouverte (au premier plan).
	if (m_openMenuBar)
		RenderMenuPopup(*m_openMenuBar, m_mouseX, m_mouseY);

	// 5ter. Tooltip du widget survolé (au premier plan, à la position de la souris).
	if (!m_blocked && m_hovered && m_hovered->tooltip)
		RenderTooltip(*m_hovered->tooltip, m_mouseX, m_mouseY);

	// 5quater. Toasts (notifications non-modales) au tout premier plan.
	RenderToasts();

	// 6. Repousse les valeurs vers les variables de l'application.
	std::function<void(Node &)> push = [&](Node &n) {
		if (n.valueRef)
			*n.valueRef = n.value;
		if (n.checkedRef)
			*n.checkedRef = n.checked;
		if (n.indexRef)
			*n.indexRef = n.selectedIndex;
		if (n.textRef)
			*n.textRef = n.text;
		for (NodePtr &c : n.children)
			push(*c);
	};
	push(*m_root);

	// Position de la souris de l'image précédente (triangle de sécurité des menus).
	m_menuPrevX = m_mouseX;
	m_menuPrevY = m_mouseY;
}

void Ui::Notify(std::string text, Color color, float seconds) {
	Toast t;
	t.text = std::move(text);
	t.color = color;
	t.duration = std::max(0.1f, seconds);
	t.remaining = t.duration;
	m_toasts.push_back(std::move(t));
	if (m_toasts.size() > 5) // borne : on ne garde que les plus récentes
		m_toasts.erase(m_toasts.begin(), m_toasts.end() - 5);
}

void Ui::TickToasts(float dt) {
	if (m_toasts.empty())
		return;
	for (Toast &t : m_toasts)
		t.remaining -= dt;
	std::erase_if(m_toasts, [](const Toast &t) { return t.remaining <= 0.f; });
}

void Ui::RenderToasts() {
	if (m_toasts.empty())
		return;
	const float padH = 14.f, padV = 9.f;
	const float gap = 8.f;
	const float fontSize = 14.f;
	const sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
	const float lh = m_canvas->LineHeight(fontSize, font);
	const auto a8 = [](float a) { return uint8_t(std::clamp(int(a * 255.f), 0, 255)); };
	// Empilement depuis le bas, la plus récente en bas.
	float y = m_viewport.Bottom() - 16.f;
	for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
		const Toast &t = *it;
		const float elapsed = t.duration - t.remaining;
		const float alpha = std::min(std::clamp(elapsed / 0.2f, 0.f, 1.f), std::clamp(t.remaining / 0.4f, 0.f, 1.f));
		const Color accent = t.color.a ? t.color : m_theme.accent;
		const float w = m_canvas->MeasureText(t.text, fontSize, font) + 2.f * padH + 8.f;
		const float h = lh + 2.f * padV;
		const float slide = (1.f - std::clamp(elapsed / 0.2f, 0.f, 1.f)) * 16.f; // glissé d'entrée
		const sdl3::FRect box{m_viewport.x + (m_viewport.w - w) * 0.5f, y - h + slide, w, h};
		if (alpha <= 0.f) {
			y -= h + gap;
			continue;
		}
		m_canvas->FillRoundRect(box, 8.f, sdl3::WithAlpha(m_theme.panel, a8(alpha)));
		m_canvas->StrokeRoundRect(box, 8.f, sdl3::WithAlpha(accent, a8(alpha)), 1.f);
		m_canvas->FillRect({box.x, box.y + 6.f, 3.f, box.h - 12.f}, sdl3::WithAlpha(accent, a8(alpha)));
		m_canvas->Text(box.x + padH + 4.f, box.y + (box.h - lh) * 0.5f, t.text, sdl3::WithAlpha(m_theme.text, a8(alpha)), fontSize, font);
		y -= h + gap;
	}
}

void Ui::RenderComboPopup(Node &combo, float mouseX, float mouseY) {
	const float itemH = 26.f;
	const ComputedStyle &c = combo.computed;
	// Recalcule la boîte du menu (pour le premier affichage, bornes à jour).
	const sdl3::FRect &b = combo.bounds;
	m_comboPopup = {b.x, b.Bottom() + 2.f, b.w, std::min(float(combo.options.size()) * itemH, 280.f)};
	const sdl3::FRect &popup = m_comboPopup;
	m_canvas->FillRoundRect(popup, 6.f, sdl3::WithAlpha(m_theme.panel, 250));
	m_canvas->StrokeRoundRect(popup, 6.f, m_theme.panelBorder);
	const float x0 = popup.x + 10.f;
	float y = popup.y;
	for (size_t i = 0; i < combo.options.size(); ++i) {
		const sdl3::FRect row{popup.x, y, popup.w, itemH};
		const bool selected = int(i) == combo.selectedIndex;
		const bool hovered = row.Contains(mouseX, mouseY);
		if (selected || hovered)
			m_canvas->FillRect(row.Inset(2), sdl3::Rgba(48, 58, 86, selected ? 220 : 120));
		m_canvas->Text(x0, y + (itemH - m_canvas->LineHeight(c.fontSize, sdl3::CanvasFont::Regular)) * 0.5f, combo.options[i], c.color, c.fontSize,
					   sdl3::CanvasFont::Regular);
		y += itemH;
	}
}

sdl3::FRect Ui::MenuPopupRect(const Node &owner) {
	const float itemH = 26.f;
	const ComputedStyle &c = owner.computed;
	const Menu &menu = owner.menus[size_t(owner.openMenu)];
	const sdl3::CanvasFont mf = FontOf(c);
	float maxW = 120.f;
	for (const MenuItem &mi : menu.items)
		if (!mi.separator) {
			float w = m_canvas->MeasureText(mi.label, c.fontSize, mf) + (mi.checkable ? 42.f : 24.f);
			if (!mi.shortcut.empty())
				w += m_canvas->MeasureText(mi.shortcut, c.fontSize, mf) + 24.f;
			maxW = std::max(maxW, w);
		}
	float totalH = 0.f;
	for (const MenuItem &mi : menu.items)
		totalH += mi.separator ? 8.f : itemH;
	if (owner.contextMenu) {
		// Menu contextuel : au point du clic droit, gardé dans la fenêtre.
		const float x = std::clamp(owner.menuX, 0.f, std::max(0.f, m_canvas->Width() - maxW));
		const float y = owner.menuY + totalH > m_canvas->Height() ? std::max(0.f, owner.menuY - totalH) : owner.menuY;
		return {x, y, maxW, totalH};
	}
	float titleX = owner.content.x; // barre de menus : sous le titre du menu ouvert
	for (int i = 0; i < owner.openMenu; ++i)
		titleX += m_canvas->MeasureText(owner.menus[size_t(i)].title, c.fontSize, mf) + 20.f;
	return {titleX, owner.content.Bottom(), maxW, totalH};
}

int Ui::MenuItemAt(const std::vector<MenuItem> &items, const sdl3::FRect &popup, float my, float itemH) {
	float y = popup.y;
	for (size_t i = 0; i < items.size(); ++i) {
		const float h = items[i].separator ? 8.f : itemH;
		if (my >= y && my < y + h)
			return int(i);
		y += h;
	}
	return -1;
}

float Ui::MenuItemY(const std::vector<MenuItem> &items, int index, float itemH) {
	if (index < 0 || size_t(index) >= items.size())
		return 0.f;
	float y = 0.f;
	for (int i = 0; i < index; ++i)
		y += items[size_t(i)].separator ? 8.f : itemH;
	return y;
}

sdl3::FRect Ui::SubMenuRect(const Node &owner, const MenuItem &item, float itemY) {
	const float itemH = 26.f;
	const sdl3::CanvasFont mf = FontOf(owner.computed);
	const float fs = owner.computed.fontSize;
	float w = 40.f;
	for (const MenuItem &c : item.children) {
		if (c.separator)
			continue;
		float cw = m_canvas->MeasureText(c.label, fs, mf) + (c.checkable ? 42.f : 24.f);
		if (!c.shortcut.empty())
			cw += m_canvas->MeasureText(c.shortcut, fs, mf) + 24.f;
		w = std::max(w, cw);
	}
	float h = 0.f;
	for (const MenuItem &c : item.children)
		h += c.separator ? 8.f : itemH;
	sdl3::FRect r{m_menuPopup.Right() - 4.f, itemY, w, h};
	if (r.Right() > m_canvas->Width()) // déborde : bascule à gauche du menu parent
		r.x = m_menuPopup.x - r.w + 4.f;
	r.y = std::clamp(r.y, 0.f, std::max(0.f, m_canvas->Height() - r.h));
	return r;
}

void Ui::RenderMenuPopup(Node &bar, float mouseX, float mouseY) {
	if (bar.openMenu < 0 || size_t(bar.openMenu) >= bar.menus.size())
		return;
	const float itemH = 26.f;
	const ComputedStyle &c = bar.computed;
	const Menu &menu = bar.menus[size_t(bar.openMenu)];
	const sdl3::CanvasFont mf = FontOf(c);
	// Recalcule la boîte du menu (premier affichage, bornes à jour).
	m_menuPopup = MenuPopupRect(bar);
	const sdl3::FRect &popup = m_menuPopup;
	m_canvas->FillRoundRect(popup, 6.f, sdl3::WithAlpha(m_theme.panel, 250));
	m_canvas->StrokeRoundRect(popup, 6.f, m_theme.panelBorder);
	float y = popup.y;
	for (size_t i = 0; i < menu.items.size(); ++i) {
		const MenuItem &mi = menu.items[i];
		if (mi.separator) {
			m_canvas->Line(popup.x + 8.f, y + 4.f, popup.Right() - 8.f, y + 4.f, m_theme.panelBorder, 1.f);
			y += 8.f;
			continue;
		}
		const sdl3::FRect row{popup.x, y, popup.w, itemH};
		const bool hovered = row.Contains(mouseX, mouseY) && mi.enabled;
		if (hovered)
			m_canvas->FillRect(row.Inset(2), sdl3::WithAlpha(m_theme.widgetHover, 120));
		const sdl3::Color color = mi.enabled ? c.color : sdl3::WithAlpha(m_theme.textDim, 150);
		if (mi.checkable)
			m_canvas->Text(popup.x + 10.f, y + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f,
						   mi.checked ? "\xE2\x9C\x93" : "  ", color, c.fontSize, mf);
		m_canvas->Text(popup.x + (mi.checkable ? 28.f : 10.f), y + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f,
					   mi.label, color, c.fontSize, mf);
		if (!mi.children.empty()) // chevron signalant un sous-menu
			m_canvas->Text(popup.Right() - 14.f, y + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f, "\xE2\x80\xBA", color, c.fontSize, mf);
		else if (!mi.shortcut.empty())
			m_canvas->Text(popup.Right() - 10.f - m_canvas->MeasureText(mi.shortcut, c.fontSize, mf),
						   y + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f, mi.shortcut, color, c.fontSize, mf);
		y += itemH;
	}

	// Sous-menu ouvert : panneau à droite de l'item parent.
	if (bar.openSubMenu >= 0 && size_t(bar.openSubMenu) < menu.items.size() && !menu.items[size_t(bar.openSubMenu)].children.empty()) {
		const MenuItem &parent = menu.items[size_t(bar.openSubMenu)];
		const float py = popup.y + MenuItemY(menu.items, bar.openSubMenu, itemH);
		m_subMenuPopup = SubMenuRect(bar, parent, py);
		const sdl3::FRect &sub = m_subMenuPopup;
		m_canvas->FillRoundRect(sub, 6.f, sdl3::WithAlpha(m_theme.panel, 250));
		m_canvas->StrokeRoundRect(sub, 6.f, m_theme.panelBorder);
		float sy = sub.y;
		for (const MenuItem &ci : parent.children) {
			if (ci.separator) {
				m_canvas->Line(sub.x + 8.f, sy + 4.f, sub.Right() - 8.f, sy + 4.f, m_theme.panelBorder, 1.f);
				sy += 8.f;
				continue;
			}
			const sdl3::FRect row{sub.x, sy, sub.w, itemH};
			const bool hovered = row.Contains(mouseX, mouseY) && ci.enabled;
			if (hovered)
				m_canvas->FillRect(row.Inset(2), sdl3::WithAlpha(m_theme.widgetHover, 120));
			const sdl3::Color color = ci.enabled ? c.color : sdl3::WithAlpha(m_theme.textDim, 150);
			if (ci.checkable)
				m_canvas->Text(sub.x + 10.f, sy + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f,
							   ci.checked ? "\xE2\x9C\x93" : "  ", color, c.fontSize, mf);
			m_canvas->Text(sub.x + (ci.checkable ? 28.f : 10.f), sy + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f,
						   ci.label, color, c.fontSize, mf);
			if (!ci.shortcut.empty())
				m_canvas->Text(sub.Right() - 10.f - m_canvas->MeasureText(ci.shortcut, c.fontSize, mf),
							   sy + (itemH - m_canvas->LineHeight(c.fontSize, mf)) * 0.5f, ci.shortcut, color, c.fontSize, mf);
			sy += itemH;
		}
	}
}

void Ui::RenderTooltip(Node &tip, float mouseX, float mouseY) {
	ResolveStyle(tip, nullptr, nullptr, true);
	const sdl3::CanvasFont font = FontOf(tip.computed);
	const LengthBasis b{0.f, tip.computed.fontSize, m_rootFontSize, m_canvas->Width(), m_canvas->Height(), 1.f};

	// Taille naturelle du CONTENU (texte multi-lignes, sinon hauteur intrinsèque
	// du sous-arbre et largeur explicite).
	float w = 0.f, h = IntrinsicHeight(*m_canvas, tip, b);
	if (tip.type == wt::label || tip.type == wt::text) {
		float maxW = 0.f;
		size_t start = 0;
		while (true) {
			const size_t nl = tip.text.find('\n', start);
			const std::string_view line =
				std::string_view(tip.text).substr(start, nl == std::string::npos ? std::string::npos : nl - start);
			maxW = std::max(maxW, m_canvas->MeasureText(line, tip.computed.fontSize, font));
			if (nl == std::string::npos)
				break;
			start = nl + 1;
		}
		w = maxW;
	} else if (tip.type == wt::button) {
		w = m_canvas->MeasureText(tip.text, tip.computed.fontSize, font) + tip.computed.paddingLeft + tip.computed.paddingRight;
	} else if (!tip.computed.width.IsAuto() && !tip.computed.width.IsFr()) {
		w = tip.computed.width.Resolve(b);
	}

	// Panneau (fond + bordure), décalé de la souris, borné à la fenêtre.
	const float pad = 8.f;
	sdl3::FRect panel{mouseX + 12.f, mouseY + 16.f, w + 2.f * pad, h + 2.f * pad};
	if (panel.Right() > m_canvas->Width())
		panel.x = mouseX - 12.f - panel.w;
	if (panel.Bottom() > m_canvas->Height())
		panel.y = mouseY - 16.f - panel.h;
	panel.x = std::max(0.f, panel.x);
	panel.y = std::max(0.f, panel.y);

	m_canvas->FillRoundRect(panel, 6.f, sdl3::WithAlpha(m_theme.background, 245));
	m_canvas->StrokeRoundRect(panel, 6.f, m_theme.panelBorder);

	// Injecte le padding interne, puis met en page le sous-arbre (une feuille
	// comme un Label, ou n'importe quel conteneur passé via `.Tooltip(...)`).
	tip.computed.paddingTop = tip.computed.paddingRight = tip.computed.paddingBottom = tip.computed.paddingLeft = pad;
	LayoutContext ctx{m_canvas->Width(), m_canvas->Height(), m_rootFontSize};
	LayoutNode(tip, panel, ctx);
	RenderNode(tip);
}

} // namespace ui
