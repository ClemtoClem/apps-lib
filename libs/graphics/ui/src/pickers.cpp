// Sélecteurs de l'interface immédiate : couleur (`Context::ColorPicker`) et date
// (`Context::DatePicker`). Utilisables aussi dans l'interface retenue via `ui::Custom`.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

#include "ui/i18n.hpp"
#include "ui/ui.hpp"

namespace ui {

namespace {

struct Hsv {
	float h = 0.f, s = 0.f, v = 0.f; ///< teinte, saturation, valeur dans [0, 1]
};

Hsv ToHsv(Color c) {
	const float r = float(c.r) / 255.f, g = float(c.g) / 255.f, b = float(c.b) / 255.f;
	const float max = std::max({r, g, b}), min = std::min({r, g, b}), d = max - min;
	Hsv out{0.f, max > 0.f ? d / max : 0.f, max};
	if (d > 0.f) {
		if (max == r)
			out.h = std::fmod((g - b) / d + 6.f, 6.f) / 6.f;
		else if (max == g)
			out.h = ((b - r) / d + 2.f) / 6.f;
		else
			out.h = ((r - g) / d + 4.f) / 6.f;
	}
	return out;
}

Color FromHsv(Hsv hsv, uint8_t alpha) {
	const float h = std::fmod(hsv.h, 1.f) * 6.f;
	const float c = hsv.v * hsv.s, x = c * (1.f - std::abs(std::fmod(h, 2.f) - 1.f)), m = hsv.v - c;
	float r = 0, g = 0, b = 0;
	switch (int(h)) {
	case 0: r = c, g = x; break;
	case 1: r = x, g = c; break;
	case 2: g = c, b = x; break;
	case 3: g = x, b = c; break;
	case 4: r = x, b = c; break;
	default: r = c, b = x; break;
	}
	const auto byte = [m](float v) { return uint8_t(std::lround((v + m) * 255.f)); };
	return sdl3::Rgba(byte(r), byte(g), byte(b), alpha);
}

/// Texte du traducteur global pour `key`, sinon `fallback` (français, langue des sources).
std::string Local(std::string_view key, std::string_view fallback) {
	const std::string *found = I18n().Find(key);
	return found ? *found : std::string(fallback);
}

constexpr std::array<std::string_view, 12> kMonths = {"Janvier", "Février", "Mars",		"Avril",   "Mai",	   "Juin",
													  "Juillet", "Août",	"Septembre", "Octobre", "Novembre", "Décembre"};

} // namespace

bool Context::ColorPicker(std::string_view idText, Color &color, float height) {
	sdl3::Canvas &c = *m_canvas;
	const Rect area = Next(height);
	const Id id = MakeId(idText);
	const float side = std::min(area.h, area.w * 0.55f);
	const Rect sv{area.x, area.y, side, side};
	const Rect hueBar{sv.Right() + 10.f, area.y, 16.f, side};
	const Rect info{hueBar.Right() + 14.f, area.y, std::max(0.f, area.Right() - hueBar.Right() - 14.f), side};

	Hsv hsv = ToHsv(color);
	float &hue = *m_pickerHue.TryEmplace(id, hsv.h).first;
	if (hsv.s > 0.002f && hsv.v > 0.002f)
		hue = hsv.h; // teinte significative : mémorisée ; sinon on garde la précédente
	hsv.h = hue;

	// Interactions : glisser dans le carré (saturation, valeur) ou la barre (teinte).
	const Interaction svIt = Interact(MakeId(std::string(idText) + "#sv"), sv);
	const Interaction hueIt = Interact(MakeId(std::string(idText) + "#hue"), hueBar);
	if (svIt.held) {
		hsv.s = std::clamp((MouseX() - sv.x) / sv.w, 0.f, 1.f);
		hsv.v = 1.f - std::clamp((MouseY() - sv.y) / sv.h, 0.f, 1.f);
	}
	if (hueIt.held)
		hue = hsv.h = std::clamp((MouseY() - hueBar.y) / hueBar.h, 0.f, 0.9999f);
	const Color before = color;
	if (svIt.held || hueIt.held)
		color = FromHsv(hsv, color.a);

	// Carré : teinte pure, blanc → transparent (saturation), transparent → noir (valeur).
	c.FillRect(sv, FromHsv({hsv.h, 1.f, 1.f}, 255));
	c.FillGradientH(sv, sdl3::Rgba(255, 255, 255, 255), sdl3::Rgba(255, 255, 255, 0));
	c.FillGradientV(sv, sdl3::Rgba(0, 0, 0, 0), sdl3::Rgba(0, 0, 0, 255));
	c.StrokeRect(sv, m_theme.panelBorder);
	const float mx = sv.x + hsv.s * sv.w, my = sv.y + (1.f - hsv.v) * sv.h;
	c.FillCircle(mx, my, 7.f, sdl3::Rgba(0, 0, 0, 120));
	c.FillCircle(mx, my, 5.5f, sdl3::Rgba(255, 255, 255));
	c.FillCircle(mx, my, 4.f, sdl3::WithAlpha(color, 255));

	// Barre de teinte : six dégradés entre les couleurs primaires et secondaires.
	for (int i = 0; i < 6; ++i) {
		const float y0 = hueBar.y + hueBar.h * float(i) / 6.f, y1 = hueBar.y + hueBar.h * float(i + 1) / 6.f;
		c.FillGradientV({hueBar.x, y0, hueBar.w, y1 - y0}, FromHsv({float(i) / 6.f, 1, 1}, 255), FromHsv({float(i + 1) / 6.f, 1, 1}, 255));
	}
	c.StrokeRect(hueBar, m_theme.panelBorder);
	const float hy = hueBar.y + hue * hueBar.h;
	c.FillRoundRect({hueBar.x - 3.f, hy - 3.f, hueBar.w + 6.f, 6.f}, 3.f, sdl3::Rgba(255, 255, 255));
	c.StrokeRoundRect({hueBar.x - 3.f, hy - 3.f, hueBar.w + 6.f, 6.f}, 3.f, sdl3::Rgba(0, 0, 0, 140));

	// Aperçu et valeurs.
	if (info.w > 40.f) {
		const Rect swatch{info.x, info.y, info.w, std::min(56.f, info.h * 0.45f)};
		c.FillRoundRect(swatch, m_theme.radius, sdl3::WithAlpha(color, 255));
		c.StrokeRoundRect(swatch, m_theme.radius, m_theme.panelBorder);
		float y = swatch.Bottom() + 10.f;
		const std::string hex = std::format("#{:02X}{:02X}{:02X}", color.r, color.g, color.b);
		c.Text(info.x, y, hex, m_theme.text, m_theme.fontSize, sdl3::CanvasFont::Mono);
		y += c.LineHeight(m_theme.fontSize) + 4.f;
		c.Text(info.x, y,
			   std::format("{} {}  {} {}  {} {}", Local("ui.color.r", "R"), color.r, Local("ui.color.g", "V"), color.g, Local("ui.color.b", "B"),
						   color.b),
			   m_theme.textDim, m_theme.fontSize - 2.f);
		y += c.LineHeight(m_theme.fontSize - 2.f) + 2.f;
		c.Text(info.x, y,
			   std::format("{} {:.0f}°  {} {:.0f} %  {} {:.0f} %", Local("ui.color.h", "T"), hue * 360.f, Local("ui.color.s", "S"), hsv.s * 100.f,
						   Local("ui.color.v", "V"), hsv.v * 100.f),
			   m_theme.textDim, m_theme.fontSize - 2.f);
	}
	return color != before;
}

bool Context::DatePicker(std::string_view idText, Date &date) {
	using namespace std::chrono;
	sdl3::Canvas &c = *m_canvas;
	const Id id = MakeId(idText);
	int &shown = *m_calendarMonth.TryEmplace(id, date.year * 12 + (date.month - 1)).first;
	const int year = shown / 12, month = shown % 12 + 1;

	// En-tête : mois précédent, « Mois Année », mois suivant.
	const Rect head = Next(30.f);
	if (IconButton({head.x, head.y, 30.f, head.h}, icons::Codepoint(icons::MaterialIcons::CHEVRON_LEFT), Local("ui.calendar.previous", "Mois précédent")))
		--shown;
	if (IconButton({head.Right() - 30.f, head.y, 30.f, head.h}, icons::Codepoint(icons::MaterialIcons::CHEVRON_RIGHT), Local("ui.calendar.next", "Mois suivant")))
		++shown;
	const std::string monthName = Local(std::format("ui.month.{}", month), kMonths[size_t(month - 1)]);
	c.TextCentered(head, std::format("{} {}", monthName, year), m_theme.text, m_theme.fontSize, sdl3::CanvasFont::Bold);

	// Jours de la semaine, puis grille de six semaines.
	const Rect names = Next(22.f);
	const float cellW = names.w / 7.f;
	static constexpr std::array<std::string_view, 7> kDays = {"Lu", "Ma", "Me", "Je", "Ve", "Sa", "Di"};
	for (size_t i = 0; i < 7; ++i)
		c.TextCentered({names.x + float(i) * cellW, names.y, cellW, names.h}, Local(std::format("ui.weekday.{}", i), kDays[i]), m_theme.textDim,
					   m_theme.fontSize - 2.f);

	const year_month ym{std::chrono::year{year}, std::chrono::month{unsigned(month)}};
	const int lead = int(weekday{sys_days{ym / 1}}.iso_encoding()) - 1; // lundi = 0
	const int days = int(unsigned((ym / last).day()));
	const year_month_day today{floor<std::chrono::days>(system_clock::now())};
	const bool currentMonth = today.year() == ym.year() && today.month() == ym.month();
	const bool selectedMonth = date.year == year && date.month == month;

	bool changed = false;
	const float cellH = 28.f;
	for (int week = 0; week < 6; ++week) {
		const Rect row = Next(cellH);
		for (int dow = 0; dow < 7; ++dow) {
			const int day = week * 7 + dow - lead + 1;
			if (day < 1 || day > days)
				continue;
			const Rect cell = Rect{row.x + float(dow) * cellW, row.y, cellW, row.h}.Inset(2.f);
			PushId(uint64_t(shown * 64 + day));
			const Interaction it = Interact(MakeId("day"), cell);
			PopId();
			const bool selected = selectedMonth && date.day == day;
			const bool isToday = currentMonth && unsigned(today.day()) == unsigned(day);
			if (selected)
				c.FillRoundRect(cell, m_theme.radius, m_theme.accent);
			else if (it.hovered)
				c.FillRoundRect(cell, m_theme.radius, m_theme.widgetHover);
			if (isToday && !selected)
				c.StrokeRoundRect(cell, m_theme.radius, m_theme.accent, 1.5f);
			const Color text = selected ? m_theme.accentText : (dow >= 5 ? m_theme.textDim : m_theme.text);
			c.TextCentered(cell, std::format("{}", day), text, m_theme.fontSize - 1.f, selected ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular);
			if (it.leftClicked && !selected) {
				date = Date{year, month, day};
				changed = true;
			}
		}
	}
	return changed;
}

} // namespace ui
