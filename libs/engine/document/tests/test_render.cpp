// Tests du rendu d'engine.document : papier et fond de page (y compris page virtuelle),
// fonds et bordures des blocs et cellules, texte positionné (police, corps, couleur),
// surlignage et soulignement, images et cadre de substitution, formes, ordre de dessin,
// options (limites des blocs, débordements), rendu parallèle identique au séquentiel.
#include <cpp-maker/test.hpp>

#include <algorithm>
#include <vector>

#include <document/render.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	NodeId Para(std::string_view text, NodeId parent = kNoNode) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent == kNoNode ? body : parent).Value();
		const NodeId t = d.CreateNode(NodeType::Text, p).Value();
		(void)d.Set(t, props::Text, text);
		return p;
	}
	DisplayList Render(const RenderOptions &o = {}) { return RenderPage(d, Layout(d), 0, o); }
};

std::vector<const DrawCommand *> Of(const DisplayList &l, DrawKind kind, NodeId node = kNoNode) {
	std::vector<const DrawCommand *> out;
	for (const DrawCommand &c : l.commands)
		if (c.kind == kind && (node == kNoNode || c.node == node))
			out.push_back(&c);
	return out;
}

RenderOptions Options(void (*edit)(RenderOptions &)) {
	RenderOptions o;
	edit(o);
	return o;
}

size_t IndexOf(const DisplayList &l, const DrawCommand *c) { return size_t(c - l.commands.data()); }

} // namespace

TEST(Render, PaperAndPageBackground) {
	Fixture f;
	DisplayList l = f.Render();
	REQUIRE(!l.commands.empty());
	CHECK(l.commands[0].kind == DrawKind::FillRect);
	CHECK(l.commands[0].color == Color::WHITE());
	CHECK_NEAR(l.commands[0].rect.w, l.width, 1e-9);
	CHECK_NEAR(l.height, StandardSize(PageFormat::A4).height, 1e-9);
	CHECK(f.Render(Options([](RenderOptions &o) { o.paper = false; })).commands.empty());
	// Fond porté par le groupe de pages : appliqué à la page virtuelle comme à la page créée.
	const NodeId group = NodeId(std::get<int64_t>(*f.d.Explicit(f.section, PropertyId::PageGroup)));
	REQUIRE(f.d.Set(group, props::BackgroundColor, Color(250, 240, 220)).IsOk());
	l = f.Render();
	REQUIRE(l.commands.size() >= 2);
	CHECK(l.commands[1].color == Color(250, 240, 220));
	LayoutResult layout = Layout(f.d);
	(void)SyncPages(f.d, layout);
	l = RenderPage(f.d, layout, 0);
	CHECK(l.page == layout.pages[0].page);
	CHECK(l.commands[1].color == Color(250, 240, 220));
	CHECK(RenderPage(f.d, layout, 5).commands.empty());
}

TEST(Render, TextRunsUnderlineAndHighlight) {
	Fixture f;
	const NodeId p = f.Para("Bonjour monde");
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(t, props::FontSize, 20.0).IsOk());
	REQUIRE(f.d.Set(t, props::Italic, true).IsOk());
	REQUIRE(f.d.Set(t, props::FontFamily, "Times").IsOk());
	REQUIRE(f.d.Set(t, props::TextColor, Color::NAVY()).IsOk());
	REQUIRE(f.d.Set(t, props::Underline, true).IsOk());
	REQUIRE(f.d.Set(t, props::BackgroundColor, Color::YELLOW()).IsOk());
	const LayoutResult layout = Layout(f.d);
	const DisplayList l = RenderPage(f.d, layout, 0);
	const auto texts = Of(l, DrawKind::Text, t);
	REQUIRE(texts.size() == 1);
	const DrawCommand &c = *texts[0];
	CHECK(c.text == "Bonjour monde");
	CHECK(c.font.standard == pdf::StandardFont::TimesItalic);
	CHECK_NEAR(c.size, 20.0, 1e-9);
	CHECK(c.color == Color::NAVY());
	CHECK_NEAR(c.baseline, layout.pages[0].lines[0].baseline, 1e-9);
	CHECK_NEAR(c.rect.x, 72.0, 1e-9);
	CHECK_NEAR(c.rect.w, pdf::TextWidth(pdf::StandardFont::TimesItalic, "Bonjour monde", 20), 1e-9);
	// Surlignage dessiné avant le texte, soulignement sous la ligne de base (AFM : -100 / 50).
	const auto fills = Of(l, DrawKind::FillRect, t);
	REQUIRE(fills.size() == 1);
	CHECK(fills[0]->color == Color::YELLOW());
	CHECK(IndexOf(l, fills[0]) < IndexOf(l, texts[0]));
	const auto lines = Of(l, DrawKind::Line, t);
	REQUIRE(lines.size() == 1);
	CHECK_NEAR(lines[0]->rect.y, c.baseline + 2.0, 1e-9);
	CHECK_NEAR(lines[0]->lineWidth, 1.0, 1e-9);
	CHECK_NEAR(lines[0]->rect.w, c.rect.w, 1e-9);
}

TEST(Render, BlocksTablesAndDrawingOrder) {
	Fixture f;
	const NodeId p = f.Para("encadré");
	REQUIRE(f.d.Set(p, props::BackgroundColor, Color(220, 220, 220)).IsOk());
	REQUIRE(f.d.Set(p, props::BorderWidth, 2.0).IsOk());
	REQUIRE(f.d.Set(p, props::BorderColor, Color::RED()).IsOk());
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	REQUIRE(f.d.Set(table, props::BorderWidth, 1.0).IsOk());
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	REQUIRE(f.d.Set(cell, props::BackgroundColor, Color::GREEN()).IsOk());
	REQUIRE(f.d.Set(cell, props::BorderWidth, 0.5).IsOk());
	const NodeId inCell = f.Para("dans la cellule", cell);
	const LayoutResult layout = Layout(f.d);
	const DisplayList l = RenderPage(f.d, layout, 0);
	const auto pFill = Of(l, DrawKind::FillRect, p), pStroke = Of(l, DrawKind::StrokeRect, p);
	REQUIRE(pFill.size() == 1);
	REQUIRE(pStroke.size() == 1);
	CHECK(pStroke[0]->color == Color::RED());
	CHECK_NEAR(pStroke[0]->lineWidth, 2.0, 1e-9);
	CHECK(IndexOf(l, pFill[0]) < IndexOf(l, pStroke[0]));
	CHECK(Of(l, DrawKind::StrokeRect, table).size() == 1);
	const auto cellFill = Of(l, DrawKind::FillRect, cell);
	REQUIRE(cellFill.size() == 1);
	CHECK(cellFill[0]->color == Color::GREEN());
	// Texte de la cellule au-dessus de son fond.
	const NodeId cellText = f.d.Find(inCell)->Children()[0];
	const auto text = Of(l, DrawKind::Text, cellText);
	REQUIRE(text.size() == 1);
	CHECK(IndexOf(l, cellFill[0]) < IndexOf(l, text[0]));
	CHECK(cellFill[0]->rect.Contains(text[0]->rect.x, text[0]->baseline));
}

TEST(Render, ImagesAndShapes) {
	Fixture f;
	const ResourceId res = f.d.AddResource({4, 4, std::vector<uint8_t>(64, 200)});
	const NodeId image = f.d.CreateNode(NodeType::Image, f.body).Value();
	REQUIRE(f.d.Set(image, props::Resource, int64_t(res)).IsOk());
	REQUIRE(f.d.Set(image, props::Opacity, 0.5).IsOk());
	const NodeId missing = f.d.CreateNode(NodeType::Image, f.body).Value();
	const NodeId ellipse = f.d.CreateNode(NodeType::Shape, f.body).Value();
	REQUIRE(f.d.Set(ellipse, props::Shape, ShapeKind::Ellipse).IsOk());
	REQUIRE(f.d.Set(ellipse, props::BackgroundColor, Color::BLUE()).IsOk());
	REQUIRE(f.d.Set(ellipse, props::BorderWidth, 1.0).IsOk());
	REQUIRE(f.d.Set(ellipse, props::Opacity, 0.5).IsOk());
	const NodeId line = f.d.CreateNode(NodeType::Shape, f.body).Value();
	REQUIRE(f.d.Set(line, props::Shape, ShapeKind::Line).IsOk());
	const DisplayList l = f.Render();
	const auto img = Of(l, DrawKind::Image, image);
	REQUIRE(img.size() == 1);
	CHECK(img[0]->image == res);
	CHECK_NEAR(img[0]->opacity, 0.5, 1e-9);
	CHECK_NEAR(img[0]->rect.w, 3.0, 1e-9); // 4 px à 96 ppp
	CHECK(Of(l, DrawKind::Image, missing).empty());
	CHECK_EQ(Of(l, DrawKind::Line, missing).size(), size_t(2)); // cadre barré
	const auto fill = Of(l, DrawKind::FillEllipse, ellipse);
	REQUIRE(fill.size() == 1);
	CHECK(fill[0]->color == Color(0, 0, 255, 128));
	CHECK(Of(l, DrawKind::StrokeEllipse, ellipse).size() == 1);
	const auto rule = Of(l, DrawKind::Line, line);
	REQUIRE(rule.size() == 1);
	CHECK_NEAR(rule[0]->rect.h, 0.0, 1e-9); // trait horizontal
	CHECK_NEAR(rule[0]->lineWidth, 1.0, 1e-9);
}

TEST(Render, OptionsAndParallelDocument) {
	Fixture f;
	for (int i = 0; i < 90; ++i)
		f.Para("ligne " + std::to_string(i));
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	(void)f.d.CreateNode(NodeType::TableCell, row);
	REQUIRE(f.d.Set(row, props::MinHeight, 2000.0).IsOk()); // ligne plus haute qu'une page
	const LayoutResult layout = Layout(f.d);
	const DisplayList plain = RenderPage(f.d, layout, 0);
	const DisplayList bounds = RenderPage(f.d, layout, 0, Options([](RenderOptions &o) { o.blockBounds = true; }));
	CHECK_EQ(bounds.commands.size(), plain.commands.size() + layout.pages[0].boxes.size());
	size_t overflowPage = layout.pages.size();
	for (size_t i = 0; i < layout.pages.size(); ++i)
		for (const LayoutBox &b : layout.pages[i].boxes)
			if (b.overflow)
				overflowPage = i;
	REQUIRE(overflowPage < layout.pages.size());
	const DisplayList marked = RenderPage(f.d, layout, overflowPage, Options([](RenderOptions &o) { o.overflowMarkers = true; }));
	CHECK(std::ranges::any_of(marked.commands, [](const DrawCommand &c) { return c.kind == DrawKind::StrokeRect && c.color == Color(220, 40, 40); }));
	const auto parallel = RenderDocument(f.d, layout);
	const auto sequential = RenderDocument(f.d, layout, Options([](RenderOptions &o) { o.parallel = false; }));
	REQUIRE(parallel.size() == layout.pages.size());
	for (size_t i = 0; i < parallel.size(); ++i) {
		REQUIRE(parallel[i].commands.size() == sequential[i].commands.size());
		CHECK_EQ(parallel[i].number, i + 1);
		for (size_t k = 0; k < parallel[i].commands.size(); ++k)
			CHECK(parallel[i].commands[k].text == sequential[i].commands[k].text);
	}
}
