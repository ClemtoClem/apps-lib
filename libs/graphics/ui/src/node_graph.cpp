// Définitions de ui/node_graph.hpp — modèle du graphe et éditeur immédiat.
#include "ui/node_graph.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ui/ui.hpp"

namespace ui {

namespace {

dataio::NodePtr ColorJson(Color c) {
	dataio::NodePtr a = dataio::Node::MakeArray();
	for (const uint8_t v : {c.r, c.g, c.b, c.a})
		a->Push(dataio::Node::MakeInt(int64_t(v)));
	return a;
}

Color JsonColor(const dataio::NodePtr &j, Color fallback) {
	if (!j->IsArray() || j->Size() < 4)
		return fallback;
	return sdl3::Rgba(uint8_t((*j)[0]->AsInt()), uint8_t((*j)[1]->AsInt()), uint8_t((*j)[2]->AsInt()), uint8_t((*j)[3]->AsInt()));
}

float Distance(GraphPoint a, GraphPoint b) { return std::hypot(a.x - b.x, a.y - b.y); }

/// Distance d'un point au segment [a, b].
float SegmentDistance(GraphPoint p, GraphPoint a, GraphPoint b) {
	const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
	const float t = len2 > 0.f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.f, 1.f) : 0.f;
	return Distance(p, {a.x + dx * t, a.y + dy * t});
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// Modèle
// ════════════════════════════════════════════════════════════════════════════

GraphNode &NodeGraph::AddNode(std::string kind, std::string title, GraphPoint pos, Color header) {
	GraphNode node;
	node.id = m_nextId++;
	node.kind = std::move(kind);
	node.title = std::move(title);
	node.pos = pos;
	node.header = header;
	m_nodes.push_back(std::move(node));
	return m_nodes.back();
}

uint32_t NodeGraph::AddPin(uint32_t nodeId, PinKind kind, std::string label, std::string type, PinShape shape, Color color) {
	GraphNode *node = FindNode(nodeId);
	if (!node)
		return 0;
	GraphPin pin{m_nextId++, nodeId, kind, std::move(label), std::move(type), shape, color};
	(kind == PinKind::Input ? node->inputs : node->outputs).push_back(pin.id);
	m_pins.push_back(std::move(pin));
	return m_pins.back().id;
}

uint32_t NodeGraph::AddInput(uint32_t node, std::string label, std::string type, PinShape shape, Color color) {
	return AddPin(node, PinKind::Input, std::move(label), std::move(type), shape, color);
}

uint32_t NodeGraph::AddOutput(uint32_t node, std::string label, std::string type, PinShape shape, Color color) {
	return AddPin(node, PinKind::Output, std::move(label), std::move(type), shape, color);
}

void NodeGraph::RemoveNode(uint32_t nodeId) {
	std::erase_if(m_links, [&](const GraphLink &l) {
		const GraphPin *a = FindPin(l.from), *b = FindPin(l.to);
		return (a && a->node == nodeId) || (b && b->node == nodeId);
	});
	std::erase_if(m_pins, [&](const GraphPin &p) { return p.node == nodeId; });
	std::erase_if(m_nodes, [&](const GraphNode &n) { return n.id == nodeId; });
}

bool NodeGraph::CanConnect(uint32_t pinA, uint32_t pinB) const {
	const GraphPin *a = FindPin(pinA), *b = FindPin(pinB);
	if (!a || !b || a->node == b->node || a->kind == b->kind)
		return false;
	return a->type.empty() || b->type.empty() || a->type == b->type;
}

uint32_t NodeGraph::Connect(uint32_t pinA, uint32_t pinB) {
	if (!CanConnect(pinA, pinB))
		return 0;
	const bool aIsOutput = FindPin(pinA)->kind == PinKind::Output;
	const uint32_t from = aIsOutput ? pinA : pinB, to = aIsOutput ? pinB : pinA;
	std::erase_if(m_links, [to](const GraphLink &l) { return l.to == to; }); // une seule source par entrée
	GraphLink link;
	link.id = m_nextId++;
	link.from = from;
	link.to = to;
	m_links.push_back(link);
	return link.id;
}

void NodeGraph::Disconnect(uint32_t link) {
	std::erase_if(m_links, [link](const GraphLink &l) { return l.id == link; });
}

const GraphLink *NodeGraph::LinkInto(uint32_t inputPin) const {
	for (const GraphLink &l : m_links)
		if (l.to == inputPin)
			return &l;
	return nullptr;
}

GraphNode *NodeGraph::FindNode(uint32_t id) {
	for (GraphNode &n : m_nodes)
		if (n.id == id)
			return &n;
	return nullptr;
}
const GraphNode *NodeGraph::FindNode(uint32_t id) const { return const_cast<NodeGraph *>(this)->FindNode(id); }

GraphPin *NodeGraph::FindPin(uint32_t id) {
	for (GraphPin &p : m_pins)
		if (p.id == id)
			return &p;
	return nullptr;
}
const GraphPin *NodeGraph::FindPin(uint32_t id) const { return const_cast<NodeGraph *>(this)->FindPin(id); }

GraphLink *NodeGraph::FindLink(uint32_t id) {
	for (GraphLink &l : m_links)
		if (l.id == id)
			return &l;
	return nullptr;
}

std::vector<uint32_t> NodeGraph::TopologicalOrder() const {
	std::unordered_map<uint32_t, int> indegree;
	std::unordered_map<uint32_t, std::vector<uint32_t>> next;
	for (const GraphNode &n : m_nodes)
		indegree[n.id] = 0;
	for (const GraphLink &l : m_links) {
		const GraphPin *a = FindPin(l.from), *b = FindPin(l.to);
		if (!a || !b)
			continue;
		next[a->node].push_back(b->node);
		++indegree[b->node];
	}
	std::vector<uint32_t> order, ready;
	for (const GraphNode &n : m_nodes) // ordre stable : celui des nœuds
		if (indegree[n.id] == 0)
			ready.push_back(n.id);
	while (!ready.empty()) {
		const uint32_t id = ready.front();
		ready.erase(ready.begin());
		order.push_back(id);
		for (const uint32_t m : next[id])
			if (--indegree[m] == 0)
				ready.push_back(m);
	}
	if (order.size() != m_nodes.size())
		return {}; // cycle
	return order;
}

void NodeGraph::ClearSelection() {
	for (GraphNode &n : m_nodes)
		n.selected = false;
	for (GraphLink &l : m_links)
		l.selected = false;
}

void NodeGraph::SelectAll() {
	for (GraphNode &n : m_nodes)
		n.selected = true;
}

std::vector<uint32_t> NodeGraph::SelectedNodes() const {
	std::vector<uint32_t> out;
	for (const GraphNode &n : m_nodes)
		if (n.selected)
			out.push_back(n.id);
	return out;
}

bool NodeGraph::DeleteSelection() {
	const size_t before = m_nodes.size() + m_links.size();
	std::erase_if(m_links, [](const GraphLink &l) { return l.selected; });
	for (const uint32_t id : SelectedNodes())
		RemoveNode(id);
	return m_nodes.size() + m_links.size() != before;
}

dataio::NodePtr NodeGraph::Export(bool selectedOnly) const {
	dataio::NodePtr root = dataio::Node::MakeObject();
	dataio::NodePtr nodes = dataio::Node::MakeArray(), links = dataio::Node::MakeArray();
	const auto exported = [&](uint32_t nodeId) {
		const GraphNode *n = FindNode(nodeId);
		return n && (!selectedOnly || n->selected);
	};
	const auto pinJson = [&](uint32_t id) {
		const GraphPin *p = FindPin(id);
		dataio::NodePtr j = dataio::Node::MakeObject();
		j->Set("id", dataio::Node::MakeInt(int64_t(id)));
		j->Set("label", dataio::Node::MakeString(p->label));
		j->Set("type", dataio::Node::MakeString(p->type));
		j->Set("shape", dataio::Node::MakeInt(int(p->shape)));
		j->Set("color", ColorJson(p->color));
		return j;
	};
	for (const GraphNode &n : m_nodes) {
		if (!exported(n.id))
			continue;
		dataio::NodePtr j = dataio::Node::MakeObject();
		j->Set("id", dataio::Node::MakeInt(int64_t(n.id)));
		j->Set("kind", dataio::Node::MakeString(n.kind));
		j->Set("title", dataio::Node::MakeString(n.title));
		j->Set("x", dataio::Node::MakeFloat(n.pos.x));
		j->Set("y", dataio::Node::MakeFloat(n.pos.y));
		j->Set("width", dataio::Node::MakeFloat(n.width));
		j->Set("bodyHeight", dataio::Node::MakeFloat(n.bodyHeight));
		j->Set("header", ColorJson(n.header));
		dataio::NodePtr props = dataio::Node::MakeObject();
		for (const auto &[key, value] : n.props)
			props->Set(key, dataio::Node::MakeString(value));
		j->Set("props", std::move(props));
		dataio::NodePtr ins = dataio::Node::MakeArray(), outs = dataio::Node::MakeArray();
		for (const uint32_t id : n.inputs)
			ins->Push(pinJson(id));
		for (const uint32_t id : n.outputs)
			outs->Push(pinJson(id));
		j->Set("inputs", std::move(ins));
		j->Set("outputs", std::move(outs));
		nodes->Push(std::move(j));
	}
	for (const GraphLink &l : m_links) {
		const GraphPin *a = FindPin(l.from), *b = FindPin(l.to);
		if (!a || !b || !exported(a->node) || !exported(b->node))
			continue;
		dataio::NodePtr j = dataio::Node::MakeObject();
		j->Set("from", dataio::Node::MakeInt(int64_t(l.from)));
		j->Set("to", dataio::Node::MakeInt(int64_t(l.to)));
		j->Set("style", dataio::Node::MakeInt(int(l.style)));
		j->Set("color", ColorJson(l.color));
		j->Set("thickness", dataio::Node::MakeFloat(l.thickness));
		j->Set("arrow", dataio::Node::MakeBool(l.arrow));
		dataio::NodePtr way = dataio::Node::MakeArray();
		for (const GraphPoint &p : l.waypoints) {
			dataio::NodePtr xy = dataio::Node::MakeArray();
			xy->Push(dataio::Node::MakeFloat(p.x));
			xy->Push(dataio::Node::MakeFloat(p.y));
			way->Push(std::move(xy));
		}
		j->Set("waypoints", std::move(way));
		links->Push(std::move(j));
	}
	root->Set("nodes", std::move(nodes));
	root->Set("links", std::move(links));
	return root;
}

bool NodeGraph::Import(const dataio::NodePtr &json, bool remap, GraphPoint at) {
	const dataio::NodePtr nodes = json->Get("nodes");
	const dataio::NodePtr links = json->Get("links");
	if (!nodes || !nodes->IsArray())
		return false;
	// Décalage du collage : le coin haut gauche des nœuds importés va en `at`.
	float minX = 1e30f, minY = 1e30f;
	for (const dataio::NodePtr &n : nodes->AsArray()) {
		minX = std::min(minX, (*n)["x"]->AsFloat());
		minY = std::min(minY, (*n)["y"]->AsFloat());
	}
	const float dx = remap ? at.x - minX : 0.f, dy = remap ? at.y - minY : 0.f;
	std::unordered_map<int64_t, uint32_t> pins; // ancien identifiant → nouveau
	const auto newId = [&](int64_t old) {
		if (remap)
			return m_nextId++;
		m_nextId = std::max(m_nextId, uint32_t(old) + 1);
		return uint32_t(old);
	};
	for (const dataio::NodePtr &j : nodes->AsArray()) {
		GraphNode node;
		node.id = newId((*j)["id"]->AsInt());
		node.kind = std::string((*j)["kind"]->AsString());
		node.title = std::string((*j)["title"]->AsString());
		node.pos = {(*j)["x"]->AsFloat() + dx, (*j)["y"]->AsFloat() + dy};
		node.width = (*j)["width"]->AsFloat(180.f);
		node.bodyHeight = (*j)["bodyHeight"]->AsFloat();
		node.header = JsonColor((*j)["header"], node.header);
		node.selected = remap;
		if (const dataio::NodePtr props = j->Get("props"); props && props->IsObject())
			for (const auto &[key, value] : props->AsObject())
				node.props[key] = std::string(value->AsString());
		for (const auto &[field, kind] : {std::pair{"inputs", PinKind::Input}, {"outputs", PinKind::Output}}) {
			const dataio::NodePtr list = j->Get(field);
			if (!list || !list->IsArray())
				continue;
			for (const dataio::NodePtr &p : list->AsArray()) {
				GraphPin pin{newId((*p)["id"]->AsInt()), node.id, kind, std::string((*p)["label"]->AsString()), std::string((*p)["type"]->AsString()),
							 PinShape(std::clamp<int64_t>((*p)["shape"]->AsInt(), 0, 2)), JsonColor((*p)["color"], {0, 0, 0, 0})};
				pins[(*p)["id"]->AsInt()] = pin.id;
				(kind == PinKind::Input ? node.inputs : node.outputs).push_back(pin.id);
				m_pins.push_back(std::move(pin));
			}
		}
		m_nodes.push_back(std::move(node));
	}
	if (links && links->IsArray())
		for (const dataio::NodePtr &j : links->AsArray()) {
			const auto from = pins.find((*j)["from"]->AsInt()), to = pins.find((*j)["to"]->AsInt());
			if (from == pins.end() || to == pins.end())
				continue;
			GraphLink *link = FindLink(Connect(from->second, to->second));
			if (!link)
				continue;
			link->style = LinkStyle(std::clamp<int64_t>((*j)["style"]->AsInt(), 0, 2));
			link->color = JsonColor((*j)["color"], {0, 0, 0, 0});
			link->thickness = (*j)["thickness"]->AsFloat(2.f);
			link->arrow = (*j)["arrow"]->AsBool();
			if (const dataio::NodePtr way = j->Get("waypoints"); way && way->IsArray())
				for (const dataio::NodePtr &p : way->AsArray())
					link->waypoints.push_back({(*p)[0]->AsFloat() + dx, (*p)[1]->AsFloat() + dy});
		}
	return true;
}

void NodeGraph::CopySelection() { m_clipboard = Export(true); }

void NodeGraph::Paste(GraphPoint at) {
	if (!m_clipboard)
		return;
	ClearSelection();
	(void)Import(m_clipboard, true, at);
}

dataio::NodePtr NodeGraph::Serialize() const { return Export(false); }

bool NodeGraph::Deserialize(const dataio::NodePtr &json) {
	if (!json->IsObject() || !json->Get("nodes"))
		return false;
	Clear();
	return Import(json, false, {});
}

void NodeGraph::Clear() {
	m_nodes.clear();
	m_pins.clear();
	m_links.clear();
	m_nextId = 1;
}

// ════════════════════════════════════════════════════════════════════════════
// Éditeur
// ════════════════════════════════════════════════════════════════════════════

GraphPoint NodeGraphEditor::ToGraph(float sx, float sy) const noexcept {
	return {(sx - m_area.x) / zoom - panX, (sy - m_area.y) / zoom - panY};
}

GraphPoint NodeGraphEditor::ToScreen(GraphPoint p) const noexcept { return {m_area.x + (p.x + panX) * zoom, m_area.y + (p.y + panY) * zoom}; }

Rect NodeGraphEditor::NodeRect(const GraphNode &node) const noexcept {
	const GraphPoint p = ToScreen(node.pos);
	const size_t rows = std::max(node.inputs.size(), node.outputs.size());
	const float h = HeaderHeight() + float(rows) * PinRow() + (node.bodyHeight > 0.f ? node.bodyHeight * zoom + 8.f * zoom : 0.f) + 8.f * zoom;
	return {p.x, p.y, node.width * zoom, h};
}

GraphPoint NodeGraphEditor::PinPosition(const NodeGraph &graph, const GraphPin &pin) const {
	const GraphNode *node = graph.FindNode(pin.node);
	if (!node)
		return {};
	const Rect r = NodeRect(*node);
	const std::vector<uint32_t> &list = pin.kind == PinKind::Input ? node->inputs : node->outputs;
	const size_t index = size_t(std::find(list.begin(), list.end(), pin.id) - list.begin());
	return {pin.kind == PinKind::Input ? r.x : r.Right(), r.y + HeaderHeight() + (float(index) + 0.5f) * PinRow() + 4.f * zoom};
}

uint32_t NodeGraphEditor::PinAt(const NodeGraph &graph, float sx, float sy) const {
	const float radius = std::max(7.f, 9.f * zoom);
	for (auto it = graph.Nodes().rbegin(); it != graph.Nodes().rend(); ++it)
		for (const auto *list : {&it->inputs, &it->outputs})
			for (const uint32_t id : *list)
				if (const GraphPin *pin = graph.FindPin(id); pin && Distance(PinPosition(graph, *pin), {sx, sy}) <= radius)
					return id;
	return 0;
}

uint32_t NodeGraphEditor::NodeAt(const NodeGraph &graph, float sx, float sy, bool headerOnly) const {
	for (auto it = graph.Nodes().rbegin(); it != graph.Nodes().rend(); ++it) {
		Rect r = NodeRect(*it);
		if (headerOnly)
			r.h = HeaderHeight();
		if (r.Contains(sx, sy))
			return it->id;
	}
	return 0;
}

std::vector<GraphPoint> NodeGraphEditor::LinkPath(const NodeGraph &graph, const GraphLink &link) const {
	const GraphPin *a = graph.FindPin(link.from), *b = graph.FindPin(link.to);
	if (!a || !b)
		return {};
	std::vector<GraphPoint> anchors = {PinPosition(graph, *a)};
	for (const GraphPoint &w : link.waypoints)
		anchors.push_back(ToScreen(w));
	anchors.push_back(PinPosition(graph, *b));
	std::vector<GraphPoint> path;
	for (size_t s = 0; s + 1 < anchors.size(); ++s) {
		const GraphPoint p0 = anchors[s], p3 = anchors[s + 1];
		switch (link.style) {
		case LinkStyle::Straight:
			path.push_back(p0);
			break;
		case LinkStyle::Orthogonal: {
			const float mid = (p0.x + p3.x) * 0.5f;
			path.insert(path.end(), {p0, {mid, p0.y}, {mid, p3.y}});
			break;
		}
		case LinkStyle::Bezier: {
			const float dx = std::max(30.f * zoom, std::abs(p3.x - p0.x) * 0.5f);
			const GraphPoint p1{p0.x + dx, p0.y}, p2{p3.x - dx, p3.y};
			for (int i = 0; i < 20; ++i) {
				const float t = float(i) / 20.f, u = 1.f - t;
				path.push_back({u * u * u * p0.x + 3.f * u * u * t * p1.x + 3.f * u * t * t * p2.x + t * t * t * p3.x,
								u * u * u * p0.y + 3.f * u * u * t * p1.y + 3.f * u * t * t * p2.y + t * t * t * p3.y});
			}
			break;
		}
		}
	}
	path.push_back(anchors.back());
	return path;
}

uint32_t NodeGraphEditor::LinkAt(const NodeGraph &graph, float sx, float sy, size_t *segment) const {
	for (const GraphLink &link : graph.Links()) {
		const std::vector<GraphPoint> path = LinkPath(graph, link);
		for (size_t i = 0; i + 1 < path.size(); ++i)
			if (SegmentDistance({sx, sy}, path[i], path[i + 1]) < 6.f) {
				if (segment) {
					// Rang du point intermédiaire à insérer : nombre de points déjà dépassés.
					const float along = float(i) / float(path.size() - 1);
					*segment = std::min(link.waypoints.size(), size_t(along * float(link.waypoints.size() + 1)));
				}
				return link.id;
			}
	}
	return 0;
}

void NodeGraphEditor::FrameAll(const NodeGraph &graph) {
	if (graph.Nodes().empty() || m_area.w <= 0.f)
		return;
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
	for (const GraphNode &n : graph.Nodes()) {
		const Rect r = NodeRect(n);
		x0 = std::min(x0, n.pos.x);
		y0 = std::min(y0, n.pos.y);
		x1 = std::max(x1, n.pos.x + r.w / zoom);
		y1 = std::max(y1, n.pos.y + r.h / zoom);
	}
	zoom = std::clamp(std::min(m_area.w / (x1 - x0 + 120.f), m_area.h / (y1 - y0 + 120.f)), 0.3f, 1.5f);
	panX = (m_area.w / zoom - (x1 - x0)) * 0.5f - x0;
	panY = (m_area.h / zoom - (y1 - y0)) * 0.5f - y0;
}

void NodeGraphEditor::OpenMenu(std::vector<GraphMenuItem> items, float sx, float sy) {
	m_menu = std::move(items);
	m_menuX = sx;
	m_menuY = sy;
	m_menuFresh = true;
}

bool NodeGraphEditor::DrawMenu(Context &u) {
	if (m_menu.empty())
		return false;
	sdl3::Canvas &c = u.Canvas();
	const Theme &th = u.Style();
	const float itemH = 26.f, fs = th.fontSize - 1.f;
	float w = 140.f, h = 8.f;
	for (const GraphMenuItem &item : m_menu) {
		h += item.action ? itemH : 9.f;
		w = std::max(w, c.MeasureText(item.label, fs) + (item.shortcut.empty() ? 0.f : c.MeasureText(item.shortcut, fs) + 30.f) + 28.f);
	}
	const Rect box{std::clamp(m_menuX, 0.f, std::max(0.f, c.Width() - w)), std::clamp(m_menuY, 0.f, std::max(0.f, c.Height() - h)), w, h};
	c.PushClip({0, 0, c.Width(), c.Height()}, false);
	c.FillRoundRect({box.x + 2.f, box.y + 3.f, box.w, box.h}, 8.f, sdl3::Rgba(0, 0, 0, 90));
	c.FillRoundRect(box, 8.f, sdl3::WithAlpha(th.panel, 252));
	c.StrokeRoundRect(box, 8.f, th.panelBorder);
	float y = box.y + 4.f;
	std::function<void()> chosen;
	const bool pressed = u.MousePressed(sdl3::MouseButton::Left);
	for (const GraphMenuItem &item : m_menu) {
		if (!item.action) {
			c.Line(box.x + 8.f, y + 4.f, box.Right() - 8.f, y + 4.f, th.panelBorder);
			y += 9.f;
			continue;
		}
		const Rect row{box.x + 4.f, y, box.w - 8.f, itemH};
		const bool hovered = row.Contains(u.MouseX(), u.MouseY());
		if (hovered)
			c.FillRoundRect(row, 5.f, th.widgetHover);
		c.Text(row.x + 10.f, row.y + (itemH - c.LineHeight(fs)) * 0.5f, item.label, th.text, fs);
		if (!item.shortcut.empty())
			c.Text(row.Right() - 10.f - c.MeasureText(item.shortcut, fs), row.y + (itemH - c.LineHeight(fs)) * 0.5f, item.shortcut, th.textDim, fs);
		if (hovered && pressed)
			chosen = item.action;
		y += itemH;
	}
	c.PopClip();
	if (m_menuFresh) { // image d'ouverture : l'appui du bouton droit ne doit pas le refermer
		m_menuFresh = false;
		return false;
	}
	const bool outside = !box.Contains(u.MouseX(), u.MouseY());
	if (chosen || (pressed && outside) || u.MousePressed(sdl3::MouseButton::Right) || u.KeycodePressed(SDLK_ESCAPE))
		m_menu.clear();
	if (chosen) {
		chosen();
		return true;
	}
	return false;
}

bool NodeGraphEditor::Draw(Context &u, const Rect &area, NodeGraph &graph) {
	m_area = area;
	m_time += double(u.Dt());
	sdl3::Canvas &c = u.Canvas();
	const Theme &th = u.Style();
	bool changed = false;
	const float mx = u.MouseX(), my = u.MouseY();
	const bool menuOpen = !m_menu.empty();
	const bool hover = area.Contains(mx, my) && !menuOpen && !u.Blocked();

	// ── Entrées ────────────────────────────────────────────────────────────
	if (hover && u.WheelY() != 0.f) { // zoom vers le curseur
		const GraphPoint g = ToGraph(mx, my);
		zoom = std::clamp(zoom * std::pow(1.15f, u.WheelY()), 0.3f, 2.5f);
		panX = (mx - area.x) / zoom - g.x;
		panY = (my - area.y) / zoom - g.y;
	}
	const bool space = u.Events().KeyScan(SDL_SCANCODE_SPACE).IsPressed() && !u.WantsKeyboard();
	if (hover && m_drag == Drag::None) {
		if (u.MousePressed(sdl3::MouseButton::Middle) || (space && u.MousePressed(sdl3::MouseButton::Left))) {
			m_drag = Drag::View;
		} else if (u.MousePressed(sdl3::MouseButton::Left)) {
			const bool doubleClick = m_time - m_lastClick < 0.35;
			m_lastClick = m_time;
			size_t segment = 0;
			if (const uint32_t pin = PinAt(graph, mx, my)) {
				const GraphPin *p = graph.FindPin(pin);
				if (const GraphLink *existing = p->kind == PinKind::Input ? graph.LinkInto(pin) : nullptr) {
					m_linkPin = existing->from; // détacher le lien de cette entrée
					graph.Disconnect(existing->id);
					changed = true;
				} else {
					m_linkPin = pin;
				}
				m_drag = Drag::Link;
			} else if (const uint32_t header = NodeAt(graph, mx, my, true)) {
				GraphNode *node = graph.FindNode(header);
				if (u.Ctrl())
					node->selected = !node->selected;
				else if (!node->selected) {
					graph.ClearSelection();
					node->selected = true;
				}
				// Au premier plan : dernier dessiné.
				auto it = std::find_if(graph.Nodes().begin(), graph.Nodes().end(), [header](const GraphNode &n) { return n.id == header; });
				std::rotate(it, it + 1, graph.Nodes().end());
				m_drag = Drag::Nodes;
			} else if (const uint32_t body = NodeAt(graph, mx, my, false)) {
				GraphNode *node = graph.FindNode(body);
				if (!node->selected && !u.Ctrl()) {
					graph.ClearSelection();
					node->selected = true;
				}
			} else if (const uint32_t linkId = LinkAt(graph, mx, my, &segment)) {
				GraphLink *link = graph.FindLink(linkId);
				// Un point intermédiaire existant sous la souris : on le déplace.
				for (size_t i = 0; i < link->waypoints.size(); ++i)
					if (Distance(ToScreen(link->waypoints[i]), {mx, my}) < 8.f) {
						m_drag = Drag::Waypoint;
						m_waypointLink = linkId;
						m_waypointIndex = i;
					}
				if (m_drag == Drag::None && doubleClick) {
					link->waypoints.insert(link->waypoints.begin() + std::ptrdiff_t(segment), ToGraph(mx, my));
					m_drag = Drag::Waypoint;
					m_waypointLink = linkId;
					m_waypointIndex = segment;
					changed = true;
				}
				if (!u.Ctrl())
					graph.ClearSelection();
				link->selected = true;
			} else {
				if (!u.Ctrl())
					graph.ClearSelection();
				m_drag = Drag::Box;
			}
			m_startX = m_lastX = mx;
			m_startY = m_lastY = my;
		} else if (u.MousePressed(sdl3::MouseButton::Right)) {
			if (const uint32_t nodeId = NodeAt(graph, mx, my, false)) {
				if (callbacks.nodeMenu)
					OpenMenu(callbacks.nodeMenu(*graph.FindNode(nodeId)), mx, my);
			} else if (const uint32_t linkId = LinkAt(graph, mx, my)) {
				if (callbacks.linkMenu)
					OpenMenu(callbacks.linkMenu(*graph.FindLink(linkId)), mx, my);
			} else if (callbacks.canvasMenu) {
				OpenMenu(callbacks.canvasMenu(ToGraph(mx, my)), mx, my);
			}
		}
		// Clavier (souris sur l'éditeur, aucun champ en saisie).
		if (!u.WantsKeyboard()) {
			if (u.KeycodePressed(SDLK_DELETE) || u.KeycodePressed(SDLK_BACKSPACE))
				changed |= graph.DeleteSelection();
			if (u.Ctrl() && u.KeycodePressed(SDLK_C))
				graph.CopySelection();
			if (u.Ctrl() && u.KeycodePressed(SDLK_X)) {
				graph.CopySelection();
				changed |= graph.DeleteSelection();
			}
			if (u.Ctrl() && u.KeycodePressed(SDLK_V) && graph.HasClipboard()) {
				graph.Paste(ToGraph(mx, my));
				changed = true;
			}
			if (u.Ctrl() && u.KeycodePressed(SDLK_A))
				graph.SelectAll();
			if (!u.Ctrl() && u.KeycodePressed(SDLK_F))
				FrameAll(graph);
		}
	}
	// Glissés en cours (même hors de la zone).
	const float ddx = mx - m_lastX, ddy = my - m_lastY;
	switch (m_drag) {
	case Drag::View:
		panX += ddx / zoom;
		panY += ddy / zoom;
		if (!u.MouseDown(sdl3::MouseButton::Middle) && !u.MouseDown(sdl3::MouseButton::Left))
			m_drag = Drag::None;
		break;
	case Drag::Nodes:
		if (ddx != 0.f || ddy != 0.f) {
			for (GraphNode &n : graph.Nodes())
				if (n.selected) {
					n.pos.x += ddx / zoom;
					n.pos.y += ddy / zoom;
				}
			changed = true;
		}
		if (!u.MouseDown())
			m_drag = Drag::None;
		break;
	case Drag::Waypoint:
		if (GraphLink *link = graph.FindLink(m_waypointLink); link && m_waypointIndex < link->waypoints.size() && (ddx != 0.f || ddy != 0.f)) {
			link->waypoints[m_waypointIndex] = ToGraph(mx, my);
			changed = true;
		}
		if (!u.MouseDown())
			m_drag = Drag::None;
		break;
	case Drag::Link:
		if (!u.MouseDown()) {
			if (const uint32_t target = PinAt(graph, mx, my); target && graph.Connect(m_linkPin, target))
				changed = true;
			m_drag = Drag::None;
		}
		break;
	case Drag::Box:
		if (!u.MouseDown()) {
			const Rect box{std::min(m_startX, mx), std::min(m_startY, my), std::abs(mx - m_startX), std::abs(my - m_startY)};
			for (GraphNode &n : graph.Nodes()) {
				const Rect r = NodeRect(n);
				if (r.x < box.Right() && r.Right() > box.x && r.y < box.Bottom() && r.Bottom() > box.y)
					n.selected = true;
			}
			m_drag = Drag::None;
		}
		break;
	case Drag::None: break;
	}
	m_lastX = mx;
	m_lastY = my;

	// ── Dessin ─────────────────────────────────────────────────────────────
	c.PushClip(area);
	c.FillRect(area, th.background);
	if (showGrid) {
		float step = 24.f * zoom;
		while (step < 12.f)
			step *= 2.f;
		const float ox = std::fmod(panX * zoom, step), oy = std::fmod(panY * zoom, step);
		for (int i = 0; float(i) * step < area.w + step; ++i) {
			const float x = area.x + ox + float(i) * step;
			c.Line(x, area.y, x, area.Bottom(), sdl3::WithAlpha(th.panelBorder, i % 4 == 0 ? 90 : 40));
		}
		for (int i = 0; float(i) * step < area.h + step; ++i) {
			const float y = area.y + oy + float(i) * step;
			c.Line(area.x, y, area.Right(), y, sdl3::WithAlpha(th.panelBorder, i % 4 == 0 ? 90 : 40));
		}
	}
	// Liens.
	for (const GraphLink &link : graph.Links()) {
		const std::vector<GraphPoint> path = LinkPath(graph, link);
		if (path.size() < 2)
			continue;
		const GraphPin *from = graph.FindPin(link.from);
		Color color = link.color.a ? link.color : (from && from->color.a ? from->color : th.accent);
		if (link.selected)
			color = th.accentHover;
		std::vector<float> xy;
		for (const GraphPoint &p : path)
			xy.insert(xy.end(), {p.x, p.y});
		c.Polyline(xy.data(), path.size(), color, (link.thickness + (link.selected ? 1.5f : 0.f)) * std::max(zoom, 0.6f));
		if (link.arrow) {
			const GraphPoint tip = path.back(), back = path[path.size() - 2];
			const float ang = std::atan2(tip.y - back.y, tip.x - back.x), s = 9.f * zoom;
			c.FillTriangle(tip.x, tip.y, tip.x - s * std::cos(ang - 0.45f), tip.y - s * std::sin(ang - 0.45f), tip.x - s * std::cos(ang + 0.45f),
						   tip.y - s * std::sin(ang + 0.45f), color);
		}
		for (const GraphPoint &w : link.waypoints) {
			const GraphPoint s = ToScreen(w);
			c.FillCircle(s.x, s.y, 4.f * zoom + 1.f, color);
		}
	}
	// Lien en cours de création.
	if (m_drag == Drag::Link) {
		if (const GraphPin *pin = graph.FindPin(m_linkPin)) {
			const GraphPoint a = PinPosition(graph, *pin);
			const bool out = pin->kind == PinKind::Output;
			const GraphPoint p0 = out ? a : GraphPoint{mx, my}, p3 = out ? GraphPoint{mx, my} : a;
			std::vector<float> xy;
			const float dx = std::max(30.f * zoom, std::abs(p3.x - p0.x) * 0.5f);
			for (int i = 0; i <= 20; ++i) {
				const float t = float(i) / 20.f, v = 1.f - t;
				xy.push_back(v * v * v * p0.x + 3.f * v * v * t * (p0.x + dx) + 3.f * v * t * t * (p3.x - dx) + t * t * t * p3.x);
				xy.push_back(v * v * v * p0.y + 3.f * v * v * t * p0.y + 3.f * v * t * t * p3.y + t * t * t * p3.y);
			}
			const uint32_t target = PinAt(graph, mx, my);
			const bool ok = target && graph.CanConnect(m_linkPin, target);
			c.Polyline(xy.data(), 21, ok ? th.success : sdl3::WithAlpha(th.text, 160), 2.f * std::max(zoom, 0.6f));
		}
	}
	// Nœuds.
	const float fs = 13.f * zoom;
	for (GraphNode &node : graph.Nodes()) {
		const Rect r = NodeRect(node);
		if (r.Right() < area.x || r.x > area.Right() || r.Bottom() < area.y || r.y > area.Bottom())
			continue; // hors champ
		const float radius = 8.f * zoom;
		c.FillRoundRect({r.x + 3.f, r.y + 4.f, r.w, r.h}, radius, sdl3::Rgba(0, 0, 0, 70));
		c.FillRoundRect(r, radius, sdl3::WithAlpha(th.panel, 245));
		c.FillRoundRect({r.x, r.y, r.w, HeaderHeight()}, radius, node.header);
		c.FillRect({r.x, r.y + HeaderHeight() - radius, r.w, radius}, node.header); // bas de l'en-tête droit
		c.StrokeRoundRect(r, radius, node.selected ? th.accent : th.panelBorder, node.selected ? 2.f : 1.f);
		if (zoom > 0.45f) {
			c.PushClip({r.x, r.y, r.w, HeaderHeight()});
			c.Text(r.x + 10.f * zoom, r.y + (HeaderHeight() - c.LineHeight(fs, sdl3::CanvasFont::Bold)) * 0.5f, node.title, sdl3::Rgba(245, 248, 255), fs,
				   sdl3::CanvasFont::Bold);
			c.PopClip();
		}
		// Broches et libellés.
		const uint32_t hoverPin = m_drag == Drag::Link ? PinAt(graph, mx, my) : 0;
		for (const auto *list : {&node.inputs, &node.outputs})
			for (const uint32_t id : *list) {
				const GraphPin *pin = graph.FindPin(id);
				if (!pin)
					continue;
				const GraphPoint p = PinPosition(graph, *pin);
				const Color col = pin->color.a ? pin->color : th.accent;
				const float s = 5.f * std::max(zoom, 0.6f);
				const bool linked = pin->kind == PinKind::Input ? graph.LinkInto(id) != nullptr
																: std::ranges::any_of(graph.Links(), [id](const GraphLink &l) { return l.from == id; });
				switch (pin->shape) {
				case PinShape::Circle:
					c.FillCircle(p.x, p.y, s + 1.5f, th.panelBorder);
					c.FillCircle(p.x, p.y, s, linked ? col : sdl3::WithAlpha(col, 90));
					break;
				case PinShape::Square:
					c.FillRect({p.x - s, p.y - s, s * 2.f, s * 2.f}, linked ? col : sdl3::WithAlpha(col, 110));
					break;
				case PinShape::Triangle: {
					const float d = pin->kind == PinKind::Output ? 1.f : -1.f;
					c.FillTriangle(p.x - d * s, p.y - s, p.x - d * s, p.y + s, p.x + d * s * 1.2f, p.y, linked ? col : sdl3::WithAlpha(col, 110));
					break;
				}
				}
				if (hoverPin == id && graph.CanConnect(m_linkPin, id))
					c.StrokeRoundRect({p.x - s - 4.f, p.y - s - 4.f, (s + 4.f) * 2.f, (s + 4.f) * 2.f}, s + 4.f, th.success, 2.f);
				if (zoom > 0.55f && !pin->label.empty()) {
					const float lw = c.MeasureText(pin->label, fs - zoom);
					const float lx = pin->kind == PinKind::Input ? p.x + 12.f * zoom : p.x - 12.f * zoom - lw;
					c.Text(lx, p.y - c.LineHeight(fs - zoom) * 0.5f, pin->label, th.textDim, fs - zoom);
				}
			}
		// Corps dessiné par l'application.
		if (node.bodyHeight > 0.f && callbacks.drawBody) {
			const size_t rows = std::max(node.inputs.size(), node.outputs.size());
			const Rect body{r.x + 10.f * zoom, r.y + HeaderHeight() + float(rows) * PinRow() + 8.f * zoom, r.w - 20.f * zoom, node.bodyHeight * zoom};
			c.PushClip(body);
			u.BeginColumn(body);
			callbacks.drawBody(u, node, body, zoom);
			u.EndColumn();
			c.PopClip();
		}
	}
	// Rectangle de sélection.
	if (m_drag == Drag::Box) {
		const Rect box{std::min(m_startX, mx), std::min(m_startY, my), std::abs(mx - m_startX), std::abs(my - m_startY)};
		c.FillRect(box, sdl3::WithAlpha(th.accent, 40));
		c.StrokeRect(box, th.accent);
	}
	c.PopClip();
	changed |= DrawMenu(u);
	if (changed && callbacks.onChanged)
		callbacks.onChanged();
	return changed;
}

} // namespace ui
