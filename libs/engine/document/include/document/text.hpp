#pragma once
/**
 * @file text.hpp
 * @brief Édition du texte dans le document : positions dans les segments de texte, ordre de
 *        lecture, géométrie du curseur et de la surbrillance, insertion, suppression, coupure
 *        et fusion de paragraphes, mise en forme d'une plage, fragments enrichis (copier /
 *        couper / coller), déplacement d'une plage (glisser-déposer).
 *
 * Une position désigne un TextNode et un décalage en octets dans son texte (frontière UTF-8).
 * Toutes les modifications passent par `History` (annulables) : une plage qui traverse
 * plusieurs paragraphes devient une transaction ; une frappe dans un seul segment reste une
 * commande simple, fusionnée avec les frappes suivantes dans l'historique.
 *
 * La navigation reste dans le « récit » de la position : corps de section, en-tête, pied de page
 * ou page (on ne passe pas du corps à l'en-tête avec les flèches).
 */
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "document/commands.hpp"
#include "document/layout.hpp"

namespace doc {

struct TextPosition {
	NodeId text = kNoNode;
	size_t offset = 0; ///< octets, frontière UTF-8
	[[nodiscard]] bool Valid() const noexcept { return text != kNoNode; }
	friend bool operator==(const TextPosition &, const TextPosition &) = default;
};

/// Ordre de lecture des segments de texte d'une vue (à reconstruire quand le document change).
class TextOrder {
public:
	explicit TextOrder(const DocumentView &view);

	[[nodiscard]] bool Contains(NodeId text) const noexcept { return m_index.Contains(text); }
	/// −1, 0 ou 1 selon l'ordre de lecture.
	[[nodiscard]] int Compare(const TextPosition &a, const TextPosition &b) const noexcept;
	/// Position ramenée dans le texte (frontière UTF-8, longueur) ; un champ devient sa fin.
	[[nodiscard]] TextPosition Normalize(TextPosition p) const;
	/// Caractère suivant / précédent (franchit les segments et les paragraphes du même récit ;
	/// un champ de numérotation compte pour un caractère).
	[[nodiscard]] TextPosition Next(TextPosition p) const;
	[[nodiscard]] TextPosition Previous(TextPosition p) const;
	[[nodiscard]] TextPosition NextWord(TextPosition p) const;
	[[nodiscard]] TextPosition PreviousWord(TextPosition p) const;
	/// Mot (lettres et chiffres contigus) autour de la position, dans son paragraphe.
	[[nodiscard]] std::pair<TextPosition, TextPosition> WordAt(TextPosition p) const;
	[[nodiscard]] std::pair<TextPosition, TextPosition> ParagraphRange(TextPosition p) const;
	/// Début / fin du récit (corps, en-tête…) de la position.
	[[nodiscard]] std::pair<TextPosition, TextPosition> StoryRange(TextPosition p) const;
	/// Premier segment de texte d'un paragraphe (kNoNode s'il n'en a pas).
	[[nodiscard]] NodeId FirstText(NodeId paragraph) const;
	[[nodiscard]] const DocumentView &View() const noexcept { return *m_view; }

private:
	struct Entry {
		NodeId text, paragraph, story;
		size_t length;
		bool field;
	};
	[[nodiscard]] const Entry *Find(NodeId text) const noexcept;
	[[nodiscard]] std::string_view TextOf(const Entry &e) const;

	const DocumentView *m_view; ///< doit survivre à l'ordre (reconstruit à chaque modification)
	std::vector<Entry> m_entries;
	core::RobinMap<NodeId, size_t> m_index;
};

/// Fragment de texte enrichi (presse-papiers) : paragraphes, segments, propriétés explicites.
struct TextFragment {
	struct Run {
		PropertyMap properties; ///< propriétés explicites du segment (sans le texte)
		std::string text;
	};
	struct Paragraph {
		PropertyMap properties;
		std::vector<Run> runs;
	};
	std::vector<Paragraph> paragraphs;

	[[nodiscard]] bool Empty() const noexcept;
	/// Texte brut (paragraphes séparés par « \n »).
	[[nodiscard]] std::string PlainText() const;
	[[nodiscard]] static TextFragment FromPlainText(std::string_view text);
};

struct CaretBox {
	size_t page = 0;
	Rect rect; ///< largeur nulle : x, haut et hauteur de la ligne
	/// Texte d'une forme tournée : rotation de la ligne (degrés, sens horaire) autour du pivot.
	double rotation = 0, pivotX = 0, pivotY = 0;
};

namespace text {

// ── Géométrie (vue + mise en page de cette vue) ──

[[nodiscard]] std::optional<CaretBox> Caret(const DocumentView &view, const LayoutResult &layout, TextPosition position);
/// Position de texte la plus proche d'un point de la page (lignes de cette page).
[[nodiscard]] std::optional<TextPosition> PositionAt(const DocumentView &view, const LayoutResult &layout, size_t page, double x, double y);
/// Ligne voisine (direction −1 : au-dessus, +1 : au-dessous), à l'abscisse `x` ; traverse les pages.
[[nodiscard]] std::optional<TextPosition> LineMove(const DocumentView &view, const LayoutResult &layout, TextPosition position, int direction,
												   double x);
/// Début (`end` faux) ou fin de la ligne affichée.
[[nodiscard]] std::optional<TextPosition> LineEdge(const DocumentView &view, const LayoutResult &layout, TextPosition position, bool end);
/// Rectangles de surbrillance d'une plage (une par portion de segment, par page).
[[nodiscard]] std::vector<std::pair<size_t, Rect>> RangeRects(const LayoutResult &layout, const TextOrder &order, TextPosition a, TextPosition b);

// ── Lecture ──

[[nodiscard]] TextFragment Copy(const DocumentView &view, const TextOrder &order, TextPosition a, TextPosition b);
[[nodiscard]] std::string PlainText(const DocumentView &view, const TextOrder &order, TextPosition a, TextPosition b);

// ── Modifications (annulables) ──

/// Insère du texte (« \n » coupe le paragraphe) ; rend la position après l'insertion.
DocResult<TextPosition> Insert(History &history, TextPosition at, std::string_view utf8);
/// Saut de ligne dans le paragraphe (« \n » gardé dans le segment : la ligne suivante commence
/// après lui) ; rend la position après le saut.
DocResult<TextPosition> InsertLineBreak(History &history, TextPosition at);
/// Supprime la plage (bornes dans n'importe quel ordre) ; rend la position du curseur.
DocResult<TextPosition> Erase(History &history, TextPosition a, TextPosition b);
/// Coupe le paragraphe (Entrée) ; rend le début du nouveau paragraphe.
DocResult<TextPosition> Split(History &history, TextPosition at);
/// Applique une propriété aux segments de la plage (segments coupés aux bornes) ; rend la plage.
DocResult<std::pair<TextPosition, TextPosition>> ApplyStyle(History &history, TextPosition a, TextPosition b, PropertyId property,
															PropertyValue value);
/// Colle un fragment ; rend la position après le texte collé.
DocResult<TextPosition> Paste(History &history, TextPosition at, const TextFragment &fragment);
/// Déplace (ou copie) la plage vers `target` (glisser-déposer) ; rend la plage à sa nouvelle place.
DocResult<std::pair<TextPosition, TextPosition>> MoveRange(History &history, TextPosition a, TextPosition b, TextPosition target, bool copy);

} // namespace text

} // namespace doc
