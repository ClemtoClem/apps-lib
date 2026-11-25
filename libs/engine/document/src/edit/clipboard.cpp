// Presse-papiers des nœuds : copie indépendante des identifiants et collage annulable.
#include <algorithm>

#include "document/commands.hpp"

namespace doc {

namespace {

bool Copyable(NodeType t) {
	return !TypeInfo(t).structural && t != NodeType::Section && t != NodeType::Page && t != NodeType::Group;
}

NodeFragment::Item CopyItem(const DocumentView &view, const Node &n) {
	NodeFragment::Item item;
	item.type = n.Type();
	for (const auto &[p, v] : n.Properties())
		if (p != PropertyId::Name) // un nom identifie le nœud d'origine
			item.properties.Set(p, v);
	for (const Membership &m : n.Groups())
		if (!m.automatic)
			item.groups.push_back(m.group);
	for (NodeId c : n.Children())
		if (const Node *child = view.Find(c))
			item.children.push_back(CopyItem(view, *child));
	return item;
}

void Text(const NodeFragment::Item &item, std::string &out, bool &first) {
	if (item.type == NodeType::Paragraph) {
		if (!first)
			out.push_back('\n');
		first = false;
	}
	if (item.type == NodeType::Text)
		if (const auto *v = item.properties.Find(PropertyId::Text))
			out += std::get<std::string>(*v);
	for (const auto &c : item.children)
		Text(c, out, first);
}

Status PasteItem(History &h, const NodeFragment::Item &item, NodeId parent, size_t position, NodeId *created) {
	auto command = CreateNode(item.type, parent, position);
	Command *raw = command.get();
	if (auto s = h.Execute(std::move(command)); !s)
		return s;
	const NodeId id = raw->Created();
	if (created)
		*created = id;
	for (const auto &[p, v] : item.properties)
		if (auto s = h.Execute(SetProperty(id, p, v)); !s)
			return s;
	for (NodeId g : item.groups)
		if (h.Target().Contains(g) && h.Target().Find(g)->Type() == NodeType::Group)
			if (auto s = h.Execute(AddToGroup(id, g)); !s)
				return s;
	for (const auto &c : item.children)
		if (auto s = PasteItem(h, c, id, kAppend, nullptr); !s)
			return s;
	return Success();
}

} // namespace

std::string NodeFragment::PlainText() const {
	std::string out;
	bool first = true;
	for (const Item &r : roots)
		Text(r, out, first);
	return out;
}

NodeFragment CopyNodes(const DocumentView &view, std::span<const NodeId> ids) {
	NodeFragment f;
	// Ordre du document, nœuds couverts par un ancêtre copié écartés.
	std::vector<NodeId> ordered;
	view.Visit(view.Root(), [&](const Node &n) {
		if (std::ranges::find(ids, n.Id()) == ids.end() || !Copyable(n.Type()))
			return true;
		ordered.push_back(n.Id());
		return false; // descendants inclus dans la copie
	});
	for (NodeId id : ordered)
		f.roots.push_back(CopyItem(view, *view.Find(id)));
	return f;
}

DocResult<std::vector<NodeId>> PasteNodes(History &history, const NodeFragment &fragment, NodeId parent, size_t position) {
	if (fragment.Empty())
		return Failure(ErrorCode::Nothing, "presse-papiers vide");
	const bool own = !history.InTransaction();
	if (own)
		history.BeginTransaction("Coller");
	std::vector<NodeId> created;
	Status status = Success();
	for (const NodeFragment::Item &item : fragment.roots) {
		NodeId id = kNoNode;
		status = PasteItem(history, item, parent, position, &id);
		if (!status)
			break;
		created.push_back(id);
		if (position != kAppend)
			++position;
	}
	if (own) {
		if (!status) {
			(void)history.Rollback();
			return Err(status.Error());
		}
		if (auto s = history.Commit(); !s)
			return Err(s.Error());
	} else if (!status) {
		return Err(status.Error());
	}
	return Ok(std::move(created));
}

} // namespace doc
