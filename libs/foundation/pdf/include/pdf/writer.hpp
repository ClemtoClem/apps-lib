#pragma once
/**
 * @file writer.hpp
 * @brief Écriture de fichiers PDF 1.4 : pages de tailles libres, tracés (traits, rectangles,
 *        courbes, ellipses), couleurs RVB avec transparence, texte en polices standard, images
 *        RVBA (masque alpha), liens, signets, métadonnées ; flux compressés (FlateDecode).
 *
 * Coordonnées en points (1/72 de pouce), **origine en haut à gauche de la page, y vers le bas**
 * (convention des moteurs de mise en page) ; la conversion vers le repère PDF (origine en bas
 * à gauche) est faite à l'écriture.
 *
 *   pdf::Writer w;
 *   w.SetInfo({.title = "Rapport"});
 *   pdf::Canvas &page = w.AddPage(595.28, 841.89);
 *   page.FillRect(72, 72, 200, 40, math::Color(230, 240, 255));
 *   page.Text(80, 98, pdf::StandardFont::HelveticaBold, 14, "Été 2026", math::Color(0, 0, 0));
 *   auto saved = w.Save("rapport.pdf");
 */
#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/result.hpp>
#include <math/color.hpp>

#include "pdf/fonts.hpp"
#include "pdf/object.hpp"

namespace pdf {

struct Error {
	std::string message;
};
template <typename T> using PdfResult = Result<T, Error>;

/// Image ajoutée au document (`Writer::AddImage`).
struct ImageId {
	uint32_t index = UINT32_MAX;
	[[nodiscard]] bool Valid() const noexcept { return index != UINT32_MAX; }
};

enum class LineCap : uint8_t { Butt, Round, Square };
enum class LineJoin : uint8_t { Miter, Round, Bevel };

/// Contenu d'une page : opérateurs de dessin accumulés dans le flux de la page.
class Canvas {
public:
	/// Polices incorporées partagées par les pages d'un `Writer` (index → police).
	using EmbeddedFonts = std::vector<std::shared_ptr<const TrueTypeFont>>;

	Canvas(double width, double height, std::shared_ptr<EmbeddedFonts> fonts = nullptr) noexcept
		: m_width(width), m_height(height), m_embedded(std::move(fonts)) {}

	[[nodiscard]] double Width() const noexcept { return m_width; }
	[[nodiscard]] double Height() const noexcept { return m_height; }

	// ── État graphique ──
	void Save();
	void Restore();
	/// Couleur de remplissage (et du texte) ; l'alpha devient une opacité de remplissage.
	void SetFillColor(math::Color color);
	/// Couleur de trait ; l'alpha devient une opacité de trait.
	void SetStrokeColor(math::Color color);
	void SetLineWidth(double width);
	void SetLineCap(LineCap cap);
	void SetLineJoin(LineJoin join);
	/// Pointillés (longueurs alternées trait / vide) ; vide : trait continu.
	void SetDash(std::span<const double> pattern, double phase = 0);

	// ── Tracés ──
	void MoveTo(double x, double y);
	void LineTo(double x, double y);
	void CurveTo(double x1, double y1, double x2, double y2, double x3, double y3);
	void ClosePath();
	void Rectangle(double x, double y, double w, double h);
	/// Ellipse inscrite dans le rectangle (quatre courbes de Bézier).
	void Ellipse(double x, double y, double w, double h);
	void Fill(bool evenOdd = false);
	void Stroke();
	void FillAndStroke(bool evenOdd = false);
	/// Restreint le dessin au tracé courant (jusqu'au `Restore` correspondant).
	void Clip(bool evenOdd = false);

	// ── Raccourcis ──
	void FillRect(double x, double y, double w, double h, math::Color color);
	void StrokeRect(double x, double y, double w, double h, math::Color color, double lineWidth);
	void Line(double x1, double y1, double x2, double y2, math::Color color, double lineWidth);

	// ── Texte ──
	/// Texte UTF-8 sur une ligne, ligne de base en `baseline` ; encodé en WinAnsi (caractères
	/// non représentables remplacés par « ? »).
	void Text(double x, double baseline, StandardFont font, double size, std::string_view utf8, math::Color color);
	/// Texte en police standard ou incorporée (`font.face` : TrueType / OpenType, incorporée une
	/// fois par document ; repli sur `font.standard` si sa licence interdit l'incorporation).
	void Text(double x, double baseline, const FontRef &font, double size, std::string_view utf8, math::Color color);
	/// Rotation des dessins suivants de `degrees` (sens horaire à l'écran) autour de (cx, cy),
	/// jusqu'au `Restore` correspondant.
	void Rotate(double degrees, double cx, double cy);

	// ── Images, liens ──
	/// Dessine une image dans le rectangle (étirée à ses dimensions).
	void DrawImage(ImageId image, double x, double y, double w, double h);
	/// Zone cliquable ouvrant une adresse.
	void Link(double x, double y, double w, double h, std::string uri);
	/// Zone cliquable menant à une page du document (indice 0…), ordonnée `top` en haut de la vue
	/// (repère de la page, origine en haut). Une page inexistante à l'écriture : lien ignoré.
	void LinkToPage(double x, double y, double w, double h, size_t page, double top = 0);

	/// Flux d'opérateurs (non compressé), pour inspection et tests.
	[[nodiscard]] const std::string &Content() const noexcept { return m_content; }

private:
	friend class Writer;
	struct LinkAnnotation {
		double x, y, w, h;
		std::string uri;		///< vide : lien interne
		size_t page = SIZE_MAX; ///< lien interne : page visée
		double top = 0;
	};

	void Number(double v);
	void Point(double x, double y); ///< convertit y vers le repère PDF
	void Op(std::string_view op);
	void FillAlpha(uint8_t alpha);
	void StrokeAlpha(uint8_t alpha);

	double m_width, m_height;
	std::string m_content;
	uint32_t m_fontsUsed = 0;			  ///< bit par StandardFont
	std::shared_ptr<EmbeddedFonts> m_embedded; ///< registre du Writer
	std::vector<uint32_t> m_embeddedUsed;	   ///< polices incorporées employées par la page
	std::vector<uint8_t> m_fillAlphas;	  ///< opacités de remplissage employées
	std::vector<uint8_t> m_strokeAlphas;  ///< opacités de trait employées
	std::vector<uint32_t> m_images;		  ///< images employées
	std::vector<LinkAnnotation> m_links;
	uint8_t m_fillAlpha = 255, m_strokeAlpha = 255;
	std::vector<std::pair<uint8_t, uint8_t>> m_alphaStack; ///< opacités sauvegardées par Save
};

struct DocumentInfo {
	std::string title, author, subject, keywords, creator;
	std::string producer = "apps-lib foundation.pdf";
	/// Date de création au format PDF (« D:20261122103000Z », voir `DateString`) ; vide : absente.
	std::string creationDate;
};

struct SerializeOptions {
	bool compress = true; ///< FlateDecode des flux de contenu (les images sont toujours compressées)
	int level = 6;		  ///< niveau de compression 0 à 9
};

/// Date au format PDF (UTC) depuis un nombre de secondes Unix.
[[nodiscard]] std::string DateString(int64_t unixSeconds);
/// Chaîne de texte PDF : littérale si ASCII, UTF-16BE avec BOM sinon (titres, signets, info).
[[nodiscard]] std::string TextString(std::string_view utf8);

class Writer {
public:
	void SetInfo(DocumentInfo info) { m_info = std::move(info); }
	[[nodiscard]] const DocumentInfo &Info() const noexcept { return m_info; }

	/// Ajoute une page (dimensions en points, bornées à [3, 14400] comme l'exige la norme).
	Canvas &AddPage(double width, double height);
	[[nodiscard]] size_t PageCount() const noexcept { return m_pages.size(); }
	[[nodiscard]] Canvas &Page(size_t index) { return m_pages[index]; }
	[[nodiscard]] const Canvas &Page(size_t index) const { return m_pages[index]; }

	/// Ajoute une image RVBA 8 bits (lignes contiguës). Un masque alpha n'est écrit que si un
	/// pixel n'est pas opaque.
	PdfResult<ImageId> AddImage(uint32_t width, uint32_t height, std::span<const uint8_t> rgba);

	/// Signet vers une page (`y` : haut de la vue, repère haut-gauche). `level` 0 = racine ; un
	/// signet de niveau n+1 se range sous le dernier signet de niveau n.
	void AddBookmark(std::string title, size_t page, double y = 0, int level = 0);

	[[nodiscard]] PdfResult<Bytes> Serialize(const SerializeOptions &options = {}) const;
	/// Écrit le fichier de façon atomique (fichier temporaire puis renommage) ; rend sa taille.
	PdfResult<size_t> Save(const std::filesystem::path &path, const SerializeOptions &options = {}) const;

private:
	struct Image {
		uint32_t width, height;
		Bytes rgb, alpha; ///< alpha vide : image opaque
	};
	struct Bookmark {
		std::string title;
		size_t page;
		double y;
		int level;
	};

	DocumentInfo m_info;
	std::deque<Canvas> m_pages; // références stables (AddPage rend une référence)
	std::shared_ptr<Canvas::EmbeddedFonts> m_fonts = std::make_shared<Canvas::EmbeddedFonts>();
	std::vector<Image> m_images;
	std::vector<Bookmark> m_bookmarks;
};

} // namespace pdf
