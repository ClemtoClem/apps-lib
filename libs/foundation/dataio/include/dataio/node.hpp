#pragma once
/**
 * dataio::Node — arbre de données typé, indépendant du format, utilisé comme
 * représentation intermédiaire commune pour tous les codecs (JSON, XML, INI,
 * YAML, TOML, CSV, CSS). Les enfants d'un Object ou d'un Array sont possédés par
 * des `NodePtr` (std::shared_ptr) rangés dans une table de hachage.
 *
 * Les clés et les valeurs scalaires de type chaîne utilisent `String` (cf.
 * string.hpp) plutôt que `std::string`, en cohérence avec le reste du
 * module dataio::.
 *
 * La table de hachage ne respectant pas l'ordre d'insertion, un vecteur de clés
 * `order` est maintenu à côté pour
 * préserver l'ordre — indispensable pour un round-trip stable (XML où
 * l'ordre des enfants est sémantique, JSON/YAML où on veut un ordre humain
 * cohérent, CSV où l'ordre des colonnes compte).
 */
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <string>
#include <unordered_map>

#include <core/core.hpp>

namespace dataio {

// ============================================================================
// NodeType
// ============================================================================

enum class NodeType : uint8_t {
	NONE,
	OBJECT,
	ARRAY,
	STRING,
	BOOL,
	INT,
	FLOAT,
};

class Node;
using NodePtr = std::shared_ptr<Node>;

// ============================================================================
// Node
// ============================================================================

class Node : public std::enable_shared_from_this<Node> {
public:
	NodeType type = NodeType::NONE;

	// --- Stockage scalaire ---
	String stringValue;
	bool boolValue = false;
	int64_t intValue = 0;
	double floatValue = 0.0;

	// --- Stockage Object/Array : `children` possède les enfants, `order` fixe
	// l'ordre d'énumération (ordre d'insertion).
	std::unordered_map<std::string, NodePtr> children;
	std::vector<String> order;

	Node() = default;
	explicit Node(NodeType t);

	// ── Fabriques ─────────────────────────────────────────────────────────────

	[[nodiscard]] static NodePtr MakeNone() { return std::make_shared<Node>(NodeType::NONE); }
	[[nodiscard]] static NodePtr MakeObject() { return std::make_shared<Node>(NodeType::OBJECT); }
	[[nodiscard]] static NodePtr MakeArray() { return std::make_shared<Node>(NodeType::ARRAY); }
	[[nodiscard]] static NodePtr MakeString(String v);
	[[nodiscard]] static NodePtr MakeBool(bool v);
	[[nodiscard]] static NodePtr MakeInt(int64_t v);
	[[nodiscard]] static NodePtr MakeFloat(double v);

	// ── Prédicats ─────────────────────────────────────────────────────────────

	[[nodiscard]] bool IsNone() const noexcept { return type == NodeType::NONE; }
	[[nodiscard]] bool IsObject() const noexcept { return type == NodeType::OBJECT; }
	[[nodiscard]] bool IsArray() const noexcept { return type == NodeType::ARRAY; }
	[[nodiscard]] bool IsString() const noexcept { return type == NodeType::STRING; }
	[[nodiscard]] bool IsBool() const noexcept { return type == NodeType::BOOL; }
	[[nodiscard]] bool IsInt() const noexcept { return type == NodeType::INT; }
	[[nodiscard]] bool IsFloat() const noexcept { return type == NodeType::FLOAT; }
	[[nodiscard]] bool IsNumber() const noexcept { return IsInt() || IsFloat(); }
	[[nodiscard]] bool IsScalar() const noexcept { return !IsObject() && !IsArray() && !IsNone(); }

	// ── Accès Object (clés arbitraires) ────────────────────────────────────────

	void Set(const String &key, NodePtr child);

	[[nodiscard]] NodePtr Get(const String &key) const;

	[[nodiscard]] bool Has(const String &key) const { return children.contains(key); }

	void Remove(const String &key);

	[[nodiscard]] const std::vector<String> &Keys() const noexcept { return order; }

	// ── Accès Array (indices séquentiels 0..N-1, mêmes fondations que Object) ──

	void Push(NodePtr child) { Set(String::From(static_cast<long long>(order.size())), std::move(child)); }
	[[nodiscard]] size_t GetSize() const noexcept { return order.size(); }
	[[nodiscard]] NodePtr At(size_t i) const { return i < order.size() ? Get(order[i]) : nullptr; }

	// ── Lectures tolérantes (valeur par défaut si le type ne correspond pas) ──
	[[nodiscard]] bool AsBool(bool def = false) const noexcept;
	[[nodiscard]] double AsNumber(double def = 0.0) const noexcept;
	[[nodiscard]] float AsFloat(float def = 0.f) const noexcept { return static_cast<float>(AsNumber(def)); }
	[[nodiscard]] int64_t AsInt(int64_t def = 0) const noexcept;
	[[nodiscard]] StringView AsString(StringView def = {}) const noexcept;
	/// Enfants d'un tableau (ou d'un objet) dans l'ordre d'insertion.
	[[nodiscard]] std::vector<NodePtr> AsArray() const;
	/// Paires (clé, enfant) d'un objet dans l'ordre d'insertion.
	[[nodiscard]] std::vector<std::pair<String, NodePtr>> AsObject() const;

	// ── Accès tolérants (nœud nul partagé si absent) ───────────────────────────
	/// Membre d'objet, ou un nœud nul partagé si la clé n'existe pas.
	[[nodiscard]] NodePtr operator[](StringView key) const;
	/// Élément de tableau, ou un nœud nul partagé si l'index dépasse.
	[[nodiscard]] NodePtr operator[](size_t index) const;
	/// Chemin pointé `a.b.c`, ou un nœud nul partagé s'il n'existe pas.
	[[nodiscard]] NodePtr At(StringView dottedPath) const;

	// ── Taille / fusion / égalité ──────────────────────────────────────────────
	[[nodiscard]] size_t Size() const noexcept { return GetSize(); }
	/// Fusion profonde : les objets sont fusionnés récursivement, le reste remplacé.
	void MergeFrom(const NodePtr &other);
	[[nodiscard]] bool Equals(const NodePtr &other) const;

	// ── Copie profonde ────────────────────────────────────────────────────────

	[[nodiscard]] NodePtr Clone() const;
};

} // namespace dataio