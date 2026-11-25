// Tests de l'édition du texte d'engine.document : ordre de lecture et navigation (caractères,
// mots, paragraphes, champs, récits), insertion, coupure et fusion de paragraphes, suppression
// d'une plage (dans un segment, entre segments, entre paragraphes, à travers un tableau, entre
// conteneurs), mise en forme d'une sous-plage, copier / coller enrichi, glisser-déposer,
// annulation exacte, géométrie du curseur et de la surbrillance, polices installées.
#include <cpp-maker/test.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <document/export.hpp>
#include <document/text.hpp>
#include <pdf/reader.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	History h{d};
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	/// Paragraphe de segments ; rend les identifiants des segments.
	std::vector<NodeId> Para(std::initializer_list<std::string_view> runs, NodeId parent = kNoNode) {
		const NodeId p = d.CreateNode(NodeType::Paragraph, parent == kNoNode ? body : parent).Value();
		std::vector<NodeId> out;
		for (std::string_view r : runs) {
			const NodeId t = d.CreateNode(NodeType::Text, p).Value();
			(void)d.Set(t, props::Text, r);
			out.push_back(t);
		}
		return out;
	}
	std::string ParagraphText(NodeId paragraph) const {
		std::string s;
		for (NodeId t : d.Find(paragraph)->Children())
			s += d.Get(t, props::Text);
		return s;
	}
	/// Texte du corps : paragraphes séparés par « | ».
	std::string BodyText() const {
		std::string s;
		for (NodeId c : d.Find(body)->Children())
			if (d.Find(c)->Type() == NodeType::Paragraph)
				s += (s.empty() ? "" : "|") + ParagraphText(c);
			else
				s += (s.empty() ? "[" : "|[") + std::string(NodeTypeName(d.Find(c)->Type())) + "]";
		return s;
	}
	NodeId ParagraphOf(NodeId text) const { return d.Find(text)->Parent(); }
};

} // namespace

TEST(TextOrder, NavigationAcrossRunsParagraphsFieldsAndStories) {
	Fixture f;
	const auto p1 = f.Para({"Bon", "jour"});
	const auto p2 = f.Para({"le monde"});
	const NodeId field = f.d.CreateNode(NodeType::Text, f.ParagraphOf(p2[0])).Value();
	REQUIRE(f.d.Set(field, props::Field, FieldKind::PageNumber).IsOk());
	const NodeId header = f.d.CreateNode(NodeType::Header, f.section).Value();
	const auto inHeader = f.Para({"En-tête"}, header);
	const TextOrder o(f.d);
	// Caractère par caractère : fin d'un segment = début du suivant (un seul pas).
	TextPosition p{p1[0], 2};
	p = o.Next(p);
	CHECK((p == TextPosition{p1[0], 3}));
	p = o.Next(p);
	CHECK((p == TextPosition{p1[1], 1}));
	p = o.Previous(p);
	CHECK((p == TextPosition{p1[1], 0}));
	p = o.Previous(p);
	CHECK((p == TextPosition{p1[0], 2}));
	// Fin de paragraphe → début du suivant ; le champ compte pour un pas.
	CHECK((o.Next({p1[1], 4}) == TextPosition{p2[0], 0}));
	CHECK((o.Previous({p2[0], 0}) == TextPosition{p1[1], 4}));
	CHECK((o.Next({p2[0], 8}) == TextPosition{field, 0}));
	// Le récit du corps ne se prolonge pas dans l'en-tête.
	CHECK((o.Next({field, 0}) == TextPosition{field, 0}));
	CHECK((o.Previous({inHeader[0], 0}) == TextPosition{inHeader[0], 0}));
	// Mots, paragraphe, récit, comparaisons, normalisation UTF-8.
	CHECK((o.NextWord({p2[0], 0}) == TextPosition{p2[0], 3}));
	CHECK((o.PreviousWord({p2[0], 8}) == TextPosition{p2[0], 3}));
	const auto word = o.WordAt({p1[0], 1});
	CHECK((word.first == TextPosition{p1[0], 0}));
	CHECK((word.second == TextPosition{p1[1], 4})); // « Bonjour » sur deux segments
	CHECK((o.ParagraphRange({p1[1], 1}).second == TextPosition{p1[1], 4}));
	CHECK(o.Compare({p1[0], 3}, {p1[1], 0}) < 0);
	CHECK(o.Compare({p2[0], 1}, {p2[0], 1}) == 0);
	CHECK((o.Normalize({inHeader[0], 5}) == TextPosition{inHeader[0], 4})); // milieu de « ê » → début du caractère
	CHECK(!o.Normalize({999, 0}).Valid());
}

TEST(TextEdit, InsertSplitAndTypingMerge) {
	Fixture f;
	const auto p = f.Para({"Bonjour monde"});
	auto r = text::Insert(f.h, {p[0], 7}, " le");
	REQUIRE(r.IsOk());
	CHECK((r.Value() == TextPosition{p[0], 10}));
	r = text::Insert(f.h, r.Value(), ",");
	REQUIRE(r.IsOk());
	CHECK(f.BodyText() == "Bonjour le, monde");
	CHECK_EQ(f.h.Entries().size(), size_t(1)); // frappes fusionnées
	// Saut de paragraphe dans le texte inséré.
	r = text::Insert(f.h, {p[0], 0}, "Titre\nIntro ");
	REQUIRE(r.IsOk());
	CHECK(f.BodyText() == "Titre|Intro Bonjour le, monde");
	CHECK(f.d.Get(r.Value().text, props::Text).substr(0, r.Value().offset) == "Intro ");
	// Entrée au milieu, en début et en fin.
	REQUIRE(f.d.Set(f.ParagraphOf(p[0]), props::SpaceAfter, 20.0).IsOk());
	auto s = text::Split(f.h, {p[0], 13}); // après « Intro Bonjour »
	REQUIRE(s.IsOk());
	CHECK(f.BodyText() == "Titre|Intro Bonjour| le, monde");
	CHECK_NEAR(f.d.Get(f.ParagraphOf(s.Value().text), props::SpaceAfter), 20.0, 1e-9); // propriétés recopiées
	const NodeId last = s.Value().text;
	s = text::Split(f.h, {last, f.d.Get(last, props::Text).size()});
	REQUIRE(s.IsOk());
	CHECK(f.BodyText() == "Titre|Intro Bonjour| le, monde|");
	CHECK(f.d.Find(f.ParagraphOf(s.Value().text))->Children().size() == 1); // segment vide pour le curseur
	// Annulation complète.
	REQUIRE(f.h.JumpTo(0).IsOk());
	CHECK(f.BodyText() == "Bonjour monde");
}

TEST(TextEdit, EraseWithinAndAcrossParagraphs) {
	Fixture f;
	const auto a = f.Para({"Premier ", "paragraphe"});
	const auto b = f.Para({"Second"});
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const auto inCell = f.Para({"cellule"}, cell);
	const auto c = f.Para({"Troisième"});
	REQUIRE(f.d.Set(a[1], props::Bold, true).IsOk());
	// Dans un segment.
	auto r = text::Erase(f.h, {a[0], 0}, {a[0], 3});
	REQUIRE(r.IsOk());
	CHECK(f.ParagraphText(f.ParagraphOf(a[0])) == "mier paragraphe");
	// Entre segments du même paragraphe (le gras reste sur ce qui reste).
	r = text::Erase(f.h, {a[0], 3}, {a[1], 4});
	REQUIRE(r.IsOk());
	CHECK(f.ParagraphText(f.ParagraphOf(r.Value().text)) == "miegraphe");
	// Entre paragraphes : fusion ; le tableau intermédiaire disparaît.
	r = text::Erase(f.h, {f.d.Find(f.ParagraphOf(a[1]))->Children().back(), 3}, {c[0], 4});
	REQUIRE(r.IsOk());
	CHECK(f.BodyText() == "miegrasième"); // « miegra|phe … Troi|sième »
	CHECK(!f.d.Contains(table));
	CHECK(!f.d.Contains(b[0])); // paragraphe intermédiaire supprimé
	CHECK(f.d.Find(f.body)->Children().size() == 1);
	(void)inCell;
	// Annulation exacte de tout.
	REQUIRE(f.h.JumpTo(0).IsOk());
	CHECK(f.BodyText() == "Premier paragraphe|Second|[Table]|Troisième");
	CHECK(f.d.Contains(table));
	CHECK(f.d.Get(a[1], props::Bold));
}

TEST(TextEdit, EraseAcrossContainersKeepsStructure) {
	Fixture f;
	const auto a = f.Para({"avant"});
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const auto inCell = f.Para({"dans la cellule"}, cell);
	const NodeId pa = f.ParagraphOf(a[0]), pc = f.ParagraphOf(inCell[0]); // les segments coupés changent d'identifiant
	auto r = text::Erase(f.h, {a[0], 2}, {inCell[0], 8});
	REQUIRE(r.IsOk());
	CHECK(f.ParagraphText(pa) == "av");
	CHECK(f.ParagraphText(pc) == "cellule");
	CHECK(f.ParagraphOf(r.Value().text) == pa);
	CHECK(f.d.Contains(table)); // pas de fusion entre le corps et une cellule
}

TEST(TextEdit, StyleSubRangeCopyPasteAndMove) {
	Fixture f;
	const auto p = f.Para({"Un texte à mettre en forme"});
	const NodeId paragraph = f.ParagraphOf(p[0]);
	auto styled = text::ApplyStyle(f.h, {p[0], 3}, {p[0], 8}, PropertyId::Bold, true);
	REQUIRE(styled.IsOk());
	const auto runs = f.d.Find(paragraph)->Children();
	REQUIRE(runs.size() == 3);
	CHECK(f.d.Get(runs[0], props::Text) == std::string("Un "));
	CHECK(f.d.Get(runs[1], props::Text) == std::string("texte"));
	CHECK(f.d.Get(runs[1], props::Bold));
	CHECK(!f.d.Get(runs[2], props::Bold));
	CHECK((styled.Value().first == TextPosition{runs[1], 0}));
	CHECK((styled.Value().second == TextPosition{runs[1], 5}));
	// Retrait du gras : les trois segments identiques fusionnent.
	REQUIRE(text::ApplyStyle(f.h, styled.Value().first, styled.Value().second, PropertyId::Bold, false).IsOk());
	CHECK(f.d.Find(paragraph)->Children().size() <= 3);
	CHECK(f.ParagraphText(paragraph) == "Un texte à mettre en forme");
	REQUIRE(f.h.JumpTo(0).IsOk());
	CHECK(f.d.Find(paragraph)->Children().size() == 1);

	// Copier / coller enrichi sur deux paragraphes.
	const auto q = f.Para({"Deuxième ", "ligne"});
	REQUIRE(f.d.Set(q[1], props::Italic, true).IsOk());
	TextOrder order(f.d);
	const TextFragment frag = text::Copy(f.d, order, {p[0], 22}, {q[1], 3}); // « à » : deux octets
	CHECK(frag.PlainText() == "forme\nDeuxième lig");
	REQUIRE(frag.paragraphs.size() == 2);
	CHECK(frag.paragraphs[1].runs[1].properties.Contains(PropertyId::Italic));
	auto pasted = text::Paste(f.h, {p[0], 3}, frag);
	REQUIRE(pasted.IsOk());
	CHECK(f.BodyText() == "Un forme|Deuxième ligtexte à mettre en forme|Deuxième ligne");
	REQUIRE(f.h.Undo().IsOk());
	CHECK(f.BodyText() == "Un texte à mettre en forme|Deuxième ligne");
	CHECK(TextFragment::FromPlainText("a\r\nb\n").paragraphs.size() == 3);
	CHECK(text::PlainText(f.d, order, {q[0], 0}, {q[1], 5}) == "Deuxième ligne");

	// Glisser-déposer : « texte » déplacé à la fin du paragraphe, puis copié au début.
	auto moved = text::MoveRange(f.h, {p[0], 3}, {p[0], 8}, {p[0], 27}, false); // fin du paragraphe
	REQUIRE(moved.IsOk());
	CHECK(f.ParagraphText(paragraph) == "Un  à mettre en formetexte");
	order = TextOrder(f.d);
	CHECK(text::PlainText(f.d, order, moved.Value().first, moved.Value().second) == "texte");
	auto copied = text::MoveRange(f.h, moved.Value().first, moved.Value().second, {f.d.Find(paragraph)->Children()[0], 0}, true);
	REQUIRE(copied.IsOk());
	CHECK(f.ParagraphText(paragraph) == "texteUn  à mettre en formetexte");
	// Déplacement vers un paragraphe précédent (la plage d'origine est supprimée après le collage).
	TextOrder now(f.d);
	const auto range = now.ParagraphRange({q[0], 0});
	auto back = text::MoveRange(f.h, range.first, {q[0], 10}, {f.d.Find(paragraph)->Children()[0], 0}, false); // « Deuxième »
	REQUIRE(back.IsOk());
	CHECK(f.ParagraphText(paragraph) == "Deuxième texteUn  à mettre en formetexte");
	CHECK(f.ParagraphText(f.ParagraphOf(q[1])) == "ligne");
	// Dépôt dans la plage elle-même : rien.
	const std::string before = f.BodyText();
	REQUIRE(text::MoveRange(f.h, {q[1], 0}, {q[1], 5}, {q[1], 2}, false).IsOk());
	CHECK(f.BodyText() == before);
	REQUIRE(f.h.JumpTo(0).IsOk());
	CHECK(f.BodyText() == "Un texte à mettre en forme|Deuxième ligne");
}

TEST(TextGeometry, CaretPositionsLinesAndHighlight) {
	Fixture f;
	const auto p = f.Para({"Une ligne de texte assez longue pour être coupée en plusieurs lignes dans une colonne étroite."});
	const auto empty = f.Para({""});
	REQUIRE(f.d.Set(f.section, props::MarginRight, 380.0).IsOk()); // colonne étroite
	const LayoutResult layout = Layout(f.d);
	const PlacedLine &first = layout.pages[0].lines[0];
	// Curseur : début du texte au début de la première ligne.
	auto caret = text::Caret(f.d, layout, {p[0], 0});
	REQUIRE(caret.has_value());
	CHECK_NEAR(caret->rect.x, first.runs[0].x, 1e-9);
	CHECK_NEAR(caret->rect.h, first.rect.h, 1e-9);
	// Aller-retour point → position → curseur.
	const double px = first.runs[0].x + 40;
	auto pos = text::PositionAt(f.d, layout, 0, px, first.rect.y + 2);
	REQUIRE(pos.has_value());
	CHECK(pos->text == p[0]);
	caret = text::Caret(f.d, layout, *pos);
	REQUIRE(caret.has_value());
	CHECK(std::abs(caret->rect.x - px) < 8);
	// Ligne suivante / précédente, bords de ligne.
	auto down = text::LineMove(f.d, layout, *pos, 1, px);
	REQUIRE(down.has_value());
	CHECK(down->offset > pos->offset);
	CHECK(text::Caret(f.d, layout, *down)->rect.y > caret->rect.y);
	auto up = text::LineMove(f.d, layout, *down, -1, px);
	REQUIRE(up.has_value());
	CHECK_EQ(up->offset, pos->offset);
	auto end = text::LineEdge(f.d, layout, {p[0], 0}, true);
	REQUIRE(end.has_value());
	CHECK_EQ(end->offset, first.runs.back().offset + first.runs.back().text.size());
	// Paragraphe vide : curseur sur sa ligne.
	auto emptyCaret = text::Caret(f.d, layout, {empty[0], 0});
	REQUIRE(emptyCaret.has_value());
	CHECK(emptyCaret->rect.y > caret->rect.y);
	// Surbrillance : une portion par ligne couverte.
	const TextOrder order(f.d);
	const auto rects = text::RangeRects(layout, order, {p[0], 2}, *down);
	CHECK(rects.size() == 2);
	CHECK(rects[0].second.x > first.runs[0].x);
	CHECK(text::RangeRects(layout, order, {p[0], 2}, {p[0], 2}).empty());
}

TEST(TextFonts, InstalledFontsInLayoutAndPdf) {
	const std::vector<std::filesystem::path> dirs{"assets/fonts/Ubuntu"};
	auto fonts = FontLibrary::Scan(dirs);
	if (fonts->InstalledCount() == 0)
		SKIP("polices Ubuntu du dépôt absentes");
	const auto families = fonts->Families();
	REQUIRE(families.size() >= 4);
	CHECK(families[0] == "Helvetica");
	CHECK(std::ranges::find(families, "Ubuntu Nerd Font") != families.end());
	const pdf::FontRef bold = fonts->Resolve("ubuntu nerd font", true, false);
	REQUIRE(bold.face != nullptr);
	CHECK(bold.face->Bold());
	CHECK(!fonts->Resolve("Helvetica", false, false).face); // famille standard
	CHECK(fonts->Resolve("Inconnue", false, true).standard == pdf::StandardFont::HelveticaOblique);

	Fixture f;
	const auto p = f.Para({"Texte en police installée"});
	REQUIRE(f.d.Set(f.ParagraphOf(p[0]), props::FontFamily, "Ubuntu Nerd Font").IsOk());
	LayoutOptions options;
	options.fonts = fonts;
	const LayoutResult layout = Layout(f.d, nullptr, options);
	const GlyphRun &run = layout.pages[0].lines[0].runs[0];
	REQUIRE(run.style.font.face != nullptr);
	CHECK(run.style.font.Family() == "Ubuntu Nerd Font");
	CHECK_NEAR(run.width, run.style.font.face->TextWidth("Texte en police installée", 11), 1e-9);
	CHECK(run.width != pdf::TextWidth(pdf::StandardFont::Helvetica, "Texte en police installée", 11));
	// PDF : police incorporée, texte relu.
	auto bytes = ExportPdf(f.d, layout);
	REQUIRE(bytes.IsOk());
	const std::string s(bytes.Value().begin(), bytes.Value().end());
	CHECK(s.find("/FontFile2") != std::string::npos);
	auto r = pdf::Reader::Open(bytes.Value());
	REQUIRE(r.IsOk());
	CHECK(r.Value().ExtractText(0).Value() == "Texte en police installée");
}

TEST(Text, LineBreakStaysInParagraph) {
	Fixture f;
	const NodeId t = f.Para({"avant après"}).front();
	const NodeId p = f.d.Find(t)->Parent();
	auto r = text::InsertLineBreak(f.h, TextPosition{t, 5}); // après « avant »
	REQUIRE(r.IsOk());
	CHECK(f.d.FindAll(NodeType::Paragraph).size() == 1);
	CHECK(f.d.Get(t, props::Text) == std::string("avant\n après"));
	CHECK(r.Value().text == t);
	CHECK(r.Value().offset == 6);
	// Deux lignes ; le curseur après le saut est sur la seconde.
	LayoutResult layout = Layout(f.d);
	size_t lines = 0;
	for (const PlacedLine &l : layout.pages[0].lines)
		lines += l.paragraph == p ? 1 : 0;
	CHECK(lines == 2);
	const auto before = text::Caret(f.d, layout, TextPosition{t, 0});
	const auto after = text::Caret(f.d, layout, r.Value());
	REQUIRE(before && after);
	CHECK(after->rect.y > before->rect.y);
	// Annulable en une fois.
	REQUIRE(f.h.Undo().IsOk());
	CHECK(f.d.Get(t, props::Text) == std::string("avant après"));
	// Saut en fin de paragraphe : la ligne vide suivante reçoit le curseur.
	auto end = text::InsertLineBreak(f.h, TextPosition{t, std::string("avant après").size()});
	REQUIRE(end.IsOk());
	layout = Layout(f.d);
	const auto last = text::Caret(f.d, layout, end.Value());
	REQUIRE(last.has_value());
	CHECK(last->rect.y > before->rect.y);
	// Clic sur la ligne vide : la position après le saut.
	const auto clicked = text::PositionAt(f.d, layout, 0, last->rect.x + 30, last->rect.y + 2);
	REQUIRE(clicked.has_value());
	CHECK(*clicked == end.Value());
}
