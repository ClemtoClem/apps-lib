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
#include <set>
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
	/// Polygone simple plein (convexe ou concave : étoile, flèche…), sommets (x, y) à la suite.
	void FillPolygon(const float *xy, size_t points, Color c);
	/// Contour fermé d'un polygone.
	void StrokePolygon(const float *xy, size_t points, Color c, float thickness = 1.f);
	/// Triangulation d'un polygone simple par découpe d'oreilles : indices de sommets, trois par
	/// triangle (n − 2 triangles ; sens des sommets quelconque ; polygone dégénéré : en éventail).
	[[nodiscard]] static std::vector<uint32_t> TriangulatePolygon(const float *xy, size_t points);

	/// Texte UTF-8 ; `y` = haut de la ligne. Retourne la largeur logique.
	float Text(float x, float y, std::string_view utf8, Color c, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Texte centré dans `r` ; rogné à `r` s'il est plus large (ne déborde jamais de sa boîte).
	float TextCentered(const FRect &r, std::string_view utf8, Color c, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Icône Material (point de code) centrée dans `r`. Faux si la police ne la contient pas.
	bool Icon(const FRect &r, char32_t codepoint, Color c, float size = 20.f);
	[[nodiscard]] float MeasureText(std::string_view utf8, float size = 16.f, CanvasFont font = CanvasFont::Regular);
	/// Police supplémentaire (fichier TTF / OTF quelconque, ex. police choisie dans un document) :
	/// rend son identifiant, utilisable comme les familles prédéfinies (`Text`, `MeasureText`…).
	/// Un même fichier rend toujours le même identifiant ; fichier illisible : `CanvasFont::Regular`.
	/// Au plus 250 polices supplémentaires.
	CanvasFont AddFont(const std::string &path);
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

	/// Les dessins suivants (formes, traits, texte, images) tournent de `degrees` (sens horaire,
	/// axe y vers le bas) autour de (`cx`, `cy`), coordonnées logiques ; les rotations s'empilent.
	/// Le clip reste un rectangle aligné sur l'écran.
	void PushRotation(float degrees, float cx, float cy);
	void PopRotation();
	/// Point logique transformé par les rotations empilées.
	[[nodiscard]] FPoint Transform(float x, float y) const noexcept;

	[[nodiscard]] sdl3::Renderer &Renderer() noexcept { return *m_renderer; }
	[[nodiscard]] size_t DrawCalls() const noexcept { return m_drawCalls; }

	/// Points de code rastérisés dans les pages d'icônes. Facultatif : une icône inconnue
	/// demandée à `Icon` est enregistrée d'elle-même et apparaît dès l'image suivante
	/// (pages d'icônes reconstruites au `Begin`) ; enregistrer d'avance évite cette image vide.
	static void RegisterIcons(std::initializer_list<char32_t> codepoints);

	/// Police de repli (fichier TTF / OTF / TTC) pour les caractères absents de la police du texte
	/// (chinois, japonais…), essayée dans l'ordre d'ajout. Un caractère inconnu des pages est
	/// retenu au premier dessin et rastérisé à l'image suivante (pages de texte refaites au
	/// `Begin`) : il apparaît « ? » une image. Au plus `kMaxDynamicGlyphs` caractères retenus.
	void AddFallbackFont(const std::string &path);
	[[nodiscard]] const std::vector<std::string> &FallbackFonts() const noexcept { return m_fallbacks; }
	static constexpr size_t kMaxDynamicGlyphs = 6000;

private:
	struct Glyph {
		float u0, v0, u1, v1;
		float w, h, advance; // pixels physiques
		float dy;			 // décalage vertical (police de repli : alignement des lignes de base)
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
	[[nodiscard]] FPoint Physical(float x, float y) const noexcept; ///< transformé puis mis à l'échelle
	void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);
	void QuadColors(float x0, float y0, float x1, float y1, Color c00, Color c10, Color c11, Color c01);
	void Triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c);

	sdl3::Renderer *m_renderer = nullptr;
	std::optional<sdl3::TtfContext> m_ttf;
	std::vector<std::string> m_paths = std::vector<std::string>(4); ///< index = CanvasFont
	std::map<int, std::unique_ptr<CanvasFontPage>> m_pages; // clé = famille * 1000 + taille
	sdl3::Texture *m_current = nullptr;
	bool m_atlasCurrent = false; // la texture courante est un atlas (texel blanc disponible)
	bool m_iconsDirty = false;	 // une icône nouvelle a été enregistrée : pages d'icônes à refaire
	std::vector<std::string> m_fallbacks;		 // polices de repli
	mutable std::set<uint32_t> m_dynamic;		 // caractères rencontrés hors des pages (rastérisés à la demande)
	mutable bool m_glyphsDirty = false;			 // caractère nouveau : pages de texte à refaire
	std::vector<sdl3::Vertex> m_vertices;
	std::vector<int> m_indices;
	std::vector<FRect> m_clips;
	struct Affine {
		float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0; ///< x' = a·x + c·y + tx ; y' = b·x + d·y + ty
	};
	Affine m_transform;
	std::vector<Affine> m_transforms; ///< pile des transformations précédentes
	float m_scale = 1.f;
	float m_width = 0, m_height = 0;
	size_t m_drawCalls = 0;
	static constexpr float kWhiteUV = 2.f / 1024.f;
};

} // namespace sdl3
