#pragma once
/**
 * dataio::JsonDocument — codec JSON (RFC 8259) sur l'arbre dataio::Node commun.
 * Aucune exception : le décodage retourne Result<NodePtr,ParseError> en
 * interne, converti en Option<ParseError> par DecodeImpl.
 */
#include <charconv>
#include <cstdio>
#include <iomanip>
#include <limits>

#include "../document.hpp"

namespace dataio {

class JsonDocument final : public Document {
public:
	JsonDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	// ── Décodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static Result<NodePtr, ParseError> FailNode(const Reader &r, const String &msg);
	[[nodiscard]] static Result<String, ParseError> FailStr(const Reader &r, const String &msg);

	[[nodiscard]] static Result<String, ParseError> ParseStringLiteral(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseLiteral(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseArray(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseObject(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseNode(Reader &r);
};

/// Écrit un nœud en JSON. `indent` < 0 : sortie compacte (une ligne) ;
/// sinon `indent` espaces par niveau d'imbrication.
[[nodiscard]] String WriteJsonNode(const NodePtr &node, int indent = 2);

DOC_REGISTER_FORMAT("json", JsonDocument, ".json")

} // namespace dataio