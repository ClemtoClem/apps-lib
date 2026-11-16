#pragma once
/**
 * dataio::Document — classe de base pour tous les codecs de scripts de
 * données, plus un registre de formats (nom / extension -> constructeur).
 *
 * Aucune exception n'est utilisée dans ce module : toute opération pouvant
 * échouer retourne Option<T> (absence de valeur) ou Result<T,E> (succès /
 * erreur typée), cf. option.hpp / result.hpp. Les chaînes de caractères
 * utilisent String (stockage) / StringView (vue non-possédante), cf.
 * string.hpp / string_view.hpp, à la place de std::string /
 * std::string_view. Le parsage texte se fait via dataio::Reader (curseur sur
 * une StringView) à la place de std::istringstream.
 */
#include <cctype>
#include <cstdlib>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <span>

#include <core/core.hpp>
#include "node.hpp"

namespace dataio {

// ============================================================================
// ParseError
// ============================================================================

struct ParseError {
	String message;
	int line = -1;
	int column = -1;

	ParseError() = default;
	explicit ParseError(String msg, int l = -1, int c = -1) : message(std::move(msg)), line(l), column(c) {}

	[[nodiscard]] String Format() const;
};

// ============================================================================
// Reader — curseur de lecture sur une StringView, remplace std::istringstream
// pour tous les codecs texte de ce module. Ne lève jamais d'exception : Get()
// / Peek() retournent Option<char>, NONE signalant la fin de flux.
// ============================================================================

class Reader {
public:
	explicit Reader(StringView data) noexcept : m_data(data) {}
	explicit Reader(const String &data) noexcept : m_data(data.View()) {}

	[[nodiscard]] bool Eof() const noexcept { return m_pos >= m_data.GetSize(); }

	[[nodiscard]] Option<char> Peek() const noexcept;

	[[nodiscard]] Option<char> PeekAt(size_t offset) const noexcept;

	[[nodiscard]] bool PeekIs(char c) const noexcept;

	[[nodiscard]] bool PeekIs(std::initializer_list<char> lst) const noexcept;

	Option<char> Get() noexcept;

	/// Recule d'un caractère — n'est valide qu'immédiatement après un Get().
	void Putback() noexcept;

	/// Consomme `c` si c'est le prochain caractère. Retourne true si consommé.
	bool Eat(char c) noexcept;

	void SkipWs() noexcept;

	[[nodiscard]] int Line() const noexcept { return m_line; }
	[[nodiscard]] size_t Pos() const noexcept { return m_pos; }
	void SetPos(size_t p) noexcept { m_pos = p; }

	[[nodiscard]] StringView Remaining() const noexcept { return m_data.Substr(m_pos); }
	[[nodiscard]] StringView All() const noexcept { return m_data; }

private:
	StringView m_data;
	size_t m_pos = 0;
	int m_line = 1;
};

// ============================================================================
// scalar — détection automatique du type le plus précis pour un texte brut
// (utilisé par les formats texte : INI/CSV/XML/YAML/TOML/CSS — JSON a sa
// propre grammaire de littéraux et n'en a pas besoin).
// ============================================================================

namespace scalar {

[[nodiscard]] Option<bool> ParseBool(StringView text);

[[nodiscard]] inline Option<int64_t> ParseInt(StringView text) { return String(text).TryParseInt(); }

[[nodiscard]] inline Option<double> ParseFloat(StringView text) { return String(text).TryParseDouble(); }

// Construit le nœud le plus précis pour un texte brut : bool, puis int,
// puis float, sinon string telle quelle.
[[nodiscard]] NodePtr ParseNode(StringView text);

[[nodiscard]] String ToString(const NodePtr &node);

} // namespace scalar

// ============================================================================
// Document — base commune à tous les codecs
// ============================================================================

class Document {
public:
	virtual ~Document() = default;

	// ── Décodage ─────────────────────────────────────────────────────────────
	// NONE = succès (la racine a été remplie via SetRoot()) ; Some(err) = échec.

	/// Octets en mémoire.
	[[nodiscard]] Option<ParseError> DecodeBytes(std::span<const std::byte> bytes);
	[[nodiscard]] Option<ParseError> DecodeBytes(std::span<const uint8_t> bytes) { return DecodeBytes(std::as_bytes(bytes)); }
	/// Fichier sur disque. Une erreur d'ouverture est rendue comme ParseError (ligne -1).
	[[nodiscard]] Option<ParseError> DecodeFile(const String &path);

	[[nodiscard]] Option<ParseError> DecodeStr(const String &content) { return DecodeImpl(content); }

	// ── Encodage ─────────────────────────────────────────────────────────────

	/// Écrit le document dans `path` (fichier temporaire puis renommage : un fichier
	/// existant n'est jamais laissé à moitié écrit). NONE = réussi, sinon le message d'erreur.
	[[nodiscard]] Option<String> EncodeFile(const String &path) const;

	[[nodiscard]] virtual String EncodeStr() const = 0;

	// ── Racine ───────────────────────────────────────────────────────────────

	[[nodiscard]] NodePtr GetRoot() const noexcept { return mRoot; }
	void SetRoot(NodePtr node) { mRoot = std::move(node); }

protected:
	/// Implémentation du décodage — remplit la racine via SetRoot(). Doit
	/// être totalement exempte d'exceptions : toute erreur de syntaxe se
	/// signale en retournant Some(ParseError).
	[[nodiscard]] virtual Option<ParseError> DecodeImpl(const String &content) = 0;

	NodePtr mRoot;
};

using DocumentPtr = std::shared_ptr<Document>;

// ============================================================================
// DocumentFactory — registre nom/extension -> constructeur (singleton)
// ============================================================================

class DocumentFactory {
public:
	using Creator = std::function<DocumentPtr()>;

	struct FormatInfo {
		String name;
		std::vector<String> extensions;
		Creator creator;
	};

	[[nodiscard]] static DocumentFactory &Instance();

	DocumentFactory(const DocumentFactory &) = delete;
	DocumentFactory &operator=(const DocumentFactory &) = delete;

	bool RegisterFormat(const String &name, std::vector<String> extensions, Creator creator);

	[[nodiscard]] DocumentPtr CreateByName(const String &name) const;

	[[nodiscard]] DocumentPtr CreateByFilename(const String &filename) const;

	[[nodiscard]] std::vector<String> RegisteredFormats() const;

private:
	DocumentFactory() = default;

	[[nodiscard]] static String ExtractExtension(const String &filename);

	std::unordered_map<String, FormatInfo> m_byName;
	std::unordered_map<String, FormatInfo> m_byExtension;
};

// Macro d'auto-enregistrement, à placer une fois par unité de compilation
// (ou dans le header lui-même vu que ce module est header-only : un
// `inline` au lieu d'une variable anonyme statique par TU).
#define DOC_REGISTER_FORMAT(formatName, ClassName, ...)                                                               \
	namespace {                                                                                                        \
	inline const bool _data_reg_##ClassName = dataio::DocumentFactory::Instance().RegisterFormat(                        \
		formatName, std::vector<String>{__VA_ARGS__},                                                                  \
		[]() -> dataio::DocumentPtr { return std::make_shared<ClassName>(); });                                          \
	}

} // namespace dataio