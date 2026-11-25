#include "document/document.hpp"

#include <algorithm>

namespace doc {

std::string_view SourceName(ValueSource source) noexcept {
	switch (source) {
	case ValueSource::Explicit: return "Explicit";
	case ValueSource::Group: return "Group";
	case ValueSource::Parent: return "Parent";
	case ValueSource::Document: return "Document";
	case ValueSource::Default: return "Default";
	}
	return "?";
}

const Node *DocumentView::Find(NodeId id) const noexcept {
	const auto *slot = m_nodes.Find(id);
	return slot ? slot->get() : nullptr;
}

std::vector<NodeId> DocumentView::Sections() const {
	std::vector<NodeId> out;
	if (const Node *root = Find(m_root))
		for (NodeId c : root->Children())
			if (const Node *n = Find(c); n && n->Type() == NodeType::Section)
				out.push_back(c);
	return out;
}

NodeId DocumentView::Body(NodeId section) const noexcept {
	const Node *s = Find(section);
	return s && s->Type() == NodeType::Section ? ChildOfType(section, NodeType::Body) : kNoNode;
}

NodeId DocumentView::ChildOfType(NodeId parent, NodeType type) const noexcept {
	if (const Node *p = Find(parent))
		for (NodeId c : p->Children())
			if (const Node *n = Find(c); n && n->Type() == type)
				return c;
	return kNoNode;
}

NodeId DocumentView::AncestorOfType(NodeId id, NodeType type) const noexcept {
	const Node *n = Find(id);
	for (n = n ? Find(n->Parent()) : nullptr; n; n = Find(n->Parent()))
		if (n->Type() == type)
			return n->Id();
	return kNoNode;
}

bool DocumentView::IsAncestor(NodeId ancestor, NodeId id) const noexcept {
	const Node *n = Find(id);
	for (n = n ? Find(n->Parent()) : nullptr; n; n = Find(n->Parent()))
		if (n->Id() == ancestor)
			return true;
	return false;
}

size_t DocumentView::Depth(NodeId id) const noexcept {
	size_t depth = 0;
	for (const Node *n = Find(id); n && n->Parent() != kNoNode; n = Find(n->Parent()))
		++depth;
	return depth;
}

std::vector<NodeId> DocumentView::SubtreeOf(NodeId from) const {
	std::vector<NodeId> out;
	Visit(from, [&](const Node &n) { out.push_back(n.Id()); });
	return out;
}

std::vector<NodeId> DocumentView::FindAll(NodeType type) const {
	std::vector<NodeId> out;
	Visit(m_root, [&](const Node &n) {
		if (n.Type() == type)
			out.push_back(n.Id());
	});
	return out;
}

std::vector<NodeId> DocumentView::FindByName(std::string_view name) const {
	std::vector<NodeId> out;
	Visit(m_root, [&](const Node &n) {
		const auto *v = n.Properties().Find(PropertyId::Name);
		if (const auto *s = v ? std::get_if<std::string>(v) : nullptr; s && *s == name)
			out.push_back(n.Id());
	});
	return out;
}

std::vector<NodeId> DocumentView::AllGroups() const {
	std::vector<NodeId> out;
	Visit(m_groupsRoot, [&](const Node &n) {
		if (n.Type() == NodeType::Group)
			out.push_back(n.Id());
	});
	return out;
}

const PropertyValue *DocumentView::Explicit(NodeId id, PropertyId property) const noexcept {
	const Node *n = Find(id);
	return n ? n->Properties().Find(property) : nullptr;
}

std::vector<NodeId> DocumentView::GroupOrder(NodeId id) const {
	struct Candidate {
		NodeId group;
		size_t distance; ///< 0 : groupe direct ; n : n-ième groupe ancêtre
		size_t order;	 ///< 0 : appartenance la plus récente
		int64_t priority;
	};
	std::vector<Candidate> candidates;
	const Node *node = Find(id);
	if (!node)
		return {};
	const auto groups = node->Groups();
	for (size_t i = 0; i < groups.size(); ++i) {
		const size_t order = groups.size() - 1 - i;
		size_t distance = 0;
		for (const Node *g = Find(groups[i].group); g && g->Type() == NodeType::Group; g = Find(g->Parent()), ++distance) {
			const auto it = std::ranges::find(candidates, g->Id(), &Candidate::group);
			if (it != candidates.end()) {
				if (distance < it->distance || (distance == it->distance && order < it->order)) {
					it->distance = distance;
					it->order = order;
				}
				continue;
			}
			const auto *p = g->Properties().Find(PropertyId::Priority);
			const auto *priority = p ? std::get_if<int64_t>(p) : nullptr;
			candidates.push_back({g->Id(), distance, order, priority ? *priority : 0});
		}
	}
	const bool priorityFirst = m_resolution == GroupResolution::PriorityFirst;
	std::ranges::sort(candidates, [priorityFirst](const Candidate &a, const Candidate &b) {
		if (priorityFirst && a.priority != b.priority)
			return a.priority > b.priority;
		if (a.distance != b.distance)
			return a.distance < b.distance;
		if (a.priority != b.priority)
			return a.priority > b.priority;
		if (a.order != b.order)
			return a.order < b.order;
		return a.group < b.group;
	});
	std::vector<NodeId> out;
	out.reserve(candidates.size());
	for (const Candidate &c : candidates)
		out.push_back(c.group);
	return out;
}

Resolved DocumentView::Resolve(NodeId id, PropertyId property) const {
	if (size_t(property) >= kPropertyCount)
		return {};
	const Node *node = Find(id);
	if (!node)
		return {Info(property).defaultValue, ValueSource::Default, kNoNode};
	if (!Info(property).cascades) { // propriété locale : explicite ou défaut
		if (const PropertyValue *v = node->Properties().Find(property))
			return {*v, ValueSource::Explicit, id};
		return {Info(property).defaultValue, ValueSource::Default, kNoNode};
	}
	if (node->Type() == NodeType::Group)
		return ResolveGroup(*node, property);
	return ResolveFrom(*node, property, node->Type(), false);
}

Resolved DocumentView::ResolveFrom(const Node &node, PropertyId property, NodeType target, bool viaParent) const {
	const PropertyInfo &info = Info(property);
	if (const PropertyValue *v = node.Properties().Find(property))
		return {*v, viaParent ? ValueSource::Parent : ValueSource::Explicit, node.Id()};
	const bool applies = info.inheritable || (info.targets & MaskOf(target)) != 0;
	if (applies && node.Type() != NodeType::Document)
		for (NodeId g : GroupOrder(node.Id()))
			if (const PropertyValue *v = Explicit(g, property))
				return {*v, viaParent ? ValueSource::Parent : ValueSource::Group, g};
	if (info.inheritable && node.Parent() != kNoNode && node.Parent() != m_root)
		if (const Node *parent = Find(node.Parent()))
			return ResolveFrom(*parent, property, parent->Type(), true);
	if (applies && node.Id() != m_root)
		if (const PropertyValue *v = Explicit(m_root, property))
			return {*v, ValueSource::Document, m_root};
	return {info.defaultValue, ValueSource::Default, kNoNode};
}

Resolved DocumentView::ResolveGroup(const Node &group, PropertyId property) const {
	if (const PropertyValue *v = group.Properties().Find(property))
		return {*v, ValueSource::Explicit, group.Id()};
	for (const Node *g = Find(group.Parent()); g && g->Type() == NodeType::Group; g = Find(g->Parent()))
		if (const PropertyValue *v = g->Properties().Find(property))
			return {*v, ValueSource::Group, g->Id()};
	return {Info(property).defaultValue, ValueSource::Default, kNoNode};
}

const ImageResource *DocumentView::FindResource(ResourceId id) const noexcept {
	const auto *slot = m_resources.Find(id);
	return slot ? slot->get() : nullptr;
}

std::vector<ResourceId> DocumentView::Resources() const {
	std::vector<ResourceId> out;
	out.reserve(m_resources.Size());
	for (const auto &[id, res] : m_resources)
		out.push_back(id);
	std::ranges::sort(out);
	return out;
}

// ── PropertyCache ──

static_assert(kPropertyCount <= 256, "la clé du cache réserve 8 bits à la propriété");

const Resolved &PropertyCache::Get(const DocumentView &view, NodeId id, PropertyId property) {
	if (view.Stamp() != m_stamp || view.StyleRevision() != m_styleRevision) {
		m_entries.Clear();
		m_stamp = view.Stamp();
		m_styleRevision = view.StyleRevision();
	}
	const uint64_t key = (id << 8) | uint64_t(property);
	const Node *node = view.Find(id);
	const uint64_t revision = node ? node->Revision() : 0;
	if (Entry *e = m_entries.Find(key); e && e->nodeRevision == revision) {
		++m_hits;
		return e->value;
	}
	++m_misses;
	return m_entries.InsertOrAssign(key, Entry{view.Resolve(id, property), revision}).value;
}


} // namespace doc
