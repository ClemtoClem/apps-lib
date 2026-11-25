// Tests de la structure d'engine.document : titres numérotés, légendes, listes (puces,
// numérotations, niveaux, reprise), préfixes générés dans la mise en page (hors des positions
// de texte), table des matières (entrées, points de conduite, numéros de page, mise à jour
// incrémentale), renvois, titre courant d'en-tête, liens et signets du PDF, export HTML,
// exposant / barré / côtés de bordure / espaces conservés, persistance.
#include <cpp-maker/test.hpp>

#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <document/export.hpp>
#include <document/outline.hpp>
#include <document/persistence.hpp>
#include <document/render.hpp>
#include <document/text.hpp>

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
	NodeId Heading(std::string_view text, int64_t level) {
		const NodeId p = Para(text);
		(void)d.Set(p, props::OutlineLevel, level);
		return p;
	}
	NodeId Item(std::string_view text, ListStyle style, int64_t level = 0) {
		const NodeId p = Para(text);
		(void)d.Set(p, props::ListStyle, style);
		(void)d.Set(p, props::ListLevel, level);
		return p;
	}
	NodeId TextOf(NodeId paragraph) const { return d.Find(paragraph)->Children().front(); }
	const PlacedLine *FirstLine(const LayoutResult &r, NodeId paragraph) const {
		for (const PageLayout &p : r.pages)
			for (const PlacedLine &l : p.lines)
				if (l.paragraph == paragraph)
					return &l;
		return nullptr;
	}
};

std::string RunsText(const PlacedLine &line) {
	std::string s;
	for (const GlyphRun &r : line.runs)
		s += r.text;
	return s;
}

} // namespace

TEST(Structure, HeadingsCaptionsAndListsAreNumbered) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::DecimalDot);
	const NodeId h1 = f.Heading("Introduction", 1);
	const NodeId h11 = f.Heading("Contexte", 2);
	const NodeId h12 = f.Heading("Objectifs", 2);
	const NodeId h121 = f.Heading("Détail", 3);
	const NodeId h2 = f.Heading("Mise en œuvre", 1);
	const NodeId h21 = f.Heading("Serveur", 2);
	const NodeId fig = f.Para("Schéma");
	(void)f.d.Set(fig, props::CaptionKind, CaptionKind::Figure);
	const NodeId tab = f.Para("Mesures");
	(void)f.d.Set(tab, props::CaptionKind, CaptionKind::Table);
	const NodeId tab2 = f.Para("Brochage");
	(void)f.d.Set(tab2, props::CaptionKind, CaptionKind::Table);
	(void)f.d.Set(tab2, props::CaptionLabel, "Table");
	const Outline o = BuildOutline(f.d);
	REQUIRE(o.entries.size() == 9);
	CHECK(o.Find(h1)->label == "1.");
	CHECK(o.Find(h11)->label == "1.1.");
	CHECK(o.Find(h12)->label == "1.2.");
	CHECK(o.Find(h121)->label == "1.2.1.");
	CHECK(o.Find(h2)->label == "2.");
	CHECK(o.Find(h21)->number == "2.1");
	CHECK(o.Find(h21)->text == "Serveur");
	CHECK(o.Find(fig)->label == "Figure 1");
	CHECK(o.Find(tab)->label == "Tableau 1");
	CHECK(o.Find(tab2)->label == "Table 2"); // libellé propre, compteur des tableaux
	CHECK(o.Reference(h121) == "1.2.1");
	CHECK(o.Reference(tab2) == "Table 2");
	CHECK(o.Reference(f.body) == "?");
	CHECK_EQ(o.Entries(TocContent::Headings, 2).size(), size_t(5));
	CHECK_EQ(o.Entries(TocContent::Tables, 9).size(), size_t(2));
	// Sans numérotation : entrées sans numéro (renvoi : texte du titre).
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::None);
	const Outline plain = BuildOutline(f.d);
	CHECK(plain.Find(h1)->label.empty());
	CHECK(plain.Reference(h1) == "Introduction");
	CHECK(plain.Prefix(h1) == nullptr);
}

TEST(Structure, ListMarkersLevelsAndRestart) {
	Fixture f;
	const NodeId a = f.Item("un", ListStyle::Decimal);
	const NodeId b = f.Item("deux", ListStyle::Decimal);
	const NodeId b1 = f.Item("deux-a", ListStyle::LowerAlpha, 1);
	const NodeId b2 = f.Item("deux-b", ListStyle::LowerAlpha, 1);
	const NodeId c = f.Item("trois", ListStyle::Decimal);
	const NodeId c1 = f.Item("trois-a", ListStyle::LowerAlpha, 1); // niveau inférieur repris à « a »
	f.Para("texte");											   // coupe la liste
	const NodeId d = f.Item("un encore", ListStyle::Decimal);
	const NodeId e = f.Item("dix", ListStyle::UpperRoman);
	(void)f.d.Set(e, props::ListStart, int64_t(10));
	const NodeId bullet = f.Item("puce", ListStyle::Bullet);
	const NodeId sub = f.Item("sous-puce", ListStyle::Bullet, 1);
	const Outline o = BuildOutline(f.d);
	CHECK(o.Prefix(a)->text == "1.");
	CHECK(o.Prefix(b)->text == "2.");
	CHECK(o.Prefix(b1)->text == "a.");
	CHECK(o.Prefix(b2)->text == "b.");
	CHECK(o.Prefix(c)->text == "3.");
	CHECK(o.Prefix(c1)->text == "a.");
	CHECK(o.Prefix(d)->text == "1.");
	CHECK(o.Prefix(e)->text == "X.");
	CHECK(o.Prefix(bullet)->text == "•");
	CHECK(o.Prefix(sub)->text == "–");
	CHECK(o.Prefix(sub)->marker);
	CHECK_EQ(o.Prefix(sub)->listLevel, 1);
	CHECK(o.entries.empty()); // les listes ne sont pas des entrées de table des matières
	CHECK(ListMarker(ListStyle::UpperAlpha, 0, 28) == "AB.");
	CHECK(ListMarker(ListStyle::DecimalParen, 0, 4) == "4)");
	CHECK(Roman(1994, false) == "mcmxciv");
	CHECK(Roman(5000, true) == "5000");
}

TEST(Structure, PrefixesInLayoutAreNotTextPositions) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::Decimal);
	const NodeId h = f.Heading("Titre", 1);
	const NodeId item = f.Item("Élément de liste", ListStyle::Bullet);
	const NodeId plain = f.Para("Corps");
	const LayoutResult r = Layout(f.d);
	const PlacedLine *hl = f.FirstLine(r, h);
	REQUIRE(hl != nullptr);
	REQUIRE(hl->runs.size() >= 2);
	CHECK(hl->runs.front().generated);
	CHECK(hl->runs.front().node == h);
	CHECK(RunsText(*hl).starts_with("1 "));
	CHECK(RunsText(*hl).ends_with("Titre"));
	// Puce dans le retrait : le texte de l'élément commence 18 pt à droite du corps.
	const PlacedLine *il = f.FirstLine(r, item);
	const PlacedLine *pl = f.FirstLine(r, plain);
	REQUIRE(il != nullptr);
	REQUIRE(pl != nullptr);
	CHECK(il->runs.front().generated);
	CHECK(il->runs.front().text == "•");
	CHECK_NEAR(il->runs.front().x, pl->runs.front().x, 1e-6);
	CHECK_NEAR(il->runs[1].x, pl->runs.front().x + 18, 1e-6);
	// Clic sur la puce ou sur le numéro : début du texte, jamais le préfixe.
	auto pos = text::PositionAt(f.d, r, 0, il->runs.front().x + 1, il->baseline - 2);
	REQUIRE(pos.has_value());
	CHECK(pos->text == f.TextOf(item));
	CHECK_EQ(pos->offset, size_t(0));
	pos = text::PositionAt(f.d, r, 0, hl->runs.front().x + 1, hl->baseline - 2);
	REQUIRE(pos.has_value());
	CHECK(pos->text == f.TextOf(h));
	const auto edge = text::LineEdge(f.d, r, {f.TextOf(h), 3}, false);
	REQUIRE(edge.has_value());
	CHECK(*edge == (TextPosition{f.TextOf(h), 0}));
	// Le curseur d'un titre vide se place après le numéro.
	(void)f.d.Set(f.TextOf(h), props::Text, "");
	const LayoutResult r2 = Layout(f.d);
	const auto caret = text::Caret(f.d, r2, {f.TextOf(h), 0});
	REQUIRE(caret.has_value());
	CHECK(caret->rect.x > f.FirstLine(r2, h)->runs.front().x + 1);
}

TEST(Structure, TableOfContentsPageNumbersAndUpdates) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::DecimalDot);
	const NodeId title = f.Para("Table des matières");
	(void)title;
	const NodeId toc = f.d.CreateNode(NodeType::TableOfContents, f.body).Value();
	f.Heading("Premier chapitre", 1);
	f.Para("texte");
	const NodeId brk = f.d.CreateNode(NodeType::PageBreak, f.body).Value();
	(void)brk;
	const NodeId h2 = f.Heading("Deuxième chapitre", 1);
	const NodeId h21 = f.Heading("Section profonde", 2);
	const NodeId h211 = f.Heading("Trop profond pour la profondeur 2", 3);
	(void)h211;
	(void)f.d.Set(toc, props::TocDepth, int64_t(2));
	LayoutCache cache;
	LayoutResult r = Layout(f.d, &cache);
	REQUIRE(r.pages.size() == 2);
	std::vector<const PlacedLine *> lines;
	for (const PlacedLine &l : r.pages[0].lines)
		if (l.paragraph == toc)
			lines.push_back(&l);
	REQUIRE(lines.size() == 3); // deux chapitres, une section (niveau 3 exclu)
	const auto number = [](const PlacedLine &l) { return l.runs.back(); };
	CHECK(number(*lines[0]).field == FieldKind::PageReference);
	CHECK(number(*lines[0]).text == "1");
	CHECK(number(*lines[1]).text == "2");
	CHECK(number(*lines[2]).text == "2");
	CHECK(number(*lines[1]).target == h2);
	CHECK(lines[1]->runs.front().style.link == InternalLink(h2));
	CHECK(RunsText(*lines[1]).starts_with("2.  Deuxième chapitre"));
	CHECK(RunsText(*lines[1]).find(" . . .") != std::string::npos); // points de conduite
	// Numéros alignés à droite sur le bord du bloc.
	const double right = number(*lines[0]).x + number(*lines[0]).width;
	CHECK_NEAR(number(*lines[1]).x + number(*lines[1]).width, right, 1e-6);
	CHECK(lines[2]->runs.front().x > lines[1]->runs.front().x); // retrait du niveau 2
	// Titre modifié : la table suit (mise en page incrémentale), sans liens si désactivés.
	(void)f.d.Set(f.TextOf(h21), props::Text, "Section renommée");
	(void)f.d.Set(toc, props::Hyperlinks, false);
	r = Layout(f.d, &cache);
	std::string all;
	for (const PlacedLine &l : r.pages[0].lines)
		if (l.paragraph == toc) {
			all += RunsText(l) + "\n";
			CHECK(l.runs.front().style.link.empty());
		}
	CHECK(all.find("Section renommée") != std::string::npos);
	// Table des figures : légendes.
	const NodeId fig = f.Para("Courbe");
	(void)f.d.Set(fig, props::CaptionKind, CaptionKind::Figure);
	(void)f.d.Set(toc, props::TocContent, TocContent::Figures);
	r = Layout(f.d, &cache);
	lines.clear();
	for (const PlacedLine &l : r.pages[0].lines)
		if (l.paragraph == toc)
			lines.push_back(&l);
	REQUIRE(lines.size() == 1);
	CHECK(RunsText(*lines[0]).starts_with("Figure 1 : Courbe"));
	// Mise en page incrémentale = mise en page complète.
	const LayoutResult full = Layout(f.d);
	REQUIRE(full.pages.size() == r.pages.size());
	for (size_t p = 0; p < r.pages.size(); ++p) {
		REQUIRE(full.pages[p].lines.size() == r.pages[p].lines.size());
		for (size_t i = 0; i < r.pages[p].lines.size(); ++i)
			CHECK(RunsText(full.pages[p].lines[i]) == RunsText(r.pages[p].lines[i]));
	}
}

TEST(Structure, NumberingFollowsEditsIncrementally) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::Decimal);
	const NodeId a = f.Heading("A", 1);
	f.Para("corps");
	const NodeId b = f.Heading("B", 1);
	// Légende dans une cellule : le tableau dépend de la numérotation.
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId cap = f.Para("dans la cellule", cell);
	(void)f.d.Set(cap, props::CaptionKind, CaptionKind::Figure);
	LayoutCache cache;
	LayoutResult r = Layout(f.d, &cache);
	CHECK(RunsText(*f.FirstLine(r, b)).starts_with("2 "));
	CHECK(RunsText(*f.FirstLine(r, cap)).starts_with("Figure 1"));
	// Un titre et une légende insérés avant : les suivants sont renumérotés, cache compris.
	const NodeId first = f.d.CreateNode(NodeType::Paragraph, f.body, 0).Value();
	(void)f.d.Set(f.d.CreateNode(NodeType::Text, first).Value(), props::Text, "Avant");
	(void)f.d.Set(first, props::OutlineLevel, int64_t(1));
	const NodeId early = f.d.CreateNode(NodeType::Paragraph, f.body, 1).Value();
	(void)f.d.Set(f.d.CreateNode(NodeType::Text, early).Value(), props::Text, "figure avant");
	(void)f.d.Set(early, props::CaptionKind, CaptionKind::Figure);
	r = Layout(f.d, &cache);
	CHECK(RunsText(*f.FirstLine(r, a)).starts_with("2 "));
	CHECK(RunsText(*f.FirstLine(r, b)).starts_with("3 "));
	CHECK(RunsText(*f.FirstLine(r, cap)).starts_with("Figure 2"));
	CHECK(r.stats.reused > 0); // le paragraphe de corps est repris du cache
}

TEST(Structure, ReferencesChapterHeaderAndLinksInPdf) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::Decimal);
	f.Heading("Présentation", 1);
	const NodeId cap = f.Para("Valeurs limites");
	(void)f.d.Set(cap, props::CaptionKind, CaptionKind::Table);
	(void)f.d.CreateNode(NodeType::PageBreak, f.body);
	const NodeId h2 = f.Heading("Brochage", 1);
	// « voir Tableau 1, page 1 » et un lien externe.
	const NodeId p = f.d.CreateNode(NodeType::Paragraph, f.body).Value();
	const auto run = [&](std::string_view text, FieldKind field, std::string link) {
		const NodeId t = f.d.CreateNode(NodeType::Text, p).Value();
		(void)f.d.Set(t, props::Text, text);
		if (field != FieldKind::None)
			(void)f.d.Set(t, props::Field, field);
		if (!link.empty())
			(void)f.d.Set(t, props::Link, link);
		return t;
	};
	run("voir ", FieldKind::None, {});
	run("", FieldKind::Reference, InternalLink(cap));
	run(", page ", FieldKind::None, {});
	run("", FieldKind::PageReference, InternalLink(cap));
	run(" et ", FieldKind::None, {});
	run("le site", FieldKind::None, "https://example.org/doc");
	// En-tête : titre courant.
	const NodeId header = f.d.CreateNode(NodeType::Header, f.section).Value();
	const NodeId hp = f.d.CreateNode(NodeType::Paragraph, header).Value();
	const NodeId ht = f.d.CreateNode(NodeType::Text, hp).Value();
	(void)f.d.Set(ht, props::Field, FieldKind::Chapter);
	const LayoutResult r = Layout(f.d);
	REQUIRE(r.pages.size() == 2);
	// Segments : « voir », renvoi (numéro de la légende), « , page », renvoi de page, « et », lien.
	const PlacedLine *refs = f.FirstLine(r, p);
	REQUIRE(refs != nullptr);
	REQUIRE(refs->runs.size() == 6);
	CHECK(refs->runs[1].field == FieldKind::Reference);
	CHECK(refs->runs[1].text == "Tableau 1");
	CHECK(refs->runs[3].field == FieldKind::PageReference);
	CHECK(refs->runs[3].text == "1");
	CHECK(refs->runs[5].style.link == "https://example.org/doc");
	const auto headerText = [&](size_t page) {
		for (const PlacedLine &l : r.pages[page].lines)
			if (l.paragraph == hp)
				return RunsText(l);
		return std::string();
	};
	CHECK(headerText(0) == "1  Présentation");
	CHECK(headerText(1) == "2  Brochage");
	size_t page = 9;
	double top = 0;
	REQUIRE(r.Locate(h2, page, top));
	CHECK_EQ(page, size_t(1));
	CHECK(r.RunAt(0, 0, 0) == nullptr);
	// PDF : lien interne (renvoi), lien externe, signets des titres.
	PdfExportOptions options;
	options.compress = false;
	auto pdf = ExportPdf(f.d, r, options);
	REQUIRE(pdf.IsOk());
	const std::string s(pdf.Value().begin(), pdf.Value().end());
	CHECK(s.find("/Dest [") != std::string::npos);
	CHECK(s.find("/URI (https://example.org/doc)") != std::string::npos);
	CHECK(s.find("/Outlines") != std::string::npos);
	CHECK(s.find("/Title <FEFF") != std::string::npos);
	// HTML : titres, ancres, renvoi en lien.
	auto html = ExportHtml(f.d);
	REQUIRE(html.IsOk());
	CHECK(html.Value().find(std::format("<h1 id=\"n{}\"", h2)) != std::string::npos);
	CHECK(html.Value().find(std::format("<a href=\"#n{}\"", cap)) != std::string::npos);
	CHECK(html.Value().find("<a href=\"https://example.org/doc\"") != std::string::npos);
	CHECK(html.Value().find("<span class=\"number\">2</span>") != std::string::npos);
}

TEST(Structure, ScriptStrikeBordersAndPreservedSpaces) {
	Fixture f;
	const NodeId p = f.d.CreateNode(NodeType::Paragraph, f.body).Value();
	const NodeId base = f.d.CreateNode(NodeType::Text, p).Value();
	(void)f.d.Set(base, props::Text, "x");
	const NodeId sup = f.d.CreateNode(NodeType::Text, p).Value();
	(void)f.d.Set(sup, props::Text, "2");
	(void)f.d.Set(sup, props::Script, TextScript::Superscript);
	(void)f.d.Set(sup, props::Strike, true);
	const NodeId note = f.Para("Note : encadré à filets");
	(void)f.d.Set(note, props::BorderWidth, 1.0);
	(void)f.d.Set(note, props::BorderSides, border::kTop | border::kBottom);
	const NodeId code = f.Para("    retrait");
	(void)f.d.Set(code, props::PreserveSpaces, true);
	const NodeId plain = f.Para("    retrait");
	const LayoutResult r = Layout(f.d);
	const PlacedLine *line = f.FirstLine(r, p);
	REQUIRE(line != nullptr);
	REQUIRE(line->runs.size() == 2);
	CHECK_NEAR(line->runs[1].style.size, 11 * 0.65, 1e-9);
	CHECK(line->runs[1].style.rise > 0);
	const DisplayList list = RenderPage(f.d, r, 0);
	bool raised = false, struck = false, rule = false, frame = false;
	for (const DrawCommand &c : list.commands) {
		if (c.kind == DrawKind::Text && c.node == sup)
			raised = c.baseline < line->baseline - 1;
		if (c.kind == DrawKind::Line && c.node == sup)
			struck = true;
		if (c.kind == DrawKind::Line && c.node == note)
			rule = true;
		if (c.kind == DrawKind::StrokeRect && c.node == note)
			frame = true;
	}
	CHECK(raised);
	CHECK(struck);
	CHECK(rule);
	CHECK(!frame); // côtés choisis : pas de cadre complet
	// Espaces de début de ligne conservés (code) : le texte est décalé.
	CHECK(f.FirstLine(r, code)->runs.front().x > f.FirstLine(r, plain)->runs.front().x + 5);
}

TEST(Structure, NewPropertiesAndTocPersist) {
	Fixture f;
	(void)f.d.Set(f.d.Root(), props::HeadingNumbering, HeadingNumbering::DecimalDot);
	const NodeId toc = f.d.CreateNode(NodeType::TableOfContents, f.body).Value();
	(void)f.d.Set(toc, props::TocDepth, int64_t(4));
	const NodeId h = f.Heading("Titre", 2);
	const NodeId item = f.Item("élément", ListStyle::LowerRoman, 2);
	(void)f.d.Set(f.TextOf(item), props::Link, "https://example.org");
	const auto path = std::filesystem::temp_directory_path() / "apps-lib-structure.docdb";
	REQUIRE(SaveDocument(f.d, path).IsOk());
	auto loaded = LoadDocument(path);
	REQUIRE(loaded.IsOk());
	const Document &d = loaded.Value();
	CHECK(d.Find(toc)->Type() == NodeType::TableOfContents);
	CHECK_EQ(d.Get(toc, props::TocDepth), int64_t(4));
	CHECK_EQ(d.Get(h, props::OutlineLevel), int64_t(2));
	CHECK(d.Get(item, props::ListStyle) == ListStyle::LowerRoman);
	CHECK(d.Get(d.Find(item)->Children().front(), props::Link) == "https://example.org");
	CHECK(d.Get(d.Root(), props::HeadingNumbering) == HeadingNumbering::DecimalDot);
	std::filesystem::remove(path);
	CHECK(LinkTarget("#42") == 42);
	CHECK(LinkTarget("#4x") == kNoNode);
	CHECK(LinkTarget("https://a") == kNoNode);
}
