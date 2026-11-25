#pragma once
/**
 * @file node.hpp
 * @brief Nœud du document et règles de chaque type (enfants admis, unicité, protection).
 *
 * Un nœud ne se modifie que par le `Document` qui le possède : l'interface publique est en
 * lecture seule. Les nœuds sont partagés entre le document et ses instantanés (copie sur
 * écriture), d'où l'absence de pointeurs vers d'autres nœuds : les liens sont des identifiants.
 */
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "document/properties.hpp"
#include "document/types.hpp"

namespace doc {

struct NodeTypeInfo {
	NodeType type{};
	std::string_view name;		///< identifiant stable (persistance, interface)
	NodeTypeMask children = 0;	///< types d'enfants admis
	bool uniqueInParent = false; ///< au plus un enfant de ce type par parent (Body, Groups)
	bool structural = false;	///< créé avec son parent, jamais supprimé seul (Document, Groups, Body)
	bool flowBlock = false;		///< bloc de flux (paginé : paragraphe, tableau, image, forme, saut)
};

[[nodiscard]] const NodeTypeInfo &TypeInfo(NodeType type) noexcept;
[[nodiscard]] std::optional<NodeType> FindNodeType(std::string_view name) noexcept;
[[nodiscard]] inline std::string_view NodeTypeName(NodeType type) noexcept { return TypeInfo(type).name; }
[[nodiscard]] inline bool CanContain(NodeType parent, NodeType child) noexcept {
	return (TypeInfo(parent).children & MaskOf(child)) != 0;
}

/// Appartenance d'un nœud à un groupe ; `automatic` : posée par le moteur (pages d'une section).
struct Membership {
	NodeId group = kNoNode;
	bool automatic = false;
	friend bool operator==(const Membership &, const Membership &) = default;
};

class Node {
public:
	Node(NodeId id, NodeType type) noexcept : m_id(id), m_type(type) {}

	[[nodiscard]] NodeId Id() const noexcept { return m_id; }
	[[nodiscard]] NodeType Type() const noexcept { return m_type; }
	[[nodiscard]] NodeId Parent() const noexcept { return m_parent; }
	[[nodiscard]] std::span<const NodeId> Children() const noexcept { return m_children; }
	/// Groupes du nœud, dans l'ordre d'ajout.
	[[nodiscard]] std::span<const Membership> Groups() const noexcept { return m_groups; }
	/// Membres (nœud de type Group uniquement), dans l'ordre d'ajout.
	[[nodiscard]] std::span<const NodeId> Members() const noexcept { return m_members; }
	[[nodiscard]] const PropertyMap &Properties() const noexcept { return m_properties; }
	/// Nœud produit par le moteur (pages de la pagination) plutôt que par l'utilisateur.
	[[nodiscard]] bool Generated() const noexcept { return m_generated; }
	/// Révision du contenu : change quand le nœud ou un descendant change (cache de mise en page).
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	/// Révision du style transmis : change quand une propriété héritable ou une appartenance de
	/// CE nœud change (ce qui peut modifier ses descendants) — pas quand un descendant change.
	[[nodiscard]] uint64_t InheritedStyleRevision() const noexcept { return m_styleRevision; }
	[[nodiscard]] bool InGroup(NodeId group) const noexcept;
	[[nodiscard]] size_t IndexOfChild(NodeId child) const noexcept; ///< SIZE_MAX si absent

private:
	friend class Document;
	friend class DocumentBuilder; // chargement (.docdb)
	NodeId m_id;
	NodeType m_type;
	NodeId m_parent = kNoNode;
	bool m_generated = false;
	uint64_t m_revision = 0;
	uint64_t m_styleRevision = 0;
	std::vector<NodeId> m_children;
	std::vector<Membership> m_groups;
	std::vector<NodeId> m_members;
	PropertyMap m_properties;
};

} // namespace doc
