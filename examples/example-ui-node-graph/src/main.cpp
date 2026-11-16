// example-ui-node-graph — éditeur de graphes de nœuds : une calculatrice visuelle.
//
// Des nœuds Valeur alimentent des opérations (addition, soustraction, multiplication,
// division) reliées à des nœuds Résultat. Le graphe est évalué à chaque image dans l'ordre
// topologique (`NodeGraph::TopologicalOrder`) ; un cycle est signalé. Le corps des nœuds
// (champ numérique, résultat) est dessiné par l'application, le reste par ui::NodeGraphEditor.
//
//   Molette : zoom · bouton du milieu ou Espace + glisser : vue · glisser une broche : lien
//   Ctrl + clic : sélection multiple · glisser dans le vide : rectangle
//   Double-clic sur un lien : point intermédiaire · clic droit : menus
//   Suppr, Ctrl+C / X / V / A, F : tout cadrer · Ctrl+S / Ctrl+O : sauver / charger
//
//   cpp-maker run example-ui-node-graph
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <ui/node_graph.hpp>

namespace {

const std::filesystem::path kSavePath = "data/node-graph/calculatrice.json";

struct Operation {
	std::string_view kind, title;
	sdl3::Color header;
};
constexpr Operation kOperations[] = {
	{"add", "Addition", sdl3::Rgba(46, 120, 92)},
	{"sub", "Soustraction", sdl3::Rgba(120, 92, 46)},
	{"mul", "Multiplication", sdl3::Rgba(150, 84, 40)},
	{"div", "Division", sdl3::Rgba(110, 60, 140)},
};
const sdl3::Color kNumber = sdl3::Rgba(120, 190, 255);

class NodeCalculator final : public app::Application {
protected:
	bool OnInit() override {
		// Graphe de départ : (3 + 4) × 2.
		const uint32_t a = AddValue({40, 40}, 3.f), b = AddValue({40, 190}, 4.f), c = AddValue({300, 280}, 2.f);
		const uint32_t sum = AddOperation(kOperations[0], {300, 80});
		const uint32_t product = AddOperation(kOperations[2], {560, 160});
		const uint32_t result = AddResult({820, 170});
		Link(a, 0, sum, 0);
		Link(b, 0, sum, 1);
		Link(sum, 0, product, 0);
		Link(c, 0, product, 1);
		if (ui::GraphLink *l = m_graph.FindLink(Link(product, 0, result, 0))) {
			l->arrow = true;
			l->thickness = 3.f;
		}

		ui::NodeGraphCallbacks &cb = m_editor.callbacks;
		cb.drawBody = [this](ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body, float zoom) { DrawBody(u, node, body, zoom); };
		cb.canvasMenu = [this](ui::GraphPoint at) {
			std::vector<ui::GraphMenuItem> items = {{"Ajouter une valeur", [this, at] { AddValue(at, 1.f); }, ""}};
			for (const Operation &op : kOperations)
				items.push_back({std::format("Ajouter : {}", op.title), [this, &op, at] { AddOperation(op, at); }, ""});
			items.push_back({"Ajouter un résultat", [this, at] { AddResult(at); }, ""});
			items.push_back({"", nullptr, ""});
			items.push_back({"Tout cadrer", [this] { m_editor.FrameAll(m_graph); }, "F"});
			return items;
		};
		cb.nodeMenu = [this](ui::GraphNode &node) {
			const uint32_t id = node.id;
			return std::vector<ui::GraphMenuItem>{
				{"Dupliquer", [this, id] {
					 m_graph.ClearSelection();
					 if (ui::GraphNode *n = m_graph.FindNode(id)) {
						 n->selected = true;
						 m_graph.CopySelection();
						 m_graph.Paste({n->pos.x + 40.f, n->pos.y + 40.f});
					 }
				 }, "Ctrl+C, Ctrl+V"},
				{"", nullptr, ""},
				{"Supprimer", [this, id] { m_graph.RemoveNode(id); }, "Suppr"},
			};
		};
		cb.linkMenu = [this](ui::GraphLink &link) {
			const uint32_t id = link.id;
			const auto style = [this, id](ui::LinkStyle s) {
				return [this, id, s] {
					if (ui::GraphLink *l = m_graph.FindLink(id))
						l->style = s;
				};
			};
			return std::vector<ui::GraphMenuItem>{
				{"Courbe de Bézier", style(ui::LinkStyle::Bezier), ""},
				{"Orthogonal", style(ui::LinkStyle::Orthogonal), ""},
				{"Droit", style(ui::LinkStyle::Straight), ""},
				{"Flèche", [this, id] { if (ui::GraphLink *l = m_graph.FindLink(id)) l->arrow = !l->arrow; }, ""},
				{"", nullptr, ""},
				{"Supprimer le lien", [this, id] { m_graph.Disconnect(id); }, "Suppr"},
			};
		};
		return true;
	}

	void OnUpdate(double) override {
		ui::Context &u = Ui();
		if (u.Ctrl() && u.KeycodePressed(SDLK_S))
			Save();
		if (u.Ctrl() && u.KeycodePressed(SDLK_O))
			Load();
		Evaluate();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::FRect status{area.x, area.Bottom() - 28.f, area.w, 28.f};
		const sdl3::FRect canvas{area.x, area.y, area.w, area.h - status.h};
		if (m_firstFrame) {
			m_editor.Draw(Ui(), canvas, m_graph); // mémorise la zone, puis cadre le graphe
			m_editor.FrameAll(m_graph);
			m_firstFrame = false;
		}
		m_editor.Draw(Ui(), canvas, m_graph);
		c.FillRect(status, th.title);
		c.Line(status.x, status.y, status.Right(), status.y, th.panelBorder);
		const std::string left = m_cycle ? "Cycle détecté : le graphe ne peut pas être évalué"
										 : std::format("{} nœuds · {} liens · zoom {:.0f} %", m_graph.Nodes().size(), m_graph.Links().size(), m_editor.zoom * 100.f);
		c.Text(status.x + 12, status.y + 7, left, m_cycle ? th.danger : th.textDim, 12);
		const std::string_view help = "Clic droit : ajouter · double-clic sur un lien : point · Ctrl+S / Ctrl+O";
		c.Text(status.Right() - 12 - c.MeasureText(help, 12), status.y + 7, help, th.textDim, 12);
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "save", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SAVE), .tooltip = "Sauver (Ctrl+S)"});
		left.push_back({.id = "load", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), .tooltip = "Charger (Ctrl+O)"});
		left.push_back({.id = "frame", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CENTER_FOCUS_STRONG), .tooltip = "Tout cadrer (F)", .separator = true});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "save")
			Save();
		else if (id == "load")
			Load();
		else if (id == "frame")
			m_editor.FrameAll(m_graph);
	}

private:
	uint32_t AddValue(ui::GraphPoint at, float value) {
		ui::GraphNode &n = m_graph.AddNode("value", "Valeur", at, sdl3::Rgba(52, 74, 118));
		const uint32_t id = n.id;
		n.width = 170.f;
		n.bodyHeight = 32.f;
		n.props["value"] = std::format("{}", value);
		m_graph.AddOutput(id, "nombre", "nombre", ui::PinShape::Circle, kNumber);
		return id;
	}

	uint32_t AddOperation(const Operation &op, ui::GraphPoint at) {
		const uint32_t id = m_graph.AddNode(std::string(op.kind), std::string(op.title), at, op.header).id;
		m_graph.AddInput(id, "a", "nombre", ui::PinShape::Circle, kNumber);
		m_graph.AddInput(id, "b", "nombre", ui::PinShape::Circle, kNumber);
		m_graph.AddOutput(id, "résultat", "nombre", ui::PinShape::Triangle, op.header);
		m_graph.FindNode(id)->bodyHeight = 22.f;
		return id;
	}

	uint32_t AddResult(ui::GraphPoint at) {
		ui::GraphNode &n = m_graph.AddNode("result", "Résultat", at, sdl3::Rgba(70, 76, 92));
		const uint32_t id = n.id;
		n.width = 190.f;
		n.bodyHeight = 46.f;
		m_graph.AddInput(id, "valeur", "nombre", ui::PinShape::Circle, kNumber);
		return id;
	}

	uint32_t Link(uint32_t fromNode, size_t output, uint32_t toNode, size_t input) {
		return m_graph.Connect(m_graph.FindNode(fromNode)->outputs[output], m_graph.FindNode(toNode)->inputs[input]);
	}

	/// Valeur d'une entrée : celle de la sortie reliée, NaN si l'entrée est libre.
	float Input(const ui::GraphNode &node, size_t index) const {
		if (index >= node.inputs.size())
			return NAN;
		const ui::GraphLink *link = m_graph.LinkInto(node.inputs[index]);
		if (!link)
			return NAN;
		const auto it = m_values.find(link->from);
		return it == m_values.end() ? NAN : it->second;
	}

	void Evaluate() {
		m_values.clear();
		const std::vector<uint32_t> order = m_graph.TopologicalOrder();
		m_cycle = order.empty() && !m_graph.Nodes().empty();
		for (const uint32_t id : order) {
			const ui::GraphNode *n = m_graph.FindNode(id);
			float v = NAN;
			if (n->kind == "value")
				v = std::strtof(n->props.contains("value") ? n->props.at("value").c_str() : "0", nullptr);
			else if (n->kind == "result")
				v = Input(*n, 0);
			else {
				const float a = Input(*n, 0), b = Input(*n, 1);
				if (n->kind == "add")
					v = a + b;
				else if (n->kind == "sub")
					v = a - b;
				else if (n->kind == "mul")
					v = a * b;
				else if (n->kind == "div")
					v = b != 0.f ? a / b : NAN;
			}
			m_results[id] = v;
			for (const uint32_t out : n->outputs)
				m_values[out] = v;
		}
	}

	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body, float zoom) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Theme &th = u.Style();
		const float v = m_results.contains(node.id) ? m_results[node.id] : NAN;
		const std::string text = std::isnan(v) ? std::string("—") : std::format("{:g}", v);
		if (node.kind == "value") {
			float value = std::strtof(node.props["value"].c_str(), nullptr);
			u.PushId(uint64_t(node.id));
			if (u.DragNumber(body, "value", value, 0.1f, "{:.2f}", kNumber))
				node.props["value"] = std::format("{}", value);
			u.PopId();
		} else if (node.kind == "result") {
			c.FillRoundRect(body, 6.f * zoom, th.widget);
			c.TextCentered(body, text, std::isnan(v) ? th.textDim : th.success, 22.f * zoom, sdl3::CanvasFont::Bold);
		} else {
			c.TextCentered(body, std::format("= {}", text), th.textDim, 13.f * zoom);
		}
	}

	void Save() {
		std::error_code ec;
		std::filesystem::create_directories(kSavePath.parent_path(), ec);
		std::ofstream(kSavePath) << dataio::WriteJson(m_graph.Serialize());
		Ui().Notify("Graphe sauvé : " + kSavePath.string(), Theme().success);
	}

	void Load() {
		std::ifstream in(kSavePath);
		std::stringstream text;
		text << in.rdbuf();
		auto json = dataio::ParseJson(text.str());
		if (!in || json.IsError() || !m_graph.Deserialize(json.Value())) {
			Ui().Notify("Aucun graphe à charger : " + kSavePath.string(), Theme().danger);
			return;
		}
		m_editor.FrameAll(m_graph);
		Ui().Notify("Graphe chargé", Theme().success);
	}

	ui::NodeGraph m_graph;
	ui::NodeGraphEditor m_editor;
	std::map<uint32_t, float> m_values;	 ///< valeur de chaque broche de sortie
	std::map<uint32_t, float> m_results; ///< valeur de chaque nœud
	bool m_cycle = false, m_firstFrame = true;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Calculatrice visuelle — graphe de nœuds";
	config.width = 1200;
	config.height = 740;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::WORKSPACES);
	config.ParseArgs(argc, argv);
	return NodeCalculator().Run(config);
}
