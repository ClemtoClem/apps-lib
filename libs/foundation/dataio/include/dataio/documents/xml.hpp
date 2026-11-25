#pragma once
/**
 * dataio::XmlDocument — codec XML 1.0 (sous-ensemble : éléments, attributs,
 * texte, commentaires, sections CDATA, instructions de traitement `<?… ?>`,
 * déclarations `<!DOCTYPE …>` ignorées, entités standard (`&amp;` `&lt;` `&gt;`
 * `&quot;` `&apos;` `&#…;` `&#x…;`) décodées — pas de namespaces/entités custom).
 *
 * Convention de correspondance XML <-> arbre commun (identique à
 * SDL3pp_dataScripts.h) : les attributs d'un élément sont regroupés sous la
 * clé "@attributes" (un Object), le texte sous "#text". Deux enfants avec le
 * même nom de balise sont fusionnés en un Array — c'est l'écart XML/arbre
 * habituel : cela préserve le regroupement par nom de balise, mais PAS
 * l'ordre d'entrelacement entre balises de noms différents
 * (`<a/><b/><a/>` redevient `{a:[...,...], b:[...]}`, l'ordre relatif de
 * `b` par rapport aux deux `a` est perdu). Un lecteur XML "événementiel"
 * séparé serait nécessaire pour préserver un ordre de document strict ;
 * hors de portée de ce module dont le but est l'interopérabilité entre
 * formats via un arbre commun, pas la fidélité XML à 100%.
 *
 * Aucune exception : toute erreur de syntaxe est propagée via
 * Option<ParseError>/Result<NodePtr,ParseError>.
 */
#include "../document.hpp"

namespace dataio {

class XmlDocument final : public Document {
public:
	XmlDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	static constexpr const char *ATTR_KEY = "@attributes";
	static constexpr const char *TEXT_KEY = "#text";

	// ── Encodage ─────────────────────────────────────────────────────────────

	static void WriteIndent(String &out, int indent);

	static void EncodeAttributes(String &out, const NodePtr &attrs);

	static void EncodeNode(String &out, const String &tag, const NodePtr &node, int indent);

	// ── Décodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static Option<ParseError> Fail(const Reader &r, const String &msg);
	[[nodiscard]] static Result<NodePtr, ParseError> FailR(const Reader &r, const String &msg);
	[[nodiscard]] static Result<String, ParseError> FailS(const Reader &r, const String &msg);

	/// Saute une construction `<!…>` : commentaire `<!-- … -->`, déclaration
	/// `<!DOCTYPE …>` (avec sous-ensemble interne `[… ]`), ou section
	/// `<![CDATA[…]]>` dont le texte est rendu via `outText` (peut être nul).
	[[nodiscard]] static Option<ParseError> SkipMarkup(Reader &r, String *outText);

	/// Saute une instruction de traitement `<?… ?>` (le reader est positionné
	/// sur le `?`).
	[[nodiscard]] static Option<ParseError> SkipProcessingInstruction(Reader &r);

	[[nodiscard]] static String ReadName(Reader &r);

	[[nodiscard]] static Result<String, ParseError> ReadAttrValue(Reader &r);

	[[nodiscard]] static String ReadText(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseAttributes(Reader &r);

	static void MergeChild(NodePtr parent, const String &key, NodePtr child);

	[[nodiscard]] static Option<ParseError> ParseContent(Reader &r, NodePtr parent);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseElement(Reader &r, String &outTag);
};

DOC_REGISTER_FORMAT("xml", XmlDocument, ".xml")

} // namespace dataio