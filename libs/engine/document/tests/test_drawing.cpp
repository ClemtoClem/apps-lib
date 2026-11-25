// Tests des formes et des zones de dessin d'engine.document : contours des formes, alignement
// horizontal « aucun », texte d'une forme (placement, rotation, saisie), zone de dessin (éléments
// placés librement), rendu en polygones, exports PDF / HTML et enregistrement.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <filesystem>
#include <string>

#include <document/commands.hpp>
#include <document/export.hpp>
#include <document/persistence.hpp>
#include <document/render.hpp>
#include <document/text.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	NodeId Para(std::string_view text, NodeId parent) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent).Value();
		(void)d.Set(d.CreateNode(NodeType::Text, p).Value(), props::Text, text);
		return p;
	}
	NodeId Shape(NodeId parent, ShapeKind kind, double w, double h) {
		const NodeId s = d.CreateNode(NodeType::Shape, parent).Value();
		(void)d.Set(s, props::Shape, kind);
		(void)d.Set(s, props::Width, w);
		(void)d.Set(s, props::Height, h);
		return s;
	}
	static const LayoutBox *Box(const LayoutResult &r, NodeId id) {
		for (const PageLayout &p : r.pages)
			for (const LayoutBox &b : p.boxes)
				if (b.node == id)
					return &b;
		return nullptr;
	}
};

/// Boîte englobante d'un contour.
Rect Bounds(const std::vector<double> &p) {
	double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
	for (size_t i = 0; i + 1 < p.size(); i += 2) {
		x0 = std::min(x0, p[i]);
		x1 = std::max(x1, p[i]);
		y0 = std::min(y0, p[i + 1]);
		y1 = std::max(y1, p[i + 1]);
	}
	return Rect(x0, y0, x1 - x0, y1 - y0);
}

} // namespace

TEST(Shapes, OutlinesFillTheirBox) {
	const Rect box(10, 20, 200, 100);
	for (int k = 0; k <= int(ShapeKind::ArrowLine); ++k) {
		const auto kind = ShapeKind(k);
		const std::vector<double> outline = ShapeOutline(kind, box);
		if (IsLineShape(kind)) {
			CHECK(outline.empty());
			continue;
		}
		REQUIRE(outline.size() >= 6);
		CHECK(outline.size() % 2 == 0);
		const Rect b = Bounds(outline);
		// Chaque contour touche les quatre bords de sa boîte (à un segment près pour les courbes).
		CHECK_NEAR(b.x, box.x, 1.0);
		CHECK_NEAR(b.y, box.y, 1.0);
		CHECK_NEAR(b.Right(), box.Right(), 1.0);
		CHECK_NEAR(b.Bottom(), box.Bottom(), 1.0);
	}
	CHECK(ShapeOutline(ShapeKind::Star5, box).size() == 20); // 10 sommets
	CHECK(ShapeOutline(ShapeKind::Triangle, box).size() == 6);
	const std::vector<double> head = ArrowHead(box, 2);
	REQUIRE(head.size() == 6);
	CHECK_NEAR(head[0], box.Right(), 1e-9); // pointe au bout droit, à mi-hauteur
	CHECK_NEAR(head[1], box.y + box.h / 2, 1e-9);
	// Le catalogue accepte toutes les formes.
	CHECK(Validate(PropertyId::Shape, int64_t(ShapeKind::ArrowLine)).IsOk());
	CHECK(Validate(PropertyId::Shape, int64_t(ShapeKind::ArrowLine) + 1).IsError());
}

TEST(Shapes, PolygonsRenderedAndExported) {
	Fixture f;
	const NodeId star = f.Shape(f.body, ShapeKind::Star5, 120, 120);
	REQUIRE(f.d.Set(star, props::BackgroundColor, Color::YELLOW()).IsOk());
	REQUIRE(f.d.Set(star, props::BorderWidth, 2.0).IsOk());
	const NodeId arrow = f.Shape(f.body, ShapeKind::ArrowLine, 150, 0);
	REQUIRE(f.d.Set(arrow, props::BorderWidth, 2.0).IsOk());
	const LayoutResult r = Layout(f.d);
	const DisplayList list = RenderPage(f.d, r, 0);
	int fills = 0, strokes = 0, heads = 0, lines = 0;
	for (const DrawCommand &c : list.commands) {
		if (c.node == star && c.kind == DrawKind::FillPolygon && c.points.size() == 20)
			++fills;
		if (c.node == star && c.kind == DrawKind::StrokePolygon && c.lineWidth == 2.0)
			++strokes;
		if (c.node == arrow && c.kind == DrawKind::FillPolygon && c.points.size() == 6)
			++heads;
		if (c.node == arrow && c.kind == DrawKind::Line)
			++lines;
	}
	CHECK(fills == 1);
	CHECK(strokes == 1);
	CHECK(heads == 1);
	CHECK(lines == 1);
	auto pdf = ExportPdf(f.d, r, {});
	REQUIRE(pdf.IsOk());
	CHECK(pdf.Value().size() > 500);
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("<polygon points=") != std::string::npos);
	CHECK(html.Value().find("<line x1=") != std::string::npos);
}

TEST(Shapes, AlignNoneKeepsPosition) {
	Fixture f;
	const NodeId s = f.Shape(f.body, ShapeKind::Rectangle, 100, 40);
	REQUIRE(f.d.Set(s, props::HAlign, HorizontalAlign::Center).IsOk());
	LayoutResult r = Layout(f.d);
	const double column = r.pages[0].content.x, width = r.pages[0].content.w;
	REQUIRE(Fixture::Box(r, s) != nullptr);
	CHECK_NEAR(Fixture::Box(r, s)->rect.x, column + (width - 100) / 2, 1e-9);
	CHECK_NEAR(Fixture::Box(r, s)->alignX, (width - 100) / 2, 1e-9);
	// Aucun : la place est le bord de la colonne, seule la position compte.
	REQUIRE(f.d.Set(s, props::HAlign, HorizontalAlign::None).IsOk());
	REQUIRE(f.d.Set(s, props::PositionX, 42.0).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(Fixture::Box(r, s)->rect.x, column + 42, 1e-9);
	CHECK_NEAR(Fixture::Box(r, s)->alignX, 0.0, 1e-9);
	// Texte : « aucun » se comporte comme « gauche » (mise en page et HTML).
	const NodeId p = f.Para("texte", f.body);
	REQUIRE(f.d.Set(p, props::HAlign, HorizontalAlign::None).IsOk());
	r = Layout(f.d);
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("text-align: left") != std::string::npos);
}

TEST(Shapes, TextInsideRotatesWithShape) {
	Fixture f;
	const NodeId s = f.Shape(f.body, ShapeKind::Ellipse, 200, 100);
	REQUIRE(f.d.Set(s, props::VAlign, VerticalAlign::Middle).IsOk());
	const NodeId p = f.Para("Bonjour", s);
	LayoutResult r = Layout(f.d);
	const LayoutBox *shape = Fixture::Box(r, s);
	REQUIRE(shape != nullptr);
	const PlacedLine *line = nullptr;
	for (const PlacedLine &l : r.pages[0].lines)
		if (l.paragraph == p)
			line = &l;
	REQUIRE(line != nullptr);
	// Texte centré verticalement dans la forme.
	CHECK(line->rect.y > shape->rect.y + 20);
	CHECK(line->rect.Bottom() < shape->rect.Bottom() - 20);
	CHECK(line->rotation == 0.0);
	// Hauteur automatique : celle du texte.
	REQUIRE(f.d.Set(s, props::Height, 0.0).IsOk());
	r = Layout(f.d);
	CHECK(Fixture::Box(r, s)->rect.h > 5);
	CHECK(Fixture::Box(r, s)->rect.h < 40);
	// Forme tournée : son texte aussi (rendu, test de position, curseur).
	REQUIRE(f.d.Set(s, props::Height, 100.0).IsOk());
	REQUIRE(f.d.Set(s, props::Rotation, 90.0).IsOk());
	r = Layout(f.d);
	shape = Fixture::Box(r, s);
	for (const PlacedLine &l : r.pages[0].lines)
		if (l.paragraph == p)
			line = &l;
	CHECK_NEAR(line->rotation, 90.0, 1e-9);
	CHECK_NEAR(line->pivotX, shape->pivotX, 1e-9);
	const DisplayList list = RenderPage(f.d, r, 0);
	bool rotatedText = false;
	for (const DrawCommand &c : list.commands)
		if (c.kind == DrawKind::Text && c.text == "Bonjour")
			rotatedText = c.rotation == 90.0;
	CHECK(rotatedText);
	// Point sur le texte tourné d'un quart de tour : retrouvé par le test de position.
	const double cx = line->rect.x + line->rect.w / 2, cy = line->rect.y + line->rect.h / 2;
	const double dx = cx - line->pivotX, dy = cy - line->pivotY;
	const double sx = line->pivotX - dy, sy = line->pivotY + dx; // rotation horaire de 90°
	const NodeId text = f.d.Find(p)->Children().front();
	CHECK(r.HitTest(0, sx, sy) == text);
	const auto pos = text::PositionAt(f.d, r, 0, sx, sy);
	REQUIRE(pos.has_value());
	CHECK(pos->text == text);
	const auto caret = text::Caret(f.d, r, TextPosition{text, 0});
	REQUIRE(caret.has_value());
	CHECK_NEAR(caret->rotation, 90.0, 1e-9);
	// Le texte d'une forme est son propre fil : la navigation ne sort pas de la forme.
	const NodeId after = f.Para("après", f.body);
	const TextOrder order(f.d);
	const auto story = order.StoryRange(TextPosition{text, 0});
	CHECK(story.first.text == text);
	CHECK(story.second.text == text); // « après » (corps) n'en fait pas partie
	CHECK(order.StoryRange(TextPosition{f.d.Find(after)->Children().front(), 0}).first.text != text);
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("justify-content: center") != std::string::npos);
	CHECK(html.Value().find("Bonjour") != std::string::npos);
}

TEST(Drawing, ItemsPlacedFreely) {
	Fixture f;
	f.Para("avant", f.body);
	const NodeId drawing = f.d.CreateNode(NodeType::Drawing, f.body).Value();
	REQUIRE(f.d.Set(drawing, props::Height, 150.0).IsOk());
	REQUIRE(f.d.Set(drawing, props::BorderWidth, 1.0).IsOk());
	REQUIRE(f.d.Set(drawing, props::PaddingLeft, 10.0).IsOk());
	REQUIRE(f.d.Set(drawing, props::PaddingTop, 5.0).IsOk());
	// Une forme très large n'est pas réduite à la colonne, ni alignée : seule sa position compte.
	const NodeId wide = f.Shape(drawing, ShapeKind::Hexagon, 900, 30);
	REQUIRE(f.d.Set(wide, props::HAlign, HorizontalAlign::Center).IsOk());
	const NodeId box = f.Shape(drawing, ShapeKind::Rectangle, 80, 40);
	REQUIRE(f.d.Set(box, props::PositionX, 30.0).IsOk());
	REQUIRE(f.d.Set(box, props::PositionY, 60.0).IsOk());
	REQUIRE(f.d.Set(box, props::Rotation, 45.0).IsOk());
	f.Para("étiquette", box); // texte à n'importe quelle position et inclinaison
	CHECK(f.d.CreateNode(NodeType::Paragraph, drawing).IsError()); // texte : dans une forme
	CHECK(f.d.CreateNode(NodeType::Table, drawing).IsError());
	f.Para("après", f.body);
	LayoutResult r = Layout(f.d);
	const LayoutBox *area = Fixture::Box(r, drawing), *w = Fixture::Box(r, wide), *b = Fixture::Box(r, box);
	REQUIRE(area && w && b);
	CHECK_NEAR(area->rect.w, r.pages[0].content.w, 1e-9); // largeur : la colonne par défaut
	CHECK_NEAR(area->rect.h, 150.0, 1e-9);
	CHECK_NEAR(w->rect.w, 900.0, 1e-9);
	CHECK_NEAR(w->rect.x, area->rect.x + 1 + 10, 1e-9);
	CHECK_NEAR(b->rect.x, area->rect.x + 1 + 10 + 30, 1e-9);
	CHECK_NEAR(b->rect.y, area->rect.y + 1 + 5 + 60, 1e-9);
	CHECK(b->depth > area->depth);
	CHECK_NEAR(b->parentX, area->rect.x + 11, 1e-9);
	// Le texte de la forme tourne avec elle.
	bool rotated = false;
	for (const PlacedLine &l : r.pages[0].lines)
		if (!l.runs.empty() && l.runs.front().text == "étiquette")
			rotated = l.rotation == 45.0;
	CHECK(rotated);
	// Le paragraphe suivant vient sous la zone.
	for (const PlacedLine &l : r.pages[0].lines)
		if (!l.runs.empty() && l.runs.front().text == "après")
			CHECK(l.rect.y >= area->rect.Bottom());
	// Rendu, exports et enregistrement.
	const DisplayList list = RenderPage(f.d, r, 0);
	bool frame = false;
	for (const DrawCommand &c : list.commands)
		frame = frame || (c.node == drawing && c.kind == DrawKind::StrokeRect);
	CHECK(frame);
	REQUIRE(ExportPdf(f.d, r, {}).IsOk());
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("position: absolute; left: 40pt; top: 65pt") != std::string::npos);
	const auto path = std::filesystem::temp_directory_path() / "apps-lib-document-drawing.docdb";
	REQUIRE(SaveDocument(f.d, path).IsOk());
	auto loaded = LoadDocument(path);
	REQUIRE(loaded.IsOk());
	const Document &d2 = loaded.Value();
	REQUIRE(d2.Contains(drawing));
	CHECK(d2.Find(drawing)->Type() == NodeType::Drawing);
	CHECK(d2.Find(drawing)->Children().size() == 2);
	CHECK(d2.Get(wide, props::Shape) == ShapeKind::Hexagon);
	std::filesystem::remove(path);
}

TEST(Drawing, KeepAspectRatioProperty) {
	Fixture f;
	const NodeId s = f.Shape(f.body, ShapeKind::Rectangle, 10, 10);
	CHECK(!f.d.Get(s, props::KeepAspectRatio));
	REQUIRE(f.d.Set(s, props::KeepAspectRatio, true).IsOk());
	CHECK(f.d.Get(s, props::KeepAspectRatio));
	CHECK(AppliesTo(PropertyId::KeepAspectRatio, NodeType::Image));
	CHECK(AppliesTo(PropertyId::KeepAspectRatio, NodeType::Drawing));
	CHECK(!AppliesTo(PropertyId::KeepAspectRatio, NodeType::Paragraph));
}
