// Tests des tableaux d'engine.document : création rapide (grands tableaux), insertion,
// suppression, duplication et déplacement de lignes et de colonnes (mise en forme reprise,
// annulation d'un bloc), largeurs de colonnes, hauteurs de lignes, bordures, lignes alternées.
#include <cpp-maker/test.hpp>

#include <chrono>
#include <cstdio>
#include <string>

#include <document/export.hpp>
#include <document/render.hpp>
#include <document/tables.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	History h{d};
	NodeId body = d.Body(d.Sections().front());

	/// Texte de la cellule (premier segment du premier paragraphe).
	std::string Text(NodeId cell) const {
		const Node *p = d.Find(d.Find(cell)->Children().front());
		return d.Get(p->Children().front(), props::Text);
	}
	void Fill(NodeId table) {
		const TableGrid g = TableGrid::Of(d, table);
		for (size_t r = 0; r < g.rows.size(); ++r)
			for (size_t c = 0; c < g.cells[r].size(); ++c) {
				const Node *p = d.Find(d.Find(g.cells[r][c])->Children().front());
				(void)d.Set(p->Children().front(), props::Text, std::format("{}{}", char('A' + c), r));
			}
	}
	std::string Row(NodeId table, size_t r) const {
		const TableGrid g = TableGrid::Of(d, table);
		std::string s;
		for (NodeId c : g.cells[r])
			s += Text(c) + " ";
		return s;
	}
};

} // namespace

TEST(Tables, CreateLargeTableInOneEntry) {
	Fixture f;
	const auto start = std::chrono::steady_clock::now();
	auto t = table::Create(f.h, f.body, kAppend, 2000, 40);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	REQUIRE(t.IsOk());
	const TableGrid g = TableGrid::Of(f.d, t.Value());
	CHECK_EQ(g.rows.size(), size_t(2000));
	CHECK_EQ(g.columns, size_t(40));
	CHECK_EQ(f.h.Entries().size(), size_t(1));
	CHECK_EQ(f.d.SubtreeOf(t.Value()).size(), size_t(1 + 2000 + 2000 * 40 * 3));
	const auto since = [](auto t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
	LayoutCache cache;
	auto t0 = std::chrono::steady_clock::now();
	LayoutResult r = Layout(f.d, &cache);
	const double coldMs = since(t0);
	CHECK(r.pages.size() > 10);
	// Frappe dans une cellule : seule cette cellule est remesurée.
	const NodeId cell = TableGrid::Of(f.d, t.Value()).Cell(1234, 17);
	const NodeId text = f.d.Find(f.d.Find(cell)->Children().front())->Children().front();
	(void)f.d.Set(text, props::Text, "valeur modifiée");
	t0 = std::chrono::steady_clock::now();
	r = Layout(f.d, &cache);
	const double editMs = since(t0);
	CHECK_EQ(r.stats.measured, size_t(1)); // le tableau (assemblage)…
	CHECK_EQ(r.stats.cells, size_t(1));	   // …et la seule cellule modifiée
	bool found = false;
	for (const PageLayout &p : r.pages)
		for (const PlacedLine &l : p.lines)
			found = found || (l.paragraph == f.d.Find(text)->Parent() && !l.runs.empty() && l.runs.front().node == text);
	CHECK(found);
	// Sans modification : tout repris.
	t0 = std::chrono::steady_clock::now();
	r = Layout(f.d, &cache);
	const double warmMs = since(t0);
	CHECK_EQ(r.stats.measured + r.stats.cells, size_t(0));
	std::printf("    tableau 2000 × 40 (240 001 nœuds) : création %.0f ms, mise en page %.0f ms à froid, %.0f ms après frappe, %.0f ms sans "
				"modification, %zu pages\n",
				ms, coldMs, editMs, warmMs, r.pages.size());
	REQUIRE(f.h.Undo().IsOk());
	CHECK(!f.d.Contains(t.Value()));
	CHECK(table::Create(f.h, f.body, kAppend, 2, 1001).IsError());
	CHECK(table::Create(f.h, f.body, kAppend, 0, 3).IsError());
	CHECK(table::Create(f.h, f.d.Root(), kAppend, 2, 2).IsError()); // parent refusé : rien ne reste
	CHECK_EQ(f.h.Entries().size(), size_t(1));
}

TEST(Tables, InsertRowsAndColumnsCopyFormatting) {
	Fixture f;
	PropertyMap cell;
	cell.Set(PropertyId::PaddingLeft, 4.0);
	const NodeId t = table::Create(f.h, f.body, kAppend, 2, 2, cell).Value();
	f.Fill(t);
	TableGrid g = TableGrid::Of(f.d, t);
	(void)f.d.Set(g.cells[1][1], props::BackgroundColor, Color::YELLOW());
	(void)f.d.Set(g.cells[0][0], props::Width, 120.0);
	REQUIRE(f.d.Set(f.d.Find(f.d.Find(g.cells[1][1])->Children().front())->Children().front(), props::Bold, true).IsOk());
	// Deux lignes après la dernière : fond et gras de la ligne voisine, sans le texte.
	auto rows = table::InsertRows(f.h, t, 2, 2);
	REQUIRE(rows.IsOk());
	CHECK_EQ(rows.Value().size(), size_t(2));
	g = TableGrid::Of(f.d, t);
	REQUIRE(g.rows.size() == 4);
	CHECK(f.d.Get(g.cells[3][1], props::BackgroundColor) == Color::YELLOW());
	CHECK(f.Text(g.cells[3][1]).empty());
	CHECK(f.d.Get(f.d.Find(f.d.Find(g.cells[2][1])->Children().front())->Children().front(), props::Bold));
	CHECK_NEAR(f.d.Get(g.cells[2][0], props::PaddingLeft), 4.0, 1e-9);
	REQUIRE(table::InsertRows(f.h, t, 0).IsOk());
	CHECK(f.Row(t, 1) == "A0 B0 ");
	// Colonne au rang 1 : mise en forme de la colonne 0, sans sa largeur.
	REQUIRE(table::InsertColumns(f.h, t, 1, 1).IsOk());
	g = TableGrid::Of(f.d, t);
	CHECK_EQ(g.columns, size_t(3));
	CHECK(f.Row(t, 1) == "A0  B0 ");
	CHECK(!f.d.Find(g.cells[0][1])->Properties().Contains(PropertyId::Width));
	CHECK_NEAR(f.d.Get(g.cells[2][1], props::PaddingLeft), 4.0, 1e-9);
	CHECK(table::InsertRows(f.h, t, 99).IsError());
	CHECK(table::InsertColumns(f.h, t, 9).IsError());
	// Une entrée par opération ; annuler la colonne la retire de chaque ligne.
	REQUIRE(f.h.Undo().IsOk());
	CHECK_EQ(TableGrid::Of(f.d, t).columns, size_t(2));
}

TEST(Tables, DeleteDuplicateAndMove) {
	Fixture f;
	const NodeId t = table::Create(f.h, f.body, kAppend, 5, 4).Value();
	f.Fill(t);
	REQUIRE(table::DuplicateRows(f.h, t, 1, 2).IsOk());
	CHECK_EQ(TableGrid::Of(f.d, t).rows.size(), size_t(7));
	CHECK(f.Row(t, 3) == "A1 B1 C1 D1 ");
	CHECK(f.Row(t, 4) == "A2 B2 C2 D2 ");
	REQUIRE(table::DeleteRows(f.h, t, 3, 2).IsOk());
	CHECK(f.Row(t, 3) == "A3 B3 C3 D3 ");
	// Lignes 3 et 4 en tête, puis la ligne 0 à la fin.
	REQUIRE(table::MoveRows(f.h, t, 3, 2, 0).IsOk());
	CHECK(f.Row(t, 0) == "A3 B3 C3 D3 ");
	CHECK(f.Row(t, 1) == "A4 B4 C4 D4 ");
	CHECK(f.Row(t, 2) == "A0 B0 C0 D0 ");
	REQUIRE(table::MoveRows(f.h, t, 0, 1, 5).IsOk());
	CHECK(f.Row(t, 4) == "A3 B3 C3 D3 ");
	CHECK(f.Row(t, 0) == "A4 B4 C4 D4 ");
	CHECK(table::MoveRows(f.h, t, 1, 2, 2).Error().code == ErrorCode::Nothing);
	// Colonnes : duplication, déplacement vers la droite puis la gauche, suppression.
	REQUIRE(table::DuplicateColumns(f.h, t, 0, 1).IsOk());
	CHECK(f.Row(t, 0) == "A4 A4 B4 C4 D4 ");
	REQUIRE(table::MoveColumns(f.h, t, 0, 2, 5).IsOk());
	CHECK(f.Row(t, 0) == "B4 C4 D4 A4 A4 ");
	REQUIRE(table::MoveColumns(f.h, t, 2, 1, 0).IsOk());
	CHECK(f.Row(t, 0) == "D4 B4 C4 A4 A4 ");
	REQUIRE(table::DeleteColumns(f.h, t, 3, 2).IsOk());
	CHECK(f.Row(t, 0) == "D4 B4 C4 ");
	// Annulation exacte de l'ensemble.
	while (f.h.CanUndo())
		REQUIRE(f.h.Undo().IsOk());
	CHECK(!f.d.Contains(t));
	while (f.h.CanRedo())
		REQUIRE(f.h.Redo().IsOk());
	CHECK(f.Row(t, 0) == "D4 B4 C4 ");
	// Tout supprimer : le tableau disparaît.
	REQUIRE(table::DeleteColumns(f.h, t, 0, 3).IsOk());
	CHECK(!f.d.Contains(t));
}

TEST(Tables, WidthsHeightsBordersAndBands) {
	Fixture f;
	const NodeId t = table::Create(f.h, f.body, kAppend, 4, 3).Value();
	REQUIRE(table::SetColumnWidth(f.h, t, 0, 200).IsOk());
	TableGrid g = TableGrid::Of(f.d, t);
	REQUIRE(table::SetRowHeight(f.h, g.rows[1], 60).IsOk());
	LayoutResult r = Layout(f.d);
	const auto box = [&](NodeId id) {
		for (const LayoutBox &b : r.pages[0].boxes)
			if (b.node == id)
				return b.rect;
		return Rect();
	};
	CHECK_NEAR(box(g.cells[2][0]).w, 200, 1e-6);
	CHECK_NEAR(box(g.cells[2][1]).w, (r.pages[0].content.w - 200) / 2, 1e-6);
	CHECK_NEAR(box(g.rows[1]).h, 60, 1e-6);
	// Largeur posée ailleurs dans la colonne : la nouvelle valeur l'emporte.
	(void)f.d.Set(g.cells[3][2], props::Width, 50.0);
	REQUIRE(table::SetColumnWidth(f.h, t, 2, 120).IsOk());
	CHECK(!f.d.Find(g.cells[3][2])->Properties().Contains(PropertyId::Width));
	REQUIRE(table::DistributeColumns(f.h, t).IsOk());
	r = Layout(f.d);
	CHECK_NEAR(box(g.cells[0][0]).w, r.pages[0].content.w / 3, 1e-6);
	CHECK(table::SetRowHeight(f.h, g.rows[0], 0).Error().code == ErrorCode::Nothing);
	// Filets horizontaux, cadre extérieur, lignes alternées.
	REQUIRE(table::ApplyBorders(f.h, t, BorderPreset::Horizontal, 0.75, Color::GREY1()).IsOk());
	CHECK_EQ(f.d.Get(g.cells[1][1], props::BorderSides), border::kTop | border::kBottom);
	CHECK_NEAR(f.d.Get(t, props::BorderWidth), 0.0, 1e-9);
	REQUIRE(table::ApplyBorders(f.h, t, BorderPreset::Outer, 1, Color::BLACK()).IsOk());
	CHECK_EQ(f.d.Get(g.cells[0][0], props::BorderSides), border::kTop | border::kLeft);
	CHECK_NEAR(f.d.Get(g.cells[1][1], props::BorderWidth), 0.0, 1e-9);
	CHECK_EQ(f.d.Get(g.cells[3][2], props::BorderSides), border::kBottom | border::kRight);
	REQUIRE(table::BandRows(f.h, t, Color::WHITE(), Color::ALICE_BLUE(), true).IsOk());
	CHECK(f.d.Get(g.cells[0][1], props::BackgroundColor) == Color::TRANSPARENT());
	CHECK(f.d.Get(g.cells[1][1], props::BackgroundColor) == Color::WHITE());
	CHECK(f.d.Get(g.cells[2][0], props::BackgroundColor) == Color::ALICE_BLUE());
	// Sélection rectangulaire et colonne.
	CHECK_EQ(g.Range(2, 2, 1, 0).size(), size_t(6));
	CHECK_EQ(g.Column(1).size(), size_t(4));
	size_t row = 0, col = 0;
	const NodeId inner = f.d.Find(g.cells[2][1])->Children().front();
	REQUIRE(g.Find(f.d, inner, row, col));
	CHECK_EQ(row, size_t(2));
	CHECK_EQ(col, size_t(1));
	const std::vector<NodeId> cells = g.Range(0, 0, 0, 2);
	REQUIRE(table::SetProperty(f.h, cells, PropertyId::BackgroundColor, PropertyValue(Color::LIGHT_YELLOW())).IsOk());
	CHECK(f.d.Get(g.cells[0][2], props::BackgroundColor) == Color::LIGHT_YELLOW());
	CHECK(table::SetColumnWidth(f.h, t, 7, 10).IsError());
	CHECK(table::InsertRows(f.h, f.body, 0).IsError()); // pas un tableau
}

TEST(Tables, MergeAndSplitCells) {
	Fixture f;
	auto t = table::Create(f.h, f.body, kAppend, 4, 4);
	REQUIRE(t.IsOk());
	const NodeId table = t.Value();
	f.Fill(table);
	TableGrid g = TableGrid::Of(f.d, table);
	// Rectangle 2 × 2 (lignes 0-1, colonnes 1-2) : l'ancre B0 couvre C0, B1, C1 et reçoit leur texte.
	REQUIRE(table::MergeCells(f.h, table, 1, 2, 0, 1).IsOk()); // bornes dans n'importe quel ordre
	const NodeId anchor = g.cells[0][1];
	CHECK(f.d.Get(anchor, props::ColumnSpan) == 2);
	CHECK(f.d.Get(anchor, props::RowSpan) == 2);
	CHECK(f.d.Find(anchor)->Children().size() == 4); // B0, C0, B1, C1
	CHECK(f.Text(g.cells[0][2]).empty());			 // case couverte : un paragraphe vide
	const std::vector<CellSpan> spans = table::Spans(f.d, g.cells);
	REQUIRE(spans.size() == 1);
	CHECK(spans.front() == (CellSpan{0, 1, 2, 2}));
	CHECK(table::SpanAt(spans, 1, 2) == spans.front());
	CHECK(table::SpanAt(spans, 3, 3) == (CellSpan{3, 3, 1, 1}));
	// Une fusion qui couperait celle-ci est refusée ; une case seule : rien à faire.
	CHECK(table::MergeCells(f.h, table, 0, 2, 0, 3).IsError());
	CHECK(table::MergeCells(f.h, table, 3, 3, 3, 3).Error().code == ErrorCode::Nothing);
	// Mise en page : l'ancre fait deux colonnes de large et deux lignes de haut ; pas de boîte
	// pour les cases couvertes ; le HTML porte colspan / rowspan.
	const LayoutResult r = Layout(f.d);
	const LayoutBox *a = nullptr, *covered = nullptr, *b0 = nullptr, *b2 = nullptr;
	for (const LayoutBox &box : r.pages[0].boxes) {
		if (box.node == anchor)
			a = &box;
		if (box.node == g.cells[1][2])
			covered = &box;
		if (box.node == g.cells[0][0])
			b0 = &box;
		if (box.node == g.cells[2][0])
			b2 = &box;
	}
	REQUIRE(a && b0 && b2);
	CHECK(covered == nullptr);
	CHECK_NEAR(a->rect.w, 2 * b0->rect.w, 1e-6);
	CHECK_NEAR(a->rect.Bottom(), b2->rect.y, 1e-6); // jusqu'au haut de la ligne 2
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find("colspan=\"2\" rowspan=\"2\"") != std::string::npos);
	// Fusion englobante : la précédente est absorbée.
	REQUIRE(table::MergeCells(f.h, table, 0, 0, 2, 3).IsOk());
	g = TableGrid::Of(f.d, table);
	CHECK(f.d.Get(anchor, props::RowSpan) == 1);
	CHECK(table::Spans(f.d, g.cells).size() == 1);
	CHECK(table::Spans(f.d, g.cells).front() == (CellSpan{0, 0, 3, 4}));
	// Séparer : étendues remises à 1, le contenu reste dans l'ancre.
	REQUIRE(table::SplitCells(f.h, table, 1, 1, 1, 1).IsOk());
	CHECK(table::Spans(f.d, TableGrid::Of(f.d, table).cells).empty());
	CHECK(table::SplitCells(f.h, table, 0, 0, 3, 3).Error().code == ErrorCode::Nothing);
	// Annulable : chaque opération est une entrée.
	REQUIRE(f.h.Undo().IsOk());
	CHECK(table::Spans(f.d, TableGrid::Of(f.d, table).cells).size() == 1);
}

TEST(Tables, MergedRowsStayOnOnePage) {
	Fixture f;
	auto t = table::Create(f.h, f.body, kAppend, 60, 2);
	REQUIRE(t.IsOk());
	const NodeId table = t.Value();
	TableGrid g = TableGrid::Of(f.d, table);
	// Fusion verticale de 10 lignes autour de la coupure de la première page.
	LayoutResult before = Layout(f.d);
	size_t firstOnPage2 = 0;
	for (size_t r = 0; r < g.rows.size(); ++r)
		if (r > 0) {
			size_t page = 0;
			double y = 0;
			if (before.Locate(g.rows[r], page, y) && page == 1) {
				firstOnPage2 = r;
				break;
			}
		}
	REQUIRE(firstOnPage2 > 5);
	const size_t top = firstOnPage2 - 5;
	REQUIRE(table::MergeCells(f.h, table, top, 0, top + 9, 0).IsOk());
	const LayoutResult after = Layout(f.d);
	size_t pageTop = 0, pageBottom = 0;
	double y = 0;
	REQUIRE(after.Locate(g.rows[top], pageTop, y));
	REQUIRE(after.Locate(g.rows[top + 9], pageBottom, y));
	CHECK(pageTop == pageBottom); // les lignes liées ne sont pas coupées
	CHECK(pageTop == 1);
	// Une cellule fusionnée au contenu plus haut que ses lignes agrandit la dernière.
	const NodeId anchor = g.cells[0][1];
	REQUIRE(table::MergeCells(f.h, table, 0, 1, 1, 1).IsOk());
	for (int i = 0; i < 8; ++i)
		(void)f.d.CreateNode(NodeType::Paragraph, anchor);
	const NodeId p = f.d.Find(anchor)->Children().front();
	(void)f.d.Set(f.d.Find(p)->Children().front(), props::Text, std::string("haut"));
	PropertyCache cache;
	const BlockPtr measured = MeasureBlock(f.d, cache, table, 400);
	CHECK(measured->rows[1].height > measured->rows[2].height);
}
