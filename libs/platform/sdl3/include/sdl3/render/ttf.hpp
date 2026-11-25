#pragma once
/**
 * @file ttf.hpp
 * @brief Rendu de texte TrueType (SDL3_ttf) : polices, métriques, surfaces et textures.
 */
#include <SDL3_ttf/SDL_ttf.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <core/core.hpp>
#include "sdl3/io/iostream.hpp"
#include "sdl3/render/render.hpp"

namespace sdl3 {

// ============================================================================
// TtfContext — RAII TTF_Init / TTF_Quit
// ============================================================================

/// Contexte SDL3_ttf (RAII sur TTF_Init / TTF_Quit).
class TtfContext {
	bool m_owns = false;

public:
	TtfContext() = default;
	/// Quitte SDL3_ttf.
	~TtfContext();

	TtfContext(const TtfContext &) = delete;
	TtfContext &operator=(const TtfContext &) = delete;
	TtfContext(TtfContext &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
	TtfContext &operator=(TtfContext &&o) noexcept {
		if (this != &o) {
			if (m_owns)
				TTF_Quit();
			m_owns = o.m_owns;
			o.m_owns = false;
		}
		return *this;
	}

	/// Vrai si SDL3_ttf est initialisé.
	[[nodiscard]] explicit operator bool() const noexcept { return m_owns; }

	/// Initialise SDL3_ttf (avec message d'erreur en cas d'échec).
	[[nodiscard]] static Result<TtfContext, Error> Create();
};

// ============================================================================
// FontStyle flags
// ============================================================================

/// Style d'une police (drapeaux combinables).
enum class FontStyle : int {
	NORMAL = TTF_STYLE_NORMAL,			 ///< normal
	BOLD = TTF_STYLE_BOLD,				 ///< gras
	ITALIC = TTF_STYLE_ITALIC,			 ///< italique
	UNDERLINE = TTF_STYLE_UNDERLINE,	 ///< souligné
	STRIKETHROUGH = TTF_STYLE_STRIKETHROUGH, ///< barré
};

/// Combine deux styles de police.
inline FontStyle operator|(FontStyle a, FontStyle b) { return static_cast<FontStyle>(int(a) | int(b)); }

// ============================================================================
// Version
// ============================================================================

/// SDL3_ttf version number compiled into the library (TTF_Version).
[[nodiscard]] inline int TtfVersion() { return TTF_Version(); }

/// FreeType version linked into SDL3_ttf, as {major, minor, patch}.
[[nodiscard]] inline std::array<int, 3> TtfFreeTypeVersion() {
	std::array<int, 3> v{};
	TTF_GetFreeTypeVersion(&v[0], &v[1], &v[2]);
	return v;
}

/// HarfBuzz version linked into SDL3_ttf, as {major, minor, patch}.
[[nodiscard]] inline std::array<int, 3> TtfHarfBuzzVersion() {
	std::array<int, 3> v{};
	TTF_GetHarfBuzzVersion(&v[0], &v[1], &v[2]);
	return v;
}

/// Metrics of a single glyph (TTF_GetGlyphMetrics).
struct GlyphMetrics {
	int minX = 0;
	int maxX = 0;
	int minY = 0;
	int maxY = 0;
	int advance = 0;
};

// ============================================================================
// Font search — cross-platform
// ============================================================================

/// Nom de connexion de l'utilisateur système (vide si indisponible).
[[nodiscard]] String GetSystemUserName() noexcept;

namespace detail {

// Lowercase + strip separators (-/_/ ) for fuzzy name matching.
// "DejaVuSans-Bold" and "dejavusansbold" both normalize to "dejavusansbold".
// "Courier New" and "CourierNew" both normalize to "couriernew".
std::string FontNorm(std::string_view s) noexcept;

// Extract font stem (filename without font extension). Handles .gz wrappers:
//   "DejaVuSans-Bold.ttf"  → "DejaVuSans-Bold"
//   "NotoSerif.ttf.gz"     → "NotoSerif"
// Returns "" for non-font files.
std::string FontStem(const std::filesystem::path &p) noexcept;

// Default platform font directories, searched in order.
std::vector<std::filesystem::path> SystemFontDirs();

// Core search: scan dirs in order, return path of first file whose
// normalized stem matches any of the normalized names (in names priority order).
[[nodiscard]] Option<String> FontSearch(std::span<const std::string> norms,
											   std::span<const std::filesystem::path> dirs);

} // namespace detail

/// Recherche la première police correspondant à un des noms donnés (par ordre
/// de priorité) dans les répertoires système. La correspondance ignore la casse
/// et les séparateurs (-/_/ ).
///
///   auto path = sdl3::FindFontPath({"DejaVuSans-Bold", "Arial", "FreeSans"});
[[nodiscard]] Option<String> FindFontPath(std::initializer_list<const char *> names);

// Same as above, but searches the given directories in priority order instead
// of the platform defaults. Each string may be a std::string or a literal.
//
//   auto path = sdl3::findFontPath(
//       {"DejaVuSans-Bold", "Arial"},
//       {"./assets/fonts/",
//        std::format("/home/{}/.local/share/fonts", sdl3::getSystemUserName()),
//        "/usr/share/fonts/"});
[[nodiscard]] Option<String> FindFontPath(std::initializer_list<const char *> names,
												 std::initializer_list<std::string> dirs);

// ============================================================================
// Font — RAII TTF_Font
// ============================================================================

/// Police TrueType chargée (RAII sur TTF_Font).
class Font : public Wrapper<TTF_Font, TTF_CloseFont> {
public:
	using Wrapper::Wrapper;

	/// Ouvre une police depuis un fichier.
	[[nodiscard]] static Result<Font, StringView> Open(const String &path, float ptSize);

	/// Open a font from an IO stream. `closeio = true` transfers ownership of `src` to SDL.
	[[nodiscard]] static Result<Font, StringView> OpenIO(IOStream &src, bool closeio, float ptSize);

	// Search platform font directories for the first matching font name.
	// Names are tried in priority order; matching ignores case and separators.
	//
	//   auto font = sdl3::Font::FindLocal({"DejaVuSans-Bold", "Arial", "FreeSans"}, 14.f);
	[[nodiscard]] static Result<Font, StringView> FindLocal(std::initializer_list<const char *> names,
															float ptSize = 14.f);

	// Same, but searches the given directories in priority order.
	// Pass std::string or string literals freely (const char* converts implicitly).
	//
	//   auto font = sdl3::Font::FindLocal(
	//       {"DejaVuSans-Bold", "Arial"},
	//       {"./assets/fonts/",
	//        std::format("/home/{}/.local/share/fonts", sdl3::getSystemUserName()),
	//        "/usr/share/fonts/"},
	//       14.f);
	[[nodiscard]] static Result<Font, StringView>
	FindLocal(std::initializer_list<const char *> names, std::initializer_list<std::string> dirs, float ptSize = 14.f);

	// ── Metrics & style ──────────────────────────────────────────────────────

	void SetStyle(FontStyle s);
	void SetOutline(int px);
	void SetSize(float pt);
	/// Taille courante, en points.
	[[nodiscard]] float PointSize() const { return mHandle ? TTF_GetFontSize(mHandle) : 0.f; }
	/// Copie INDÉPENDANTE de la police (même fichier, même style) : sa taille
	/// peut changer sans toucher l'originale — ni les textes déjà mis en forme
	/// avec elle, que `SetSize` sur la police partagée re-mettrait en page.
	[[nodiscard]] Result<Font, StringView> Copy() const;
	void SetHinting(TTF_HintingFlags h);

	[[nodiscard]] FontStyle Style() const;
	[[nodiscard]] int Ascent() const { return mHandle ? TTF_GetFontAscent(mHandle) : 0; }
	[[nodiscard]] int Descent() const { return mHandle ? TTF_GetFontDescent(mHandle) : 0; }
	[[nodiscard]] int Height() const { return mHandle ? TTF_GetFontHeight(mHandle) : 0; }
	[[nodiscard]] int LineSkip() const { return mHandle ? TTF_GetFontLineSkip(mHandle) : 0; }
	[[nodiscard]] bool IsFixedWidth() const { return mHandle && TTF_FontIsFixedWidth(mHandle); }

	/// True if the font contains a glyph for `codepoint`.
	[[nodiscard]] bool HasGlyph(std::uint32_t codepoint) const {
		return mHandle && TTF_FontHasGlyph(mHandle, codepoint);
	}

	/// Metrics of a glyph (NONE if the codepoint is missing).
	[[nodiscard]] Option<GlyphMetrics> GlyphMetricsOf(std::uint32_t codepoint) const {
		if (!mHandle) return NONE;
		GlyphMetrics m{};
		if (!TTF_GetGlyphMetrics(mHandle, codepoint, &m.minX, &m.maxX, &m.minY, &m.maxY, &m.advance)) return NONE;
		return Some(m);
	}

	void SetKerning(bool enabled) { if (mHandle) TTF_SetFontKerning(mHandle, enabled); }
	[[nodiscard]] bool HasKerning() const { return mHandle && TTF_GetFontKerning(mHandle); }

	void SetSDF(bool enabled) { if (mHandle) TTF_SetFontSDF(mHandle, enabled); }
	[[nodiscard]] bool HasSDF() const { return mHandle && TTF_GetFontSDF(mHandle); }

	[[nodiscard]] TTF_HintingFlags Hinting() const { return mHandle ? TTF_GetFontHinting(mHandle) : TTF_HINTING_NORMAL; }
	[[nodiscard]] int Outline() const { return mHandle ? TTF_GetFontOutline(mHandle) : 0; }
	[[nodiscard]] int NumFaces() const { return mHandle ? TTF_GetNumFontFaces(mHandle) : 0; }

	[[nodiscard]] StringView FamilyName() const {
		const char *name = mHandle ? TTF_GetFontFamilyName(mHandle) : nullptr;
		return StringView(name);
	}
	[[nodiscard]] StringView StyleName() const {
		const char *name = mHandle ? TTF_GetFontStyleName(mHandle) : nullptr;
		return StringView(name);
	}
	/// Font weight (TTF_FONT_WEIGHT_* range: 100..1000).
	[[nodiscard]] int Weight() const { return mHandle ? TTF_GetFontWeight(mHandle) : 0; }

	/// Pixel dimensions of the rendered text.
	[[nodiscard]] Option<Point> Measure(const String &text) const;

	/// Pixel dimensions of the text wrapped at `wrap_width` pixels.
	[[nodiscard]] Option<Point> MeasureWrapped(const String &text, int wrapWidth) const;

	// ── Surface rendering ────────────────────────────────────────────────────

	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, Color fg) const;
	
	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, FColor fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, Color fg, Color bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, FColor fg, FColor bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, Color fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, FColor fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, Color fg, Color bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, FColor fg, FColor bg) const;

	/// Render text directly to a Texture (blended quality, single call).
	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, Color fg) const;

	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, FColor fg) const;
};

// ============================================================================
// TextEngine — GPU-accelerated text engine (SDL3_ttf new API)
// ============================================================================

/// Moteur de texte accéléré par GPU (nouvelle API SDL3_ttf).
class TextEngine : public Wrapper<TTF_TextEngine, TTF_DestroyRendererTextEngine> {
public:
	using Wrapper::Wrapper;

	/// Crée un moteur de texte pour un renderer.
	[[nodiscard]] static Result<TextEngine, Error> Create(Renderer &ren);
};

// ============================================================================
// Text — objet TTF_Text rendu (adossé à un TextEngine)
// ============================================================================

/// Texte mis en forme (RAII sur TTF_Text), adossé à un TextEngine.
class Text : public Wrapper<TTF_Text, TTF_DestroyText> {
public:
	using Wrapper::Wrapper;

	/// Crée un texte.
	[[nodiscard]] static Result<Text, Error> Create(TextEngine &engine, Font &font, const String &str);

	/// Remplace la chaîne du texte.
	bool SetString(const String &str) { return mHandle && TTF_SetTextString(mHandle, str.c_str(), str.size()); }

	/// Fixe la couleur (entier 8 bits).
	bool SetColor(Color c) { return mHandle && TTF_SetTextColor(mHandle, c.r, c.g, c.b, c.a); }

	/// Fixe la couleur (flottant).
	bool SetColor(FColor c) { return mHandle && TTF_SetTextColorFloat(mHandle, c.r, c.g, c.b, c.a); }

	/// Enveloppe le texte à `width` pixels (0 = désactive l'enveloppement,
	/// comportement par défaut) — les retours à la ligne explicites ('\n')
	/// restent respectés comme des coupures de paragraphe. Nécessaire pour
	/// un bloc de texte multi-paragraphe de largeur fixe et hauteur adaptée
	/// au contenu : appeler puis lire size() donne la hauteur réellement
	/// occupée après enveloppement, pour dimensionner un widget en
	/// conséquence (cf. UiCanvas — aucun widget ui:: natif ne fait encore
	/// d'enveloppement de PARAGRAPHE, seulement UiInput au niveau mot-par-mot
	/// interne à systems.hpp, indépendant de ce mécanisme SDL_ttf).
	bool SetWrapWidth(int width) { return mHandle && TTF_SetTextWrapWidth(mHandle, width); }

	/// Dessine le texte à la position donnée.
	bool Draw(float x, float y) { return mHandle && TTF_DrawRendererText(mHandle, x, y); }
	/// Dessine le texte à la position donnée.
	bool Draw(FPoint p) { return Draw(p.x, p.y); }

	/// Dimensions du texte rendu.
	[[nodiscard]] Option<Point> GetSize() const;
};

} // namespace sdl3
