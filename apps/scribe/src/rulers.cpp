#include "scribe/rulers.hpp"

#include <cmath>
#include <format>

namespace scribe {

std::vector<RulerMark> RulerMarks(double length, double scale, double minPixels) {
	std::vector<RulerMark> out;
	const double mm = kPointsPerCm / 10;
	const bool showMm = mm * scale >= minPixels;
	const bool showHalf = mm * 5 * scale >= minPixels;
	const auto count = int(std::floor(length / mm + 1e-9));
	for (int i = 0; i <= count; ++i) {
		const int level = i % 10 == 0 ? 2 : i % 5 == 0 ? 1 : 0;
		if ((level == 0 && !showMm) || (level == 1 && !showHalf))
			continue;
		out.push_back({i * mm, level, i / 10});
	}
	return out;
}

double SnapLength(double points, bool fine) {
	const double step = kPointsPerCm / (fine ? 100 : 20); // 0,1 mm ou 0,5 mm
	return std::max(0.0, std::round(points / step) * step);
}

std::string FormatLength(double points) {
	std::string s = std::format("{:.2f} cm", points / kPointsPerCm);
	for (char &c : s)
		if (c == '.')
			c = ',';
	return s;
}

namespace {

const sdl3::Color kBack(232, 234, 239, 255), kMargin(196, 200, 210, 255), kTick(90, 96, 110, 255), kHandle(60, 110, 200, 255),
	kActive(230, 120, 0, 255), kTable(120, 126, 140, 255), kSpace(46, 150, 90, 255);

} // namespace

void DrawRuler(sdl3::Canvas &canvas, const sdl3::FRect &area, bool horizontal, float origin, float scale, double length, double marginStart,
			   double marginEnd, const std::vector<RulerHandle> &handles, int active) {
	canvas.FillRect(area, sdl3::Color(48, 53, 66, 255));
	const float page0 = origin, page1 = origin + float(length) * scale;
	const auto along = [&](float a, float b) { // segment de l'axe → rectangle de la règle
		return horizontal ? sdl3::FRect{a, area.y + 2, b - a, area.h - 4} : sdl3::FRect{area.x + 2, a, area.w - 4, b - a};
	};
	canvas.PushClip(area);
	canvas.FillRect(along(page0, page1), kBack);
	canvas.FillRect(along(page0, origin + float(marginStart) * scale), kMargin);
	canvas.FillRect(along(page1 - float(marginEnd) * scale, page1), kMargin);
	for (const RulerMark &m : RulerMarks(length, scale)) {
		const float p = origin + float(m.position) * scale;
		const float size = m.level == 2 ? 0.45f : m.level == 1 ? 0.32f : 0.18f;
		const float extent = (horizontal ? area.h : area.w) * size;
		if (horizontal)
			canvas.Line(p, area.y + area.h - 2 - extent, p, area.y + area.h - 2, kTick, 1.f);
		else
			canvas.Line(area.x + area.w - 2 - extent, p, area.x + area.w - 2, p, kTick, 1.f);
		if (m.level == 2 && m.centimeter > 0 && kPointsPerCm * double(scale) >= 14) {
			const std::string label = std::to_string(m.centimeter);
			if (horizontal)
				canvas.Text(p + 2, area.y + 1, label, kTick, 10);
			else
				canvas.Text(area.x + 2, p + 1, label, kTick, 10);
		}
	}
	for (size_t i = 0; i < handles.size(); ++i) {
		const RulerHandle &h = handles[i];
		const float p = origin + float(h.position) * scale;
		const bool space = h.kind == RulerHandle::SpaceBefore || h.kind == RulerHandle::SpaceAfter;
		const sdl3::Color color = int(i) == active ? kActive : h.kind == RulerHandle::TableBorder ? kTable : space ? kSpace : kHandle;
		// Repère : triangle pointant vers la page (marges), barre (bords de tableau), petit
		// rectangle à gauche de la règle (espaces avant / après du paragraphe).
		if (space) {
			if (horizontal)
				canvas.FillRect({p - 1.5f, area.y + 2, 3, area.h * 0.5f}, color);
			else
				canvas.FillRect({area.x + 2, p - 1.5f, area.w * 0.5f, 3}, color);
			continue;
		}
		if (h.kind == RulerHandle::TableBorder) {
			if (horizontal)
				canvas.FillRect({p - 2, area.y + 3, 4, area.h - 6}, color);
			else
				canvas.FillRect({area.x + 3, p - 2, area.w - 6, 4}, color);
			continue;
		}
		if (horizontal)
			canvas.FillTriangle(p - 6, area.y + area.h - 10, p + 6, area.y + area.h - 10, p, area.y + area.h - 2, color);
		else
			canvas.FillTriangle(area.x + area.w - 10, p - 6, area.x + area.w - 10, p + 6, area.x + area.w - 2, p, color);
	}
	canvas.PopClip();
}

double DraggedSpaceBefore(double top, double current, double position, bool fine) { return SnapLength(current + (position - top), fine); }

double DraggedSpaceAfter(double bottom, double position, bool fine) { return SnapLength(position - bottom, fine); }

int HandleAt(const std::vector<RulerHandle> &handles, float origin, float scale, float pixel, float tolerance) {
	int best = -1;
	float bestDistance = tolerance;
	for (size_t i = 0; i < handles.size(); ++i) {
		const float d = std::abs(origin + float(handles[i].position) * scale - pixel);
		if (d <= bestDistance) {
			bestDistance = d;
			best = int(i);
		}
	}
	return best;
}

} // namespace scribe
