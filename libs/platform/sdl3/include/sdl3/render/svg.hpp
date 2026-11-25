#pragma once
/**
 * @file svg.hpp
 * @brief Analyse et rastérisation de fichiers SVG.
 *
 * Le document SVG (XML) est analysé avec le module `dataio` (`XmlDocument`) :
 * on lit la taille naturelle, la `viewBox` et le nombre de formes, sans écrire
 * de parseur XML dédié au wrapper. La rastérisation en `Surface` RGBA est
 * déléguée à SDL3_image (nanosvg), qui gère l'intégralité du langage SVG.
 */
#include <sdl3/render/render.hpp>

namespace sdl3 {

class Svg {
public:
	/// Analyse un document SVG (XML) depuis une chaîne.
	[[nodiscard]] static Result<Svg, String> Parse(StringView xml);
	/// Analyse un fichier `.svg` (lu sur disque puis décodé via dataio).
	[[nodiscard]] static Result<Svg, String> Load(const String &path);

	/// Taille naturelle (attributs `width`/`height`, sinon `viewBox`, sinon 0).
	[[nodiscard]] float Width() const noexcept { return m_width; }
	[[nodiscard]] float Height() const noexcept { return m_height; }
	[[nodiscard]] bool HasViewBox() const noexcept { return m_hasViewBox; }
	[[nodiscard]] FRect ViewBox() const noexcept { return m_viewBox; }
	/// Nombre de formes (<rect>, <circle>, <path>, <g>…) du document.
	[[nodiscard]] size_t ShapeCount() const noexcept { return m_shapes; }
	/// Source XML brute (conservée pour la rastérisation).
	[[nodiscard]] const String &Xml() const noexcept { return m_xml; }

	/// Rastérise en Surface ARGB à la taille demandée (SDL3_image).
	[[nodiscard]] Result<Surface, StringView> Rasterize(int width, int height) const;
	/// Rastérise à la taille naturelle (attributs, sinon viewBox, sinon 256×256).
	[[nodiscard]] Result<Surface, StringView> Rasterize() const;

private:
	String m_xml;
	float m_width = 0.f, m_height = 0.f;
	FRect m_viewBox{};
	bool m_hasViewBox = false;
	size_t m_shapes = 0;
};

} // namespace sdl3
