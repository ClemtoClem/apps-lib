// Modèle du graphe de nœuds : liens validés, ordre topologique, sélection, copier-coller,
// sérialisation JSON (l'éditeur est testé avec la souris dans test_layout.cpp).
#include <cpp-maker/test.hpp>

#include <ui/node_graph.hpp>

namespace {

struct Calc {
	ui::NodeGraph graph;
	uint32_t a = 0, b = 0, out = 0;		  ///< nœuds
	uint32_t aOut = 0, bOut = 0, inX = 0, inY = 0, sumOut = 0; ///< broches
	Calc() {
		a = graph.AddNode("value", "A", {0, 0}).id;
		aOut = graph.AddOutput(a, "valeur", "nombre");
		b = graph.AddNode("value", "B", {0, 100}).id;
		bOut = graph.AddOutput(b, "valeur", "nombre");
		out = graph.AddNode("add", "Somme", {200, 50}).id;
		inX = graph.AddInput(out, "x", "nombre");
		inY = graph.AddInput(out, "y", "nombre");
		sumOut = graph.AddOutput(out, "x + y", "nombre");
	}
};

} // namespace

TEST(NodeGraph_ConnectRules) {
	Calc c;
	CHECK(c.graph.Connect(c.aOut, c.inX) != 0);
	CHECK(c.graph.Connect(c.inY, c.bOut) != 0); // dans un sens ou dans l'autre
	CHECK_EQ(c.graph.Links().size(), 2u);
	CHECK_EQ(c.graph.Links()[1].from, c.bOut); // normalisé : sortie → entrée
	CHECK_EQ(c.graph.Connect(c.aOut, c.bOut), 0u); // deux sorties
	CHECK_EQ(c.graph.Connect(c.sumOut, c.inX), 0u); // même nœud
	const uint32_t text = c.graph.AddInput(c.out, "texte", "chaîne");
	CHECK_EQ(c.graph.Connect(c.aOut, text), 0u); // types différents
	// Une entrée n'a qu'une source : le nouveau lien remplace l'ancien.
	CHECK(c.graph.Connect(c.bOut, c.inX) != 0);
	CHECK_EQ(c.graph.Links().size(), 2u);
	CHECK_EQ(c.graph.LinkInto(c.inX)->from, c.bOut);
	c.graph.Disconnect(c.graph.LinkInto(c.inX)->id);
	CHECK(c.graph.LinkInto(c.inX) == nullptr);
}

TEST(NodeGraph_TopologicalOrderAndCycles) {
	Calc c;
	c.graph.Connect(c.aOut, c.inX);
	c.graph.Connect(c.bOut, c.inY);
	const std::vector<uint32_t> order = c.graph.TopologicalOrder();
	REQUIRE(order.size() == 3u);
	CHECK_EQ(order.back(), c.out); // après ses deux sources
	// Cycle : la somme alimente A, qui alimente la somme.
	const uint32_t aIn = c.graph.AddInput(c.a, "entrée", "nombre");
	c.graph.Connect(c.sumOut, aIn);
	CHECK(c.graph.TopologicalOrder().empty());
}

TEST(NodeGraph_SelectionDeleteCopyPaste) {
	Calc c;
	c.graph.Connect(c.aOut, c.inX);
	c.graph.Connect(c.bOut, c.inY);
	c.graph.FindNode(c.a)->selected = true;
	c.graph.FindNode(c.out)->selected = true;
	c.graph.CopySelection();
	CHECK(c.graph.HasClipboard());
	c.graph.Paste({500, 500});
	CHECK_EQ(c.graph.Nodes().size(), 5u);
	// Les nœuds collés ont de nouveaux identifiants, sont sélectionnés, et leur lien interne
	// (A → Somme) est recopié ; le lien venant de B (non copié) ne l'est pas.
	CHECK_EQ(c.graph.SelectedNodes().size(), 2u);
	CHECK_EQ(c.graph.Links().size(), 3u);
	const ui::GraphNode *pasted = c.graph.FindNode(c.graph.SelectedNodes()[0]);
	CHECK(pasted->id != c.a && pasted->pos.x >= 500.f);
	// Suppression de la sélection (nœuds collés et leurs liens).
	CHECK(c.graph.DeleteSelection());
	CHECK_EQ(c.graph.Nodes().size(), 3u);
	CHECK_EQ(c.graph.Links().size(), 2u);
	c.graph.SelectAll();
	CHECK(c.graph.DeleteSelection());
	CHECK(c.graph.Nodes().empty() && c.graph.Links().empty());
}

TEST(NodeGraph_SerializeRoundTrip) {
	Calc c;
	ui::GraphNode *a = c.graph.FindNode(c.a);
	a->props["valeur"] = "42";
	a->bodyHeight = 30.f;
	ui::GraphLink *link = c.graph.FindLink(c.graph.Connect(c.aOut, c.inX));
	link->style = ui::LinkStyle::Orthogonal;
	link->arrow = true;
	link->waypoints = {{120.f, 10.f}};
	const std::string text = dataio::WriteJson(c.graph.Serialize());

	ui::NodeGraph copy;
	auto parsed = dataio::ParseJson(text);
	REQUIRE(parsed.IsOk());
	REQUIRE(copy.Deserialize(parsed.Value()));
	CHECK_EQ(copy.Nodes().size(), 3u);
	CHECK_EQ(copy.FindNode(c.a)->props.at("valeur"), std::string("42"));
	CHECK_EQ(copy.FindNode(c.a)->bodyHeight, 30.f);
	REQUIRE(copy.Links().size() == 1u);
	CHECK(copy.Links()[0].style == ui::LinkStyle::Orthogonal && copy.Links()[0].arrow);
	CHECK_EQ(copy.Links()[0].waypoints.size(), 1u);
	CHECK_EQ(copy.LinkInto(c.inX)->from, c.aOut); // mêmes identifiants
	// Les nouveaux identifiants ne recouvrent pas les anciens.
	const uint32_t fresh = copy.AddNode("x", "X", {}).id;
	CHECK(fresh > c.sumOut);
	CHECK(!copy.Deserialize(dataio::Node::MakeInt(int64_t(3))));
}
