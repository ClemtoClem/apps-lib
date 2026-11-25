#include "document/selection.hpp"

#include <algorithm>

namespace doc {

bool Selection::CanSelect(const DocumentView &view, NodeId id, bool allowHidden) {
	const Node *n = view.Find(id);
	if (!n || TypeInfo(n->Type()).structural)
		return false;
	if (!view.Get(id, props::Selectable))
		return false;
	if (!allowHidden && n->Type() != NodeType::Group) {
		// Masqué : le nœud ou l'un de ses ancêtres (Visible n'est pas héritable).
		for (const Node *a = n; a; a = view.Find(a->Parent()))
			if (AppliesTo(PropertyId::Visible, a->Type()) && !view.Get(a->Id(), props::Visible))
				return false;
	}
	return true;
}

bool Selection::Contains(NodeId id) const noexcept { return std::ranges::find(m_nodes, id) != m_nodes.end(); }

void Selection::Clear() noexcept {
	if (!m_nodes.empty()) {
		m_nodes.clear();
		++m_revision;
	}
}

size_t Selection::Apply(const std::vector<NodeId> &ids, SelectMode mode) {
	size_t changed = 0;
	if (mode == SelectMode::Replace) {
		const bool same = ids == m_nodes;
		if (same)
			return 0;
		changed = m_nodes.size() + ids.size();
		m_nodes.clear();
		for (NodeId id : ids)
			if (!Contains(id))
				m_nodes.push_back(id);
	} else {
		for (NodeId id : ids) {
			const auto it = std::ranges::find(m_nodes, id);
			const bool present = it != m_nodes.end();
			if ((mode == SelectMode::Add && !present) || (mode == SelectMode::Toggle && !present)) {
				m_nodes.push_back(id);
				++changed;
			} else if ((mode == SelectMode::Remove || mode == SelectMode::Toggle) && present) {
				m_nodes.erase(it);
				++changed;
			}
		}
	}
	if (changed)
		++m_revision;
	return changed;
}

size_t Selection::Select(const DocumentView &view, NodeId id, SelectMode mode, bool allowHidden) {
	if (!CanSelect(view, id, allowHidden))
		return mode == SelectMode::Replace ? (Clear(), 0u) : 0u;
	std::vector<NodeId> ids{id};
	if (view.Get(id, props::SelectViaGroup))
		if (const Node *n = view.Find(id))
			for (const Membership &m : n->Groups())
				if (const Node *g = view.Find(m.group))
					for (NodeId member : g->Members())
						if (member != id && std::ranges::find(ids, member) == ids.end() && CanSelect(view, member, allowHidden))
							ids.insert(ids.end() - 1, member); // le nœud cliqué reste principal (dernier)
	return Apply(ids, mode);
}

size_t Selection::SelectIds(const DocumentView &view, std::span<const NodeId> ids, SelectMode mode, bool allowHidden) {
	std::vector<NodeId> valid;
	for (NodeId id : ids)
		if (CanSelect(view, id, allowHidden) && std::ranges::find(valid, id) == valid.end())
			valid.push_back(id);
	return Apply(valid, mode);
}

size_t Selection::SelectGroup(const DocumentView &view, NodeId group, SelectMode mode) {
	std::vector<NodeId> ids;
	view.Visit(group, [&](const Node &g) {
		if (g.Type() == NodeType::Group)
			for (NodeId member : g.Members())
				if (std::ranges::find(ids, member) == ids.end() && CanSelect(view, member))
					ids.push_back(member);
	});
	return Apply(ids, mode);
}

size_t Selection::SelectType(const DocumentView &view, NodeType type, NodeId scope, SelectMode mode) {
	std::vector<NodeId> ids;
	view.Visit(scope == kNoNode ? view.Root() : scope, [&](const Node &n) {
		if (n.Type() == type && CanSelect(view, n.Id()))
			ids.push_back(n.Id());
	});
	return Apply(ids, mode);
}

size_t Selection::SelectArea(const DocumentView &view, const LayoutResult &layout, size_t page, const Rect &area, SelectMode mode) {
	std::vector<NodeId> ids;
	if (page < layout.pages.size())
		for (const LayoutBox &box : layout.pages[page].boxes) {
			const bool block = box.type == NodeType::Paragraph || box.type == NodeType::Table || box.type == NodeType::Image ||
							   box.type == NodeType::Shape;
			if (block && area.Contains(box.rect) && std::ranges::find(ids, box.node) == ids.end() && CanSelect(view, box.node))
				ids.push_back(box.node);
		}
	return Apply(ids, mode);
}

size_t Selection::Invert(const DocumentView &view) {
	std::vector<NodeId> candidates;
	if (const Node *primary = view.Find(Primary())) {
		if (const Node *parent = view.Find(primary->Parent()))
			for (NodeId c : parent->Children())
				if (CanSelect(view, c))
					candidates.push_back(c);
	} else {
		for (NodeId s : view.Sections())
			if (const Node *body = view.Find(view.Body(s)))
				for (NodeId c : body->Children())
					if (CanSelect(view, c))
						candidates.push_back(c);
	}
	std::vector<NodeId> inverted;
	for (NodeId c : candidates)
		if (!Contains(c))
			inverted.push_back(c);
	return Apply(inverted, SelectMode::Replace);
}

size_t Selection::Prune(const DocumentView &view) {
	const size_t before = m_nodes.size();
	std::erase_if(m_nodes, [&](NodeId id) { return !CanSelect(view, id, true); });
	if (m_nodes.size() != before)
		++m_revision;
	return before - m_nodes.size();
}

SelectionContext Selection::Context(const DocumentView &view) const {
	SelectionContext c;
	for (NodeId id : m_nodes) {
		const Node *n = view.Find(id);
		if (!n)
			continue;
		++c.count;
		c.types |= MaskOf(n->Type());
		switch (n->Type()) {
		case NodeType::Paragraph:
		case NodeType::Text: c.text = true; break;
		case NodeType::Table:
		case NodeType::TableRow:
		case NodeType::TableCell: c.table = true; break;
		case NodeType::Image: c.image = true; break;
		case NodeType::Shape: c.shape = true; break;
		case NodeType::Page: c.page = true; break;
		case NodeType::Group: c.group = true; break;
		case NodeType::Header:
		case NodeType::Footer: c.headerFooter = true; break;
		default: break;
		}
		if (view.AncestorOfType(id, NodeType::Table) != kNoNode)
			c.table = true;
		if (view.AncestorOfType(id, NodeType::Header) != kNoNode || view.AncestorOfType(id, NodeType::Footer) != kNoNode)
			c.headerFooter = true;
		if (view.AncestorOfType(id, NodeType::Page) != kNoNode)
			c.page = true;
		if (n->Type() != NodeType::Group && view.Get(id, props::Locked))
			c.locked = true;
		if (AppliesTo(PropertyId::Visible, n->Type()) && !view.Get(id, props::Visible))
			c.hidden = true;
	}
	c.mixed = c.types != 0 && (c.types & (c.types - 1)) != 0;
	if (const Node *p = view.Find(Primary())) {
		c.primary = p->Id();
		c.primaryType = p->Type();
		c.enclosingTable = p->Type() == NodeType::Table ? p->Id() : view.AncestorOfType(p->Id(), NodeType::Table);
	}
	return c;
}

} // namespace doc
