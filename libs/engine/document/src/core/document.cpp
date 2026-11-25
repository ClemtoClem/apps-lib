#include "document/document.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <functional>

namespace doc {

namespace {

std::atomic<uint64_t> g_nextStamp{0};

template <typename T> void InsertAt(std::vector<T> &v, size_t position, T value) {
	v.insert(v.begin() + std::ptrdiff_t(std::min(position, v.size())), std::move(value));
}

bool InSorted(const std::vector<NodeId> &sorted, NodeId id) { return std::ranges::binary_search(sorted, id); }

} // namespace

Document::Document(Empty) { m_stamp = ++g_nextStamp; }

Document::Document() : Document(Empty{}) {
	m_root = Allocate(NodeType::Document, kNoNode, kAppend, false);
	m_groupsRoot = Allocate(NodeType::Groups, m_root, kAppend, false);
	CreateSectionParts(Allocate(NodeType::Section, m_root, kAppend, false));
}

// ── Outils internes ──

Node &Document::Mutable(NodeId id) {
	std::shared_ptr<Node> &slot = *m_nodes.Find(id);
	if (slot.use_count() > 1) // partagé avec un instantané : copie sur écriture
		slot = std::make_shared<Node>(*slot);
	return *slot;
}

NodeId Document::Allocate(NodeType type, NodeId parent, size_t position, bool generated) {
	const NodeId id = m_nextId++;
	auto node = std::make_shared<Node>(id, type);
	node->m_parent = parent;
	node->m_generated = generated;
	m_nodes.InsertOrAssign(id, std::move(node));
	if (parent != kNoNode)
		InsertAt(Mutable(parent).m_children, position, id);
	Touch(id);
	return id;
}

void Document::Touch(NodeId id) {
	const uint64_t revision = ++m_revision;
	for (NodeId cur = id; cur != kNoNode && Contains(cur);) {
		Node &n = Mutable(cur);
		n.m_revision = revision;
		cur = n.m_parent;
	}
}

bool Document::AffectsOthers(NodeId id, PropertyId property) const {
	// Une propriété non héritable d'un nœud ordinaire ne change que la résolution de ce nœud,
	// dont la révision suffit à invalider les caches (PropertyCache, mesures de mise en page).
	const NodeType type = Find(id)->Type();
	return Info(property).inheritable || type == NodeType::Group || type == NodeType::Document || property == PropertyId::Priority;
}

void Document::InheritedStyleChanged(NodeId id) {
	Node &n = Mutable(id);
	n.m_styleRevision = m_revision;
	if (n.Type() == NodeType::Group || n.Type() == NodeType::Document)
		++m_sharedStyleRevision; // valeurs reçues par des membres ou par tout le document
}

void Document::Notify(ChangeKind kind, NodeId node, PropertyId property) { m_changed.Emit(Change{kind, node, property}); }

Status Document::CheckUnlocked(NodeId id) const {
	if (Get(id, props::Locked))
		return Failure(ErrorCode::Locked, std::format("nœud {} verrouillé", id));
	return Success();
}

Status Document::CheckInsertion(NodeType type, NodeId parent, NodeId ignore) const {
	const Node *p = Find(parent);
	if (!p)
		return Failure(ErrorCode::NotFound, std::format("parent {} introuvable", parent));
	if (!CanContain(p->Type(), type))
		return Failure(ErrorCode::InvalidParent, std::format("{} n'accepte pas d'enfant {}", NodeTypeName(p->Type()), NodeTypeName(type)));
	if (TypeInfo(type).uniqueInParent)
		for (NodeId c : p->Children())
			if (c != ignore && Find(c)->Type() == type)
				return Failure(ErrorCode::InvalidParent, std::format("{} a déjà un enfant {}", NodeTypeName(p->Type()), NodeTypeName(type)));
	return Success();
}

NodeId Document::CreateSectionParts(NodeId section) {
	Allocate(NodeType::Body, section, kAppend, false);
	const NodeId group = Allocate(NodeType::Group, m_groupsRoot, kAppend, false);
	Mutable(group).m_properties.Set(PropertyId::Name, std::format("Pages de la section {}", Sections().size()));
	Mutable(section).m_properties.Set(PropertyId::PageGroup, int64_t(group));
	StyleChanged();
	return group;
}

// ── Arborescence ──

DocResult<NodeId> Document::CreateNode(NodeType type, NodeId parent, size_t position) {
	if (size_t(type) >= kNodeTypeCount)
		return Failure(ErrorCode::InvalidValue, "type de nœud inconnu");
	if (type == NodeType::Page)
		return Failure(ErrorCode::Unsupported, "les pages sont produites par la pagination (AddPage)");
	if (TypeInfo(type).structural)
		return Failure(ErrorCode::Protected, std::format("{} est créé avec son parent", NodeTypeName(type)));
	if (auto s = CheckInsertion(type, parent, kNoNode); !s)
		return Err(s.Error());
	if (auto s = CheckUnlocked(parent); !s)
		return Err(s.Error());
	const NodeId id = Allocate(type, parent, position, false);
	if (type == NodeType::Section)
		CreateSectionParts(id);
	StyleChanged();
	Notify(ChangeKind::Created, id);
	return Ok(id);
}

Status Document::DeleteNode(NodeId id) {
	auto removed = RemoveSubtree(id);
	if (!removed)
		return Err(removed.Error());
	return Success();
}

DocResult<Subtree> Document::RemoveSubtree(NodeId id) {
	const Node *node = Find(id);
	if (!node)
		return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", id));
	if (TypeInfo(node->Type()).structural)
		return Failure(ErrorCode::Protected, std::format("{} ne peut pas être supprimé", NodeTypeName(node->Type())));
	if (node->Type() == NodeType::Page)
		return Failure(ErrorCode::Protected, "les pages sont gérées par la pagination");
	if (node->Type() == NodeType::Section && Sections().size() <= 1)
		return Failure(ErrorCode::Protected, "un document garde au moins une section");
	if (auto s = CheckUnlocked(id); !s)
		return Err(s.Error());

	std::vector<NodeId> ids = SubtreeOf(id);
	for (NodeId n : ids)
		if (const auto *v = Explicit(n, PropertyId::Locked); v && std::get<bool>(*v))
			return Failure(ErrorCode::Locked, std::format("le sous-arbre contient le nœud verrouillé {}", n));

	Subtree tree;
	tree.parent = node->Parent();
	tree.position = Find(tree.parent)->IndexOfChild(id);
	tree.nodes.reserve(ids.size());
	for (NodeId n : ids)
		tree.nodes.push_back(*m_nodes.Find(n));

	std::vector<NodeId> sorted = ids;
	std::ranges::sort(sorted);
	// Appartenances traversant la frontière, relevées avant toute modification.
	for (const auto &shared : tree.nodes) {
		const Node &n = *shared;
		const auto groups = n.Groups();
		for (size_t gi = 0; gi < groups.size(); ++gi)
			if (!InSorted(sorted, groups[gi].group)) {
				const auto members = Find(groups[gi].group)->Members();
				const size_t mi = size_t(std::ranges::find(members, n.Id()) - members.begin());
				tree.links.push_back({n.Id(), groups[gi].group, mi, gi, groups[gi].automatic});
			}
		const auto members = n.Members();
		for (size_t mi = 0; mi < members.size(); ++mi)
			if (!InSorted(sorted, members[mi])) {
				const auto memberGroups = Find(members[mi])->Groups();
				const auto it = std::ranges::find(memberGroups, n.Id(), &Membership::group);
				tree.links.push_back({members[mi], n.Id(), mi, size_t(it - memberGroups.begin()), it->automatic});
			}
	}
	for (const Subtree::Link &l : tree.links) {
		if (!InSorted(sorted, l.group))
			std::erase(Mutable(l.group).m_members, l.member);
		if (!InSorted(sorted, l.member)) {
			std::erase_if(Mutable(l.member).m_groups, [&](const Membership &m) { return m.group == l.group; });
			Touch(l.member);
		}
	}
	std::erase(Mutable(tree.parent).m_children, id);
	for (NodeId n : ids)
		m_nodes.Erase(n);
	Touch(tree.parent);
	StyleChanged();
	if (std::ranges::any_of(tree.nodes, [](const auto &n) { return n->Type() == NodeType::Group; }))
		++m_sharedStyleRevision; // les membres perdent les valeurs de ces groupes
	Notify(ChangeKind::Deleted, id);
	return Ok(std::move(tree));
}

Status Document::InsertSubtree(const Subtree &tree) {
	if (tree.nodes.empty() || !tree.nodes.front())
		return Failure(ErrorCode::InvalidValue, "sous-arbre vide");
	const Node &top = *tree.nodes.front();
	if (top.Parent() != tree.parent)
		return Failure(ErrorCode::InvalidValue, "racine du sous-arbre incohérente");
	if (auto s = CheckInsertion(top.Type(), tree.parent, kNoNode); !s)
		return s;
	if (auto s = CheckUnlocked(tree.parent); !s)
		return s;
	std::vector<NodeId> sorted;
	sorted.reserve(tree.nodes.size());
	for (const auto &n : tree.nodes) {
		if (!n || Contains(n->Id()))
			return Failure(ErrorCode::InvalidValue, "identifiant déjà présent dans le document");
		sorted.push_back(n->Id());
	}
	std::ranges::sort(sorted);
	for (const Subtree::Link &l : tree.links) {
		const bool groupInside = InSorted(sorted, l.group), memberInside = InSorted(sorted, l.member);
		if ((!groupInside && !Contains(l.group)) || (!memberInside && !Contains(l.member)))
			return Failure(ErrorCode::NotFound, "appartenance vers un nœud disparu");
	}

	for (const auto &n : tree.nodes)
		m_nodes.InsertOrAssign(n->Id(), std::make_shared<Node>(*n));
	InsertAt(Mutable(tree.parent).m_children, tree.position, top.Id());
	m_nextId = std::max(m_nextId, sorted.back() + 1);

	// Réinsertion par positions croissantes : reproduit l'ordre d'origine.
	std::vector<const Subtree::Link *> links;
	for (const Subtree::Link &l : tree.links)
		links.push_back(&l);
	std::ranges::sort(links, {}, &Subtree::Link::memberIndex);
	for (const Subtree::Link *l : links)
		if (!InSorted(sorted, l->group))
			InsertAt(Mutable(l->group).m_members, l->memberIndex, l->member);
	std::ranges::sort(links, {}, &Subtree::Link::groupIndex);
	for (const Subtree::Link *l : links)
		if (!InSorted(sorted, l->member)) {
			InsertAt(Mutable(l->member).m_groups, l->groupIndex, Membership{l->group, l->automatic});
			Touch(l->member);
		}
	Touch(top.Id());
	StyleChanged();
	if (std::ranges::any_of(tree.nodes, [](const auto &n) { return n->Type() == NodeType::Group; }))
		++m_sharedStyleRevision;
	Notify(ChangeKind::Created, top.Id());
	return Success();
}

Status Document::MoveNode(NodeId id, NodeId parent, size_t position) {
	const Node *node = Find(id);
	if (!node)
		return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", id));
	if (TypeInfo(node->Type()).structural || node->Type() == NodeType::Page)
		return Failure(ErrorCode::Protected, std::format("{} ne peut pas être déplacé", NodeTypeName(node->Type())));
	if (parent == id || IsAncestor(id, parent))
		return Failure(ErrorCode::Cycle, std::format("{} ne peut pas devenir son propre descendant", id));
	if (auto s = CheckInsertion(node->Type(), parent, id); !s)
		return s;
	if (auto s = CheckUnlocked(id); !s)
		return s;
	if (auto s = CheckUnlocked(parent); !s)
		return s;

	const NodeId oldParent = node->Parent();
	const size_t oldIndex = Find(oldParent)->IndexOfChild(id);
	if (oldParent == parent && position != kAppend && position > oldIndex)
		--position; // la position est exprimée avant le retrait
	std::erase(Mutable(oldParent).m_children, id);
	InsertAt(Mutable(parent).m_children, position, id);
	Mutable(id).m_parent = parent;
	Touch(oldParent);
	Touch(id);
	StyleChanged();
	if (Find(id)->Type() == NodeType::Group)
		++m_sharedStyleRevision; // hiérarchie des groupes modifiée
	Notify(ChangeKind::Moved, id);
	return Success();
}

DocResult<NodeId> Document::Duplicate(NodeId id, NodeId parent, size_t position) {
	const Node *source = Find(id);
	if (!source)
		return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", id));
	if (TypeInfo(source->Type()).structural || source->Type() == NodeType::Page || source->Type() == NodeType::Section)
		return Failure(ErrorCode::Protected, std::format("{} ne peut pas être dupliqué", NodeTypeName(source->Type())));
	if (auto s = CheckInsertion(source->Type(), parent, kNoNode); !s)
		return Err(s.Error());
	if (auto s = CheckUnlocked(parent); !s)
		return Err(s.Error());
	// Copie récursive : chaque nœud reçoit un nouvel identifiant ; les liens internes au
	// sous-arbre (groupes imbriqués) sont recopiés, les membres d'un groupe copié ne le sont pas.
	const std::function<NodeId(NodeId, NodeId, size_t)> copy = [&](NodeId from, NodeId to, size_t at) -> NodeId {
		const Node original = *Find(from); // copie : Allocate peut cloner les nœuds partagés
		const NodeId clone = Allocate(original.Type(), to, at, false);
		Node &n = Mutable(clone);
		n.m_properties = original.Properties();
		for (const Membership &m : original.Groups())
			if (!m.automatic && Contains(m.group)) {
				n.m_groups.push_back(m);
				Mutable(m.group).m_members.push_back(clone);
			}
		for (NodeId child : original.Children())
			copy(child, clone, kAppend);
		return clone;
	};
	const NodeId root = copy(id, parent, position);
	Touch(root);
	StyleChanged();
	Notify(ChangeKind::Created, root);
	return Ok(root);
}

// ── Propriétés ──

Status Document::SetProperty(NodeId id, PropertyId property, PropertyValue value) {
	const Node *node = Find(id);
	if (!node)
		return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", id));
	if (auto s = Validate(property, value); !s)
		return s;
	const NodeType type = node->Type();
	if (!AppliesTo(property, type) && type != NodeType::Group && type != NodeType::Document)
		return Failure(ErrorCode::InvalidValue,
					   std::format("{} ne s'applique pas à un nœud {}", Info(property).name, NodeTypeName(type)));
	if (property != PropertyId::Locked)
		if (auto s = CheckUnlocked(id); !s)
			return s;
	if (property == PropertyId::Text && !Get(id, props::Editable))
		return Failure(ErrorCode::Locked, std::format("le texte du nœud {} n'est pas modifiable", id));
	if (property == PropertyId::Resource)
		if (const int64_t r = std::get<int64_t>(value); r != 0 && !FindResource(ResourceId(r)))
			return Failure(ErrorCode::NotFound, std::format("ressource {} introuvable", r));
	if (property == PropertyId::PageGroup) {
		const Node *g = Find(NodeId(std::get<int64_t>(value)));
		if (!g || g->Type() != NodeType::Group)
			return Failure(ErrorCode::InvalidValue, "PageGroup doit désigner un groupe");
	}
	if (const PropertyValue *current = node->Properties().Find(property); current && *current == value)
		return Success();
	Mutable(id).m_properties.Set(property, std::move(value));
	Touch(id);
	if (AffectsOthers(id, property)) {
		StyleChanged();
		if (Info(property).cascades)
			InheritedStyleChanged(id);
	}
	Notify(ChangeKind::Property, id, property);
	return Success();
}

Status Document::ClearProperty(NodeId id, PropertyId property) {
	const Node *node = Find(id);
	if (!node)
		return Failure(ErrorCode::NotFound, std::format("nœud {} introuvable", id));
	if (size_t(property) >= kPropertyCount)
		return Failure(ErrorCode::NotFound, "propriété inconnue");
	if (!node->Properties().Contains(property))
		return Success();
	if (property != PropertyId::Locked)
		if (auto s = CheckUnlocked(id); !s)
			return s;
	Mutable(id).m_properties.Erase(property);
	Touch(id);
	if (AffectsOthers(id, property)) {
		StyleChanged();
		if (Info(property).cascades)
			InheritedStyleChanged(id);
	}
	Notify(ChangeKind::Property, id, property);
	return Success();
}

// ── Groupes ──

DocResult<NodeId> Document::CreateGroup(std::string_view name, int64_t priority, NodeId parentGroup) {
	const NodeId parent = parentGroup == kNoNode ? m_groupsRoot : parentGroup;
	if (auto s = CheckInsertion(NodeType::Group, parent, kNoNode); !s)
		return Err(s.Error());
	if (auto s = Validate(PropertyId::Priority, priority); !s)
		return Err(s.Error());
	const NodeId id = Allocate(NodeType::Group, parent, kAppend, false);
	Node &g = Mutable(id);
	g.m_properties.Set(PropertyId::Name, std::string(name));
	if (priority != 0)
		g.m_properties.Set(PropertyId::Priority, priority);
	StyleChanged();
	Notify(ChangeKind::Created, id);
	return Ok(id);
}

Status Document::AddToGroup(NodeId node, NodeId group, bool automatic, size_t groupIndex, size_t memberIndex) {
	const Node *n = Find(node);
	const Node *g = Find(group);
	if (!n || !g)
		return Failure(ErrorCode::NotFound, "nœud ou groupe introuvable");
	if (g->Type() != NodeType::Group)
		return Failure(ErrorCode::InvalidValue, std::format("{} n'est pas un groupe", group));
	if (n->Type() == NodeType::Document || n->Type() == NodeType::Groups || n->Type() == NodeType::Group)
		return Failure(ErrorCode::InvalidValue, "les groupes s'imbriquent par l'arborescence, pas par appartenance");
	if (n->InGroup(group))
		return Failure(ErrorCode::Nothing, std::format("{} appartient déjà au groupe {}", node, group));
	if (auto s = CheckUnlocked(node); !s)
		return s;
	InsertAt(Mutable(node).m_groups, groupIndex, Membership{group, automatic});
	InsertAt(Mutable(group).m_members, memberIndex, node);
	Touch(node);
	InheritedStyleChanged(node);
	StyleChanged();
	Notify(ChangeKind::Membership, node);
	return Success();
}

Status Document::RemoveFromGroup(NodeId node, NodeId group) {
	const Node *n = Find(node);
	if (!n || !n->InGroup(group))
		return Failure(ErrorCode::NotFound, std::format("{} n'appartient pas au groupe {}", node, group));
	if (auto s = CheckUnlocked(node); !s)
		return s;
	std::erase_if(Mutable(node).m_groups, [group](const Membership &m) { return m.group == group; });
	std::erase(Mutable(group).m_members, node);
	Touch(node);
	InheritedStyleChanged(node);
	StyleChanged();
	Notify(ChangeKind::Membership, node);
	return Success();
}

void Document::SetResolution(GroupResolution resolution) {
	if (resolution == m_resolution)
		return;
	m_resolution = resolution;
	++m_revision;
	++m_sharedStyleRevision;
	StyleChanged();
	Notify(ChangeKind::Structure, m_root);
}

// ── Pages ──

DocResult<NodeId> Document::AddPage(NodeId section) {
	const Node *s = Find(section);
	if (!s || s->Type() != NodeType::Section)
		return Failure(ErrorCode::InvalidParent, std::format("{} n'est pas une section", section));
	const NodeId page = Allocate(NodeType::Page, section, kAppend, true);
	if (const auto *g = Explicit(section, PropertyId::PageGroup)) {
		const NodeId group = NodeId(std::get<int64_t>(*g));
		if (const Node *gn = Find(group); gn && gn->Type() == NodeType::Group) {
			Mutable(page).m_groups.push_back({group, true});
			Mutable(group).m_members.push_back(page);
		}
	}
	StyleChanged();
	Notify(ChangeKind::Created, page);
	return Ok(page);
}

Status Document::RemovePage(NodeId page) {
	const Node *p = Find(page);
	if (!p || p->Type() != NodeType::Page || !p->Generated())
		return Failure(ErrorCode::InvalidValue, std::format("{} n'est pas une page générée", page));
	if (!p->Children().empty()) // contenu propre de l'utilisateur : jamais supprimé silencieusement
		return Failure(ErrorCode::Protected, std::format("la page {} porte du contenu propre", page));
	const std::vector<NodeId> ids = SubtreeOf(page);
	for (NodeId id : ids) {
		const Node &n = *Find(id);
		for (const Membership &m : n.Groups())
			if (Contains(m.group))
				std::erase(Mutable(m.group).m_members, id);
	}
	const NodeId parent = p->Parent();
	std::erase(Mutable(parent).m_children, page);
	for (NodeId id : ids)
		m_nodes.Erase(id);
	Touch(parent);
	StyleChanged();
	Notify(ChangeKind::Deleted, page);
	return Success();
}

// ── Ressources ──

ResourceId Document::AddResource(ImageResource image) {
	const ResourceId id = m_nextResource++;
	m_resources.InsertOrAssign(id, std::make_shared<const ImageResource>(std::move(image)));
	++m_revision;
	Notify(ChangeKind::Resource, id);
	return id;
}

Snapshot Document::TakeSnapshot() const { return std::make_shared<const DocumentView>(static_cast<const DocumentView &>(*this)); }

} // namespace doc
