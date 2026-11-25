#pragma once
/**
 * @file selection.hpp
 * @brief Sélection : un nœud, plusieurs, un groupe, un type, une zone de page, inversion,
 *        sélection par identifiants ; règles (non sélectionnable, masqué, sélection via le
 *        groupe) ; contexte de sélection pour l'interface (onglet de ruban suggéré, commandes
 *        disponibles).
 *
 * La sélection ne modifie jamais le document : elle référence des identifiants, et `Prune`
 * retire ceux qui ont disparu ou ne sont plus sélectionnables après une modification.
 */
#include <span>
#include <vector>

#include "document/layout.hpp"

namespace doc {

enum class SelectMode : uint8_t { Replace, Add, Toggle, Remove };

/// Résumé de la sélection pour l'interface (décisions de ruban, commandes actives).
struct SelectionContext {
	size_t count = 0;
	NodeId primary = kNoNode;
	NodeType primaryType = NodeType::Document;
	NodeTypeMask types = 0;		 ///< types présents
	bool text = false;			 ///< paragraphe ou segment de texte
	bool table = false;			 ///< tableau, ligne, cellule ou contenu d'une cellule
	NodeId enclosingTable = kNoNode; ///< tableau englobant le nœud principal
	bool image = false, shape = false;
	bool headerFooter = false;	 ///< dans un en-tête ou un pied de page
	bool page = false;			 ///< page ou contenu propre d'une page
	bool group = false;			 ///< nœud de type Group
	bool locked = false;		 ///< au moins un nœud verrouillé (modifications refusées)
	bool hidden = false;		 ///< au moins un nœud masqué
	bool mixed = false;			 ///< plusieurs types différents
	[[nodiscard]] bool Empty() const noexcept { return count == 0; }
};

class Selection {
public:
	/// Le nœud peut-il être sélectionné ? Refusés : nœuds structurels (Document, Groups, Body),
	/// Selectable faux ; nœuds masqués sauf si `allowHidden` (sélection depuis l'arborescence).
	[[nodiscard]] static bool CanSelect(const DocumentView &view, NodeId id, bool allowHidden = false);

	/// Sélectionne un nœud. Avec SelectViaGroup, les membres de ses groupes sont sélectionnés
	/// ensemble. Rend le nombre de nœuds ajoutés ou retirés.
	size_t Select(const DocumentView &view, NodeId id, SelectMode mode = SelectMode::Replace, bool allowHidden = false);
	/// Sélection par identifiants (les identifiants non sélectionnables sont ignorés).
	size_t SelectIds(const DocumentView &view, std::span<const NodeId> ids, SelectMode mode = SelectMode::Replace, bool allowHidden = false);
	/// Membres d'un groupe (et de ses sous-groupes).
	size_t SelectGroup(const DocumentView &view, NodeId group, SelectMode mode = SelectMode::Replace);
	/// Tous les nœuds d'un type sous `scope` (racine par défaut).
	size_t SelectType(const DocumentView &view, NodeType type, NodeId scope = kNoNode, SelectMode mode = SelectMode::Replace);
	/// Nœuds dont la boîte est entièrement contenue dans `area` sur la page (blocs du flux,
	/// images, formes ; les conteneurs de ces blocs sont ignorés).
	size_t SelectArea(const DocumentView &view, const LayoutResult &layout, size_t page, const Rect &area,
					  SelectMode mode = SelectMode::Replace);
	/// Inverse la sélection parmi les blocs de même niveau : nœuds sélectionnables partageant le
	/// parent du nœud principal (tous les blocs des corps si la sélection est vide).
	size_t Invert(const DocumentView &view);
	void Clear() noexcept;
	/// Retire les nœuds disparus ou devenus non sélectionnables.
	size_t Prune(const DocumentView &view);

	[[nodiscard]] std::span<const NodeId> Nodes() const noexcept { return m_nodes; }
	[[nodiscard]] NodeId Primary() const noexcept { return m_nodes.empty() ? kNoNode : m_nodes.back(); }
	[[nodiscard]] bool Contains(NodeId id) const noexcept;
	[[nodiscard]] size_t Size() const noexcept { return m_nodes.size(); }
	[[nodiscard]] bool Empty() const noexcept { return m_nodes.empty(); }
	/// Change à chaque modification de la sélection.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	[[nodiscard]] SelectionContext Context(const DocumentView &view) const;

private:
	size_t Apply(const std::vector<NodeId> &ids, SelectMode mode);

	std::vector<NodeId> m_nodes; ///< ordre de sélection ; le dernier est le nœud principal
	uint64_t m_revision = 0;
};

} // namespace doc
