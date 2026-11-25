// Définitions de sdl3/render/svg.hpp — analyse SVG via dataio (XML).
#include "sdl3/render/svg.hpp"

#include <dataio/documents/xml.hpp>

#include "sdl3/render/image.hpp"

namespace sdl3 {

namespace {

/// Nombre flottant en tête de chaîne (ignore les unités : « 100px », « 50% »…).
[[nodiscard]] float ParseNumber(StringView s) noexcept {
	size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
		++i;
	int sign = 1;
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
		if (s[i] == '-')
			sign = -1;
		++i;
	}
	float v = 0.f;
	bool any = false;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		v = v * 10.f + float(s[i] - '0');
		any = true;
		++i;
	}
	if (i < s.size() && s[i] == '.') {
		++i;
		float f = 0.1f;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
			v += float(s[i] - '0') * f;
			f *= 0.1f;
			any = true;
			++i;
		}
	}
	return any ? float(sign) * v : 0.f;
}

/// Formes SVG reconnues pour le comptage / la validation.
constexpr std::string_view kShapes[] = {"rect", "circle", "ellipse", "line", "polyline",
										"polygon", "path", "g", "text", "image", "use"};

[[nodiscard]] size_t CountShapes(const dataio::NodePtr &node) noexcept {
	size_t n = 0;
	for (std::string_view name : kShapes) {
		const dataio::NodePtr child = node->Get(String(name));
		if (!child)
			continue;
		if (child->IsArray()) {
			for (size_t i = 0; i < child->GetSize(); ++i)
				n += 1 + CountShapes(child->At(i));
		} else {
			n += 1 + CountShapes(child);
		}
	}
	return n;
}

} // namespace

Result<Svg, String> Svg::Parse(StringView xml) {
	dataio::XmlDocument doc;
	String content(xml);
	if (auto err = doc.DecodeStr(content); err.IsSome())
		return Err(err->message);
	const dataio::NodePtr root = doc.GetRoot();
	if (!root)
		return Err(String("document SVG vide"));
	const dataio::NodePtr svg = root->Get("svg");
	if (!svg)
		return Err(String("élément <svg> introuvable"));

	Svg out;
	out.m_xml = std::move(content);
	if (const dataio::NodePtr attrs = svg->Get("@attributes"); attrs) {
		if (const dataio::NodePtr w = attrs->Get("width"); w)
			out.m_width = ParseNumber(w->AsString());
		if (const dataio::NodePtr h = attrs->Get("height"); h)
			out.m_height = ParseNumber(h->AsString());
		if (const dataio::NodePtr vb = attrs->Get("viewBox"); vb) {
			// « min-x min-y width height » (séparés par espaces ou virgules).
			StringView s = vb->AsString();
			float v[4]{};
			size_t n = 0;
			for (size_t i = 0; i < s.size() && n < 4;) {
				while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == '\t'))
					++i;
				const size_t start = i;
				while (i < s.size() && s[i] != ' ' && s[i] != ',' && s[i] != '\t')
					++i;
				if (i > start)
					v[n++] = ParseNumber(s.substr(start, i - start));
			}
			if (n == 4) {
				out.m_viewBox = {v[0], v[1], v[2], v[3]};
				out.m_hasViewBox = true;
			}
		}
	}
	out.m_shapes = CountShapes(svg);
	return Ok(std::move(out));
}

Result<Svg, String> Svg::Load(const String &path) {
	const auto content = String::ReadFile(path);
	if (content.IsNone())
		return Err(String::Format("fichier introuvable : {}", path));
	return Parse(content.Value().View());
}

Result<Surface, StringView> Svg::Rasterize(int width, int height) const {
	return ImgLoadSizedSvgMemory(m_xml.data(), m_xml.size(), width, height);
}

Result<Surface, StringView> Svg::Rasterize() const {
	int w = int(m_width), h = int(m_height);
	if (w <= 0 || h <= 0) {
		if (m_hasViewBox) {
			w = int(m_viewBox.w);
			h = int(m_viewBox.h);
		} else {
			w = h = 256;
		}
	}
	return Rasterize(std::max(1, w), std::max(1, h));
}

} // namespace sdl3
