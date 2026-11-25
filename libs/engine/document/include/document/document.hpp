#pragma once
/**
 * @file document.hpp
 * @brief Modèle documentaire : `DocumentView` (lecture, résolution des propriétés),
 *        `Snapshot` (instantané immuable pour les traitements de fond), `Document` (modèle
 *        modifiable), `PropertyCache` (cache des propriétés résolues).
 *
 * **Concurrence** — un `Document` n'est modifié que par son fil propriétaire. Ses nœuds sont
 * partagés par `shared_ptr` avec les instantanés : `TakeSnapshot()` ne copie que des pointeurs,
 * et le document clone un nœud avant de le modifier s'il est encore partagé (copie sur
 * écriture). Un instantané ne change donc jamais et se lit sans verrou depuis n'importe quel fil.
 *
 * **Cascade** — pour une propriété P d'un nœud N (voir README §2.2) :
 *  1. valeur explicite de N ;
 *  2. groupes de N (et leurs groupes ancêtres), ordonnés selon `GroupResolution` ;
 *  3. si P est héritable : valeur résolue du parent ;
 *  4. valeur explicite du nœud Document ;
 *  5. valeur par défaut du catalogue.
 * Les étapes 2 et 4 ne s'appliquent qu'aux types que P cible (sauf si P est héritable).
 *
 *   doc::Document d;
 *   const doc::NodeId body = d.Body(d.Sections().front());
 *   const doc::NodeId p = d.CreateNode(doc::NodeType::Paragraph, body).Value();
 *   const doc::NodeId titles = d.CreateGroup("Titres", 10).Value();
 *   d.Set(titles, doc::props::FontSize, 20.0);
 *   d.AddToGroup(p, titles);
 *   d.Get(p, doc::props::FontSize);                     // 20 (source : Group)
 */
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <core/robin_map.hpp>
#include <events/events.hpp>

#include "document/node.hpp"
#include "document/properties.hpp"
#include "document/types.hpp"

namespace doc {

/// Ordre de consultation des groupes d'un nœud.
enum class GroupResolution : uint8_t {
	PriorityFirst,	///< priorité décroissante, puis proximité (groupe direct avant groupe ancêtre)
	ProximityFirst, ///< proximité d'abord, puis priorité décroissante
};

/// Origine d'une valeur résolue.
enum class ValueSource : uint8_t { Explicit, Group, Parent, Document, Default };
[[nodiscard]] std::string_view SourceName(ValueSource source) noexcept;

struct Resolved {
	PropertyValue value;
	ValueSource source = ValueSource::Default;
	NodeId origin = kNoNode; ///< nœud (ou groupe) qui porte la valeur ; kNoNode pour Default
};

/// Image décodée (RVBA 8 bits, lignes contiguës) référencée par les ImageNode.
struct ImageResource {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> rgba;
};

enum class ChangeKind : uint8_t { Created, Deleted, Moved, Property, Membership, Resource, Structure };
/// Notification de modification (émise par `Document::Changed()` sur le fil propriétaire).
struct Change {
	ChangeKind kind = ChangeKind::Structure;
	NodeId node = kNoNode;
	PropertyId property = PropertyId::Count; ///< ChangeKind::Property uniquement
};

/// Sous-arbre retiré du document, réinsérable à l'identique (mêmes identifiants, mêmes positions).
struct Subtree {
	/// Appartenance traversant la frontière du sous-arbre (membre ou groupe à l'extérieur).
	struct Link {
		NodeId member = kNoNode, group = kNoNode;
		size_t memberIndex = 0; ///< position dans les membres du groupe
		size_t groupIndex = 0;	///< position dans les groupes du membre
		bool automatic = false;
	};
	NodeId parent = kNoNode;
	size_t position = 0;
	std::vector<std::shared_ptr<const Node>> nodes; ///< ordre préfixe, racine en tête
	std::vector<Link> links;
};

inline constexpr size_t kAppend = SIZE_MAX;

/// Accès en lecture au contenu d'un document (partagé par `Document` et les instantanés).
class DocumentView {
public:
	[[nodiscard]] const Node *Find(NodeId id) const noexcept;
	[[nodiscard]] bool Contains(NodeId id) const noexcept { return Find(id) != nullptr; }
	[[nodiscard]] NodeId Root() const noexcept { return m_root; }
	[[nodiscard]] NodeId GroupsRoot() const noexcept { return m_groupsRoot; }
	[[nodiscard]] size_t NodeCount() const noexcept { return m_nodes.Size(); }
	[[nodiscard]] std::vector<NodeId> Sections() const;
	/// Corps (BodyNode) d'une section ; kNoNode si `section` n'en est pas une.
	[[nodiscard]] NodeId Body(NodeId section) const noexcept;
	/// Enfant direct de `parent` du type demandé (le premier) ; kNoNode sinon.
	[[nodiscard]] NodeId ChildOfType(NodeId parent, NodeType type) const noexcept;
	/// Ancêtre le plus proche du type demandé (le nœud lui-même exclu) ; kNoNode sinon.
	[[nodiscard]] NodeId AncestorOfType(NodeId id, NodeType type) const noexcept;
	/// `ancestor` est-il un ancêtre strict de `id` ?
	[[nodiscard]] bool IsAncestor(NodeId ancestor, NodeId id) const noexcept;
	[[nodiscard]] size_t Depth(NodeId id) const noexcept;

	/// Parcours préfixe depuis `from` (inclus). `fn(const Node &)` retourne `bool` (faux :
	/// ne pas descendre sous ce nœud) ou `void`.
	template <typename Fn> void Visit(NodeId from, Fn &&fn) const;
	/// Identifiants de `from` et de ses descendants, ordre préfixe.
	[[nodiscard]] std::vector<NodeId> SubtreeOf(NodeId from) const;
	[[nodiscard]] std::vector<NodeId> FindAll(NodeType type) const;
	[[nodiscard]] std::vector<NodeId> FindByName(std::string_view name) const;
	[[nodiscard]] std::vector<NodeId> AllGroups() const;

	/// Valeur résolue (cascade complète, sans cache, utilisable depuis n'importe quel fil sur un instantané).
	[[nodiscard]] Resolved Resolve(NodeId id, PropertyId property) const;
	template <typename T> [[nodiscard]] T Get(NodeId id, Prop<T> p) const { return Prop<T>::FromValue(Resolve(id, p.id).value); }
	/// Valeur explicite du nœud uniquement (nullptr si absente).
	[[nodiscard]] const PropertyValue *Explicit(NodeId id, PropertyId property) const noexcept;
	/// Groupes consultés pour `id`, dans l'ordre de la cascade (groupes ancêtres compris).
	[[nodiscard]] std::vector<NodeId> GroupOrder(NodeId id) const;

	[[nodiscard]] const ImageResource *FindResource(ResourceId id) const noexcept;
	[[nodiscard]] std::vector<ResourceId> Resources() const;
	[[nodiscard]] GroupResolution Resolution() const noexcept { return m_resolution; }
	/// Révision globale : change à chaque modification.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	/// Révision de style : change à chaque modification pouvant affecter la valeur résolue d'un
	/// *autre* nœud (propriété héritable, groupe, document, appartenance, structure). Une propriété
	/// non héritable d'un nœud ordinaire ne change que la révision de ce nœud (`Node::Revision`).
	[[nodiscard]] uint64_t StyleRevision() const noexcept { return m_styleRevision; }
	/// Révision du style partagé : propriétés des groupes et du nœud Document, politique et
	/// hiérarchie des groupes — modifications pouvant toucher n'importe quel bloc. Le reste
	/// (style d'un paragraphe, d'une cellule, d'une section…) est suivi nœud par nœud
	/// (`Node::InheritedStyleRevision`) : le cache de mise en page n'invalide que les blocs touchés.
	[[nodiscard]] uint64_t SharedStyleRevision() const noexcept { return m_sharedStyleRevision; }
	/// Identité du document (partagée par ses instantanés).
	[[nodiscard]] uint64_t Stamp() const noexcept { return m_stamp; }
	/// Prochain identifiant attribué (les identifiants ne sont jamais réutilisés).
	[[nodiscard]] NodeId NextId() const noexcept { return m_nextId; }

protected:
	DocumentView() = default;

	core::RobinMap<NodeId, std::shared_ptr<Node>> m_nodes;
	core::RobinMap<ResourceId, std::shared_ptr<const ImageResource>> m_resources;
	NodeId m_root = kNoNode, m_groupsRoot = kNoNode;
	NodeId m_nextId = 1;
	ResourceId m_nextResource = 1;
	GroupResolution m_resolution = GroupResolution::PriorityFirst;
	uint64_t m_revision = 0, m_styleRevision = 0, m_stamp = 0;
	uint64_t m_sharedStyleRevision = 0;

private:
	[[nodiscard]] Resolved ResolveFrom(const Node &node, PropertyId property, NodeType target, bool viaParent) const;
	[[nodiscard]] Resolved ResolveGroup(const Node &group, PropertyId property) const;
};

/// Instantané immuable d'un document.
using Snapshot = std::shared_ptr<const DocumentView>;

/// Cache des valeurs résolues d'un document ou de ses instantanés. Vidé quand la révision de
/// style (ou l'identité) de la vue change ; une entrée est recalculée si la révision de son nœud
/// a changé. Non synchronisé : un cache par fil.
class PropertyCache {
public:
	[[nodiscard]] const Resolved &Get(const DocumentView &view, NodeId id, PropertyId property);
	template <typename T> [[nodiscard]] T Get(const DocumentView &view, NodeId id, Prop<T> p) {
		// Propriété non héritée : explicite, groupes du nœud, document, défaut — moins cher à
		// résoudre qu'à mettre en cache (les grands tableaux en lisent des centaines de milliers).
		if (!Info(p.id).inheritable)
			return Prop<T>::FromValue(view.Resolve(id, p.id).value);
		return Prop<T>::FromValue(Get(view, id, p.id).value);
	}
	[[nodiscard]] size_t Size() const noexcept { return m_entries.Size(); }
	[[nodiscard]] uint64_t Hits() const noexcept { return m_hits; }
	[[nodiscard]] uint64_t Misses() const noexcept { return m_misses; }
	void Clear() noexcept { m_entries.Clear(); }

private:
	struct Entry {
		Resolved value;
		uint64_t nodeRevision = 0;
	};
	core::RobinMap<uint64_t, Entry> m_entries;
	uint64_t m_stamp = 0, m_styleRevision = UINT64_MAX;
	uint64_t m_hits = 0, m_misses = 0;
};

/// Document modifiable. Construit avec sa structure minimale : Document, Groups, une Section
/// (son Body et son groupe automatique de pages).
class Document : public DocumentView {
public:
	Document();
	Document(const Document &) = delete;
	Document &operator=(const Document &) = delete;
	Document(Document &&) noexcept = default;
	Document &operator=(Document &&) noexcept = default;

	// ── Arborescence ──
	/// Crée un nœud vide sous `parent` (à `position`, ou à la fin). Les Section reçoivent leur
	/// Body et leur groupe de pages ; les pages se créent par `AddPage` (pagination).
	DocResult<NodeId> CreateNode(NodeType type, NodeId parent, size_t position = kAppend);
	/// Supprime un nœud et ses descendants (refusé pour les nœuds structurels et la dernière section).
	Status DeleteNode(NodeId id);
	/// Retire un sous-arbre en le conservant pour une réinsertion exacte (`InsertSubtree`).
	DocResult<Subtree> RemoveSubtree(NodeId id);
	/// Réinsère un sous-arbre retiré (identifiants, positions et appartenances restaurés).
	Status InsertSubtree(const Subtree &tree);
	/// Déplace un nœud (contrôle des cycles, des enfants admis et des verrous).
	Status MoveNode(NodeId id, NodeId parent, size_t position = kAppend);
	/// Copie un nœud et ses descendants sous `parent` (nouveaux identifiants, propriétés et
	/// appartenances non automatiques copiées). Rend l'identifiant de la copie.
	DocResult<NodeId> Duplicate(NodeId id, NodeId parent, size_t position = kAppend);

	// ── Propriétés ──
	Status SetProperty(NodeId id, PropertyId property, PropertyValue value);
	template <typename T> Status Set(NodeId id, Prop<T> p, const T &value) { return SetProperty(id, p.id, Prop<T>::ToValue(value)); }
	Status Set(NodeId id, Prop<std::string> p, std::string_view value) { return SetProperty(id, p.id, std::string(value)); }
	/// Retire la valeur explicite (retour à la cascade).
	Status ClearProperty(NodeId id, PropertyId property);

	// ── Groupes ──
	/// Crée un groupe (sous le conteneur des groupes, ou sous `parentGroup`).
	DocResult<NodeId> CreateGroup(std::string_view name, int64_t priority = 0, NodeId parentGroup = kNoNode);
	/// Ajoute `node` au groupe ; `groupIndex` / `memberIndex` : rang dans les groupes du nœud et
	/// dans les membres du groupe (fin par défaut ; une annulation restaure le rang d'origine).
	Status AddToGroup(NodeId node, NodeId group, bool automatic = false, size_t groupIndex = kAppend, size_t memberIndex = kAppend);
	Status RemoveFromGroup(NodeId node, NodeId group);
	void SetResolution(GroupResolution resolution);

	// ── Pages (réservé à la pagination : pas de contrôle de verrou, nœuds « générés ») ──
	/// Ajoute une page générée à la fin de la section ; elle rejoint le groupe de pages de la section.
	DocResult<NodeId> AddPage(NodeId section);
	/// Supprime une page générée (refusé pour un autre nœud).
	Status RemovePage(NodeId page);

	// ── Ressources ──
	ResourceId AddResource(ImageResource image);

	// ── Instantanés et notifications ──
	[[nodiscard]] Snapshot TakeSnapshot() const;
	[[nodiscard]] events::Signal<const Change &> &Changed() noexcept { return m_changed; }

private:
	friend class DocumentBuilder;
	struct Empty {};
	explicit Document(Empty);

	Node &Mutable(NodeId id);
	NodeId Allocate(NodeType type, NodeId parent, size_t position, bool generated);
	void Touch(NodeId id);
	void Notify(ChangeKind kind, NodeId node, PropertyId property = PropertyId::Count);
	void StyleChanged() noexcept { ++m_styleRevision; }
	[[nodiscard]] bool AffectsOthers(NodeId id, PropertyId property) const;
	/// Le style transmis par `id` a changé (propriété héritable ou appartenance).
	void InheritedStyleChanged(NodeId id);
	[[nodiscard]] Status CheckUnlocked(NodeId id) const;
	[[nodiscard]] Status CheckInsertion(NodeType type, NodeId parent, NodeId ignore) const;
	NodeId CreateSectionParts(NodeId section);

	events::Signal<const Change &> m_changed;
};

template <typename Fn> void DocumentView::Visit(NodeId from, Fn &&fn) const {
	std::vector<NodeId> stack;
	if (Contains(from))
		stack.push_back(from);
	while (!stack.empty()) {
		const Node *node = Find(stack.back());
		stack.pop_back();
		bool descend = true;
		if constexpr (std::is_same_v<std::invoke_result_t<Fn &, const Node &>, bool>)
			descend = fn(*node);
		else
			fn(*node);
		if (!descend)
			continue;
		const auto children = node->Children();
		for (size_t i = children.size(); i-- > 0;)
			stack.push_back(children[i]);
	}
}

} // namespace doc
