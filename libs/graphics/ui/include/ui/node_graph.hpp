#pragma once
/**
 * @file node_graph.hpp
 * @brief Éditeur de graphes de nœuds : modèle (`NodeGraph`) et vue immédiate
 *        (`NodeGraphEditor`).
 *
 * Le modèle ne connaît que des nœuds, des broches typées et des liens ; l'application y
 * range ses paramètres (`GraphNode::props`) et dessine le corps de chaque nœud par une
 * fonction de rappel (curseurs, valeurs, aperçus). Le modèle se sérialise en JSON et donne
 * un ordre topologique, si bien qu'un graphe de traitement (audio, calcul) s'évalue
 * directement.
 *
 * @code{.cpp}
 * ui::NodeGraph graph;
 * ui::GraphNode &add = graph.AddNode("add", "Addition", {60, 60});
 * graph.AddInput(add.id, "a", "nombre");
 * const uint32_t sum = graph.AddOutput(add.id, "a + b", "nombre");
 * ...
 * ui::NodeGraphEditor editor;
 * editor.callbacks.drawBody = [&](ui::Context &u, ui::GraphNode &n, const sdl3::FRect &body) { ... };
 * editor.Draw(Ui(), area, graph);   // chaque image
 * @endcode
 *
 * Souris : molette = zoom vers le curseur, bouton du milieu (ou Espace + glisser) = vue,
 * glisser un en-tête = déplacer (la sélection entière), Ctrl + clic = sélection multiple,
 * glisser dans le vide = rectangle de sélection, glisser d'une broche à une autre = lien,
 * glisser depuis une entrée reliée = détacher, double-clic sur un lien = point
 * intermédiaire (déplaçable), clic droit = menu (nœud, lien ou fond).
 * Clavier (souris sur l'éditeur) : Suppr, Ctrl+C / X / V / A, F = tout cadrer.
 */
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <dataio/json.hpp>
#include <sdl3/render/canvas.hpp>

namespace ui {

class Context;
using sdl3::Color;
using Rect = sdl3::FRect;

enum class PinKind : uint8_t { Input, Output };
enum class PinShape : uint8_t { Circle, Triangle, Square };
enum class LinkStyle : uint8_t { Bezier, Orthogonal, Straight };

struct GraphPoint {
	float x = 0.f, y = 0.f;
};

struct GraphPin {
	uint32_t id = 0;
	uint32_t node = 0;
	PinKind kind = PinKind::Input;
	std::string label;
	std::string type; ///< deux broches typées ne se relient que si leurs types sont égaux
	PinShape shape = PinShape::Circle;
	Color color{0, 0, 0, 0}; ///< alpha nul : couleur du thème
};

struct GraphNode {
	uint32_t id = 0;
	std::string kind;  ///< type applicatif (« oscillateur », « filtre »…)
	std::string title;
	GraphPoint pos;	   ///< coin haut gauche, en coordonnées du graphe
	float width = 180.f;
	float bodyHeight = 0.f; ///< hauteur du corps dessiné par l'application
	Color header{56, 70, 104, 255};
	std::vector<uint32_t> inputs, outputs; ///< broches (ordre d'affichage)
	std::map<std::string, std::string> props; ///< paramètres de l'application (sérialisés)
	bool selected = false;
};

struct GraphLink {
	uint32_t id = 0;
	uint32_t from = 0, to = 0; ///< broche de sortie → broche d'entrée
	LinkStyle style = LinkStyle::Bezier;
	Color color{0, 0, 0, 0}; ///< alpha nul : couleur de la broche de sortie
	float thickness = 2.f;
	bool arrow = false;
	std::vector<GraphPoint> waypoints;
	bool selected = false;
};

/// Modèle du graphe : identifiants stables, liens validés, sélection, presse-papiers.
class NodeGraph {
public:
	GraphNode &AddNode(std::string kind, std::string title, GraphPoint pos, Color header = {56, 70, 104, 255});
	uint32_t AddInput(uint32_t node, std::string label, std::string type = {}, PinShape shape = PinShape::Circle, Color color = {0, 0, 0, 0});
	uint32_t AddOutput(uint32_t node, std::string label, std::string type = {}, PinShape shape = PinShape::Circle, Color color = {0, 0, 0, 0});
	/// Supprime le nœud, ses broches et ses liens.
	void RemoveNode(uint32_t node);

	/// Relie une sortie à une entrée (dans un sens ou dans l'autre) : refusé entre deux broches
	/// du même nœud, de même sens ou de types différents ; une entrée n'a qu'une source (le lien
	/// existant est remplacé). Rend l'identifiant du lien, 0 si refusé.
	uint32_t Connect(uint32_t pinA, uint32_t pinB);
	[[nodiscard]] bool CanConnect(uint32_t pinA, uint32_t pinB) const;
	void Disconnect(uint32_t link);
	/// Lien arrivant sur une entrée (nullptr si aucun).
	[[nodiscard]] const GraphLink *LinkInto(uint32_t inputPin) const;

	[[nodiscard]] GraphNode *FindNode(uint32_t id);
	[[nodiscard]] const GraphNode *FindNode(uint32_t id) const;
	[[nodiscard]] GraphPin *FindPin(uint32_t id);
	[[nodiscard]] const GraphPin *FindPin(uint32_t id) const;
	[[nodiscard]] GraphLink *FindLink(uint32_t id);
	[[nodiscard]] std::vector<GraphNode> &Nodes() noexcept { return m_nodes; }
	[[nodiscard]] const std::vector<GraphNode> &Nodes() const noexcept { return m_nodes; }
	[[nodiscard]] std::vector<GraphLink> &Links() noexcept { return m_links; }
	[[nodiscard]] const std::vector<GraphLink> &Links() const noexcept { return m_links; }

	/// Nœuds dans un ordre où chaque nœud vient après ceux qui l'alimentent ; vide s'il y a
	/// un cycle.
	[[nodiscard]] std::vector<uint32_t> TopologicalOrder() const;

	void ClearSelection();
	void SelectAll();
	[[nodiscard]] std::vector<uint32_t> SelectedNodes() const;
	/// Supprime les nœuds et liens sélectionnés ; rend vrai si quelque chose a disparu.
	bool DeleteSelection();
	/// Copie les nœuds sélectionnés et les liens entre eux.
	void CopySelection();
	/// Colle le presse-papiers (nouveaux identifiants), le coin haut gauche en `at` ; les
	/// nœuds collés deviennent la sélection.
	void Paste(GraphPoint at);
	[[nodiscard]] bool HasClipboard() const noexcept { return m_clipboard != nullptr; }

	[[nodiscard]] dataio::NodePtr Serialize() const;
	/// Remplace le graphe par `json` (produit par Serialize) ; faux si le document est invalide.
	bool Deserialize(const dataio::NodePtr &json);
	void Clear();

private:
	uint32_t AddPin(uint32_t node, PinKind kind, std::string label, std::string type, PinShape shape, Color color);
	/// Nœuds (tous, ou sélectionnés) et liens entre eux, au format de Serialize.
	[[nodiscard]] dataio::NodePtr Export(bool selectedOnly) const;
	/// Ajoute le contenu de `json` ; `remap` : nouveaux identifiants, coin haut gauche en `at`,
	/// nœuds ajoutés sélectionnés.
	bool Import(const dataio::NodePtr &json, bool remap, GraphPoint at);
	std::vector<GraphNode> m_nodes;
	std::vector<GraphPin> m_pins;
	std::vector<GraphLink> m_links;
	dataio::NodePtr m_clipboard;
	uint32_t m_nextId = 1;
};

/// Entrée d'un menu contextuel de l'éditeur.
struct GraphMenuItem {
	std::string label;
	std::function<void()> action; ///< vide : séparateur
	std::string shortcut;
};

struct NodeGraphCallbacks {
	/// Corps d'un nœud (sous les broches) : `body` en coordonnées écran, `zoom` de la vue.
	std::function<void(Context &, GraphNode &, const Rect &body, float zoom)> drawBody;
	/// Menus contextuels : d'un nœud, d'un lien, du fond (`at` en coordonnées du graphe).
	std::function<std::vector<GraphMenuItem>(GraphNode &)> nodeMenu;
	std::function<std::vector<GraphMenuItem>(GraphLink &)> linkMenu;
	std::function<std::vector<GraphMenuItem>(GraphPoint at)> canvasMenu;
	/// Le graphe a changé (nœud déplacé, lien créé ou supprimé…).
	std::function<void()> onChanged;
};

/// Vue immédiate d'un NodeGraph (état de la vue et des glissés).
class NodeGraphEditor {
public:
	NodeGraphCallbacks callbacks;
	float panX = 0.f, panY = 0.f, zoom = 1.f;
	bool showGrid = true;

	/// Dessine et traite les entrées ; rend vrai si le graphe a changé cette image.
	bool Draw(Context &u, const Rect &area, NodeGraph &graph);
	/// Cadre tous les nœuds dans la dernière zone d'affichage.
	void FrameAll(const NodeGraph &graph);
	/// Conversions écran ↔ graphe (dernière zone d'affichage).
	[[nodiscard]] GraphPoint ToGraph(float sx, float sy) const noexcept;
	[[nodiscard]] GraphPoint ToScreen(GraphPoint p) const noexcept;
	/// Rectangle écran d'un nœud.
	[[nodiscard]] Rect NodeRect(const GraphNode &node) const noexcept;
	/// Position écran d'une broche.
	[[nodiscard]] GraphPoint PinPosition(const NodeGraph &graph, const GraphPin &pin) const;

private:
	enum class Drag : uint8_t { None, View, Nodes, Box, Link, Waypoint };
	[[nodiscard]] float HeaderHeight() const noexcept { return 26.f * zoom; }
	[[nodiscard]] float PinRow() const noexcept { return 22.f * zoom; }
	uint32_t PinAt(const NodeGraph &graph, float sx, float sy) const;
	uint32_t NodeAt(const NodeGraph &graph, float sx, float sy, bool headerOnly) const;
	uint32_t LinkAt(const NodeGraph &graph, float sx, float sy, size_t *segment = nullptr) const;
	std::vector<GraphPoint> LinkPath(const NodeGraph &graph, const GraphLink &link) const;
	void OpenMenu(std::vector<GraphMenuItem> items, float sx, float sy);
	bool DrawMenu(Context &u);

	Rect m_area{};
	Drag m_drag = Drag::None;
	float m_startX = 0.f, m_startY = 0.f, m_lastX = 0.f, m_lastY = 0.f;
	uint32_t m_linkPin = 0;	  ///< broche d'origine du lien en cours
	uint32_t m_waypointLink = 0;
	size_t m_waypointIndex = 0;
	double m_lastClick = -1.0;
	double m_time = 0.0;
	std::vector<GraphMenuItem> m_menu;
	float m_menuX = 0.f, m_menuY = 0.f;
	bool m_menuFresh = false; ///< ouvert cette image : l'appui qui l'a ouvert ne le referme pas
};

} // namespace ui
