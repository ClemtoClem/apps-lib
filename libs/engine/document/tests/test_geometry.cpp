// Tests des repères et de la rotation d'engine.document (position relative au nœud, au parent,
// à la page ; centre de rotation dans les trois repères ; test de position d'une forme tournée ;
// rotation dans le rendu, le PDF et le HTML) et du presse-papiers des nœuds.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <string>
#include <tuple>

#include <document/commands.hpp>
#include <document/export.hpp>
#include <document/render.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	NodeId Para(std::string_view text, NodeId parent = kNoNode) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent == kNoNode ? body : parent).Value();
		(void)d.Set(d.CreateNode(NodeType::Text, p).Value(), props::Text, text);
		return p;
	}
	NodeId Shape(double w, double h) {
		const NodeId s = d.CreateNode(NodeType::Shape, body).Value();
		(void)d.Set(s, props::Width, w);
		(void)d.Set(s, props::Height, h);
		return s;
	}
	const LayoutBox &Box(const LayoutResult &r, NodeId id) {
		for (const PageLayout &p : r.pages)
			for (const LayoutBox &b : p.boxes)
				if (b.node == id)
					return b;
		static LayoutBox none;
		return none;
	}
};

} // namespace

TEST(Geometry, PositionReferences) {
	Fixture f;
	f.Para("avant");
	const NodeId s = f.Shape(100, 50);
	LayoutResult r = Layout(f.d);
	const LayoutBox slot = f.Box(r, s);
	const double content = r.pages[0].content.x;
	CHECK_NEAR(slot.rect.x, content, 1e-9);
	CHECK_NEAR(slot.slotX, slot.rect.x, 1e-9);
	// Dans le flux, repère « nœud » (Auto) : décalage depuis sa place.
	REQUIRE(f.d.Set(s, props::PositionX, 30.0).IsOk());
	REQUIRE(f.d.Set(s, props::PositionY, 5.0).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).rect.x, slot.rect.x + 30, 1e-9);
	CHECK_NEAR(f.Box(r, s).rect.y, slot.rect.y + 5, 1e-9);
	// Repère « page ».
	REQUIRE(f.d.Set(s, props::PositionReference, PositionBase::Page).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).rect.x, 30.0, 1e-9);
	CHECK_NEAR(f.Box(r, s).rect.y, 5.0, 1e-9);
	// Repère « parent » : zone de contenu de la page pour un bloc du corps.
	REQUIRE(f.d.Set(s, props::PositionReference, PositionBase::Parent).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).rect.x, content + 30, 1e-9);
	CHECK_NEAR(f.Box(r, s).rect.y, r.pages[0].content.y + 5, 1e-9);
	CHECK_NEAR(f.Box(r, s).parentX, content, 1e-9);
	// Position absolue (hors flux), Auto = page.
	REQUIRE(f.d.Set(s, props::PositionReference, PositionBase::Auto).IsOk());
	REQUIRE(f.d.Set(s, props::Position, Positioning::Absolute).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).rect.x, 30.0, 1e-9);
}

TEST(Geometry, RotationPivotsAndHitTest) {
	Fixture f;
	const NodeId s = f.Shape(200, 20);
	REQUIRE(f.d.Set(s, props::Rotation, 90.0).IsOk());
	LayoutResult r = Layout(f.d);
	const LayoutBox b = f.Box(r, s);
	CHECK_NEAR(b.rotation, 90.0, 1e-9);
	CHECK_NEAR(b.pivotX, b.rect.x + 100, 1e-9); // centre par défaut
	CHECK_NEAR(b.pivotY, b.rect.y + 10, 1e-9);
	// Tournée d'un quart de tour : la barre horizontale devient verticale autour de son centre.
	CHECK(b.Contains(b.pivotX, b.pivotY + 80));
	CHECK(!b.Contains(b.pivotX + 80, b.pivotY));
	CHECK(r.HitTest(0, b.pivotX, b.pivotY + 80) == s);
	// Centre de rotation : décalage depuis le centre, ou repères page et parent.
	REQUIRE(f.d.Set(s, props::RotationOriginX, -100.0).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).pivotX, f.Box(r, s).rect.x, 1e-9); // bord gauche
	REQUIRE(f.d.Set(s, props::RotationOriginReference, OriginBase::Page).IsOk());
	REQUIRE(f.d.Set(s, props::RotationOriginX, 50.0).IsOk());
	REQUIRE(f.d.Set(s, props::RotationOriginY, 60.0).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).pivotX, 50.0, 1e-9);
	CHECK_NEAR(f.Box(r, s).pivotY, 60.0, 1e-9);
	REQUIRE(f.d.Set(s, props::RotationOriginReference, OriginBase::Parent).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(f.Box(r, s).pivotX, r.pages[0].content.x + 50, 1e-9);
}

TEST(Geometry, RotationInRenderPdfAndHtml) {
	Fixture f;
	const NodeId s = f.Shape(120, 60);
	REQUIRE(f.d.Set(s, props::BackgroundColor, Color::BLUE()).IsOk());
	REQUIRE(f.d.Set(s, props::Rotation, 30.0).IsOk());
	const LayoutResult r = Layout(f.d);
	const DisplayList list = RenderPage(f.d, r, 0);
	bool found = false;
	for (const DrawCommand &c : list.commands)
		if (c.node == s && c.kind == DrawKind::FillRect) {
			found = true;
			CHECK_NEAR(c.rotation, 30.0, 1e-9);
			CHECK_NEAR(c.pivotX, c.rect.x + 60, 1e-9);
		} else if (c.node != s) {
			CHECK(c.rotation == 0.0);
		}
	CHECK(found);
	auto pdf = ExportPdf(f.d, r, {});
	REQUIRE(pdf.IsOk());
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("transform: rotate(30deg)") != std::string::npos);
}

TEST(Clipboard, CopyAndPasteNodes) {
	Fixture f;
	const NodeId p = f.Para("paragraphe");
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	f.Para("cellule", cell);
	REQUIRE(f.d.Set(table, props::BorderWidth, 2.0).IsOk());
	REQUIRE(f.d.Set(p, props::Name, "intro").IsOk());
	const NodeId g = f.d.CreateGroup("g").Value();
	REQUIRE(f.d.AddToGroup(p, g).IsOk());
	// Le paragraphe de la cellule est couvert par la copie du tableau ; la page est ignorée.
	LayoutResult layout = Layout(f.d);
	(void)SyncPages(f.d, layout);
	const NodeId page = f.d.FindAll(NodeType::Page).front();
	const std::vector<NodeId> ids{table, p, cell, page};
	const NodeFragment fragment = CopyNodes(f.d, ids);
	REQUIRE(fragment.roots.size() == 2);
	CHECK(fragment.roots[0].type == NodeType::Paragraph); // ordre du document
	CHECK(fragment.PlainText() == "paragraphe\ncellule");
	History h(f.d);
	auto pasted = PasteNodes(h, fragment, f.body);
	REQUIRE(pasted.IsOk());
	REQUIRE(pasted.Value().size() == 2);
	const NodeId copy = pasted.Value()[0], tableCopy = pasted.Value()[1];
	CHECK(copy != p);
	CHECK(f.d.Find(copy)->InGroup(g));
	CHECK(f.d.Get(copy, props::Name).empty()); // le nom ne se duplique pas
	CHECK_NEAR(f.d.Get(tableCopy, props::BorderWidth), 2.0, 1e-9);
	CHECK_EQ(f.d.SubtreeOf(tableCopy).size(), f.d.SubtreeOf(table).size());
	CHECK_EQ(f.d.Find(f.body)->Children().size(), size_t(4));
	CHECK_EQ(h.Entries().size(), size_t(1)); // une transaction
	REQUIRE(h.Undo().IsOk());
	CHECK_EQ(f.d.Find(f.body)->Children().size(), size_t(2));
	// Couper = copier puis supprimer ; collage refusé par le parent : rien ne reste.
	CHECK(PasteNodes(h, fragment, cell).IsOk()); // une cellule accepte paragraphes et tableaux
	CHECK(PasteNodes(h, fragment, row).IsError()); // une ligne n'accepte que des cellules
	CHECK_EQ(f.d.Find(row)->Children().size(), size_t(1));
	CHECK(PasteNodes(h, NodeFragment{}, f.body).Error().code == ErrorCode::Nothing);
}

TEST(Geometry, ZIndexOrdersObjects) {
	Fixture f;
	// Deux formes superposées posées sur la page (position absolue au même endroit).
	const auto place = [&](Color color) {
		const NodeId s = f.Shape(100, 60);
		CHECK(f.d.Set(s, props::Position, Positioning::Absolute).IsOk());
		CHECK(f.d.Set(s, props::PositionX, 100.0).IsOk());
		CHECK(f.d.Set(s, props::PositionY, 100.0).IsOk());
		CHECK(f.d.Set(s, props::BackgroundColor, color).IsOk());
		return s;
	};
	const NodeId first = place(Color::RED()), second = place(Color::BLUE());
	const auto order = [&] { // rang de la commande de remplissage de chaque forme
		const DisplayList list = RenderPage(f.d, Layout(f.d), 0);
		size_t a = 0, b = 0;
		for (size_t i = 0; i < list.commands.size(); ++i) {
			if (list.commands[i].kind != DrawKind::FillRect)
				continue;
			if (list.commands[i].node == first)
				a = i;
			if (list.commands[i].node == second)
				b = i;
		}
		return std::pair{a, b};
	};
	auto [a, b] = order();
	CHECK(a < b); // même z : ordre du document, la seconde dessus
	CHECK(Layout(f.d).HitTest(0, 150, 130) == second);
	REQUIRE(f.d.Set(first, props::ZIndex, int64_t(2)).IsOk());
	std::tie(a, b) = order();
	CHECK(a > b); // la première passe dessus
	CHECK(Layout(f.d).HitTest(0, 150, 130) == first);
	REQUIRE(f.d.Set(second, props::ZIndex, int64_t(3)).IsOk());
	CHECK(Layout(f.d).HitTest(0, 150, 130) == second);
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("z-index: 3;") != std::string::npos);
	CHECK(f.d.Set(first, props::ZIndex, int64_t(2000000)).IsError()); // hors domaine
}
