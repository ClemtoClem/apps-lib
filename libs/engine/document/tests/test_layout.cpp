// Tests de mise en page d'engine.document : géométrie des pages, style de texte, coupure des
// lignes et alignements, images, formes, tableaux, pagination (sauts, KeepWithNext,
// KeepTogether, lignes orphelines / veuves, MaximumLines, tableaux coupés avec en-tête répété),
// en-têtes / pieds et leurs variantes, champs de numérotation, synchronisation des pages,
// cache incrémental, mesure parallèle, mise en page d'un instantané sur un autre fil.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include <document/layout.hpp>

using namespace doc;

namespace {

constexpr double kLine = 11 * 1.2 * 1.15; // ligne par défaut : corps 11, interligne 1,15
const double kContentW = StandardSize(PageFormat::A4).width - 144;
const double kContentH = StandardSize(PageFormat::A4).height - 144;

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	NodeId Text(NodeId paragraph, std::string_view text) {
		const NodeId t = d.CreateNode(NodeType::Text, paragraph).Value();
		(void)d.Set(t, props::Text, text);
		return t;
	}
	NodeId Para(std::string_view text, NodeId parent = kNoNode) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent == kNoNode ? body : parent).Value();
		if (!text.empty())
			Text(p, text);
		return p;
	}
	NodeId Filler(double height) { // forme insécable de hauteur donnée, sans espacement
		const NodeId s = d.CreateNode(NodeType::Shape, body).Value();
		(void)d.Set(s, props::Height, height);
		(void)d.Set(s, props::SpaceAfter, 0.0);
		return s;
	}
	LayoutResult Run(LayoutCache *cache = nullptr) { return Layout(d, cache); }
};

std::vector<size_t> PagesOf(const LayoutResult &r, NodeId n) { return r.PagesOf(n); }

std::string LineText(const PlacedLine &l) {
	std::string s;
	for (const GlyphRun &r : l.runs)
		s += r.text;
	return s;
}

std::string Squeeze(std::string s) {
	std::erase(s, ' ');
	return s;
}

} // namespace

// ── Géométrie et styles ──

TEST(Geometry, FormatsOrientationAndCascade) {
	Fixture f;
	PageGeometry g = ResolvePageGeometry(f.d, f.section, kNoNode);
	CHECK_NEAR(g.width, 595.276, 1e-3);
	CHECK_NEAR(g.margins.left, 72.0, 1e-9);
	REQUIRE(f.d.Set(f.section, props::PageOrientation, Orientation::Landscape).IsOk());
	g = ResolvePageGeometry(f.d, f.section, kNoNode);
	CHECK_NEAR(g.width, 841.89, 1e-2);
	REQUIRE(f.d.Set(f.d.Root(), props::Format, PageFormat::Custom).IsOk());
	REQUIRE(f.d.Set(f.d.Root(), props::PageWidth, 300.0).IsOk());
	REQUIRE(f.d.Set(f.d.Root(), props::PageHeight, 500.0).IsOk());
	g = ResolvePageGeometry(f.d, f.section, kNoNode);
	CHECK_NEAR(g.width, 500.0, 1e-9); // paysage
	CHECK_NEAR(g.height, 300.0, 1e-9);
	// Le groupe de pages de la section l'emporte sur la section pour une page virtuelle.
	const NodeId group = NodeId(std::get<int64_t>(*f.d.Explicit(f.section, PropertyId::PageGroup)));
	REQUIRE(f.d.Set(group, props::PageOrientation, Orientation::Portrait).IsOk());
	CHECK_NEAR(ResolvePageGeometry(f.d, f.section, kNoNode).width, 300.0, 1e-9);
	// Marges impossibles ramenées dans la page.
	REQUIRE(f.d.Set(f.section, props::MarginLeft, 5000.0).IsOk());
	CHECK(ResolvePageGeometry(f.d, f.section, kNoNode).margins.left < 300.0 * 0.5);
}

TEST(Geometry, TextStyleMatchesStandardFonts) {
	Fixture f;
	const NodeId p = f.Para("x");
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(p, props::FontFamily, "Times New Roman").IsOk());
	REQUIRE(f.d.Set(t, props::Bold, true).IsOk());
	REQUIRE(f.d.Set(t, props::TextColor, Color::RED()).IsOk());
	PropertyCache cache;
	const TextStyle s = ResolveTextStyle(f.d, cache, t);
	CHECK(s.font.standard == pdf::StandardFont::TimesBold && !s.font.face);
	CHECK(s.color == Color::RED());
	CHECK_NEAR(s.size, 11.0, 1e-9);
}

// ── Lignes ──

TEST(Lines, WrapAlignAndJustify) {
	Fixture f;
	const std::string text = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";
	const NodeId p = f.Para(text);
	PropertyCache cache;
	const double width = 120;
	BlockPtr b = MeasureBlock(f.d, cache, p, width);
	REQUIRE(b->lines.size() > 2);
	for (const LineBox &l : b->lines)
		CHECK(l.width <= width + 1e-9);
	CHECK_NEAR(b->lines[0].height, kLine, 1e-9);
	CHECK_NEAR(b->height, kLine * double(b->lines.size()), 1e-9);
	// Aucun mot perdu ni coupé.
	std::string joined;
	for (const LineBox &l : b->lines)
		for (const GlyphRun &r : l.runs)
			joined += r.text + " ";
	CHECK(Squeeze(joined) == Squeeze(text));

	REQUIRE(f.d.Set(p, props::HAlign, HorizontalAlign::Right).IsOk());
	b = MeasureBlock(f.d, cache, p, width);
	CHECK_NEAR(b->lines[0].runs.back().x + b->lines[0].runs.back().width, width, 1e-6);
	REQUIRE(f.d.Set(p, props::HAlign, HorizontalAlign::Center).IsOk());
	b = MeasureBlock(f.d, cache, p, width);
	CHECK_NEAR(b->lines[0].runs.front().x, (width - b->lines[0].width) / 2, 1e-6);
	REQUIRE(f.d.Set(p, props::HAlign, HorizontalAlign::Justify).IsOk());
	b = MeasureBlock(f.d, cache, p, width);
	CHECK_NEAR(b->lines[0].runs.back().x + b->lines[0].runs.back().width, width, 1e-6); // ligne pleine
	CHECK(b->lines.back().width < width);												// dernière ligne non étirée
}

TEST(Lines, BreaksLongWordsIndentsAndEmptyParagraphs) {
	Fixture f;
	const NodeId p = f.Para("a\nb\n\nc");
	PropertyCache cache;
	BlockPtr b = MeasureBlock(f.d, cache, p, 300);
	CHECK_EQ(b->lines.size(), size_t(4)); // ligne vide comprise
	const NodeId word = f.Para(std::string(80, 'm'));
	b = MeasureBlock(f.d, cache, word, 100);
	CHECK(b->lines.size() >= 5);
	for (const LineBox &l : b->lines)
		CHECK(l.width <= 100 + 1e-9);
	const NodeId empty = f.Para("");
	b = MeasureBlock(f.d, cache, empty, 300);
	REQUIRE(b->lines.size() == 1);
	CHECK_NEAR(b->height, kLine, 1e-9);
	// Retraits, retrait de première ligne, marge intérieure et bordure.
	const NodeId q = f.Para("un deux trois quatre cinq six sept huit neuf dix");
	REQUIRE(f.d.Set(q, props::IndentLeft, 20.0).IsOk());
	REQUIRE(f.d.Set(q, props::FirstLineIndent, 15.0).IsOk());
	REQUIRE(f.d.Set(q, props::PaddingTop, 4.0).IsOk());
	REQUIRE(f.d.Set(q, props::BorderWidth, 1.0).IsOk());
	b = MeasureBlock(f.d, cache, q, 200);
	CHECK_NEAR(b->x, 20.0, 1e-9);
	CHECK_NEAR(b->width, 180.0, 1e-9);
	CHECK_NEAR(b->lines[0].runs[0].x, 15.0, 1e-9);
	CHECK_NEAR(b->lines[1].runs[0].x, 0.0, 1e-9);
	CHECK_NEAR(b->height, kLine * double(b->lines.size()) + 4 + 2, 1e-9);
	// Mots sur plusieurs runs de styles différents : un seul mot, aucune coupure interne.
	const NodeId mixed = f.Para("Bon");
	const NodeId bold = f.Text(mixed, "jour monde");
	REQUIRE(f.d.Set(bold, props::Bold, true).IsOk());
	b = MeasureBlock(f.d, cache, mixed, 1000);
	REQUIRE(b->lines.size() == 1);
	CHECK(b->lines[0].runs.size() == 2);
	CHECK_NEAR(b->lines[0].runs[1].x, pdf::TextWidth(pdf::StandardFont::Helvetica, "Bon", 11), 1e-9);
}

// ── Images, formes, tableaux ──

TEST(Blocks, ImagesShapesAndTables) {
	Fixture f;
	const ResourceId r = f.d.AddResource({400, 200, std::vector<uint8_t>(400 * 200 * 4, 255)});
	const NodeId image = f.d.CreateNode(NodeType::Image, f.body).Value();
	REQUIRE(f.d.Set(image, props::Resource, int64_t(r)).IsOk());
	PropertyCache cache;
	BlockPtr b = MeasureBlock(f.d, cache, image, 1000);
	CHECK_NEAR(b->width, 300.0, 1e-9); // 400 px à 96 ppp
	CHECK_NEAR(b->height, 150.0, 1e-9);
	b = MeasureBlock(f.d, cache, image, 150); // réduite à la colonne, proportions gardées
	CHECK_NEAR(b->width, 150.0, 1e-9);
	CHECK_NEAR(b->height, 75.0, 1e-9);
	REQUIRE(f.d.Set(image, props::HAlign, HorizontalAlign::Center).IsOk());
	b = MeasureBlock(f.d, cache, image, 500);
	CHECK_NEAR(b->x, 100.0, 1e-9);

	const NodeId shape = f.d.CreateNode(NodeType::Shape, f.body).Value();
	b = MeasureBlock(f.d, cache, shape, 200);
	CHECK_NEAR(b->width, 200.0, 1e-9);
	CHECK_NEAR(b->height, 36.0, 1e-9);

	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	NodeId cells[2][3];
	for (int row = 0; row < 2; ++row) {
		const NodeId tr = f.d.CreateNode(NodeType::TableRow, table).Value();
		for (int c = 0; c < 3; ++c)
			cells[row][c] = f.d.CreateNode(NodeType::TableCell, tr).Value();
	}
	REQUIRE(f.d.Set(cells[0][0], props::Width, 100.0).IsOk());
	f.Para("une ligne", cells[0][1]);
	const NodeId tall = f.Para("a\nb\nc", cells[1][2]);
	(void)tall;
	REQUIRE(f.d.Set(cells[1][2], props::PaddingTop, 5.0).IsOk());
	b = MeasureBlock(f.d, cache, table, 400);
	REQUIRE(b->rows.size() == 2);
	CHECK_NEAR(b->rows[0].cells[0].width, 100.0, 1e-9);
	CHECK_NEAR(b->rows[0].cells[1].width, 150.0, 1e-9);
	CHECK_NEAR(b->rows[0].cells[2].x, 250.0, 1e-9);
	CHECK_NEAR(b->rows[0].height, kLine, 1e-9); // l'espace après du dernier bloc ne compte pas
	CHECK_NEAR(b->rows[1].height, 3 * kLine + 5, 1e-9);
	CHECK_NEAR(b->height, 4 * kLine + 5, 1e-9);
}

// ── Pagination ──

TEST(Pagination, FlowCreatesAndRemovesPages) {
	Fixture f;
	std::vector<NodeId> paras;
	for (int i = 0; i < 70; ++i)
		paras.push_back(f.Para("ligne " + std::to_string(i)));
	const size_t perPage = size_t((kContentH + 6) / (kLine + 6));
	LayoutResult r = f.Run();
	CHECK_EQ(r.pages.size(), (70 + perPage - 1) / perPage);
	CHECK(PagesOf(r, paras[perPage - 1]) == std::vector<size_t>{0});
	CHECK(PagesOf(r, paras[perPage]) == std::vector<size_t>{1});
	CHECK(r.pages[1].number == 2);
	// Synchronisation : pages créées puis stables.
	CHECK_EQ(SyncPages(f.d, r), r.pages.size());
	CHECK_EQ(f.d.FindAll(NodeType::Page).size(), r.pages.size());
	CHECK(r.pages[0].page != kNoNode);
	LayoutResult again = f.Run();
	CHECK_EQ(SyncPages(f.d, again), size_t(0));
	CHECK(again.pages[2].page == r.pages[2].page);
	// Contenu retiré : pages en trop supprimées.
	for (size_t i = 10; i < paras.size(); ++i)
		REQUIRE(f.d.DeleteNode(paras[i]).IsOk());
	LayoutResult shorter = f.Run();
	CHECK_EQ(shorter.pages.size(), size_t(1));
	CHECK_EQ(SyncPages(f.d, shorter), r.pages.size() - 1);
	CHECK_EQ(f.d.FindAll(NodeType::Page).size(), size_t(1));
}

TEST(Pagination, ExplicitBreaks) {
	Fixture f;
	const NodeId a = f.Para("a");
	(void)f.d.CreateNode(NodeType::PageBreak, f.body);
	const NodeId b = f.Para("b");
	const NodeId c = f.Para("c");
	REQUIRE(f.d.Set(c, props::PageBreakBefore, true).IsOk());
	const LayoutResult r = f.Run();
	CHECK_EQ(r.pages.size(), size_t(3));
	CHECK(PagesOf(r, a) == std::vector<size_t>{0});
	CHECK(PagesOf(r, b) == std::vector<size_t>{1});
	CHECK(PagesOf(r, c) == std::vector<size_t>{2});
	// PageBreakBefore en tête de page : pas de page vide.
	Fixture g;
	const NodeId first = g.Para("x");
	REQUIRE(g.d.Set(first, props::PageBreakBefore, true).IsOk());
	CHECK_EQ(g.Run().pages.size(), size_t(1));
}

TEST(Pagination, WidowsOrphansAndMaximumLines) {
	{ // Place pour 3 lignes ; 4 lignes, MinimumLines 2 : 2 + 2 (pas de veuve).
		Fixture f;
		f.Filler(kContentH - 3.5 * kLine);
		const NodeId p = f.Para("a\nb\nc\nd");
		const LayoutResult r = f.Run();
		REQUIRE(r.pages.size() == 2);
		size_t onFirst = 0;
		for (const PlacedLine &l : r.pages[0].lines)
			onFirst += l.paragraph == p ? 1 : 0;
		CHECK_EQ(onFirst, size_t(2));
		CHECK_EQ(r.pages[1].lines.size(), size_t(2));
		CHECK(r.pages[0].boxes.back().continues);
		CHECK(r.pages[1].boxes.front().continued);
	}
	{ // Place pour 1 ligne : pas d'orpheline, paragraphe entier page suivante.
		Fixture f;
		f.Filler(kContentH - 1.5 * kLine);
		const NodeId p = f.Para("a\nb\nc\nd");
		const LayoutResult r = f.Run();
		CHECK(PagesOf(r, p) == std::vector<size_t>{1});
	}
	{ // MinimumLines 1 : la ligne restante est utilisée.
		Fixture f;
		f.Filler(kContentH - 1.5 * kLine);
		const NodeId p = f.Para("a\nb\nc\nd");
		REQUIRE(f.d.Set(p, props::MinimumLines, int64_t(1)).IsOk());
		CHECK(PagesOf(f.Run(), p) == (std::vector<size_t>{0, 1}));
	}
	{ // MaximumLines 2 : au plus deux lignes par page.
		Fixture f;
		const NodeId p = f.Para("1\n2\n3\n4\n5\n6");
		REQUIRE(f.d.Set(p, props::MaximumLines, int64_t(2)).IsOk());
		REQUIRE(f.d.Set(p, props::MinimumLines, int64_t(1)).IsOk());
		const LayoutResult r = f.Run();
		CHECK_EQ(r.pages.size(), size_t(3));
		for (const PageLayout &page : r.pages)
			CHECK_EQ(page.lines.size(), size_t(2));
	}
}

TEST(Pagination, KeepWithNextAndKeepTogether) {
	{
		Fixture f;
		f.Filler(kContentH - 2.5 * kLine);
		const NodeId title = f.Para("Titre");
		REQUIRE(f.d.Set(title, props::KeepWithNext, true).IsOk());
		const NodeId text = f.Para("a\nb\nc\nd");
		const LayoutResult r = f.Run();
		CHECK(PagesOf(r, title) == std::vector<size_t>{1});
		CHECK(PagesOf(r, text) == std::vector<size_t>{1});
	}
	{
		Fixture f;
		f.Filler(kContentH - 2.5 * kLine);
		const NodeId p = f.Para("a\nb\nc\nd");
		REQUIRE(f.d.Set(p, props::KeepTogether, true).IsOk());
		CHECK(PagesOf(f.Run(), p) == std::vector<size_t>{1});
	}
	{ // Bloc insécable plus grand qu'une page (ligne de tableau) : débordement, sans boucle.
		Fixture f;
		const NodeId huge = f.d.CreateNode(NodeType::Table, f.body).Value();
		const NodeId row = f.d.CreateNode(NodeType::TableRow, huge).Value();
		(void)f.d.CreateNode(NodeType::TableCell, row);
		REQUIRE(f.d.Set(row, props::MinHeight, kContentH * 2).IsOk());
		const NodeId after = f.Para("après");
		const LayoutResult r = f.Run();
		REQUIRE(r.pages.size() == 2);
		CHECK(r.pages[0].boxes[0].node == huge);
		CHECK(r.pages[0].boxes[0].overflow);
		CHECK(PagesOf(r, after) == std::vector<size_t>{1});
	}
}

TEST(Pagination, TablesSplitBetweenRowsWithRepeatedHeader) {
	Fixture f;
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	NodeId header = kNoNode;
	for (int i = 0; i < 100; ++i) {
		const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
		if (i == 0) {
			header = row;
			REQUIRE(f.d.Set(row, props::RepeatHeader, true).IsOk());
		}
		const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
		f.Para(i == 0 ? "En-tête" : "ligne " + std::to_string(i), cell);
	}
	const LayoutResult r = f.Run();
	const size_t perPage = size_t(kContentH / kLine); // en-tête compris
	const size_t bodyPerPage = perPage - 1;
	CHECK_EQ(r.pages.size(), (99 + bodyPerPage - 1) / bodyPerPage);
	std::vector<size_t> expected(r.pages.size());
	for (size_t i = 0; i < expected.size(); ++i)
		expected[i] = i;
	CHECK(PagesOf(r, header) == expected);
	// Aucune ligne n'est coupée : chaque ligne apparaît sur une seule page.
	for (NodeId row : f.d.Find(table)->Children())
		if (row != header)
			CHECK_EQ(PagesOf(r, row).size(), size_t(1));
	const LayoutBox &firstTable = r.pages[0].boxes[0];
	CHECK(firstTable.type == NodeType::Table && firstTable.continues && !firstTable.continued);
}

// ── En-têtes, pieds, champs ──

TEST(HeadersFooters, VariantsFieldsAndContentArea) {
	Fixture f;
	const NodeId header = f.d.CreateNode(NodeType::Header, f.section).Value();
	const NodeId hp = f.Para("En-tête courant", header);
	const NodeId firstHeader = f.d.CreateNode(NodeType::Header, f.section).Value();
	REQUIRE(f.d.Set(firstHeader, props::Variant, HeaderFooterVariant::First).IsOk());
	const NodeId big = f.Para("Grand titre", firstHeader);
	REQUIRE(f.d.Set(big, props::FontSize, 40.0).IsOk());
	const NodeId even = f.d.CreateNode(NodeType::Footer, f.section).Value();
	REQUIRE(f.d.Set(even, props::Variant, HeaderFooterVariant::Even).IsOk());
	f.Para("pair", even);
	const NodeId footer = f.d.CreateNode(NodeType::Footer, f.section).Value();
	const NodeId fp = f.Para("Page ", footer);
	const NodeId number = f.Text(fp, "");
	REQUIRE(f.d.Set(number, props::Field, FieldKind::PageNumber).IsOk());
	f.Text(fp, " / ");
	const NodeId count = f.Text(fp, "");
	REQUIRE(f.d.Set(count, props::Field, FieldKind::PageCount).IsOk());
	for (int i = 0; i < 80; ++i)
		f.Para("texte " + std::to_string(i));
	const LayoutResult r = f.Run();
	REQUIRE(r.pages.size() >= 3);
	CHECK(r.pages[0].header == firstHeader);
	CHECK(r.pages[1].header == header);
	CHECK(r.pages[1].footer == even);
	CHECK(r.pages[2].footer == footer);
	// Grand en-tête : zone de flux repoussée (36 + 40 × 1,2 × 1,15 > marge de 72).
	CHECK_NEAR(r.pages[0].content.y, 36 + 40 * 1.2 * 1.15, 1e-9);
	CHECK_NEAR(r.pages[1].content.y, 72.0, 1e-9);
	// Champs résolus avec le vrai nombre de pages.
	std::string text;
	for (const PlacedLine &l : r.pages[2].lines)
		if (l.paragraph == fp)
			text = LineText(l);
	CHECK(Squeeze(text) == Squeeze(std::format("Page 3 / {}", r.pages.size())));
	CHECK(PagesOf(r, hp).size() == r.pages.size() - 1);
}

// ── Contenu propre des pages, test de position ──

TEST(Pages, OwnContentOverridesAndHitTest) {
	Fixture f;
	const NodeId p = f.Para("Bonjour");
	LayoutResult r = f.Run();
	REQUIRE(SyncPages(f.d, r) == 1);
	const NodeId page = r.pages[0].page;
	// Forme posée sur la page, orientation propre à la page.
	const NodeId shape = f.d.CreateNode(NodeType::Shape, page).Value();
	REQUIRE(f.d.Set(shape, props::PositionX, 400.0).IsOk());
	REQUIRE(f.d.Set(shape, props::PositionY, 20.0).IsOk());
	REQUIRE(f.d.Set(shape, props::Width, 50.0).IsOk());
	REQUIRE(f.d.Set(page, props::PageOrientation, Orientation::Landscape).IsOk());
	r = f.Run();
	CHECK_NEAR(r.pages[0].geometry.width, 841.89, 1e-2);
	CHECK_NEAR(r.pages[0].content.w, 841.89 - 144, 1e-2);
	CHECK(f.d.Find(page) != nullptr);
	const NodeId text = f.d.Find(p)->Children()[0];
	const PlacedLine &line = r.pages[0].lines[0];
	CHECK(r.HitTest(0, line.runs[0].x + 1, line.baseline - 2) == text);
	CHECK(r.HitTest(0, 410, 30) == shape);
	CHECK(r.HitTest(0, 5, 5) == kNoNode);
	CHECK(r.HitTest(9, 5, 5) == kNoNode);
	// Une page portant du contenu propre n'est jamais supprimée.
	REQUIRE(f.d.DeleteNode(p).IsOk());
	r = f.Run();
	CHECK_EQ(r.pages.size(), size_t(1));
	CHECK_EQ(SyncPages(f.d, r), size_t(0));
	CHECK(f.d.Contains(page));
}

// ── Incrémental, parallélisme, instantanés ──

TEST(Incremental, CacheReusesUnchangedBlocks) {
	Fixture f;
	std::vector<NodeId> paras;
	for (int i = 0; i < 50; ++i)
		paras.push_back(f.Para("paragraphe " + std::to_string(i)));
	LayoutCache cache;
	LayoutResult first = f.Run(&cache);
	CHECK_EQ(first.stats.measured, size_t(50));
	CHECK_EQ(first.stats.reused, size_t(0));
	LayoutResult second = f.Run(&cache);
	CHECK_EQ(second.stats.measured, size_t(0));
	CHECK_EQ(second.stats.reused, size_t(50));
	// Texte d'un seul paragraphe modifié : un seul bloc remesuré.
	REQUIRE(f.d.Set(f.d.Find(paras[7])->Children()[0], props::Text, "modifié").IsOk());
	LayoutResult third = f.Run(&cache);
	CHECK_EQ(third.stats.measured, size_t(1));
	CHECK_EQ(third.stats.reused, size_t(49));
	// Style hérité modifié : tout est remesuré.
	REQUIRE(f.d.Set(f.d.Root(), props::FontSize, 12.0).IsOk());
	CHECK_EQ(f.Run(&cache).stats.measured, size_t(50));
	// Bloc supprimé : sa mesure quitte le cache.
	REQUIRE(f.d.DeleteNode(paras[0]).IsOk());
	(void)f.Run(&cache);
	CHECK_EQ(cache.Size(), size_t(49));
}

TEST(Incremental, StyleChangesInvalidateOnlyAffectedBlocks) {
	Fixture f;
	std::vector<NodeId> paras;
	for (int i = 0; i < 20; ++i)
		paras.push_back(f.Para("paragraphe " + std::to_string(i)));
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId inCell = f.Para("cellule", cell);
	LayoutCache cache;
	(void)f.Run(&cache);
	const auto measured = [&] { return f.Run(&cache).stats.measured; };
	// Style hérité d'un paragraphe : ce paragraphe seulement.
	REQUIRE(f.d.Set(paras[3], props::FontSize, 15.0).IsOk());
	CHECK_EQ(measured(), size_t(1));
	// Style hérité d'une cellule : son tableau seulement.
	REQUIRE(f.d.Set(cell, props::Bold, true).IsOk());
	CHECK_EQ(measured(), size_t(1));
	// Propriété non héritable d'un paragraphe : lui seul.
	REQUIRE(f.d.Set(paras[5], props::SpaceAfter, 20.0).IsOk());
	CHECK_EQ(measured(), size_t(1));
	// Appartenance d'un paragraphe à un groupe stylé : lui seul.
	const NodeId g = f.d.CreateGroup("g").Value();
	(void)measured(); // création d'un groupe vide : rien
	REQUIRE(f.d.Set(g, props::Italic, true).IsOk());
	CHECK_EQ(measured(), size_t(21)); // style d'un groupe : partagé, tout est remesuré
	REQUIRE(f.d.AddToGroup(paras[7], g).IsOk());
	CHECK_EQ(measured(), size_t(1));
	// Style hérité du corps (ancêtre de tous les blocs) : tous les blocs de ce corps.
	REQUIRE(f.d.Set(f.body, props::TextColor, Color::RED()).IsOk());
	CHECK_EQ(measured(), size_t(21));
	// Déplacement d'un paragraphe dans la cellule : sa lignée change, le tableau aussi.
	REQUIRE(f.d.MoveNode(paras[0], cell).IsOk());
	CHECK_EQ(measured(), size_t(1)); // le tableau (qui contient désormais le paragraphe)
	CHECK(f.Run(&cache).PagesOf(paras[0]).size() == 1);
	(void)inCell;
	// Politique des groupes : partagée.
	f.d.SetResolution(GroupResolution::ProximityFirst);
	CHECK_EQ(measured(), size_t(20));
	// Suppression d'un groupe : ses membres perdent ses valeurs.
	REQUIRE(f.d.DeleteNode(g).IsOk());
	const LayoutResult after = f.Run(&cache);
	CHECK_EQ(after.stats.measured, size_t(20));
	bool italic = false;
	for (const PageLayout &page : after.pages)
		for (const PlacedLine &l : page.lines)
			for (const GlyphRun &r : l.runs)
				italic = italic || r.style.font.Italic();
	CHECK(!italic);
}

TEST(Incremental, RandomEditsMatchFreshLayout) {
	Fixture f;
	std::vector<NodeId> blocks;
	for (int i = 0; i < 12; ++i)
		blocks.push_back(f.Para(std::format("bloc {} avec quelques mots pour remplir une ligne ou deux de texte", i)));
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	std::vector<NodeId> cells;
	for (int r = 0; r < 3; ++r) {
		const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
		for (int c = 0; c < 2; ++c) {
			cells.push_back(f.d.CreateNode(NodeType::TableCell, row).Value());
			f.Para(std::format("c{}{}", r, c), cells.back());
		}
	}
	std::vector<NodeId> groups{f.d.CreateGroup("a", 2).Value(), f.d.CreateGroup("b", 1).Value()};
	groups.push_back(f.d.CreateGroup("c", 0, groups[0]).Value());
	LayoutCache cache;
	uint64_t seed = 12345;
	const auto next = [&](uint64_t n) {
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		return (seed >> 33) % n;
	};
	const auto same = [](const LayoutResult &a, const LayoutResult &b) {
		if (a.pages.size() != b.pages.size())
			return false;
		for (size_t p = 0; p < a.pages.size(); ++p) {
			const auto &la = a.pages[p].lines, &lb = b.pages[p].lines;
			if (la.size() != lb.size())
				return false;
			for (size_t i = 0; i < la.size(); ++i) {
				if (std::abs(la[i].baseline - lb[i].baseline) > 1e-9 || la[i].runs.size() != lb[i].runs.size())
					return false;
				for (size_t k = 0; k < la[i].runs.size(); ++k)
					if (la[i].runs[k].text != lb[i].runs[k].text || la[i].runs[k].style.font != lb[i].runs[k].style.font ||
						std::abs(la[i].runs[k].x - lb[i].runs[k].x) > 1e-9 || !(la[i].runs[k].style.color == lb[i].runs[k].style.color))
						return false;
			}
		}
		return true;
	};
	const std::vector<PropertyId> styles{PropertyId::FontSize, PropertyId::Bold, PropertyId::Italic, PropertyId::TextColor, PropertyId::LineSpacing,
										 PropertyId::HAlign, PropertyId::FontFamily};
	size_t mismatches = 0;
	for (int step = 0; step < 300; ++step) {
		std::vector<NodeId> targets = blocks;
		targets.insert(targets.end(), cells.begin(), cells.end());
		targets.push_back(f.body);
		targets.push_back(f.section);
		targets.push_back(f.d.Root());
		targets.insert(targets.end(), groups.begin(), groups.end());
		const NodeId target = targets[next(targets.size())];
		const PropertyId prop = styles[next(styles.size())];
		switch (next(6)) {
		case 0:
		case 1: {
			PropertyValue v;
			switch (Info(prop).type) {
			case PropertyType::Real: v = prop == PropertyId::LineSpacing ? 1.0 + double(next(3)) * 0.5 : 8.0 + double(next(10)); break;
			case PropertyType::Bool: v = next(2) == 0; break;
			case PropertyType::Rgba: v = Color(uint8_t(next(255)), 0, 0); break;
			case PropertyType::Int: v = int64_t(next(4)); break;
			case PropertyType::Text: v = std::string(next(2) ? "Times" : "Courier"); break;
			}
			(void)f.d.SetProperty(target, prop, v);
			break;
		}
		case 2: (void)f.d.ClearProperty(target, prop); break;
		case 3: {
			const NodeId member = blocks[next(blocks.size())];
			const NodeId g = groups[next(groups.size())];
			if (!f.d.AddToGroup(member, g))
				(void)f.d.RemoveFromGroup(member, g);
			break;
		}
		case 4: { // déplacement d'un bloc (corps ou cellule)
			const NodeId b = blocks[next(blocks.size())];
			const NodeId to = next(3) == 0 ? cells[next(cells.size())] : f.body;
			(void)f.d.MoveNode(b, to, next(4));
			break;
		}
		case 5: f.d.SetResolution(next(2) ? GroupResolution::PriorityFirst : GroupResolution::ProximityFirst); break;
		}
		if (!same(Layout(f.d, &cache), Layout(f.d)))
			++mismatches;
	}
	CHECK_EQ(mismatches, size_t(0));
}

TEST(Incremental, ParallelAndSequentialAgree) {
	Fixture f;
	for (int i = 0; i < 120; ++i)
		f.Para(std::string(size_t(20 + i % 50), 'x') + " fin " + std::to_string(i));
	LayoutOptions sequential;
	sequential.parallel = false;
	const LayoutResult a = Layout(f.d, nullptr, sequential);
	const LayoutResult b = Layout(f.d);
	REQUIRE(a.pages.size() == b.pages.size());
	for (size_t p = 0; p < a.pages.size(); ++p) {
		REQUIRE(a.pages[p].lines.size() == b.pages[p].lines.size());
		for (size_t l = 0; l < a.pages[p].lines.size(); ++l) {
			CHECK_NEAR(a.pages[p].lines[l].baseline, b.pages[p].lines[l].baseline, 1e-9);
			CHECK(LineText(a.pages[p].lines[l]) == LineText(b.pages[p].lines[l]));
		}
	}
}

TEST(Incremental, SnapshotLayoutOnAnotherThread) {
	Fixture f;
	for (int i = 0; i < 200; ++i)
		f.Para("instantané " + std::to_string(i));
	const Snapshot snap = f.d.TakeSnapshot();
	LayoutResult result;
	std::thread worker([&] { result = Layout(*snap); });
	// Pendant ce temps, le fil propriétaire modifie le document.
	for (int i = 0; i < 200; ++i)
		f.Para("après " + std::to_string(i));
	REQUIRE(f.d.DeleteNode(f.d.Find(f.body)->Children()[0]).IsOk());
	worker.join();
	CHECK_EQ(result.revision, snap->Revision());
	size_t lines = 0;
	for (const PageLayout &p : result.pages)
		lines += p.lines.size();
	CHECK_EQ(lines, size_t(200));
	CHECK(Layout(f.d).pages.size() > result.pages.size());
}
