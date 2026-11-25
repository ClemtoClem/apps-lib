// Profiler — visualiseur, à la manière d'EasyProfiler en thème sombre :
//   barre d'outils  : ouvrir, recharger, captures récentes, temps d'image visé, zoom ;
//   moniteur        : courbe des durées d'image (plus lente, plus rapide, moyenne, budget) ;
//   diagramme       : fils à gauche, zones par profondeur, règle des temps, bande d'ensemble
//                     (occurrences de la zone choisie ou durées d'image), ligne d'état ;
//   onglets         : hiérarchie (arbre des appels triable et filtrable), compteurs,
//                     diagnostic, incident (rapport de plantage / blocage et journal).
// Interface retenue (`ui::Ui`, fabrique `ui::`) ; les tracés sont des feuilles `Custom`
// dessinées avec le contexte immédiat de l'application.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/io/dialog.hpp>
#include <ui/builder.hpp>
#include <ui/icons.hpp>
#include <ui/retained.hpp>
#include <ui/ui.hpp>

#include "analysis.hpp"
#include "catalog.hpp"
#include "viewer.hpp"

namespace profiler {

namespace {

using sdl3::Rgba;
using Icon = ui::icons::MaterialIcons;

constexpr const char *kCss = R"css(
:root { color: #d4d9e3; font-size: 13px; accent-color: #4f9dff; widget-color: #232834; widget-hover-color: #2c3342; widget-active-color: #36405a; }
.toolbar { background-color: #1b1f28; padding: 4px 8px; gap: 6px; align-items: center; }
.caption { color: #8c96a8; font-size: 12px; }
.pane { background-color: #12151b; }
.title { background-color: #1b1f28; color: #9aa4b6; font-size: 12px; padding: 3px 8px; }
.status { background-color: #1b1f28; color: #8c96a8; font-size: 12px; padding: 3px 8px; }
toolbutton { border-radius: 6px; }
combobox, spinbox, lineedit { padding: 4px 8px; border-radius: 6px; background-color: #232834; border: 1px solid #333a49; }
)css";

sdl3::Color ZoneColor(std::string_view name) {
	const uint64_t h = core::HashString(name);
	const float hue = float(h % 360) / 360.f;
	auto channel = [&](float off) {
		const float v = std::fabs(std::fmod(hue * 6.f + off, 6.f) - 3.f) - 1.f;
		return uint8_t(60 + 140 * std::clamp(v, 0.f, 1.f));
	};
	return Rgba(channel(0), channel(4), channel(2));
}

/// Couleur de fond d'un fil (bandeau de nom) : teintes sombres alternées.
sdl3::Color ThreadColor(size_t index) {
	static constexpr sdl3::Color kColors[] = {{58, 54, 28, 255}, {36, 42, 66, 255}, {30, 54, 48, 255}, {58, 36, 48, 255}};
	return kColors[index % std::size(kColors)];
}

std::string FormatMs(double ms) {
	if (ms >= 100.0)
		return std::format("{:.0f} ms", ms);
	if (ms >= 1.0)
		return std::format("{:.2f} ms", ms);
	return std::format("{:.0f} µs", ms * 1000.0);
}

/// Ligne affichée de la hiérarchie (arbre des appels aplati).
struct HierRow {
	const CallNode *node = nullptr; ///< nul : en-tête d'un fil
	const CallTree *tree = nullptr;
	const CallNode *parent = nullptr;
	int depth = 0;
	std::string key; ///< chemin « fil/a/b » (état déplié)
	bool hasChildren = false;
};

struct ViewerState {
	diagnostics::capture::Capture capture;
	std::string path;
	std::string incident, logTail;
	Report report;
	std::vector<CallTree> trees;
	double viewStart = 0, viewEnd = 1; // ms
	double selStart = -1, selEnd = -1; // plage sélectionnée (ms)
	const diagnostics::capture::ZoneSpan *selectedZone = nullptr;
	std::string selectedThread;
	std::string focusName; ///< zone mise en avant (hiérarchie ou diagramme) : bande d'ensemble
	float budgetMs = 16.67f;
	// Interactions du diagramme.
	bool panning = false, selecting = false, overviewDrag = false;
	float dragX = 0;
	double dragViewStart = 0;
	float threadScroll = 0;
	const diagnostics::capture::ZoneSpan *lastClicked = nullptr;
	std::chrono::steady_clock::time_point lastClickAt{};
	// Hiérarchie.
	std::string filter;
	std::set<std::string> expanded;
	std::vector<HierRow> rows; ///< pointent dans `trees` : refaites à chaque analyse (Reanalyze)
	size_t matches = 0;
	int sortColumn = -1; ///< dernier tri lu sur l'en-tête du tableau
	bool sortDescending = false;
};

void RebuildRows(ViewerState &s, int sortColumn, bool descending);

double DurationMs(const ViewerState &s) { return std::max(1.0, double(s.capture.durationNs) / 1e6); }

void Reanalyze(ViewerState &s) {
	AnalyzeOptions o;
	o.targetFps = 1000.0 / std::max(1.0, double(s.budgetMs));
	if (s.selStart >= 0 && s.selEnd > s.selStart) {
		o.fromMs = s.selStart;
		o.toMs = s.selEnd;
	}
	s.report = Analyze(s.capture, o);
	s.trees = BuildCallTrees(s.capture, o.fromMs, o.toMs);
	for (const CallTree &t : s.trees) // racines dépliées par défaut
		for (const CallNode &root : t.roots)
			s.expanded.insert(t.thread + "/" + root.name);
	// Les lignes de la hiérarchie pointaient dans les anciens arbres : refaites tout de suite.
	// (Une analyse peut avoir lieu pendant le dessin de l'interface — changement de capture
	// dans la liste, plage choisie dans le diagramme — juste avant que le tableau ne les lise.)
	RebuildRows(s, s.sortColumn, s.sortDescending);
}

bool LoadInto(ViewerState &s, const std::string &path) {
	auto c = diagnostics::capture::Load(path);
	if (!c) {
		core::log::Error("{} : {}", path, c.Error());
		return false;
	}
	s.capture = std::move(c.Value());
	s.path = path;
	const Companions companions = CompanionsOf(path);
	s.incident = ReadText(companions.incident);
	s.logTail = ReadText(companions.log, 24 * 1024);
	s.viewStart = 0;
	s.viewEnd = DurationMs(s);
	s.selStart = s.selEnd = -1;
	s.selectedZone = nullptr;
	s.lastClicked = nullptr;
	s.focusName.clear();
	Reanalyze(s);
	return true;
}

// ── Moniteur d'images ────────────────────────────────────────────────────────

void DrawFrameMonitor(ViewerState &s, ui::Context &u) {
	const sdl3::FRect area = u.Remaining();
	sdl3::Canvas &c = u.Canvas();
	c.FillRect(area, Rgba(14, 16, 21));
	const auto &frames = s.capture.frames;
	if (frames.size() < 2) {
		c.TextCentered(area, "aucune image marquée (DIAG_FRAME)", Rgba(110, 118, 134), 13);
		return;
	}
	std::vector<double> ms(frames.size() - 1);
	double lo = 1e30, hi = 0, sum = 0;
	size_t slowest = 0, fastest = 0;
	for (size_t i = 1; i < frames.size(); ++i) {
		ms[i - 1] = double(frames[i] - frames[i - 1]) / 1e6;
		sum += ms[i - 1];
		if (ms[i - 1] > hi) {
			hi = ms[i - 1];
			slowest = i - 1;
		}
		if (ms[i - 1] < lo) {
			lo = ms[i - 1];
			fastest = i - 1;
		}
	}
	const double avg = sum / double(ms.size());
	const sdl3::FRect plot{area.x + 8, area.y + 20, area.w - 16, area.h - 40};
	// Échelle : 99e centile (une image aberrante, comme l'arrêt du programme, n'écrase pas la courbe).
	std::vector<double> sorted = ms;
	std::ranges::sort(sorted);
	const double p99 = sorted[std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.99))];
	const double top = std::max({p99 * 1.25, double(s.budgetMs) * 1.5, 1.0});
	const auto toY = [&](double v) { return std::max(plot.y, plot.Bottom() - float(v / top) * plot.h); };
	const auto toX = [&](double timeMs) { return plot.x + float(timeMs / DurationMs(s)) * plot.w; };
	// Fenêtre visible du diagramme.
	c.FillRect({toX(s.viewStart), plot.y, std::max(1.f, toX(s.viewEnd) - toX(s.viewStart)), plot.h}, Rgba(79, 157, 255, 28));
	// Budget (temps d'image visé), en tirets.
	const float by = toY(s.budgetMs);
	for (float x = plot.x; x < plot.Right(); x += 8)
		c.FillRect({x, by, 4, 1}, Rgba(230, 96, 96, 160));
	// Courbes par colonne de pixels : pic (rouge, les saccades restent visibles) et moyenne (bleu).
	const int columns = std::max(1, int(plot.w));
	std::vector<float> peak(size_t(columns), 0.f), mean(size_t(columns), 0.f);
	std::vector<int> count(size_t(columns), 0);
	for (size_t i = 0; i < ms.size(); ++i) {
		const int col = std::clamp(int((double(frames[i + 1]) / 1e6) / DurationMs(s) * columns), 0, columns - 1);
		peak[size_t(col)] = std::max(peak[size_t(col)], float(ms[i]));
		mean[size_t(col)] += float(ms[i]);
		++count[size_t(col)];
	}
	float prevPeak = -1, prevMean = -1, prevX = 0;
	for (int x = 0; x < columns; ++x) {
		if (!count[size_t(x)])
			continue;
		const float px = plot.x + float(x), py = toY(peak[size_t(x)]), my = toY(mean[size_t(x)] / float(count[size_t(x)]));
		if (prevPeak >= 0) {
			c.Line(prevX, prevMean, px, my, Rgba(110, 130, 240)); // moyenne de la colonne
			c.Line(prevX, prevPeak, px, py, Rgba(240, 110, 110)); // pic de la colonne, par-dessus
		}
		if (double(peak[size_t(x)]) > top) // hors échelle : marque en haut
			c.FillRect({px - 1, plot.y, 3, 4}, Rgba(255, 80, 80));
		prevX = px;
		prevPeak = py;
		prevMean = my;
	}
	const auto fps = [](double v) { return v > 0 ? 1000.0 / v : 0.0; };
	const sdl3::Color label = Rgba(150, 158, 172);
	c.Text(area.x + 8, area.y + 3, std::format("Plus lente  {:.0f} ips ({:.1f} ms) · image {}", fps(hi), hi, slowest), label, 12);
	c.Text(area.x + 8, area.Bottom() - 17, std::format("Plus rapide  {:.0f} ips ({:.1f} ms) · image {}", fps(lo), lo, fastest), label, 12);
	const std::string avgText = std::format("Moyenne  {:.0f} ips ({:.2f} ms) · budget {:.2f} ms", fps(avg), avg, double(s.budgetMs));
	c.Text(area.Right() - 8 - c.MeasureText(avgText, 12), area.Bottom() - 17, avgText, label, 12);
	// Survol : image sous le curseur ; clic : le diagramme s'y centre.
	if (u.MouseIn(plot)) {
		const double t = double(u.MouseX() - plot.x) / double(plot.w) * DurationMs(s);
		const auto it = std::lower_bound(frames.begin(), frames.end(), int64_t(t * 1e6));
		const size_t i = size_t(std::clamp<ptrdiff_t>(it - frames.begin(), 1, ptrdiff_t(frames.size() - 1)));
		const double fa = double(frames[i - 1]) / 1e6, fb = double(frames[i]) / 1e6;
		c.FillRect({toX(fa), plot.y, std::max(1.f, toX(fb) - toX(fa)), plot.h}, Rgba(255, 255, 255, 40));
		const std::string hover = std::format("image {} · {:.2f} ms ({:.0f} ips) · à {:.1f} s", i - 1, fb - fa, fps(fb - fa), fa / 1000.0);
		c.Text(area.Right() - 8 - c.MeasureText(hover, 12), area.y + 3, hover, Rgba(220, 226, 236), 12);
		if (u.MousePressed(sdl3::MouseButton::Left)) {
			const double span = s.viewEnd - s.viewStart;
			s.viewStart = (fa + fb) * 0.5 - span * 0.5;
			s.viewEnd = s.viewStart + span;
		}
	}
}

// ── Diagramme ────────────────────────────────────────────────────────────────

void DrawDiagram(ViewerState &s, ui::Context &u) {
	const sdl3::FRect area = u.Remaining();
	sdl3::Canvas &c = u.Canvas();
	c.FillRect(area, Rgba(14, 16, 21));
	if (s.capture.threads.empty()) {
		c.TextCentered(area, "Aucune capture : ouvrir un fichier .vcap, ou lancer un programme avec « profiler record »", Rgba(110, 118, 134), 15);
		return;
	}
	constexpr float kLabelW = 170, kRuler = 20, kOverview = 64, kStatus = 18, kRowH = 18;
	const sdl3::FRect labels{area.x, area.y, kLabelW, area.h - kRuler - kOverview - kStatus};
	const sdl3::FRect lanes{area.x + kLabelW, area.y, area.w - kLabelW, labels.h};
	const sdl3::FRect ruler{lanes.x, lanes.Bottom(), lanes.w, kRuler};
	const sdl3::FRect overview{lanes.x, ruler.Bottom(), lanes.w, kOverview};
	const sdl3::FRect status{area.x, overview.Bottom(), area.w, kStatus};
	const double span = std::max(1e-6, s.viewEnd - s.viewStart);
	const auto toX = [&](double ms) { return lanes.x + float((ms - s.viewStart) / span) * lanes.w; };
	const auto toMs = [&](float x) { return s.viewStart + double((x - lanes.x) / lanes.w) * span; };
	const bool overLanes = u.MouseIn(lanes);

	// Navigation : molette = zoom au curseur (Maj : défilement vertical des fils), glisser droit =
	// défilement, Maj + glisser gauche = sélection d'une plage.
	if (overLanes && u.WheelY() != 0) {
		if (u.Shift())
			s.threadScroll = std::max(0.f, s.threadScroll - u.WheelY() * 40.f);
		else {
			const double pivot = toMs(u.MouseX()), factor = std::pow(0.8, double(u.WheelY()));
			s.viewStart = pivot - (pivot - s.viewStart) * factor;
			s.viewEnd = pivot + (s.viewEnd - pivot) * factor;
		}
	}
	if (overLanes && u.MousePressed(sdl3::MouseButton::Right)) {
		s.panning = true;
		s.dragX = u.MouseX();
		s.dragViewStart = s.viewStart;
	}
	if (s.panning) {
		if (!u.MouseDown(sdl3::MouseButton::Right))
			s.panning = false;
		else {
			s.viewStart = s.dragViewStart - double(u.MouseX() - s.dragX) / double(lanes.w) * span;
			s.viewEnd = s.viewStart + span;
		}
	}
	if (overLanes && u.MousePressed(sdl3::MouseButton::Left) && u.Shift()) {
		s.selecting = true;
		s.selStart = s.selEnd = toMs(u.MouseX());
	}
	if (s.selecting) {
		s.selEnd = toMs(u.MouseX());
		if (!u.MouseDown(sdl3::MouseButton::Left)) {
			s.selecting = false;
			if (s.selEnd < s.selStart)
				std::swap(s.selStart, s.selEnd);
			if (s.selEnd - s.selStart < span * 0.002)
				s.selStart = s.selEnd = -1;
			Reanalyze(s);
		}
	}

	// Colonnes d'images alternées (repères comme dans EasyProfiler).
	c.PushClip(lanes);
	const auto &frames = s.capture.frames;
	for (size_t i = 1; i < frames.size(); ++i) {
		const double a = double(frames[i - 1]) / 1e6, b = double(frames[i]) / 1e6;
		if (b < s.viewStart || a > s.viewEnd)
			continue;
		if (i % 2)
			c.FillRect({toX(a), lanes.y, toX(b) - toX(a), lanes.h}, Rgba(255, 255, 255, 7));
		c.FillRect({toX(a), lanes.y, 1, lanes.h}, Rgba(255, 255, 255, 18));
	}
	c.PopClip();

	// Fils : bandeau de nom à gauche, zones empilées par profondeur à droite.
	const diagnostics::capture::ZoneSpan *hovered = nullptr;
	std::string hoveredThread;
	float y = lanes.y + 4 - s.threadScroll;
	c.PushClip({area.x, area.y, area.w, labels.h});
	for (size_t ti = 0; ti < s.capture.threads.size(); ++ti) {
		const auto &t = s.capture.threads[ti];
		if (t.zones.empty())
			continue;
		int maxDepth = 0;
		for (const auto &z : t.zones)
			maxDepth = std::max(maxDepth, int(z.depth));
		const float h = float(maxDepth + 1) * kRowH + 8;
		c.FillRect({labels.x, y - 4, labels.w, h}, ThreadColor(ti));
		c.FillRect({lanes.x, y - 4, lanes.w, 1}, Rgba(40, 46, 58));
		const std::string label = std::format("{} {}", t.name, t.id);
		c.Text(labels.Right() - 10 - c.MeasureText(label, 13, sdl3::CanvasFont::Bold), y - 4 + (h - 16) * 0.5f, label, Rgba(220, 224, 232), 13,
			   sdl3::CanvasFont::Bold);
		c.PushClip({lanes.x, std::max(lanes.y, y - 4), lanes.w, std::max(0.f, std::min(h, lanes.Bottom() - (y - 4)))});
		for (const auto &z : t.zones) {
			const double a = double(z.begin) / 1e6, b = double(z.end) / 1e6;
			if (b < s.viewStart || a > s.viewEnd)
				continue;
			const float x0 = toX(a), x1 = toX(b);
			if (x1 - x0 < 0.35f)
				continue;
			const sdl3::FRect r{x0, y + float(z.depth) * kRowH, std::max(1.f, x1 - x0), kRowH - 1};
			const std::string &name = s.capture.Name(z.name);
			const bool focused = !s.focusName.empty() && name == s.focusName;
			sdl3::Color col = ZoneColor(name);
			if (!s.focusName.empty() && !focused)
				col = sdl3::WithAlpha(col, 110); // les autres zones s'effacent
			c.FillRect(r, col);
			if (&z == s.selectedZone)
				c.StrokeRect(r, Rgba(255, 255, 255));
			else if (focused)
				c.StrokeRect(r, Rgba(255, 236, 160));
			if (r.w > 36) {
				c.PushClip(r);
				const std::string text = r.w > 120 ? std::format("{}  {}", name, FormatMs(b - a)) : name;
				const float tw = c.MeasureText(text, 11);
				c.Text(r.x + std::max(3.f, (r.w - tw) * 0.5f), r.y + 2, text, Rgba(14, 16, 20), 11);
				c.PopClip();
			}
			if (overLanes && r.Contains(u.MouseX(), u.MouseY())) {
				hovered = &z;
				hoveredThread = t.name;
			}
		}
		c.PopClip();
		y += h;
	}
	const float contentH = y + s.threadScroll - lanes.y;
	s.threadScroll = std::clamp(s.threadScroll, 0.f, std::max(0.f, contentH - lanes.h));
	c.PopClip();

	// Plage sélectionnée.
	if (s.selStart >= 0 && s.selEnd != s.selStart) {
		const float x0 = std::max(lanes.x, toX(std::min(s.selStart, s.selEnd))), x1 = std::min(lanes.Right(), toX(std::max(s.selStart, s.selEnd)));
		if (x1 > x0) {
			c.FillRect({x0, lanes.y, x1 - x0, lanes.h}, Rgba(79, 157, 255, 40));
			c.FillRect({x0, lanes.y, 1, lanes.h}, Rgba(79, 157, 255));
			c.FillRect({x1, lanes.y, 1, lanes.h}, Rgba(79, 157, 255));
		}
	}

	// Règle des temps.
	c.FillRect(ruler, Rgba(24, 27, 35));
	const double step = std::pow(10.0, std::floor(std::log10(span / 8)));
	c.PushClip(ruler);
	for (double t = std::ceil(s.viewStart / step) * step; t < s.viewEnd; t += step) {
		const float x = toX(t);
		c.FillRect({x, ruler.y, 1, 6}, Rgba(120, 128, 145));
		c.Text(x + 3, ruler.y + 4, std::format("{:.{}f}", t, step < 1 ? (step < 0.1 ? 2 : 1) : 0), Rgba(140, 148, 165), 11);
	}
	c.PopClip();

	// Coin gauche : durée de la zone choisie (ou de la plage, ou de la vue), grand format.
	const sdl3::FRect corner{area.x, ruler.y, kLabelW, kRuler + kOverview};
	c.FillRect(corner, Rgba(18, 21, 27));
	const double shown = s.selectedZone ? double(s.selectedZone->end - s.selectedZone->begin) / 1e6 : (s.selStart >= 0 ? s.selEnd - s.selStart : span);
	c.Text(corner.x + 12, corner.y + 10, FormatMs(shown), Rgba(79, 157, 255), 24, sdl3::CanvasFont::Bold);
	c.Text(corner.x + 12, corner.y + 44, s.selectedZone ? "zone choisie" : (s.selStart >= 0 ? "plage choisie" : "fenêtre visible"), Rgba(110, 118, 134), 11);

	// Bande d'ensemble : occurrences de la zone mise en avant, sinon durées d'image, sur toute
	// la capture ; la fenêtre visible y est encadrée, un clic (ou glisser) y déplace la vue.
	c.FillRect(overview, Rgba(18, 21, 27));
	const auto ovX = [&](double ms) { return overview.x + float(ms / DurationMs(s)) * overview.w; };
	size_t occurrences = 0;
	double focusTotal = 0, threadBusy = 0;
	if (!s.focusName.empty()) {
		double maxMs = 0;
		std::vector<std::pair<double, double>> bars;
		for (const auto &t : s.capture.threads)
			for (const auto &z : t.zones)
				if (s.capture.Name(z.name) == s.focusName) {
					const double d = double(z.end - z.begin) / 1e6;
					bars.emplace_back(double(z.begin) / 1e6, d);
					maxMs = std::max(maxMs, d);
					focusTotal += d;
				}
		occurrences = bars.size();
		for (const auto &[at, d] : bars) {
			const float h = float(d / std::max(maxMs, 1e-9)) * (overview.h - 6);
			c.FillRect({ovX(at), overview.Bottom() - 3 - h, 1, h}, Rgba(96, 200, 120));
		}
		for (const CallTree &tree : s.trees)
			threadBusy += tree.busyMs;
	} else {
		for (size_t i = 1; i < frames.size(); ++i) {
			const double a = double(frames[i - 1]) / 1e6, d = double(frames[i] - frames[i - 1]) / 1e6;
			const float h = std::min(1.f, float(d / (2.0 * double(s.budgetMs)))) * (overview.h - 6);
			c.FillRect({ovX(a), overview.Bottom() - 3 - h, 1, h}, d > double(s.budgetMs) ? Rgba(236, 150, 72) : Rgba(96, 200, 120));
		}
	}
	c.StrokeRect({ovX(s.viewStart), overview.y + 1, std::max(2.f, ovX(s.viewEnd) - ovX(s.viewStart)), overview.h - 2}, Rgba(79, 157, 255));
	if (u.MouseIn(overview) && u.MousePressed(sdl3::MouseButton::Left))
		s.overviewDrag = true;
	if (s.overviewDrag) {
		if (!u.MouseDown(sdl3::MouseButton::Left))
			s.overviewDrag = false;
		else {
			const double center = double(std::clamp(u.MouseX(), overview.x, overview.Right()) - overview.x) / double(overview.w) * DurationMs(s);
			s.viewStart = center - span * 0.5;
			s.viewEnd = s.viewStart + span;
		}
	}

	// Ligne d'état.
	c.FillRect(status, Rgba(24, 27, 35));
	const std::string line = s.focusName.empty()
								 ? std::format("MODE : vue d'ensemble · {} images · fenêtre {} → {}", frames.size(), FormatMs(s.viewStart), FormatMs(s.viewEnd))
								 : std::format("MODE : zone « {} » · {} appels · {} au total · {:.2f} % du temps des fils", s.focusName, occurrences, FormatMs(focusTotal),
											   threadBusy > 0 ? 100.0 * focusTotal / threadBusy : 0.0);
	c.Text(status.x + 8, status.y + 2, line, Rgba(150, 158, 172), 12);
	const std::string help = "molette : zoom · clic droit glissé : défiler · Maj + glisser : plage · double-clic : cadrer la zone";
	c.Text(status.Right() - 8 - c.MeasureText(help, 11), status.y + 3, help, Rgba(96, 104, 120), 11);

	// Survol, sélection, double-clic (cadrage sur la zone).
	if (hovered) {
		u.Tooltip(std::format("{}\n{} · fil {} · profondeur {}\ndébut {}", s.capture.Name(hovered->name), FormatMs(double(hovered->end - hovered->begin) / 1e6),
							  hoveredThread, hovered->depth, FormatMs(double(hovered->begin) / 1e6)));
		if (u.MouseReleased(sdl3::MouseButton::Left) && !u.Shift() && !s.selecting) {
			const auto now = std::chrono::steady_clock::now();
			if (s.lastClicked == hovered && now - s.lastClickAt < std::chrono::milliseconds(400)) {
				const double a = double(hovered->begin) / 1e6, b = double(hovered->end) / 1e6, pad = (b - a) * 0.1;
				s.viewStart = a - pad;
				s.viewEnd = b + pad;
			}
			s.lastClicked = hovered;
			s.lastClickAt = now;
			s.selectedZone = hovered;
			s.selectedThread = hoveredThread;
			s.focusName = s.capture.Name(hovered->name);
		}
	}
}

// ── Hiérarchie ───────────────────────────────────────────────────────────────

/// Colonnes : nom, appels, total, propre, min, moyenne, max, par image, % du parent.
constexpr int kHierColumns = 9;

double SortValue(const CallNode &n, int column, double frames, double parentTotal) {
	switch (column) {
	case 1: return double(n.calls);
	case 2: return n.totalMs;
	case 3: return n.selfMs;
	case 4: return n.minMs;
	case 5: return n.AvgMs();
	case 6: return n.maxMs;
	case 7: return n.totalMs / std::max(frames, 1.0);
	case 8: return parentTotal > 0 ? n.totalMs / parentTotal : 0.0;
	default: return 0.0;
	}
}

std::string Lower(std::string v) {
	std::ranges::transform(v, v.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
	return v;
}

/// La zone ou l'un de ses descendants contient `filter` (déjà en minuscules).
bool Matches(const CallNode &n, const std::string &filter) {
	if (filter.empty() || Lower(n.name).find(filter) != std::string::npos)
		return true;
	return std::ranges::any_of(n.children, [&](const CallNode &c) { return Matches(c, filter); });
}

/// Aplatit les arbres en lignes visibles (dépliées, filtrées, frères triés par la colonne).
void RebuildRows(ViewerState &s, int sortColumn, bool descending) {
	s.rows.clear();
	s.matches = 0;
	const std::string filter = Lower(s.filter);
	const double frames = double(std::max<size_t>(1, s.capture.frames.size()));
	const auto visit = [&](auto &self, const CallTree &tree, const std::vector<CallNode> &list, const CallNode *parent, int depth, const std::string &prefix) -> void {
		std::vector<const CallNode *> order;
		for (const CallNode &n : list)
			if (Matches(n, filter))
				order.push_back(&n);
		const double parentTotal = parent ? parent->totalMs : tree.busyMs;
		if (sortColumn == 0)
			std::ranges::sort(order, [&](const CallNode *a, const CallNode *b) { return descending ? a->name > b->name : a->name < b->name; });
		else if (sortColumn > 0)
			std::ranges::sort(order, [&](const CallNode *a, const CallNode *b) {
				const double va = SortValue(*a, sortColumn, frames, parentTotal), vb = SortValue(*b, sortColumn, frames, parentTotal);
				return descending ? va > vb : va < vb;
			});
		else // par défaut : le plus coûteux d'abord
			std::ranges::sort(order, [](const CallNode *a, const CallNode *b) { return a->totalMs > b->totalMs; });
		for (const CallNode *n : order) {
			const std::string key = prefix + "/" + n->name;
			s.rows.push_back({n, &tree, parent, depth, key, !n->children.empty()});
			if (!filter.empty() && Lower(n->name).find(filter) != std::string::npos)
				++s.matches;
			if (!n->children.empty() && (!filter.empty() || s.expanded.contains(key)))
				self(self, tree, n->children, n, depth + 1, key);
		}
	};
	for (const CallTree &tree : s.trees) {
		if (s.trees.size() > 1) // plusieurs fils : une ligne d'en-tête par fil
			s.rows.push_back({nullptr, &tree, nullptr, 0, tree.thread, false});
		visit(visit, tree, tree.roots, nullptr, s.trees.size() > 1 ? 1 : 0, tree.thread);
	}
}

std::string RowText(const ViewerState &s, const HierRow &r, size_t col) {
	if (!r.tree)
		return {};
	if (!r.node) // en-tête de fil
		return col == 0 ? std::format("fil {} (#{}) — {} occupés", r.tree->thread, r.tree->threadId, FormatMs(r.tree->busyMs)) : std::string();
	const CallNode &n = *r.node;
	const double frames = double(std::max<size_t>(1, s.capture.frames.size()));
	const double parentTotal = r.parent ? r.parent->totalMs : r.tree->busyMs;
	switch (col) {
	case 0: {
		const char *mark = !r.hasChildren ? "   " : (s.filter.empty() && !s.expanded.contains(r.key) ? "+  " : "-  "); // replié / déplié
		return std::string(size_t(r.depth) * 3, ' ') + mark + n.name;
	}
	case 1: return std::to_string(n.calls);
	case 2: return std::format("{:.3f}", n.totalMs);
	case 3: return std::format("{:.3f}", n.selfMs);
	case 4: return std::format("{:.3f}", n.minMs);
	case 5: return std::format("{:.3f}", n.AvgMs());
	case 6: return std::format("{:.3f}", n.maxMs);
	case 7: return std::format("{:.3f}", n.totalMs / frames);
	case 8: return std::format("{:.1f} %", parentTotal > 0 ? 100.0 * n.totalMs / parentTotal : 0.0);
	default: return {};
	}
}

// ── Onglets secondaires ──────────────────────────────────────────────────────

void DrawCounters(ViewerState &s, ui::Context &u) {
	u.BeginScroll("compteurs", u.Remaining());
	for (const auto &cs : s.report.counters) {
		std::vector<float> v;
		v.reserve(cs.series.size());
		for (const auto &[t, val] : cs.series)
			v.push_back(float(val));
		u.Plot(std::format("{}  (min {:.6g} · moy {:.6g} · max {:.6g} · dernier {:.6g})", cs.name, cs.min, cs.avg, cs.max, cs.last), v, 0, 0, 64);
	}
	if (s.report.counters.empty())
		u.Label("aucun compteur (DIAG_COUNTER)", u.Style().textDim);
	u.EndScroll();
}

void DrawDiagnostic(ViewerState &s, ui::Context &u) {
	const ui::Theme &th = u.Style();
	u.BeginScroll("diagnostic", u.Remaining());
	const FrameStats &f = s.report.frames;
	u.Heading(std::format("{} images · moyenne {:.2f} ms ({:.0f} ips)", f.count, f.avgMs, f.avgMs > 0 ? 1000 / f.avgMs : 0));
	u.Label(std::format("médiane {:.2f} · p95 {:.2f} · p99 {:.2f} · max {:.2f} ms · {:.1f} % au-delà du budget · {} saccades", f.medianMs, f.p95Ms, f.p99Ms,
						f.maxMs, f.overBudgetPercent, f.stutters),
			th.textDim);
	u.Separator();
	for (const auto &x : s.report.findings)
		u.Label(x.message, x.severity == Severity::Critical ? th.danger : x.severity == Severity::Warning ? th.warning : th.text);
	u.Separator();
	for (const auto &t : s.report.threads) {
		u.Label(std::format("fil {} (#{}) — {} occupés", t.name, t.id, FormatMs(t.busyMs)));
		u.ProgressBar(float(t.busyPercent / 100.0), std::format("{:.1f} %", t.busyPercent));
	}
	u.EndScroll();
}

void DrawIncident(ViewerState &s, ui::Context &u) {
	const ui::Theme &th = u.Style();
	u.BeginScroll("incident", u.Remaining());
	const auto lines = [&](std::string_view text, sdl3::Color color) {
		for (size_t start = 0; start < text.size();) {
			const size_t nl = std::min(text.find('\n', start), text.size());
			u.Label(text.substr(start, nl - start), color, 12);
			start = nl + 1;
		}
	};
	if (s.incident.empty())
		u.Label("Aucun incident enregistré pour cette capture.", th.textDim);
	else {
		u.Heading("Rapport d'incident (plantage ou blocage)");
		lines(s.incident, th.text);
	}
	u.Separator();
	u.Heading("Journal (fin)");
	if (s.logTail.empty())
		u.Label("aucun journal (capture hors session de diagnostic)", th.textDim);
	else
		lines(s.logTail, th.textDim);
	u.EndScroll();
}

} // namespace

// ── Application ──────────────────────────────────────────────────────────────

class Viewer final : public app::Application {
public:
	explicit Viewer(std::string path) : m_path(std::move(path)) {}

protected:
	bool OnInit() override {
		RefreshRecent();
		if (!m_path.empty())
			LoadInto(m_state, m_path);
		SyncRecentIndex();
		BuildInterface();
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		if (m_refresh -= frameSeconds; m_refresh <= 0.0) // nouvelles captures des applications
			RefreshRecent();
		for (const std::string &f : Events().DroppedFiles())
			Open(f);
		std::optional<std::string> chosen;
		{
			std::scoped_lock lock(m_dialogMutex); // rappel de la boîte de dialogue (autre fil)
			chosen.swap(m_pendingOpen);
		}
		if (chosen)
			Open(*chosen);
		if (!Ui().WantsKeyboard()) {
			if (Events().KeycodePressed(SDLK_HOME))
				FitAll();
			if (Events().KeycodePressed(SDLK_F5))
				Reload();
		}
		if (m_budget != m_state.budgetMs) { // temps d'image visé modifié dans la barre d'outils
			m_state.budgetMs = std::max(1.f, m_budget);
			Reanalyze(m_state);
		}
	}

	void OnRender(const sdl3::FRect &area, double) override {
		// Hiérarchie : tri lu sur l'en-tête du tableau, lignes reconstruites (arbre déplié, filtre).
		m_state.sortColumn = m_table ? m_table->modelView.sortColumn : -1;
		m_state.sortDescending = m_table && m_table->modelView.sortDescending;
		RebuildRows(m_state, m_state.sortColumn, m_state.sortDescending);
		if (m_matches)
			m_matches.SetText(m_state.filter.empty() ? std::format("{} lignes", m_state.rows.size()) : std::format("{} correspondance(s)", m_state.matches));
		if (m_status)
			m_status.SetText(StatusText());
		m_ui->Present(FrameSeconds(), area);
	}

	void TitleButtons(std::vector<ui::FrameButton> &, std::vector<ui::FrameButton> &right) override {
		right.push_back({.id = "open", .icon = ui::icons::Codepoint(Icon::FOLDER_OPEN), .tooltip = "Ouvrir une capture…"});
	}
	void OnTitleButton(std::string_view id) override {
		if (id == "open")
			ShowOpenDialog();
	}

private:
	void BuildInterface() {
		m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
		m_ui->SetTheme(Ui().Style());
		m_ui->SetImmediateContext(&Ui());
		m_ui->Sheet(ui::StyleSheet::Parse(kCss, "profiler"));
		ui::TableModel model;
		model.rowCount = [this] { return m_state.rows.size(); };
		model.columnCount = [] { return size_t(kHierColumns); };
		model.text = [this](size_t row, size_t col) { return row < m_state.rows.size() ? RowText(m_state, m_state.rows[row], col) : std::string(); };
		const auto custom = [this](void (*draw)(ViewerState &, ui::Context &)) { return ui::Custom([this, draw](ui::Context &u) { draw(m_state, u); }); };
		m_ui->Root(ui::Column().Children({
			// Barre d'outils.
			ui::Row().Class("toolbar").Height(40.f).Children({
				ui::ToolButton(ui::icons::Codepoint(Icon::FOLDER_OPEN)).Tooltip("Ouvrir une capture… (ou glisser-déposer un .vcap)").OnClick([this] { ShowOpenDialog(); }),
				ui::ToolButton(ui::icons::Codepoint(Icon::REFRESH))
					.Tooltip("Recharger (F5) : la capture d'une partie en cours est réécrite toutes les 10 s")
					.OnClick([this] { Reload(); }),
				ui::ComboBox()
					.Ref(m_recentBox)
					.Options(RecentOptions())
					.Bind(m_recentIndex)
					.Width(340.f)
					.Tooltip("Captures récentes : profiler et diagnostics des applications ; (!) : incident enregistré")
					.OnIndexChanged([this](int i) {
						m_recentIndex = i;
						if (i >= 0 && size_t(i) < m_recent.size())
							Open(m_recent[size_t(i)].string());
					}),
				ui::Separator().Vertical(),
				ui::Text("Temps d'image visé").Class("caption"),
				ui::SpinBox(1.f, 200.f, 0.5f).Bind(m_budget).Width(100.f).Tooltip("Budget d'une image (ms) : 16,67 pour 60 ips"),
				ui::Text("ms").Class("caption"),
				ui::Separator().Vertical(),
				ui::ToolButton(ui::icons::Codepoint(Icon::ZOOM_OUT_MAP)).Tooltip("Vue entière (Origine)").OnClick([this] { FitAll(); }),
				ui::ToolButton(ui::icons::Codepoint(Icon::ZOOM_IN)).Tooltip("Cadrer la zone ou la plage choisie").OnClick([this] { FitSelection(); }),
				ui::ToolButton(ui::icons::Codepoint(Icon::CLEAR)).Tooltip("Effacer la sélection").OnClick([this] { ClearSelection(); }),
				ui::Spacer(),
			}),
			// Moniteur + diagramme en haut, onglets en bas : séparateur redimensionnable.
			ui::Splitter().Vertical().Grow().Children({
				ui::Column().Class("pane").Children({
					ui::Text("Moniteur d'images").Class("title"),
					custom(DrawFrameMonitor).Height(130.f),
					ui::Text("Diagramme").Class("title"),
					custom(DrawDiagram).Grow(),
				}),
				ui::TabWidget().Tabs({"Hiérarchie", "Compteurs", "Diagnostic", "Incident"}).Bind(m_tab).Children({
					ui::Column().Children({
						ui::Row().Class("toolbar").Height(36.f).Children({
							ui::ToolButton(ui::icons::Codepoint(Icon::UNFOLD_MORE)).Tooltip("Tout déplier").OnClick([this] { ExpandAll(); }),
							ui::ToolButton(ui::icons::Codepoint(Icon::UNFOLD_LESS)).Tooltip("Tout replier").OnClick([this] { m_state.expanded.clear(); }),
							ui::LineEdit().Bind(m_state.filter).Placeholder("filtrer les zones…").Width(280.f),
							ui::Spacer(),
							ui::Text("").Ref(m_matches).Class("caption"),
						}),
						ui::TableView()
							.Ref(m_table)
							.Headers({"Nom", "Appels", "Total ms", "Propre ms", "Min ms", "Moy ms", "Max ms", "Par image ms", "% parent"})
							.Model(model)
							.Grow()
							.Tooltip("Clic : mettre la zone en avant dans le diagramme · double-clic : déplier ou replier")
							.OnSelect([this](int row) { SelectRow(row); })
							.OnActivate([this](int row) { ToggleRow(row); }),
					}),
					custom(DrawCounters),
					custom(DrawDiagnostic),
					custom(DrawIncident),
				}),
			}),
			ui::Text("").Ref(m_status).Class("status"),
		}));
		if (m_table) // largeurs : le nom d'abord, puis les colonnes numériques
			m_table->modelView.columnWidths = {360.f, 80.f, 100.f, 100.f, 90.f, 90.f, 90.f, 110.f, 90.f};
		if (!m_state.incident.empty())
			m_tab = 3; // l'incident d'abord : c'est ce qu'on vient chercher
	}

	std::vector<std::string> RecentOptions() const { return m_recentNames.empty() ? std::vector<std::string>{"(aucune capture trouvée)"} : m_recentNames; }

	std::string StatusText() const {
		if (m_state.path.empty())
			return std::format("aucune capture · dossiers explorés : {} et {}", CaptureDirectories().front().string(), CaptureDirectories().back().string());
		size_t zones = 0;
		for (const auto &t : m_state.capture.threads)
			zones += t.zones.size();
		return std::format("{} · {:.1f} s · {} images · {} fils · {} zones{}", m_state.path, DurationMs(m_state) / 1000.0, m_state.capture.frames.size(),
						   m_state.capture.threads.size(), zones, m_state.incident.empty() ? "" : " · incident enregistré");
	}

	void Open(const std::string &path) {
		if (LoadInto(m_state, path)) {
			SyncRecentIndex();
			if (!m_state.incident.empty())
				m_tab = 3;
			Ui().Notify(std::format("capture ouverte : {}", std::filesystem::path(path).filename().string()));
		} else
			Ui().Notify(std::format("lecture impossible : {}", path), Ui().Style().danger);
	}
	void Reload() {
		if (m_state.path.empty())
			return;
		const double start = m_state.viewStart, end = m_state.viewEnd;
		Open(m_state.path);
		m_state.viewStart = start;
		m_state.viewEnd = end;
	}
	void ShowOpenDialog() {
		sdl3::dialog::ShowOpenFile(
			[this](const sdl3::DialogResult &result, int) {
				if (!result.Ok())
					return;
				std::scoped_lock lock(m_dialogMutex);
				m_pendingOpen = std::string(result.First().CStr());
			},
			Window(), {{"Captures du profiler", "vcap"}}, String(CaptureDirectories().back().string().c_str()));
	}
	void FitAll() {
		m_state.viewStart = 0;
		m_state.viewEnd = DurationMs(m_state);
	}
	void FitSelection() {
		if (m_state.selectedZone) {
			const double a = double(m_state.selectedZone->begin) / 1e6, b = double(m_state.selectedZone->end) / 1e6, pad = (b - a) * 0.1;
			m_state.viewStart = a - pad;
			m_state.viewEnd = b + pad;
		} else if (m_state.selStart >= 0 && m_state.selEnd > m_state.selStart) {
			m_state.viewStart = m_state.selStart;
			m_state.viewEnd = m_state.selEnd;
		}
	}
	void ClearSelection() {
		m_state.selStart = m_state.selEnd = -1;
		m_state.selectedZone = nullptr;
		m_state.focusName.clear();
		Reanalyze(m_state);
	}
	void ExpandAll() {
		const auto visit = [&](auto &self, const std::vector<CallNode> &list, const std::string &prefix) -> void {
			for (const CallNode &n : list) {
				m_state.expanded.insert(prefix + "/" + n.name);
				self(self, n.children, prefix + "/" + n.name);
			}
		};
		for (const CallTree &t : m_state.trees)
			visit(visit, t.roots, t.thread);
	}
	void SelectRow(int row) {
		if (row >= 0 && size_t(row) < m_state.rows.size() && m_state.rows[size_t(row)].node)
			m_state.focusName = m_state.rows[size_t(row)].node->name; // le diagramme met ses occurrences en avant
	}
	void ToggleRow(int row) {
		if (row < 0 || size_t(row) >= m_state.rows.size() || !m_state.rows[size_t(row)].hasChildren)
			return;
		const std::string key = m_state.rows[size_t(row)].key;
		if (!m_state.expanded.erase(key))
			m_state.expanded.insert(key);
	}
	void RefreshRecent() {
		m_refresh = 2.0;
		m_recent = FindCaptures();
		m_recentNames.clear();
		for (const std::filesystem::path &p : m_recent)
			m_recentNames.push_back(p.filename().string() + (CompanionsOf(p).incident.empty() ? "" : "  (!)"));
		if (m_recentBox) {
			m_recentBox->options = RecentOptions();
			SyncRecentIndex();
		}
	}
	void SyncRecentIndex() {
		m_recentIndex = 0;
		for (size_t i = 0; i < m_recent.size(); ++i)
			if (m_recent[i].string() == m_state.path)
				m_recentIndex = int(i);
		if (m_recentBox)
			m_recentBox.SetSelectedIndex(m_recentIndex);
	}

	std::string m_path;
	ViewerState m_state;
	std::unique_ptr<ui::Ui> m_ui;
	ui::Widget m_table, m_status, m_matches, m_recentBox;
	std::vector<std::filesystem::path> m_recent; ///< captures trouvées (la plus récente d'abord)
	std::vector<std::string> m_recentNames;
	int m_recentIndex = 0, m_tab = 0;
	float m_budget = 16.67f;
	double m_refresh = 0.0;
	std::mutex m_dialogMutex;
	std::optional<std::string> m_pendingOpen; ///< chemin choisi dans la boîte de dialogue
};

int RunViewer(app::AppConfig config, const std::string &path) {
	if (config.title == app::AppConfig{}.title)
		config.title = "Profiler";
	config.width = std::max(config.width, 1500);
	config.height = std::max(config.height, 920);
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(Icon::SPEED);
	return Viewer(path).Run(config);
}

} // namespace profiler
