#pragma once
/**
 * @file canvas.hpp
 * @brief Dessin 2D par lots sur `sdl3::Renderer` (rectangles, arrondis, cercles, lignes,
 *        texte, icônes, images).
 *
 * - Polices : jusqu'à quatre familles (normale, grasse, chasse fixe, icônes Material),
 *   rastérisées par `sdl3::Font` (SDL_ttf) dans un atlas par (famille, taille en pixels),
 *   construit à la demande.
 * - Un texel blanc réservé au même endroit dans chaque atlas : formes et texte d'une même
 *   page partagent la texture → un seul `RenderGeometry` (nouveau lot seulement au
 *   changement de page, d'image ou de découpe).
 * - Coordonnées logiques multipliées par l'échelle (HiDPI × échelle d'interface) ; le
 *   texte est rastérisé à la taille physique (net, sans mise à l'échelle).
 */
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>
#include <sdl3/render/render.hpp>
#include <sdl3/render/ttf.hpp>

namespace sdl3 {

/// Le rectangle des coordonnées logiques du canvas est `sdl3::FRect` (voir structs.hpp) :
/// ses méthodes `Inset`, `Right`, `Bottom`, `Intersect` y ont été ajoutées.

[[nodiscard]] constexpr Color Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept { return Color{r, g, b, a}; }
[[nodiscard]] constexpr Color WithAlpha(Color c, uint8_t a) noexcept { return Color{c.r, c.g, c.b, a}; }
[[nodiscard]] Color MixColor(Color a, Color b, float t) noexcept;

enum class CanvasFont : uint8_t { Regular = 0, Bold = 1, Mono = 2, Icons = 3 };

/// Fichiers TTF/OTF des familles ; un chemin vide ou introuvable retombe sur `regular`
/// (ou sur une police système).
struct CanvasFontSet {
	std::string regular, bold, mono, icons;
};

class Canvas {
public:
	Canvas();
	~Canvas();
	Canvas(const Canvas &) = delete;
	Canvas &operator=(const Canvas &) = delete;

	bool Init(sdl3::Renderer &renderer, const CanvasFontSet &fonts, float scale = 1.f);
	void SetScale(float scale);
	[[nodiscard]] float Scale() const noexcept { return m_scale; }

	void Begin();
	void End();
	[[nodiscard]] float Width() const noexcept { return m_width; }
	[[nodiscard]] float Height() const noexcept { return m_height; }

	void FillRect(const FRect &r, Color c);
	void FillGradientV(const FRect &r, Color top, Color bottom);
	void FillGradientH(const FRect &r, Color left, Color right);
	void StrokeRect(const FRect &r, Color c, float thickness = 1.f);
	void FillRoundRect(const FRect &r, float radius, Color c);
	void StrokeRoundRect(const FRect &r, float radius, Color c, float thickness = 1.f);
	void FillCircle(float cx, float cy, float radius, Color c);
	/// Triangle plein (primitive pour diagrammes circulaires, aires…).
	void FillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
	void Line(float x0, float y0, float x1, float y1, Color c, float thickness = 1.f);
	void Polyline(const float *xy, size_t points, Color c, float thickness = 1.f);

	/// Texte UTF-8 ; `y` = haut de la ligne. Retourne la largeur logique.
	float Text(float x, float y, std::string_view utf8, Color c, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Texte centré dans `r` ; rogné à `r` s'il est plus large (ne déborde jamais de sa boîte).
	float TextCentered(const FRect &r, std::string_view utf8, Color c, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Icône Material (point de code) centrée dans `r`. Faux si la police ne la contient pas.
	bool Icon(const FRect &r, char32_t codepoint, Color c, float size = 20.f);
	[[nodiscard]] float MeasureText(std::string_view utf8, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	[[nodiscard]] float LineHeight(float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Coupe `utf8` avec « … » pour tenir dans `maxWidth`.
	[[nodiscard]] std::string Ellipsize(std::string_view utf8, float maxWidth, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	void Image(sdl3::Texture &tex, const FRect &dst, Color tint = Color{255, 255, 255, 255});
	/// Sous-région `src` (en texels) d'une texture de `texW`×`texH`, miroir horizontal facultatif.
	/// Les appels successifs sur la même texture partagent un lot.
	void ImageRegion(sdl3::Texture &tex, float texW, float texH, const FRect &src, const FRect &dst, bool flipH = false,
					 Color tint = Color{255, 255, 255, 255});

	/// Quadrilatère texturé quelconque : 4 sommets écran (`xy`, coordonnées logiques)
	/// et 4 coordonnées de texture normalisées (`uv`), dans l'ordre du contour.
	/// Sert au rendu « 3D » projeté sur le CPU (aperçu de skin).
	void TexturedQuad(sdl3::Texture &tex, const float xy[8], const float uv[8], Color tint = Color{255, 255, 255, 255});

	/// Dessins suivants dans la texture cible `target` (créée avec l'accès TARGET, à la taille
	/// de la fenêtre en pixels : mêmes coordonnées), nullptr pour revenir à la fenêtre. Le lot
	/// courant est vidé.
	void SetTarget(sdl3::Texture *target);
	/// Shader de fragments personnalisé des dessins suivants (`SDL_GPURenderState`, renderer
	/// « gpu » seulement), nullptr pour les shaders du renderer. Le lot courant est vidé.
	void SetRenderState(SDL_GPURenderState *state);

	/// Restreint le dessin à `r`, intersecté avec le clip courant (`intersect`), ou à `r`
	/// seul (surfaces flottantes qui ne doivent pas hériter du clip de leur parent).
	void PushClip(const FRect &r, bool intersect = true);
	void PopClip();

	[[nodiscard]] sdl3::Renderer &Renderer() noexcept { return *m_renderer; }
	[[nodiscard]] size_t DrawCalls() const noexcept { return m_drawCalls; }

	/// Points de code rastérisés dans les pages d'icônes (ajouter avant le premier usage).
	static void RegisterIcons(std::initializer_list<char32_t> codepoints);

private:
	struct Glyph {
		float u0, v0, u1, v1;
		float w, h, advance; // pixels physiques
	};
	struct CanvasFontPage {
		CanvasFont font = CanvasFont::Regular;
		int pixelSize = 0;
		float lineHeight = 0;
		std::optional<sdl3::Texture> atlas;
		Glyph latin[256]{};
		core::RobinMap<uint32_t, Glyph> extra;
	};

	CanvasFontPage *Page(CanvasFont font, float logicalSize);
	bool BuildPage(CanvasFontPage &page);
	const Glyph *FindGlyph(CanvasFontPage &page, uint32_t cp) const;
	void UseTexture(sdl3::Texture *tex);
	void EnsureSolidTexture();
	void Flush();
	void Vertex(float x, float y, Color c);
	void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);
	void QuadColors(float x0, float y0, float x1, float y1, Color c00, Color c10, Color c11, Color c01);
	void Triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);

	sdl3::Renderer *m_renderer = nullptr;
	std::optional<sdl3::TtfContext> m_ttf;
	std::string m_paths[4];
	std::map<int, std::unique_ptr<CanvasFontPage>> m_pages; // clé = famille * 1000 + taille
	sdl3::Texture *m_current = nullptr;
	bool m_atlasCurrent = false; // la texture courante est un atlas (texel blanc disponible)
	std::vector<sdl3::Vertex> m_vertices;
	std::vector<int> m_indices;
	std::vector<FRect> m_clips;
	float m_scale = 1.f;
	float m_width = 0, m_height = 0;
	size_t m_drawCalls = 0;
	static constexpr float kWhiteUV = 2.f / 1024.f;
};

} // namespace sdl3
