// Tests de Scribe (partie sans fenêtre) : onglet contextuel du ruban, contrôleur de l'éditeur
// (insertions, mise en forme, tableau, disposition, propriétés, sélection à la souris, mise en
// page de fond, fichiers et exports en arrière-plan), disposition des pages à l'écran,
// document de démonstration ; édition du texte (curseur, plage, saisie, presse-papiers,
// glisser-déposer), déplacement de blocs, formes (redimensionnement, rotation, repères),
// polices installées, géométrie du cadre de transformation.
#include <cpp-maker/test.hpp>

#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>

#include <pdf/truetype.hpp>

#include <document/persistence.hpp>

#include "scribe/demo.hpp"
#include "scribe/display.hpp"
#include "scribe/draft.hpp"
#include "scribe/editor.hpp"
#include "scribe/i18n.hpp"
#include "scribe/navigator.hpp"
#include "scribe/ribbon.hpp"
#include "scribe/rulers.hpp"
#include "scribe/settings.hpp"
#include "scribe/table_tools.hpp"
#include "scribe/templates.hpp"
#include "scribe/theme.hpp"

using namespace scribe;
using namespace doc;

namespace {

/// Attend la fin des tâches de fond (mise en page, fichiers) en appelant `Update`.
void Settle(Editor &e) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	do {
		e.Update();
		if (!e.Busy() && e.Current() && e.Current()->snapshot->Revision() == e.Document().Revision())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	} while (std::chrono::steady_clock::now() < deadline);
	e.Update();
}

bool EqualsFamily(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}

std::filesystem::path Temp(std::string_view name) { return std::filesystem::temp_directory_path() / std::format("apps-lib-scribe-{}", name); }

std::string TextOf(const doc::Document &d, NodeId paragraph) {
	std::string s;
	for (NodeId t : d.Find(paragraph)->Children())
		s += d.Get(t, props::Text);
	return s;
}

} // namespace

TEST(Ribbon, ContextualTab) {
	SelectionContext c;
	CHECK(SuggestTab(c, RibbonTab::View) == RibbonTab::View); // vide : inchangé
	c.count = 1;
	c.text = true;
	CHECK(SuggestTab(c, RibbonTab::File) == RibbonTab::Home);
	c.table = true;
	CHECK(SuggestTab(c, RibbonTab::Home) == RibbonTab::Table);
	c = {};
	c.count = 1;
	c.image = true;
	CHECK(SuggestTab(c, RibbonTab::Home) == RibbonTab::Arrange);
	c.text = true; // image et texte : le texte l'emporte
	CHECK(SuggestTab(c, RibbonTab::Insert) == RibbonTab::Home);
	c = {};
	c.count = 1;
	c.group = true;
	CHECK(SuggestTab(c, RibbonTab::Home) == RibbonTab::Arrange);
	c = {};
	c.count = 1;
	c.page = true;
	CHECK(SuggestTab(c, RibbonTab::Home) == RibbonTab::PageLayout);
	CHECK(TabName(RibbonTab::PageLayout) == "Mise en page");
}

TEST(Editor, InsertFormatUndo) {
	Editor e;
	CHECK(!e.Dirty());
	CHECK(e.Title() == "Sans titre");
	const NodeId first = e.Selection().Primary();
	REQUIRE(e.Document().Find(first)->Type() == NodeType::Text);
	REQUIRE(e.EditText(first, "Bonjour").IsOk());
	REQUIRE(e.EditText(first, "Bonjour monde").IsOk());
	CHECK_EQ(e.History().Entries().size(), size_t(1)); // frappe fusionnée
	CHECK(e.Dirty());
	CHECK(e.Title() == "Sans titre •");
	REQUIRE(e.ToggleText(PropertyId::Bold).IsOk());
	CHECK(e.TextFlag(PropertyId::Bold));
	REQUIRE(e.ToggleText(PropertyId::Bold).IsOk());
	CHECK(!e.TextFlag(PropertyId::Bold));
	REQUIRE(e.SetParagraph(PropertyId::HAlign, int64_t(HorizontalAlign::Center)).IsOk());
	CHECK(e.Document().Get(e.Document().Find(first)->Parent(), props::HAlign) == HorizontalAlign::Center);
	// Insertion après le paragraphe sélectionné, le nouveau texte devient la sélection.
	REQUIRE(e.InsertParagraph("Deuxième").IsOk());
	const NodeId second = e.Selection().Primary();
	const NodeId body = e.Document().Body(e.Document().Sections()[0]);
	REQUIRE(e.Document().Find(body)->Children().size() == 2);
	CHECK(TextOf(e.Document(), e.Document().Find(body)->Children()[1]) == "Deuxième");
	CHECK(e.Document().Get(second, props::Text) == std::string("Deuxième"));
	REQUIRE(e.Undo().IsOk()); // insertion annulée d'un bloc (transaction)
	CHECK_EQ(e.Document().Find(body)->Children().size(), size_t(1));
	CHECK(e.Selection().Empty() || e.Document().Contains(e.Selection().Primary()));
	REQUIRE(e.Redo().IsOk());
	CHECK_EQ(e.Document().Find(body)->Children().size(), size_t(2));
	REQUIRE(e.UndoTo(0).IsOk());
	CHECK(!e.Dirty());
	CHECK(e.ToggleText(PropertyId::FontSize).IsError()); // non booléenne
}

TEST(Editor, TablesShapesPagesAndGroups) {
	Editor e;
	REQUIRE(e.InsertTable(2, 3).IsOk());
	const doc::Document &d = e.Document();
	const NodeId table = e.Context().enclosingTable;
	REQUIRE(table != kNoNode);
	CHECK(e.Context().table);
	CHECK_EQ(d.Find(table)->Children().size(), size_t(2));
	CHECK_NEAR(d.Get(d.Find(d.Find(table)->Children()[0])->Children()[0], props::PaddingLeft), 4.0, 1e-9);
	REQUIRE(e.AddRow(true).IsOk());
	REQUIRE(e.AddColumn(true).IsOk());
	CHECK_EQ(d.Find(table)->Children().size(), size_t(3));
	for (NodeId row : d.Find(table)->Children())
		CHECK_EQ(d.Find(row)->Children().size(), size_t(4));
	REQUIRE(e.SetCells(PropertyId::BackgroundColor, Color(1, 2, 3)).IsOk());
	REQUIRE(e.DeleteColumn().IsOk());
	REQUIRE(e.DeleteRow().IsOk());
	CHECK_EQ(d.Find(table)->Children().size(), size_t(2));
	CHECK_EQ(d.Find(d.Find(table)->Children()[0])->Children().size(), size_t(3));
	// Formes, saut de page, numéro de page.
	REQUIRE(e.InsertShape(ShapeKind::Ellipse).IsOk());
	const NodeId shape = e.Selection().Primary();
	CHECK(d.Get(shape, props::Shape) == ShapeKind::Ellipse);
	CHECK(d.Get(shape, props::BackgroundColor).a != 0);
	REQUIRE(e.InsertPageBreak().IsOk());
	REQUIRE(e.InsertParagraph("après le saut").IsOk());
	REQUIRE(e.InsertPageNumber().IsOk());
	e.LayoutNow();
	REQUIRE(e.Current() != nullptr);
	CHECK_EQ(e.Current()->layout.pages.size(), size_t(2));
	CHECK_EQ(d.FindAll(NodeType::Page).size(), size_t(2)); // pages synchronisées
	CHECK(e.Current()->layout.pages[1].footer != kNoNode);
	// Mise en page de la section entière (aucune page sélectionnée).
	REQUIRE(e.SetPage(PropertyId::PageOrientation, int64_t(Orientation::Landscape)).IsOk());
	e.LayoutNow();
	CHECK(e.Current()->layout.pages[0].geometry.width > e.Current()->layout.pages[0].geometry.height);
	// Groupes, duplication, masquage, verrou, suppression.
	e.SelectNode(shape);
	REQUIRE(e.GroupSelection().IsOk());
	const NodeId group = e.Selection().Primary();
	CHECK(d.Find(group)->Type() == NodeType::Group);
	CHECK(SuggestTab(e.Context(), RibbonTab::Home) == RibbonTab::Arrange);
	REQUIRE(e.SetSelected(PropertyId::Opacity, 0.5).IsOk());
	CHECK_NEAR(d.Get(shape, props::Opacity), 0.5, 1e-9); // via le groupe
	REQUIRE(e.UngroupSelection().IsOk());
	CHECK(!d.Contains(group));
	e.SelectNode(shape);
	REQUIRE(e.DuplicateSelection().IsOk());
	const NodeId copy = e.Selection().Primary();
	CHECK(copy != shape);
	REQUIRE(e.ToggleHidden().IsOk());
	CHECK(!d.Get(copy, props::Visible));
	REQUIRE(e.ToggleLocked().IsOk());
	CHECK(e.Context().locked);
	CHECK(e.DeleteSelection().Error().code == ErrorCode::Locked);
	REQUIRE(e.ToggleLocked().IsOk());
	REQUIRE(e.DeleteSelection().IsOk());
	CHECK(!d.Contains(copy));
	CHECK(e.Selection().Empty());
	REQUIRE(e.ClearSelected(PropertyId::Opacity).Error().code == ErrorCode::Nothing);
}

TEST(Editor, MouseSelectionAndBackgroundLayout) {
	Editor e;
	REQUIRE(e.EditText(e.Selection().Primary(), "Premier paragraphe").IsOk());
	REQUIRE(e.InsertParagraph("Second paragraphe").IsOk());
	Settle(e);
	const Presentation *p = e.Current();
	REQUIRE(p != nullptr);
	REQUIRE(!p->pages.empty());
	CHECK_EQ(p->layout.pages.size(), p->pages.size());
	CHECK(p->layout.pages[0].page != kNoNode || e.Document().FindAll(NodeType::Page).size() == 1);
	const PlacedLine &line = p->layout.pages[0].lines[0];
	e.SelectAt(0, line.runs[0].x + 2, line.baseline - 3, false);
	CHECK(e.Document().Find(e.Selection().Primary())->Type() == NodeType::Text);
	CHECK(SuggestTab(e.Context(), RibbonTab::View) == RibbonTab::Home);
	e.SelectAt(0, line.runs[0].x + 2, p->layout.pages[0].lines[1].baseline - 3, true);
	CHECK_EQ(e.Selection().Size(), size_t(2));
	e.SelectArea(0, Rect(0, 0, 1000, 1000), false);
	CHECK_EQ(e.Selection().Size(), size_t(2)); // les deux paragraphes
	e.SelectAt(0, 5, 5, false); // marge : la page elle-même
	CHECK(e.Context().page || e.Selection().Empty());
	// Modification : nouvelle mise en page de fond, la présentation suit le document.
	e.SelectAll();
	REQUIRE(e.SetText(PropertyId::FontSize, 30.0).IsOk());
	Settle(e);
	CHECK_EQ(e.Current()->snapshot->Revision(), e.Document().Revision());
	CHECK_NEAR(e.Current()->layout.pages[0].lines[0].runs[0].style.size, 30.0, 1e-9);
	e.SetShowBlockBounds(true);
	Settle(e);
	CHECK(e.Current()->pages[0].commands.size() > p->pages.size());
}

TEST(Editor, FilesAndExportsInBackground) {
	Editor e;
	BuildDemo(e);
	CHECK(!e.Dirty());
	REQUIRE(e.EditText(e.Selection().Primary(), "Titre modifié").IsOk());
	CHECK(e.Dirty());
	const auto path = Temp("doc.docdb");
	e.SaveAsync(path);
	REQUIRE(e.EditText(e.Selection().Primary(), "Titre modifié encore").IsOk()); // édition pendant l'enregistrement
	Settle(e);
	auto messages = e.TakeMessages();
	REQUIRE(!messages.empty());
	CHECK(messages.back().starts_with("enregistré"));
	CHECK(e.Path() == path);
	CHECK(e.Dirty()); // l'état enregistré est celui du lancement
	auto saved = LoadDocument(path);
	REQUIRE(saved.IsOk());
	CHECK(saved.Value().Get(e.Selection().Primary(), props::Text) == std::string("Titre modifié"));
	REQUIRE(e.Save(path).IsOk());
	CHECK(!e.Dirty());
	// Ouverture de fond : le document est remplacé.
	Editor other;
	other.OpenAsync(path);
	Settle(other);
	CHECK(other.Path() == path);
	CHECK_EQ(other.Document().NodeCount(), e.Document().NodeCount());
	CHECK(other.TakeMessages().back().starts_with("ouvert"));
	other.OpenAsync(Temp("absent.docdb"));
	Settle(other);
	CHECK(other.TakeMessages().back().starts_with("ouverture impossible"));
	// Exports.
	const auto pdf = Temp("doc.pdf"), html = Temp("doc.html"), csv = Temp("doc.csv");
	e.ExportAsync(ExportKind::Pdf, pdf);
	Settle(e);
	e.ExportAsync(ExportKind::Html, html);
	Settle(e);
	CHECK(std::filesystem::file_size(pdf) > 500);
	CHECK(std::filesystem::file_size(html) > 500);
	CHECK(e.Export(ExportKind::Csv, csv).Error().code == ErrorCode::InvalidValue); // aucun tableau sélectionné
	const NodeId table = e.Document().FindAll(NodeType::Table).front();
	e.SelectNode(table);
	REQUIRE(e.Export(ExportKind::Csv, csv).IsOk());
	CHECK(std::filesystem::file_size(csv) > 10);
	for (const auto &f : {path, pdf, html, csv})
		std::filesystem::remove(f);
}

TEST(Display, PagePlacementAndCoordinates) {
	std::vector<DisplayList> pages(3);
	for (DisplayList &p : pages) {
		p.width = 595.28;
		p.height = 841.89;
	}
	pages[1].width = 841.89; // paysage
	pages[1].height = 595.28;
	const sdl3::FRect area{100, 50, 1200, 800};
	const PageLayoutResult r = PlacePages(pages, area, 1.f, 0.f);
	REQUIRE(r.pages.size() == 3);
	CHECK_NEAR(r.pages[0].scale, kPixelsPerPoint, 1e-6);
	CHECK_NEAR(r.pages[0].rect.w, 595.28f * kPixelsPerPoint, 1e-3);
	CHECK_NEAR(r.pages[0].rect.x + r.pages[0].rect.w / 2, area.x + area.w / 2, 1e-3); // centrée
	CHECK_NEAR(r.pages[1].rect.y, r.pages[0].rect.y + r.pages[0].rect.h + kPageGap, 1e-3);
	CHECK(r.contentHeight > r.pages[2].rect.y + r.pages[2].rect.h - area.y);
	const auto hit = ScreenToPage(r.pages, r.pages[1].rect.x + 10 * kPixelsPerPoint, r.pages[1].rect.y + 20 * kPixelsPerPoint);
	REQUIRE(hit.has_value());
	CHECK_EQ(hit->page, size_t(1));
	CHECK_NEAR(hit->x, 10.0, 1e-3);
	CHECK_NEAR(hit->y, 20.0, 1e-3);
	CHECK(!ScreenToPage(r.pages, 0, 0).has_value());
	const sdl3::FRect back = PageToScreen(r.pages[1], Rect(10, 20, 5, 5));
	CHECK_NEAR(back.x, r.pages[1].rect.x + 10 * kPixelsPerPoint, 1e-3);
	const PageLayoutResult scrolled = PlacePages(pages, area, 2.f, 100.f);
	CHECK_NEAR(scrolled.pages[0].rect.y, area.y + kPageGap - 100.f, 1e-3);
	CHECK_NEAR(FitWidthZoom(pages, 1200.f) * 841.89f * kPixelsPerPoint, 1200.f - 2 * kPageGap, 1e-2);
}

TEST(Demo, DocumentIsComplete) {
	Editor e;
	BuildDemo(e);
	const doc::Document &d = e.Document();
	CHECK(!e.History().CanUndo());
	CHECK(d.Find(e.Selection().Primary())->Type() == NodeType::Text); // titre sélectionné
	CHECK_EQ(d.FindAll(NodeType::Table).size(), size_t(4));
	CHECK_EQ(d.FindAll(NodeType::TableOfContents).size(), size_t(3));
	CHECK_EQ(d.FindAll(NodeType::Header).size(), size_t(2)); // courant, et vide en page de titre
	CHECK_EQ(d.FindAll(NodeType::Footer).size(), size_t(1));
	const Outline &o = e.DocumentOutline();
	CHECK_EQ(o.Entries(TocContent::Headings, 1).size(), size_t(4));
	CHECK_EQ(o.Entries(TocContent::Headings, 9).size(), size_t(11)); // 4 chapitres, 7 sections
	CHECK_EQ(o.Entries(TocContent::Tables, 9).size(), size_t(3));
	CHECK_EQ(o.Entries(TocContent::Figures, 9).size(), size_t(1));
	CHECK_EQ(o.Entries(TocContent::Listings, 9).size(), size_t(1));
	e.LayoutNow();
	const LayoutResult &r = e.Current()->layout;
	CHECK(r.pages.size() >= 6);
	// Tables des matières remplies (numéros de page), renvois résolus, titre courant d'en-tête.
	size_t tocLines = 0, unresolved = 0;
	bool reference = false, chapter = false;
	for (const PageLayout &page : r.pages)
		for (const PlacedLine &l : page.lines)
			for (const GlyphRun &run : l.runs) {
				if (run.field == FieldKind::PageReference && run.generated) {
					++tocLines;
					unresolved += run.text == "?" ? 1u : 0u;
				}
				reference = reference || (run.field == FieldKind::Reference && run.text == "Tableau\u00A02");
				chapter = chapter || (run.field == FieldKind::Chapter && run.text.starts_with("2\u00A0\u00A0Brochage"));
			}
	CHECK_EQ(tocLines, size_t(15)); // entrées des trois tables : 11 titres, 3 tableaux, 1 figure
	CHECK_EQ(unresolved, size_t(0));
	CHECK(reference);
	CHECK(chapter);
}

TEST(Editor, SelectionDerivedValuesAreMemoized) {
	Editor e;
	for (int i = 0; i < 300; ++i)
		REQUIRE(e.InsertParagraph(std::format("paragraphe {}", i)).IsOk());
	e.SelectAll();
	const SelectionContext *first = &e.Context();
	const std::vector<NodeId> *texts = &e.TextTargets();
	CHECK_EQ(texts->size(), size_t(301));
	CHECK(!e.TextFlag(PropertyId::Bold));
	for (int i = 0; i < 1000; ++i) { // même objet tant que rien ne change : aucun recalcul
		CHECK(&e.Context() == first);
		CHECK_EQ(e.TextTargets().size(), size_t(301));
	}
	CHECK_EQ(e.Context().count, size_t(301));
	REQUIRE(e.ToggleText(PropertyId::Bold).IsOk()); // modification : valeurs recalculées
	CHECK(e.TextFlag(PropertyId::Bold));
	e.SelectNode(e.TextTargets().front());
	CHECK_EQ(e.Context().count, size_t(1));
	CHECK_EQ(e.TextTargets().size(), size_t(1));
}

namespace {

NodeId BodyOf(const doc::Document &d) { return d.Body(d.Sections()[0]); }
std::string Para(const doc::Document &d, size_t index) { return TextOf(d, d.Find(BodyOf(d))->Children()[index]); }

} // namespace

TEST(Editor, TypingCaretAndRanges) {
	Editor e;
	REQUIRE(e.Editing()); // nouveau document : curseur dans le paragraphe vide
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("Bonjour").IsOk());
	REQUIRE(e.TypeText(" monde").IsOk());
	CHECK_EQ(e.History().Entries().size(), size_t(1)); // frappes fusionnées
	CHECK(Para(d, 0) == "Bonjour monde");
	CHECK_EQ(e.Caret().offset, size_t(13));
	REQUIRE(e.Enter().IsOk());
	REQUIRE(e.TypeText("Été").IsOk());
	CHECK_EQ(d.Find(BodyOf(d))->Children().size(), size_t(2));
	CHECK(Para(d, 1) == "Été");
	// Déplacements : début du récit, mot suivant, extension par Maj.
	e.MoveCaret(Motion::StoryStart, false);
	CHECK_EQ(e.Caret().offset, size_t(0));
	e.MoveCaret(Motion::WordRight, true); // convention de Word : début du mot suivant
	CHECK(e.HasTextRange());
	CHECK(e.Copy().plain == "Bonjour ");
	// La frappe remplace la plage, en une entrée d'historique.
	const size_t entries = e.History().Entries().size();
	REQUIRE(e.TypeText("Salut ").IsOk());
	CHECK(Para(d, 0) == "Salut monde");
	CHECK_EQ(e.History().Entries().size(), entries + 1);
	// Retour arrière au début du 2e paragraphe : fusion des paragraphes.
	e.MoveCaret(Motion::StoryEnd, false);
	e.MoveCaret(Motion::Left, false);
	e.MoveCaret(Motion::Left, false);
	e.MoveCaret(Motion::Left, false); // « É » compte pour un caractère
	CHECK_EQ(e.Caret().offset, size_t(0));
	REQUIRE(e.Backspace(false).IsOk());
	CHECK_EQ(d.Find(BodyOf(d))->Children().size(), size_t(1));
	CHECK(Para(d, 0) == "Salut mondeÉté");
	// Suppression d'un mot vers l'avant, puis tout le récit (Ctrl+A) et gauche réduit la plage.
	e.MoveCaret(Motion::StoryStart, false);
	REQUIRE(e.DeleteForward(true).IsOk());
	CHECK(Para(d, 0) == "mondeÉté"); // le mot et son espace
	e.SelectStory();
	CHECK(e.Copy().plain == "mondeÉté");
	e.MoveCaret(Motion::Left, false);
	CHECK(!e.HasTextRange());
	CHECK_EQ(e.Caret().offset, size_t(0));
	CHECK(e.Backspace(false).Error().code == ErrorCode::Nothing); // début du texte
	// Annuler rétablit le texte ; le curseur reste valide.
	while (e.History().CanUndo())
		REQUIRE(e.Undo().IsOk());
	CHECK(Para(d, 0).empty());
	CHECK(!e.Editing() || d.Contains(e.Caret().text));
	// Quitter le texte : sélection de nœuds.
	e.SelectAll();
	CHECK(!e.Editing());
}

TEST(Editor, MouseCaretGeometryAndStyle) {
	Editor e;
	REQUIRE(e.TypeText("Premier paragraphe assez court").IsOk());
	REQUIRE(e.Enter().IsOk());
	REQUIRE(e.TypeText("Second").IsOk());
	e.LayoutNow();
	const PageLayout &page = e.Current()->layout.pages[0];
	const PlacedLine &line = page.lines[0];
	// Clic : curseur ; double-clic : mot ; triple-clic : paragraphe.
	REQUIRE(e.PlaceCaretAt(0, line.runs[0].x + 1, line.baseline - 3));
	CHECK_EQ(e.Caret().offset, size_t(0));
	const auto caret = e.CaretGeometry(e.Caret());
	REQUIRE(caret.has_value());
	CHECK_EQ(caret->page, size_t(0));
	CHECK_NEAR(caret->rect.x, line.runs[0].x, 0.5);
	e.SelectWordAt(0, line.runs[0].x + 60, line.baseline - 3);
	CHECK(e.Copy().plain == "paragraphe");
	REQUIRE(!e.RangeGeometry().empty());
	CHECK_EQ(e.RangeGeometry()[0].first, size_t(0));
	CHECK(e.RangeGeometry()[0].second.w > 10);
	e.SelectParagraphAt(0, line.runs[0].x + 1, line.baseline - 3);
	CHECK(e.Copy().plain == "Premier paragraphe assez court");
	// Style d'une plage : seul le mot est mis en gras (segment coupé aux bornes).
	e.SelectWordAt(0, line.runs[0].x + 60, line.baseline - 3);
	REQUIRE(e.SetText(PropertyId::Bold, true).IsOk());
	const doc::Document &d = e.Document();
	const Node *p0 = d.Find(d.Find(BodyOf(d))->Children()[0]);
	REQUIRE(p0->Children().size() == 3);
	CHECK(d.Get(p0->Children()[1], props::Bold));
	CHECK(!d.Get(p0->Children()[0], props::Bold));
	CHECK(e.Copy().plain == "paragraphe"); // la plage suit le texte coupé
	// Taille de police personnalisée et surlignage sur la plage.
	REQUIRE(e.SetText(PropertyId::FontSize, 10.5).IsOk());
	REQUIRE(e.SetText(PropertyId::BackgroundColor, Color(255, 240, 0)).IsOk());
	CHECK_NEAR(d.Get(p0->Children()[1], props::FontSize), 10.5, 1e-9);
	CHECK(d.Get(p0->Children()[1], props::BackgroundColor) == Color(255, 240, 0));
	CHECK_NEAR(d.Get(p0->Children()[0], props::FontSize), 11.0, 1e-9);
	// Flèche bas : ligne suivante (second paragraphe).
	e.PlaceCaret({p0->Children()[0], 3});
	e.LayoutNow();
	e.MoveCaret(Motion::Down, false);
	CHECK(d.AncestorOfType(e.Caret().text, NodeType::Paragraph) == d.Find(BodyOf(d))->Children()[1]);
	e.MoveCaret(Motion::LineEnd, false);
	CHECK_EQ(e.Caret().offset, size_t(6));
}

TEST(Editor, ClipboardTextAndNodes) {
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("Un deux trois").IsOk());
	REQUIRE(e.Enter().IsOk());
	REQUIRE(e.TypeText("Quatre").IsOk());
	// Couper « deux » puis le coller à la fin : texte enrichi (gras conservé).
	const NodeId t0 = d.Find(d.Find(BodyOf(d))->Children()[0])->Children()[0];
	e.PlaceCaret({t0, 3});
	e.PlaceCaret({t0, 7}, true);
	REQUIRE(e.SetText(PropertyId::Bold, true).IsOk());
	Clip clip;
	REQUIRE(e.Cut(clip).IsOk());
	CHECK(!clip.isNodes);
	CHECK(clip.plain == "deux");
	CHECK(Para(d, 0) == "Un  trois");
	e.MoveCaret(Motion::StoryEnd, false);
	REQUIRE(e.Paste(clip).IsOk());
	CHECK(Para(d, 1) == "Quatredeux");
	const Node *p1 = d.Find(d.Find(BodyOf(d))->Children()[1]);
	CHECK(d.Get(p1->Children().back(), props::Bold));
	// Texte brut sur plusieurs lignes (autre application) : paragraphes créés.
	Clip plain;
	plain.plain = "A\nB";
	REQUIRE(e.Paste(plain).IsOk());
	CHECK_EQ(d.Find(BodyOf(d))->Children().size(), size_t(3));
	CHECK(Para(d, 2) == "B");
	// Nœuds : copier le premier paragraphe, coller après le paragraphe sélectionné.
	const NodeId first = d.Find(BodyOf(d))->Children()[0];
	e.SelectNode(first);
	const Clip nodes = e.Copy();
	REQUIRE(nodes.isNodes);
	CHECK(nodes.plain == "Un  trois");
	REQUIRE(e.Paste(nodes).IsOk());
	CHECK_EQ(d.Find(BodyOf(d))->Children().size(), size_t(4));
	CHECK(Para(d, 1) == "Un  trois");
	CHECK(d.Find(BodyOf(d))->Children()[1] != first);
	// Couper des nœuds : supprimés, puis recollés en fin (après le paragraphe sélectionné).
	Clip cut;
	REQUIRE(e.Cut(cut).IsOk());
	CHECK_EQ(d.Find(BodyOf(d))->Children().size(), size_t(3));
	e.SelectNode(d.Find(BodyOf(d))->Children()[2]);
	REQUIRE(e.Paste(cut).IsOk());
	CHECK(Para(d, 3) == "Un  trois");
	CHECK(Clip{}.Empty());
	e.ClearSelection();
	CHECK(e.Copy().Empty());
}

TEST(Editor, DragTextAndBlocks) {
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("alpha beta").IsOk());
	REQUIRE(e.Enter().IsOk());
	REQUIRE(e.TypeText("gamma").IsOk());
	const NodeId p0 = d.Find(BodyOf(d))->Children()[0], p1 = d.Find(BodyOf(d))->Children()[1];
	// Glisser « alpha » à la fin de « gamma » (déplacement), puis copie (Ctrl).
	e.PlaceCaret({d.Find(p0)->Children()[0], 0});
	e.PlaceCaret({d.Find(p0)->Children()[0], 5}, true);
	CHECK(e.InTextRange({d.Find(p0)->Children()[0], 2}));
	const NodeId gammaText = d.Find(p1)->Children()[0];
	CHECK(!e.InTextRange({gammaText, 2}));
	REQUIRE(e.DropText({gammaText, 5}, false).IsOk());
	CHECK(TextOf(d, p0) == " beta");
	CHECK(TextOf(d, p1) == "gammaalpha");
	CHECK(e.Copy().plain == "alpha"); // la plage déposée reste sélectionnée
	REQUIRE(e.DropText({d.Find(p0)->Children()[0], 0}, true).IsOk());
	CHECK(TextOf(d, p0) == "alpha beta");
	CHECK(TextOf(d, p1) == "gammaalpha");
	// Bloc : le tableau passe avant le premier paragraphe, puis un paragraphe dans une cellule.
	REQUIRE(e.InsertTable(1, 2).IsOk());
	const NodeId table = e.Context().enclosingTable;
	REQUIRE(e.MoveBlock(table, BodyOf(d), 0).IsOk());
	CHECK(d.Find(BodyOf(d))->Children()[0] == table);
	const NodeId cell = d.Find(d.Find(table)->Children()[0])->Children()[0];
	REQUIRE(e.MoveBlock(p1, cell, 0).IsOk());
	CHECK(d.Find(p1)->Parent() == cell);
	CHECK(e.Selection().Primary() == p1);
	CHECK(e.MoveBlock(table, cell, 0).IsError()); // cycle refusé
	REQUIRE(e.Undo().IsOk());
	CHECK(d.Find(p1)->Parent() == BodyOf(d));
}

TEST(Editor, ShapeResizeRotateAndBases) {
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("avant").IsOk());
	REQUIRE(e.InsertShape(ShapeKind::Rectangle).IsOk());
	const NodeId shape = e.Selection().Primary();
	e.LayoutNow();
	const LayoutBox *box = e.BoxOf(shape);
	REQUIRE(box != nullptr);
	const Rect start = box->rect;
	// Poignée gauche tirée de 20 pt vers la gauche : largeur +20, bord droit fixe.
	REQUIRE(e.ResizeShape(shape, Rect(start.x - 20, start.y, start.w + 20, start.h)).IsOk());
	CHECK_EQ(e.History().Entries().size(), size_t(3)); // une transaction (+ saisie, insertion)
	e.LayoutNow();
	box = e.BoxOf(shape);
	CHECK_NEAR(box->rect.x, start.x - 20, 1e-6);
	CHECK_NEAR(box->rect.x + box->rect.w, start.x + start.w, 1e-6);
	REQUIRE(e.MoveShape(shape, 10, 5).IsOk());
	e.LayoutNow();
	box = e.BoxOf(shape);
	CHECK_NEAR(box->rect.x, start.x - 10, 1e-6);
	CHECK_NEAR(box->rect.y, start.y + 5, 1e-6);
	// Rotation ramenée dans (−180, 180].
	REQUIRE(e.RotateShape(shape, 370).IsOk());
	CHECK_NEAR(d.Get(shape, props::Rotation), 10.0, 1e-9);
	REQUIRE(e.RotateShape(shape, 200).IsOk());
	CHECK_NEAR(d.Get(shape, props::Rotation), -160.0, 1e-9);
	// Centre de rotation : point de la page exprimé dans le repère choisi.
	e.LayoutNow();
	box = e.BoxOf(shape);
	const double cx = box->rect.x + box->rect.w / 2, cy = box->rect.y + box->rect.h / 2;
	REQUIRE(e.SetRotationOrigin(shape, cx + 15, cy - 5).IsOk());
	CHECK_NEAR(d.Get(shape, props::RotationOriginX), 15.0, 1e-9); // repère « forme » : décalage du centre
	CHECK_NEAR(d.Get(shape, props::RotationOriginY), -5.0, 1e-9);
	e.LayoutNow();
	CHECK_NEAR(e.BoxOf(shape)->pivotX, cx + 15, 1e-6);
	// Changement de repère du centre : coordonnées converties, le centre ne bouge pas.
	for (OriginBase base : {OriginBase::Page, OriginBase::Parent, OriginBase::Node}) {
		REQUIRE(e.SetOriginBase(shape, base).IsOk());
		e.LayoutNow();
		CHECK_NEAR(e.BoxOf(shape)->pivotX, cx + 15, 1e-6);
		CHECK_NEAR(e.BoxOf(shape)->pivotY, cy - 5, 1e-6);
	}
	REQUIRE(e.SetOriginBase(shape, OriginBase::Page).IsOk());
	CHECK_NEAR(d.Get(shape, props::RotationOriginX), cx + 15, 1e-6);
	// Changement de repère de la position : la forme reste en place dans chaque repère.
	e.LayoutNow();
	const Rect placed = e.BoxOf(shape)->rect;
	for (PositionBase base : {PositionBase::Page, PositionBase::Parent, PositionBase::Node, PositionBase::Auto}) {
		REQUIRE(e.SetPositionBase(shape, base).IsOk());
		e.LayoutNow();
		CHECK_NEAR(e.BoxOf(shape)->rect.x, placed.x, 1e-6);
		CHECK_NEAR(e.BoxOf(shape)->rect.y, placed.y, 1e-6);
	}
	REQUIRE(e.SetPositionBase(shape, PositionBase::Page).IsOk());
	CHECK_NEAR(d.Get(shape, props::PositionX), placed.x, 1e-6);
	CHECK(e.ResizeShape(kNoNode, Rect(0, 0, 1, 1)).Error().code == ErrorCode::NotFound);
}

TEST(Editor, InstalledFontsAreUsed) {
	const std::filesystem::path dir = "assets/fonts/Ubuntu";
	if (!std::filesystem::exists(dir))
		SKIP("polices Ubuntu du dépôt absentes");
	Editor e;
	CHECK(e.Fonts() == nullptr);
	e.ScanFontsAsync({dir});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (!e.Fonts() && std::chrono::steady_clock::now() < deadline) {
		e.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	REQUIRE(e.Fonts() != nullptr);
	const auto families = e.Fonts()->Families();
	REQUIRE(families.size() > 3);
	CHECK(families[0] == "Helvetica"); // polices standard en tête
	const std::string family = families[3];
	REQUIRE(e.TypeText("Texte en police installée").IsOk());
	e.SelectStory();
	REQUIRE(e.SetText(PropertyId::FontFamily, family).IsOk());
	e.LayoutNow();
	const GlyphRun &run = e.Current()->layout.pages[0].lines[0].runs[0];
	REQUIRE(run.style.font.face != nullptr); // mesurée avec le fichier, pas une police standard
	CHECK(EqualsFamily(run.style.font.face->Family(), family));
	CHECK_NEAR(run.width, run.style.font.TextWidth(run.text, run.style.size), 1e-6);
	// Export PDF : la police est incorporée.
	const auto pdfPath = Temp("fonts.pdf");
	REQUIRE(e.Export(ExportKind::Pdf, pdfPath).IsOk());
	std::ifstream in(pdfPath, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(bytes.find("/FontFile2") != std::string::npos || bytes.find("/FontFile3") != std::string::npos);
	std::filesystem::remove(pdfPath);
	// Substituts d'affichage des polices standard (mêmes métriques).
	CHECK(FontCache::Substitutes(pdf::StandardFont::Helvetica).front() == "Nimbus Sans");
	CHECK(FontCache::Substitutes(pdf::StandardFont::TimesBoldItalic).front() == "Nimbus Roman");
	CHECK(FontCache::Substitutes(pdf::StandardFont::CourierOblique).front() == "Nimbus Mono PS");
}

TEST(Display, ShapeResizeRatioGridAndCatalog) {
	// Redimensionnement en gardant les proportions (2:1).
	const ShapeFrame f{Rect(100, 100, 200, 100), 0, 200, 150};
	Rect r = ResizedRect(f, 4, 100, 10, true); // coin bas droit : la plus grande variation l'emporte
	CHECK_NEAR(r.w, 300, 1e-9);
	CHECK_NEAR(r.h, 150, 1e-9);
	CHECK_NEAR(r.x, 100, 1e-9);
	CHECK_NEAR(r.y, 100, 1e-9);
	r = ResizedRect(f, 0, -20, -40, true); // coin haut gauche : coin opposé fixe
	CHECK_NEAR(r.w / r.h, 2.0, 1e-9);
	CHECK_NEAR(r.Right(), 300, 1e-9);
	CHECK_NEAR(r.Bottom(), 200, 1e-9);
	r = ResizedRect(f, 3, 100, 0, true); // bord droit : la hauteur suit, centrée
	CHECK_NEAR(r.w, 300, 1e-9);
	CHECK_NEAR(r.h, 150, 1e-9);
	CHECK_NEAR(r.y + r.h / 2, 150, 1e-9);
	r = ResizedRect(f, 3, 100, 0, false); // sans proportions : la hauteur ne bouge pas
	CHECK_NEAR(r.h, 100, 1e-9);
	// Grille et Maj.
	CHECK_NEAR(SnapToGrid(13.0, 5.0), 15.0, 1e-9);
	CHECK_NEAR(SnapToGrid(13.0, 0.0), 13.0, 1e-9);
	auto [dx, dy] = SnappedMove(Rect(12, 7, 10, 10), 30, 4, 0, true); // Maj : axe horizontal seul
	CHECK_NEAR(dx, 30, 1e-9);
	CHECK_NEAR(dy, 0, 1e-9);
	std::tie(dx, dy) = SnappedMove(Rect(12, 7, 10, 10), 30, 4, 10, false); // coin aimanté (40, 10)
	CHECK_NEAR(12 + dx, 40, 1e-9);
	CHECK_NEAR(7 + dy, 10, 1e-9);
	std::tie(dx, dy) = SnappedMove(Rect(12, 7, 10, 10), 2, 31, 10, true); // vertical seul, aimanté
	CHECK_NEAR(dx, 0, 1e-9);
	CHECK_NEAR(7 + dy, 40, 1e-9);
	// Catalogue : toutes les formes du moteur, dans l'ordre de l'énumération, nommées.
	const auto shapes = ShapeCatalog();
	REQUIRE(shapes.size() == size_t(ShapeKind::ArrowLine) + 1);
	for (size_t i = 0; i < shapes.size(); ++i) {
		CHECK(shapes[i].kind == ShapeKind(i));
		CHECK(!shapes[i].name.empty());
		CHECK(Tr(shapes[i].name) == shapes[i].name); // français (langue des sources)
	}
	CHECK(ShapeName(ShapeKind::Star5) == "Étoile à 5 branches");
}

TEST(Display, ShapeFrameGeometry) {
	ShapeFrame f{Rect(100, 100, 200, 100), 0, 200, 150};
	auto [hx, hy] = HandlePoint(f, 4); // bas-droit
	CHECK_NEAR(hx, 300, 1e-9);
	CHECK_NEAR(hy, 200, 1e-9);
	CHECK_EQ(FrameHit(f, 101, 101, 4), 0);
	CHECK_EQ(FrameHit(f, 200, 99, 4), 1);
	CHECK_EQ(FrameHit(f, 150, 101, 4), kFrameBorder);
	CHECK_EQ(FrameHit(f, 150, 150, 4), kFrameInside);
	CHECK_EQ(FrameHit(f, 50, 50, 4), -1);
	// Poignée gauche : bord droit fixe ; taille minimale 1.
	Rect r = ResizedRect(f, 7, -30, 999);
	CHECK_NEAR(r.x, 70, 1e-9);
	CHECK_NEAR(r.w, 230, 1e-9);
	CHECK_NEAR(r.h, 100, 1e-9); // la poignée latérale ne touche pas la hauteur
	r = ResizedRect(f, 4, -500, -500);
	CHECK_NEAR(r.w, 1, 1e-9);
	CHECK_NEAR(r.h, 1, 1e-9);
	// Quart de tour : la poignée droite est en bas, glisser vers le bas élargit la forme.
	f.rotation = 90;
	std::tie(hx, hy) = HandlePoint(f, 3);
	CHECK_NEAR(hx, 200, 1e-9);
	CHECK_NEAR(hy, 250, 1e-9);
	CHECK_EQ(FrameHit(f, 200, 250, 4), 3);
	CHECK_EQ(FrameHit(f, 290, 150, 4), -1); // hors de la forme tournée
	r = ResizedRect(f, 3, 0, 40);
	CHECK_NEAR(r.w, 240, 1e-9);
	CHECK_NEAR(r.h, 100, 1e-9);
	// Rotation glissée autour du centre, pas de 15° avec Maj.
	f.rotation = 0;
	CHECK_NEAR(DraggedRotation(f, 300, 150, 200, 250, false), 90.0, 1e-9);
	CHECK_NEAR(DraggedRotation(f, 300, 150, 300, 160, true), 0.0, 1e-9);
	CHECK_NEAR(DraggedRotation(f, 300, 150, 300, 200, true), 30.0, 1e-9);
	f.rotation = 170;
	CHECK_NEAR(DraggedRotation(f, 300, 150, 200, 250, false), -100.0, 1e-9); // ramené dans (−180, 180]
}

TEST(Editor, StylesListsCaptionsAndToc) {
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("Introduction").IsOk());
	REQUIRE(e.ApplyStyle(ParagraphStyle::Heading1).IsOk());
	CHECK(e.CurrentStyle() == ParagraphStyle::Heading1);
	const NodeId h1 = d.AncestorOfType(e.Caret().text, NodeType::Paragraph);
	CHECK_EQ(d.Get(h1, props::OutlineLevel), int64_t(1));
	CHECK(d.Get(e.Caret().text, props::Bold)); // hérité du groupe du style
	// Le style est un groupe : le modifier restyle tous ses paragraphes.
	REQUIRE(e.Enter().IsOk());
	REQUIRE(e.TypeText("Contexte").IsOk());
	REQUIRE(e.ApplyStyle(ParagraphStyle::Heading2).IsOk());
	REQUIRE(e.ApplyStyle(ParagraphStyle::Heading1).IsOk()); // changement de style : un seul groupe
	const NodeId h2 = d.AncestorOfType(e.Caret().text, NodeType::Paragraph);
	size_t styleGroups = 0;
	for (NodeId g : d.AllGroups())
		styleGroups += d.Find(h2)->InGroup(g) ? 1u : 0u;
	CHECK_EQ(styleGroups, size_t(1));
	REQUIRE(e.ApplyStyle(ParagraphStyle::Normal).IsOk());
	CHECK_EQ(d.Get(h2, props::OutlineLevel), int64_t(0));
	REQUIRE(e.SetHeadingNumbering(HeadingNumbering::DecimalDot).IsOk());
	CHECK(e.Numbering() == HeadingNumbering::DecimalDot);
	CHECK(e.DocumentOutline().Find(h1)->label == "1.");
	// Listes : puces, niveau, retrait (Tab), bascule.
	REQUIRE(e.ToggleList(ListStyle::Bullet).IsOk());
	CHECK(d.Get(h2, props::ListStyle) == ListStyle::Bullet);
	REQUIRE(e.ChangeIndent(1).IsOk());
	CHECK_EQ(d.Get(h2, props::ListLevel), int64_t(1));
	REQUIRE(e.ToggleList(ListStyle::Bullet).IsOk());
	CHECK(d.Get(h2, props::ListStyle) == ListStyle::None);
	REQUIRE(e.ChangeIndent(1).IsOk()); // hors liste : retrait gauche
	CHECK_NEAR(d.Get(h2, props::IndentLeft), 18.0, 1e-9);
	// Légendes : tableau (au-dessus), forme (dessous, la forme reste avec elle).
	REQUIRE(e.InsertTable(2, 2).IsOk());
	const NodeId table = e.CurrentTable().table;
	REQUIRE(e.InsertCaption(CaptionKind::Table, "Mesures").IsOk());
	const NodeId tableCaption = d.AncestorOfType(e.Caret().text, NodeType::Paragraph);
	const auto body = d.Find(d.Body(d.Sections()[0]))->Children();
	CHECK_EQ(d.Find(d.Body(d.Sections()[0]))->IndexOfChild(tableCaption) + 1, d.Find(d.Body(d.Sections()[0]))->IndexOfChild(table));
	CHECK(d.Get(tableCaption, props::KeepWithNext));
	CHECK(e.DocumentOutline().Find(tableCaption)->label == "Tableau 1");
	e.SelectNode(table);
	REQUIRE(e.InsertShape(ShapeKind::Ellipse).IsOk());
	const NodeId shape = e.Selection().Primary();
	REQUIRE(e.InsertCaption(CaptionKind::Figure, "Schéma").IsOk());
	CHECK(d.Get(shape, props::KeepWithNext));
	(void)body;
	// Table des matières et lien interne / renvoi.
	e.SelectNode(h1);
	REQUIRE(e.InsertTableOfContents(TocContent::Headings).IsOk());
	CHECK_EQ(d.FindAll(NodeType::TableOfContents).size(), size_t(1));
	REQUIRE(e.GoTo(h1));
	CHECK(e.Editing());
	e.MoveCaret(Motion::StoryEnd, false);
	REQUIRE(e.InsertReference(tableCaption, FieldKind::Reference).IsOk());
	e.LayoutNow();
	bool found = false;
	for (const PageLayout &page : e.Current()->layout.pages)
		for (const PlacedLine &l : page.lines)
			for (const GlyphRun &run : l.runs)
				if (run.field == FieldKind::Reference) {
					found = run.text == "Tableau 1";
					CHECK(e.LinkAt(0, run.x + 1, l.rect.y + 2) == InternalLink(tableCaption));
				}
	CHECK(found);
	CHECK(e.InsertReference(999999, FieldKind::Reference).Error().code == ErrorCode::NotFound);
	// Lien externe sur une plage, puis retrait.
	REQUIRE(e.GoTo(h1));
	e.MoveCaret(Motion::WordRight, true);
	REQUIRE(e.SetLink("https://example.org").IsOk());
	const NodeId linked = e.TextTargets().front();
	CHECK(d.Get(linked, props::Link) == "https://example.org");
	CHECK(d.Get(linked, props::Underline));
	REQUIRE(e.RemoveLink().IsOk());
	CHECK(d.Get(linked, props::Link).empty());
	CHECK(e.InsertStyled(ParagraphStyle::Code, "  code").IsOk());
	CHECK(d.Get(d.AncestorOfType(e.Caret().text, NodeType::Paragraph), props::PreserveSpaces));
}

TEST(Editor, TableMultiSelectionMergeAndSplit) {
	Editor e;
	REQUIRE(e.InsertTable(5, 4).IsOk());
	const NodeId table = e.Document().FindAll(NodeType::Table).front();
	TableGrid g = TableGrid::Of(e.Document(), table);
	const auto selected = [&](size_t r, size_t c) { return e.Selection().Contains(TableGrid::Of(e.Document(), table).Cell(r, c)); };
	// Maj : rectangle contigu ; Ctrl : ajoute / retire ; Ctrl+Maj : ajoute un rectangle.
	e.SelectCells(table, 0, 0, 1, 1);
	CHECK(selected(1, 1));
	e.SelectCells(table, 3, 3, 3, 3, TableSelect::Toggle); // ajoutée
	CHECK(selected(0, 0));
	CHECK(selected(3, 3));
	e.SelectCells(table, 0, 0, 0, 0, TableSelect::Toggle); // retirée
	CHECK(!selected(0, 0));
	CHECK(selected(0, 1));
	e.SelectCells(table, 4, 0, 4, 1, TableSelect::Add);
	CHECK(selected(4, 0));
	CHECK(selected(3, 3));
	// Lignes et colonnes non contiguës.
	e.SelectRows(table, 0, 0);
	e.SelectRows(table, 2, 2, TableSelect::Toggle);
	e.SelectRows(table, 4, 4, TableSelect::Toggle);
	TableSelection t = e.CurrentTable();
	CHECK(t.rowList == (std::vector<size_t>{0, 2, 4}));
	CHECK(!t.ContiguousRows());
	CHECK(e.MoveRows(1).IsError()); // non contiguës : pas de déplacement
	e.SelectRows(table, 2, 2, TableSelect::Toggle); // désélectionnée
	CHECK(e.CurrentTable().rowList == (std::vector<size_t>{0, 4}));
	// Supprimer des lignes non contiguës : une seule entrée d'historique.
	REQUIRE(e.DeleteRows().IsOk());
	CHECK(TableGrid::Of(e.Document(), table).rows.size() == 3);
	REQUIRE(e.Undo().IsOk());
	CHECK(TableGrid::Of(e.Document(), table).rows.size() == 5);
	e.SelectColumns(table, 1, 1);
	e.SelectColumns(table, 3, 3, TableSelect::Toggle);
	REQUIRE(e.DuplicateColumns().IsOk());
	CHECK(TableGrid::Of(e.Document(), table).columns == 6);
	REQUIRE(e.Undo().IsOk());
	// Fusion : rectangle 2 × 3 ; sélectionner une partie de la fusion la sélectionne entière.
	g = TableGrid::Of(e.Document(), table);
	e.SelectCells(table, 1, 0, 2, 2);
	REQUIRE(e.MergeCells().IsOk());
	CHECK(e.Document().Get(g.Cell(1, 0), props::ColumnSpan) == 3);
	CHECK(e.Document().Get(g.Cell(1, 0), props::RowSpan) == 2);
	CHECK(e.Selection().Contains(g.Cell(1, 0)));
	CHECK(e.SelectionHasMergedCells());
	e.SelectCells(table, 2, 2, 2, 2); // case couverte : toute la fusion
	t = e.CurrentTable();
	CHECK(t.row0 == 1);
	CHECK(t.row1 == 2);
	CHECK(t.column0 == 0);
	CHECK(t.column1 == 2);
	// Géométrie des outils : toutes les colonnes, malgré la fusion.
	e.LayoutNow();
	const auto tables = TablesOn(*e.Current()->snapshot, e.Current()->layout, 0);
	REQUIRE(!tables.empty());
	CHECK(tables.front().ColumnCount() == 4);
	CHECK(tables.front().RowCount() == 5);
	// Séparer.
	REQUIRE(e.SplitCells().IsOk());
	CHECK(!e.SelectionHasMergedCells());
	CHECK(e.Document().Get(g.Cell(1, 0), props::ColumnSpan) == 1);
	e.SelectCells(table, 0, 0, 0, 0);
	CHECK(e.MergeCells().IsError()); // une seule case
}

TEST(Editor, TableSelectionAndOperations) {
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.InsertTable(4, 3).IsOk());
	TableSelection t = e.CurrentTable();
	REQUIRE(t.Valid());
	CHECK_EQ(t.rows, size_t(4));
	CHECK_EQ(t.columns, size_t(3));
	CHECK_EQ(t.row0, size_t(0));
	const NodeId table = t.table;
	const auto text = [&](size_t r, size_t c) {
		const NodeId cell = TableGrid::Of(d, table).Cell(r, c);
		return d.Get(d.Find(d.Find(cell)->Children().front())->Children().front(), props::Text);
	};
	const auto fill = [&] {
		const TableGrid g = TableGrid::Of(d, table);
		for (size_t r = 0; r < g.rows.size(); ++r)
			for (size_t c = 0; c < g.cells[r].size(); ++c)
				(void)e.Document().Set(d.Find(d.Find(g.cells[r][c])->Children().front())->Children().front(), props::Text,
									   std::format("{}{}", char('A' + c), r));
	};
	fill();
	// Lignes et colonnes entières.
	e.SelectRows(table, 1, 2);
	t = e.CurrentTable();
	CHECK(t.wholeRows);
	CHECK(!t.wholeColumns);
	CHECK_EQ(t.RowCount(), size_t(2));
	REQUIRE(e.DuplicateRows().IsOk());
	CHECK_EQ(TableGrid::Of(d, table).rows.size(), size_t(6));
	CHECK(text(3, 0) == "A1");
	CHECK(e.CurrentTable().row0 == 3); // les copies sont sélectionnées
	REQUIRE(e.DeleteRows().IsOk());
	CHECK_EQ(TableGrid::Of(d, table).rows.size(), size_t(4));
	e.SelectRows(table, 3, 3);
	REQUIRE(e.MoveRows(-2).IsOk());
	CHECK(text(1, 0) == "A3");
	CHECK(e.MoveRows(-5).Error().code == ErrorCode::Nothing);
	e.SelectColumns(table, 0, 0);
	t = e.CurrentTable();
	CHECK(t.wholeColumns);
	REQUIRE(e.MoveColumns(2).IsOk());
	CHECK(text(0, 2) == "A0");
	REQUIRE(e.InsertColumns(true, 2).IsOk());
	CHECK_EQ(TableGrid::Of(d, table).columns, size_t(5));
	REQUIRE(e.DeleteColumns().IsOk());
	CHECK_EQ(TableGrid::Of(d, table).columns, size_t(3));
	REQUIRE(e.DuplicateColumns().IsOk());
	CHECK_EQ(TableGrid::Of(d, table).columns, size_t(4));
	// Plage de cellules : fond, bordures, bandes, en-tête, largeurs.
	e.SelectCells(table, 0, 0, 1, 1);
	t = e.CurrentTable();
	CHECK_EQ(t.RowCount() * t.ColumnCount(), size_t(4));
	CHECK(!t.wholeRows);
	REQUIRE(e.SetCells(PropertyId::BackgroundColor, Color::LIGHT_YELLOW()).IsOk());
	CHECK(d.Get(TableGrid::Of(d, table).Cell(1, 1), props::BackgroundColor) == Color::LIGHT_YELLOW());
	REQUIRE(e.ApplyBorders(BorderPreset::Horizontal, 0.5, Color::BLACK()).IsOk());
	REQUIRE(e.ToggleHeaderRow().IsOk());
	CHECK(d.Get(TableGrid::Of(d, table).rows.front(), props::RepeatHeader));
	REQUIRE(e.BandRows(Color::WHITE(), Color::ALICE_BLUE()).IsOk());
	CHECK(d.Get(TableGrid::Of(d, table).Cell(2, 0), props::BackgroundColor) == Color::ALICE_BLUE()); // en-tête exclu
	REQUIRE(e.SetColumnWidth(table, 0, 90).IsOk());
	CHECK_NEAR(d.Get(TableGrid::Of(d, table).Cell(0, 0), props::Width), 90.0, 1e-9);
	REQUIRE(e.DistributeColumns().IsOk());
	REQUIRE(e.SetRowHeight(TableGrid::Of(d, table).rows[1], 40).IsOk());
	// Boutons « + » : ligne au rang 2, colonne au rang 0.
	REQUIRE(e.InsertRowAt(table, 2).IsOk());
	CHECK(e.Editing()); // curseur dans la nouvelle ligne
	REQUIRE(e.InsertColumnAt(table, 0).IsOk());
	CHECK_EQ(TableGrid::Of(d, table).rows.size(), size_t(5));
	CHECK_EQ(TableGrid::Of(d, table).columns, size_t(5));
	e.SelectTable(table);
	CHECK(e.CurrentTable().wholeRows && e.CurrentTable().wholeColumns);
	REQUIRE(e.DeleteRows().IsOk()); // tableau entier : supprimé
	CHECK(!d.Contains(table));
	// Grand tableau : création rapide.
	REQUIRE(e.InsertTable(1000, 20).IsOk());
	CHECK_EQ(e.CurrentTable().rows, size_t(1000));
	CHECK(e.InsertTable(10, 1001).IsError());
}

TEST(Display, TableToolsGeometryAndHits) {
	Editor e;
	REQUIRE(e.InsertTable(3, 4).IsOk());
	const NodeId table = e.CurrentTable().table;
	e.LayoutNow();
	const auto tables = TablesOn(e.Document(), e.Current()->layout, 0);
	REQUIRE(tables.size() == 1);
	const TableGeometry &g = tables.front();
	CHECK(g.table == table);
	CHECK_EQ(g.ColumnCount(), size_t(4));
	CHECK_EQ(g.RowCount(), size_t(3));
	CHECK_EQ(g.rowIndices[2], size_t(2));
	const double x1 = g.columns[1], top = g.rows.front(), y1 = g.rows[1], left = g.columns.front();
	CHECK_EQ(g.ColumnAt(x1 + 1), size_t(1));
	CHECK_EQ(g.RowAt(y1 + 1), size_t(1));
	CHECK_EQ(g.ColumnAt(left - 1), SIZE_MAX);
	// Bords intérieurs, « + » au-dessus d'un bord, sélection de colonne, de ligne, coin.
	TableHit h = HitTable(g, x1 + 1, y1 + 5, 3, 12);
	CHECK(h.kind == TableHitKind::ColumnBorder);
	CHECK_EQ(h.index, size_t(1));
	h = HitTable(g, x1 + 20, y1 + 1, 3, 12);
	CHECK(h.kind == TableHitKind::RowBorder);
	CHECK_EQ(h.index, size_t(0));
	h = HitTable(g, x1 + 2, top - 6, 3, 12);
	CHECK(h.kind == TableHitKind::InsertColumn);
	CHECK_EQ(h.index, size_t(1));
	h = HitTable(g, x1 + 30, top - 6, 3, 12);
	CHECK(h.kind == TableHitKind::SelectColumn);
	CHECK_EQ(h.index, size_t(1));
	h = HitTable(g, left - 6, y1 + 1, 3, 12);
	CHECK(h.kind == TableHitKind::InsertRow);
	CHECK_EQ(h.index, size_t(1));
	h = HitTable(g, left - 6, (y1 + g.rows[2]) / 2, 3, 12);
	CHECK(h.kind == TableHitKind::SelectRow);
	CHECK_EQ(h.index, size_t(1));
	CHECK(HitTable(g, left - 8, top - 8, 3, 12).kind == TableHitKind::SelectTable);
	CHECK(HitTable(g, x1 + 30, y1 + 8, 3, 12).kind == TableHitKind::None); // intérieur d'une cellule
	CHECK(HitTable(g, left - 40, top - 40, 3, 12).kind == TableHitKind::None);
}

TEST(Display, RulersAndNavigator) {
	// Règles : graduations, aimantation, affichage, repère proche.
	const auto marks = RulerMarks(2 * kPointsPerCm, 2.0);
	REQUIRE(marks.size() == 21); // 0 … 20 mm
	CHECK_EQ(marks[10].level, 2);
	CHECK_EQ(marks[10].centimeter, 1);
	CHECK_EQ(marks[5].level, 1);
	CHECK_EQ(RulerMarks(2 * kPointsPerCm, 0.2).size(), size_t(3)); // petit zoom : centimètres seuls
	CHECK_NEAR(SnapLength(kPointsPerCm * 2.537, false), kPointsPerCm * 2.55, 1e-9);
	CHECK_NEAR(SnapLength(kPointsPerCm * 2.537, true), kPointsPerCm * 2.54, 1e-9);
	CHECK_NEAR(SnapLength(-5, false), 0.0, 1e-12);
	CHECK(FormatLength(72) == "2,54 cm");
	const std::vector<RulerHandle> handles{{RulerHandle::MarginStart, 72, 0}, {RulerHandle::MarginEnd, 500, 0}};
	CHECK_EQ(HandleAt(handles, 10.f, 1.f, 84.f, 6.f), 0);
	CHECK_EQ(HandleAt(handles, 10.f, 1.f, 300.f, 6.f), -1);
	// Plan : titres, légendes sous leur titre, filtres, branches repliées.
	Editor e;
	const doc::Document &d = e.Document();
	REQUIRE(e.TypeText("Chapitre").IsOk());
	REQUIRE(e.ApplyStyle(ParagraphStyle::Heading1).IsOk());
	const NodeId chapter = d.AncestorOfType(e.Caret().text, NodeType::Paragraph);
	REQUIRE(e.InsertStyled(ParagraphStyle::Heading2, "Section").IsOk());
	REQUIRE(e.InsertTable(1, 1).IsOk());
	const NodeId values = e.CurrentTable().table;
	REQUIRE(e.InsertCaption(CaptionKind::Table, "Valeurs").IsOk());
	e.SelectNode(values); // la suite s'insère après le tableau
	REQUIRE(e.InsertStyled(ParagraphStyle::Heading1, "Autre").IsOk());
	const Outline &o = e.DocumentOutline();
	std::vector<NavigatorRow> rows = NavigatorRows(o, {}, {});
	REQUIRE(rows.size() == 4);
	CHECK_EQ(rows[1].depth, 1); // section
	CHECK_EQ(rows[2].depth, 2); // légende sous la section
	CHECK(rows[0].hasChildren);
	CHECK(!rows[3].hasChildren);
	NavigatorFilter onlyHeadings;
	onlyHeadings.tables = false;
	CHECK_EQ(NavigatorRows(o, onlyHeadings, {}).size(), size_t(3));
	rows = NavigatorRows(o, {}, {chapter}); // chapitre replié
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].collapsed);
	CHECK(rows[1].entry->text == "Autre");
}

TEST(Editor, ArrangeObjectsZIndex) {
	Editor e;
	const doc::Document &d = e.Document();
	std::vector<NodeId> shapes;
	for (int i = 0; i < 4; ++i) {
		REQUIRE(e.InsertShape(ShapeKind::Rectangle).IsOk());
		shapes.push_back(e.Selection().Primary());
	}
	e.LayoutNow();
	const auto z = [&](size_t i) { return d.Get(shapes[i], props::ZIndex); };
	// Ordre initial (z = 0 partout) : ordre du document. La première au premier plan.
	e.SelectNode(shapes[0]);
	REQUIRE(e.ArrangeSelection(Arrange::Front).IsOk());
	CHECK_EQ(z(0), int64_t(4));
	CHECK_EQ(z(1), int64_t(1));
	CHECK_EQ(z(3), int64_t(3));
	CHECK_EQ(e.History().Entries().size(), size_t(5)); // 4 insertions, un arrangement
	CHECK(e.ArrangeSelection(Arrange::Front).Error().code == ErrorCode::Nothing);
	// Reculer d'un plan : passe sous la quatrième forme.
	REQUIRE(e.ArrangeSelection(Arrange::Backward).IsOk());
	CHECK_EQ(z(0), int64_t(3));
	CHECK_EQ(z(3), int64_t(4));
	// Dernier plan puis avancer d'un plan.
	REQUIRE(e.ArrangeSelection(Arrange::Back).IsOk());
	CHECK_EQ(z(0), int64_t(1));
	CHECK(e.ArrangeSelection(Arrange::Back).Error().code == ErrorCode::Nothing);
	REQUIRE(e.ArrangeSelection(Arrange::Forward).IsOk());
	CHECK_EQ(z(0), int64_t(2));
	CHECK_EQ(z(1), int64_t(1));
	// Deux objets sélectionnés : ensemble au premier plan, ordre relatif conservé.
	e.SelectNode(shapes[1]);
	e.SelectNode(shapes[0], true);
	REQUIRE(e.ArrangeSelection(Arrange::Front).IsOk());
	CHECK_EQ(z(1), int64_t(3));
	CHECK_EQ(z(0), int64_t(4));
	REQUIRE(e.Undo().IsOk());
	CHECK_EQ(z(0), int64_t(2));
	// Pas un objet : refusé.
	REQUIRE(e.InsertParagraph("texte").IsOk());
	CHECK(e.ArrangeSelection(Arrange::Front).Error().code == ErrorCode::Nothing);
}

TEST(Display, PageMarginSides) {
	// Marges de la page par côté : nom, propriété, valeur, plus grande valeur possible.
	CHECK(MarginName(MarginSide::Top) == "Marge haute");
	CHECK(MarginName(MarginSide::Left) == "Marge gauche");
	CHECK(MarginName(MarginSide::Top) != PaddingName(PaddingSide::Top)); // marge ≠ rembourrage
	CHECK(MarginProperty(MarginSide::Right) == PropertyId::MarginRight);
	CHECK(MarginProperty(MarginSide::Bottom) == PropertyId::MarginBottom);
	PageGeometry g;
	g.width = 595;
	g.height = 842;
	g.margins = {72, 54, 36, 90}; // haut, droite, bas, gauche
	CHECK(MarginOf(g.margins, MarginSide::Top) == 72);
	CHECK(MarginOf(g.margins, MarginSide::Right) == 54);
	CHECK(MarginOf(g.margins, MarginSide::Bottom) == 36);
	CHECK(MarginOf(g.margins, MarginSide::Left) == 90);
	CHECK(MaxMargin(g, MarginSide::Left) == 595 - 54 - 36); // au moins 36 pt de contenu
	CHECK(MaxMargin(g, MarginSide::Right) == 595 - 90 - 36);
	CHECK(MaxMargin(g, MarginSide::Top) == 842 - 36 - 36);
	CHECK(MaxMargin(g, MarginSide::Bottom) == 842 - 72 - 36);
	g.margins.right = 580; // marge opposée déjà trop grande : rien de possible
	CHECK(MaxMargin(g, MarginSide::Left) == 0);
	// Une marge réglée au centième de centimètre, un seul côté.
	Editor e;
	const NodeId section = e.Document().Sections().front();
	REQUIRE(e.SetPage(MarginProperty(MarginSide::Left), 1.23 * kPointsPerCm).IsOk()); // comme le champ du ruban
	const PageGeometry after = ResolvePageGeometry(e.Document(), section, kNoNode);
	CHECK(std::abs(after.margins.left - 1.23 * kPointsPerCm) < 1e-9);
	CHECK(after.margins.right == ResolvePageGeometry(Editor().Document(), Editor().Document().Sections().front(), kNoNode).margins.right);
}

TEST(Display, PaddingHandles) {
	Editor e;
	REQUIRE(e.InsertStyled(ParagraphStyle::Note, "Note : encadré avec marges intérieures").IsOk());
	const NodeId note = e.Document().AncestorOfType(e.Caret().text, NodeType::Paragraph);
	e.LayoutNow();
	const PageLayout &page = e.Current()->layout.pages[0];
	Rect box;
	for (const LayoutBox &b : page.boxes)
		if (b.node == note)
			box = b.rect;
	REQUIRE(box.w > 0);
	// Encadré : bordure 1 pt (côtés haut et bas), marges intérieures 8 pt (gauche) et 6 pt (haut).
	auto h = PaddingAt(e.Document(), page, box.x + 1 + 8, box.y + box.h / 2, 2);
	REQUIRE(h.has_value());
	CHECK(h->node == note);
	CHECK(h->side == PaddingSide::Left);
	CHECK_NEAR(h->Value(), 8.0, 1e-9);
	CHECK(h->Property() == PropertyId::PaddingLeft);
	CHECK_NEAR(h->Edge(), box.x + 1 + 8, 1e-9);
	h = PaddingAt(e.Document(), page, box.x + box.w / 2, box.y + 1 + 6, 2);
	REQUIRE(h.has_value());
	CHECK(h->side == PaddingSide::Top);
	CHECK(!PaddingAt(e.Document(), page, box.x + box.w / 2, box.y + box.h / 2, 2).has_value()); // au milieu du texte
	// Glisser : aimanté au demi-millimètre, jamais négatif, jamais au-delà du contenu.
	h = PaddingAt(e.Document(), page, box.x + 9, box.y + box.h / 2, 2);
	REQUIRE(h.has_value());
	CHECK_NEAR(DraggedPadding(*h, box.x + 1 + kPointsPerCm, false), kPointsPerCm, 1e-9); // 1 cm
	CHECK_NEAR(DraggedPadding(*h, box.x - 50, false), 0.0, 1e-12);
	CHECK(DraggedPadding(*h, box.Right() + 100, false) <= box.w - 2 - 8 - 6 + 1e-9);
	CHECK(PaddingName(PaddingSide::Bottom) == "Rembourrage bas");
	CHECK(PaddingName(PaddingSide::Top) == "Rembourrage haut");
	// Réglage de plusieurs nœuds en une entrée d'historique.
	REQUIRE(e.InsertTable(1, 2).IsOk());
	const TableGrid g = TableGrid::Of(e.Document(), e.CurrentTable().table);
	const size_t before = e.History().Entries().size();
	REQUIRE(e.SetNodes(g.cells.front(), PropertyId::PaddingLeft, PropertyValue(12.0)).IsOk());
	CHECK_EQ(e.History().Entries().size(), before + 1);
	CHECK_NEAR(e.Document().Get(g.cells[0][1], props::PaddingLeft), 12.0, 1e-9);
}

TEST(Interface, LanguagesAndTranslations) {
	CHECK_EQ(Languages().size(), size_t(6));
	CHECK(PickLanguage(std::vector<std::string>{"de-DE", "en"}) == "de");
	CHECK(PickLanguage(std::vector<std::string>{"pt_BR", "zh_CN"}) == "zh");
	CHECK(PickLanguage(std::vector<std::string>{"pt"}) == "en"); // langue non proposée : anglais
	CHECK(PickLanguage(std::vector<std::string>{}) == "en");
	CHECK_EQ(LanguageIndex("ja"), size_t(5));
	CHECK(SamePlaceholders("Page {} / {}", "第 {} / {} 页"));
	CHECK(!SamePlaceholders("Page {} / {}", "Page {}"));
	CHECK(!SamePlaceholders("{}", "{ }x}"));
	// Chaque traduction : mêmes clés dans toutes les langues, accolades identiques à la source.
	std::set<std::string> reference;
	for (std::string_view code : {"en", "de", "it", "zh", "ja"}) {
		REQUIRE(LoadLanguage(code));
		CHECK(CurrentLanguage() == code);
		std::ifstream in(std::format("apps/scribe/i18n/{}.json", code));
		const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		REQUIRE(!json.empty());
		std::set<std::string> keys;
		for (size_t at = json.find("\"strings\""); (at = json.find("\n\t\t\"", at)) != std::string::npos; ++at) {
			std::string key; // clé JSON (échappements \" et \\ seulement)
			for (size_t i = at + 4; i < json.size() && json[i] != '"'; ++i)
				key += json[i] == '\\' ? json[++i] : json[i];
			keys.insert(key);
			CHECK(SamePlaceholders(key, Tr(key)));
		}
		CHECK(keys.size() > 300);
		if (reference.empty())
			reference = keys;
		CHECK(keys == reference);
	}
	REQUIRE(LoadLanguage("en"));
	CHECK(Tr("Gras (Ctrl+B)") == "Bold (Ctrl+B)");
	CHECK(TrF("Insérer {} ligne(s) au-dessus", 3) == "Insert 3 row(s) above");
	CHECK(TrArg("Marge gauche : {}", "2,54 cm") == "Left margin: 2,54 cm");
	CHECK(Tr("texte sans traduction") == "texte sans traduction"); // repli : le français
	Editor e;
	CHECK(e.Title() == "Untitled");
	CHECK(!LoadLanguage("xx"));
	CHECK(CurrentLanguage() == "en"); // langue précédente gardée
	REQUIRE(LoadLanguage("fr")); // langue des sources pour les autres tests
	CHECK(Tr("Gras (Ctrl+B)") == "Gras (Ctrl+B)");
	CHECK(e.Title() == "Sans titre");
	const TrList list(std::vector<std::string_view>{"Gauche", "Droite"});
	CHECK(list.views.size() == 2 && list.views[1] == "Droite");
}

TEST(Interface, SettingsThemesAndRulerSpacing) {
	const auto file = Temp("settings.ini");
	Settings s;
	s.language = "it";
	s.theme = "light";
	for (int i = 0; i < 20; ++i)
		s.AddRecent(std::format("/tmp/doc{}.docdb", i));
	s.AddRecent("/tmp/doc5.docdb"); // remonte en tête, sans doublon
	CHECK_EQ(s.recent.size(), Settings::kMaxRecent);
	CHECK(s.recent.front() == "/tmp/doc5.docdb");
	s.grid = true;
	s.gridStep = 20.5;
	REQUIRE(s.Save(file));
	const Settings loaded = Settings::Load(file);
	CHECK(loaded.grid);
	CHECK_NEAR(loaded.gridStep, 20.5, 1e-9);
	CHECK(!Settings().grid);
	CHECK(loaded.language == "it");
	CHECK(loaded.theme == "light");
	CHECK(loaded.recent == s.recent);
	std::filesystem::remove(file);
	CHECK(Settings::Load(Temp("absent.ini")).theme == "night");
	// Thèmes : palette et feuille de style.
	CHECK_EQ(Themes().size(), size_t(3));
	CHECK_EQ(ThemeIndex("contrast"), size_t(2));
	CHECK_EQ(ThemeIndex("inconnu"), size_t(0));
	const Palette light = Themes()[1].make();
	CHECK(light.ui.background.r > 200); // fond clair
	CHECK(StyleSheetFor(light).find(".status") != std::string::npos);
	SetPalette(light);
	CHECK(P().workspace == light.workspace);
	SetPalette(Themes()[0].make());
	// Règle : espaces avant / après aimantés au demi-millimètre, jamais négatifs.
	CHECK_NEAR(DraggedSpaceBefore(100, 6, 100 + kPointsPerCm, false), SnapLength(6 + kPointsPerCm, false), 1e-9);
	CHECK_NEAR(DraggedSpaceBefore(100, 6, 50, false), 0.0, 1e-12);
	CHECK_NEAR(DraggedSpaceAfter(200, 200 + kPointsPerCm / 2, false), kPointsPerCm / 2, 1e-9);
	CHECK_NEAR(DraggedSpaceAfter(200, 150, true), 0.0, 1e-12);
}

TEST(Editor, ReturnLineBreakParagraphAndPage) {
	Editor e;
	REQUIRE(e.InsertParagraph("Titre").IsOk());
	REQUIRE(e.ApplyStyle(ParagraphStyle::Heading1).IsOk());
	const NodeId body = e.Document().Body(e.Document().Sections().front());
	const auto paragraphs = [&] { return e.Document().FindAll(NodeType::Paragraph).size(); };
	const size_t before = paragraphs();
	const NodeId heading = e.Document().AncestorOfType(e.Caret().text, NodeType::Paragraph);
	// Entrée : saut de ligne dans le même paragraphe.
	REQUIRE(e.LineBreak().IsOk());
	REQUIRE(e.TypeText("suite").IsOk());
	CHECK(paragraphs() == before);
	CHECK(e.Document().AncestorOfType(e.Caret().text, NodeType::Paragraph) == heading);
	e.SelectStory();
	CHECK(e.Copy().plain.find("Titre\nsuite") != std::string::npos);
	// Maj+Entrée : nouveau paragraphe du même style en dessous (la suite du texte y passe).
	e.MoveCaret(Motion::StoryEnd, false);
	REQUIRE(e.Enter().IsOk());
	CHECK(paragraphs() == before + 1);
	const NodeId next = e.Document().AncestorOfType(e.Caret().text, NodeType::Paragraph);
	CHECK(next != heading);
	CHECK(e.Document().Get(next, props::OutlineLevel) == e.Document().Get(heading, props::OutlineLevel)); // même type
	CHECK(e.Document().Find(body)->IndexOfChild(next) == e.Document().Find(body)->IndexOfChild(heading) + 1);
	// Ctrl+Entrée : saut de page puis paragraphe du même style sur la nouvelle page.
	REQUIRE(e.TypeText("avant la coupure").IsOk());
	e.MoveCaret(Motion::WordLeft, false); // curseur avant « coupure » : ce mot passe page suivante
	REQUIRE(e.PageBreakBelow().IsOk());
	const NodeId after = e.Document().AncestorOfType(e.Caret().text, NodeType::Paragraph);
	const Node *b = e.Document().Find(body);
	const size_t i = b->IndexOfChild(after);
	REQUIRE(i > 0);
	CHECK(e.Document().Find(b->Children()[i - 1])->Type() == NodeType::PageBreak);
	CHECK(b->Children()[i - 2] == next);
	CHECK(e.Document().Get(after, props::OutlineLevel) == e.Document().Get(next, props::OutlineLevel));
	e.LayoutNow();
	size_t pageOfAfter = 0, pageOfNext = 0;
	REQUIRE(e.BoxOf(after, &pageOfAfter) != nullptr);
	REQUIRE(e.BoxOf(next, &pageOfNext) != nullptr);
	CHECK(pageOfAfter == pageOfNext + 1);
	e.SelectStory();
	CHECK(e.Copy().plain.find("coupure") != std::string::npos);
	REQUIRE(e.Undo().IsOk()); // une seule entrée d'historique
	CHECK(e.Document().FindAll(NodeType::PageBreak).empty());
	// Hors du corps (cellule de tableau) : refusé, rien ne change.
	REQUIRE(e.InsertTable(1, 1).IsOk());
	const NodeId cell = e.Document().FindAll(NodeType::TableCell).front();
	e.GoTo(e.Document().Find(e.Document().Find(cell)->Children().front())->Children().front());
	REQUIRE(e.Editing());
	const uint64_t revision = e.Document().Revision();
	CHECK(e.PageBreakBelow().IsError());
	CHECK(e.Document().Revision() == revision);
	e.LeaveText();
	CHECK(e.LineBreak().IsError()); // sans curseur
}

TEST(Editor, DrawingsTextBoxesAndShapeAlignment) {
	Editor e;
	REQUIRE(e.InsertParagraph("avant").IsOk());
	// Zone de dessin ; formes et zone de texte placées librement dedans.
	REQUIRE(e.InsertDrawing().IsOk());
	const NodeId drawing = e.Selection().Primary();
	REQUIRE(e.Document().Find(drawing)->Type() == NodeType::Drawing);
	CHECK(e.DrawingOfSelection() == drawing);
	REQUIRE(e.InsertShape(ShapeKind::Star5).IsOk());
	const NodeId star = e.Selection().Primary();
	CHECK(e.Document().Find(star)->Parent() == drawing);
	CHECK(e.DrawingOfSelection() == drawing); // élément sélectionné : sa zone
	CHECK(e.Document().Get(star, props::HAlign) == HorizontalAlign::None);
	CHECK_NEAR(e.Document().Get(star, props::PositionX), 20.0, 1e-9);
	CHECK_NEAR(e.Document().Get(star, props::Width), e.Document().Get(star, props::Height), 1e-9); // étoile : carrée
	REQUIRE(e.InsertShape(ShapeKind::Rectangle).IsOk());
	CHECK_NEAR(e.Document().Get(e.Selection().Primary(), props::PositionX), 36.0, 1e-9); // décalée de la précédente
	REQUIRE(e.InsertTextBox().IsOk());
	CHECK(e.Editing());
	REQUIRE(e.TypeText("légende libre").IsOk());
	const NodeId box = e.Document().AncestorOfType(e.Caret().text, NodeType::Shape);
	REQUIRE(box != kNoNode);
	CHECK(e.Document().Find(box)->Parent() == drawing);
	CHECK(e.Document().Get(box, props::BorderWidth) == 0.0);
	e.LayoutNow();
	REQUIRE(e.BoxOf(drawing) != nullptr);
	const Rect area = e.BoxOf(drawing)->rect; // copie : LayoutNow remplace la mise en page
	CHECK_NEAR(area.h, 200.0, 1e-9);
	// Texte d'une forme : centré, curseur dedans ; une seconde fois : même paragraphe.
	e.SelectNode(star);
	REQUIRE(e.EditShapeText(star).IsOk());
	REQUIRE(e.TypeText("★").IsOk());
	CHECK(e.Document().Get(star, props::VAlign) == VerticalAlign::Middle);
	const NodeId para = e.Document().Find(star)->Children().front();
	CHECK(e.Document().Get(para, props::HAlign) == HorizontalAlign::Center);
	REQUIRE(e.EditShapeText(star).IsOk());
	CHECK(e.Document().Find(star)->Children().size() == 1);
	CHECK(e.EditShapeText(drawing).IsError());
	// Alignement dans la zone : centrée dans sa largeur intérieure.
	e.LayoutNow();
	REQUIRE(e.AlignShapes({star}, HorizontalAlign::Center).IsOk());
	e.LayoutNow();
	const LayoutBox *s = e.BoxOf(star), *d = e.BoxOf(drawing);
	REQUIRE(s && d);
	CHECK_NEAR(s->rect.x + s->rect.w / 2, d->rect.x + d->rect.w / 2, 1e-6);
	CHECK(e.Document().Get(star, props::HAlign) == HorizontalAlign::Center);
	// Déplacement manuel : alignement « aucun ».
	const double before = s->rect.x;
	REQUIRE(e.MoveShape(star, 10, 5).IsOk());
	e.LayoutNow();
	CHECK(e.Document().Get(star, props::HAlign) == HorizontalAlign::None);
	CHECK_NEAR(e.BoxOf(star)->rect.x, before + 10, 1e-6);
	CHECK(e.MoveShape(drawing, 10, 0).IsError()); // zone de dessin : bloc du flux
	REQUIRE(e.ResizeShape(drawing, Rect(area.x, area.y, 300, 120)).IsOk());
	CHECK_NEAR(e.Document().Get(drawing, props::Height), 120.0, 1e-9);
	// Forme du flux : centrée par le moteur ; déplacée : « aucun » à la même place (+ déplacement).
	e.SelectNode(drawing);
	REQUIRE(e.InsertParagraph("après").IsOk());
	REQUIRE(e.InsertShape(ShapeKind::Hexagon).IsOk());
	const NodeId hex = e.Selection().Primary();
	CHECK(e.Document().Find(hex)->Parent() == e.Document().Body(e.Document().Sections().front()));
	e.LayoutNow();
	REQUIRE(e.MoveShape(hex, 7, 0).IsOk()); // position non nulle avant l'alignement
	e.LayoutNow();
	REQUIRE(e.AlignShapes({hex}, HorizontalAlign::Center).IsOk());
	CHECK_NEAR(e.Document().Get(hex, props::PositionX), 0.0, 1e-9); // alignement exact
	e.LayoutNow();
	const Rect content = e.Current()->layout.pages[0].content; // copie : LayoutNow remplace la mise en page
	const LayoutBox *h = e.BoxOf(hex);
	REQUIRE(h != nullptr);
	CHECK_NEAR(h->rect.x, content.x + (content.w - h->rect.w) / 2, 1e-6);
	const double centered = h->rect.x;
	REQUIRE(e.MoveShape(hex, -15, 0).IsOk());
	e.LayoutNow();
	CHECK(e.Document().Get(hex, props::HAlign) == HorizontalAlign::None);
	CHECK_NEAR(e.BoxOf(hex)->rect.x, centered - 15, 1e-6);
	// Redimensionnement depuis le bord gauche d'une forme alignée à droite : rectangle demandé.
	REQUIRE(e.AlignShapes({hex}, HorizontalAlign::Right).IsOk());
	e.LayoutNow();
	const Rect right = e.BoxOf(hex)->rect;
	CHECK_NEAR(right.Right(), content.Right(), 1e-6);
	REQUIRE(e.ResizeShape(hex, Rect(right.x - 20, right.y, right.w + 20, right.h)).IsOk());
	e.LayoutNow();
	CHECK_NEAR(e.BoxOf(hex)->rect.x, right.x - 20, 1e-6);
	CHECK_NEAR(e.BoxOf(hex)->rect.Right(), right.Right(), 1e-6);
	// Position absolue : alignée dans la zone de contenu de la page.
	REQUIRE(e.Document().Set(hex, props::Position, Positioning::Absolute).IsOk());
	e.LayoutNow();
	REQUIRE(e.AlignShapes({hex}, HorizontalAlign::Right).IsOk());
	e.LayoutNow();
	CHECK_NEAR(e.BoxOf(hex)->rect.Right(), e.Current()->layout.pages[0].content.Right(), 1e-6);
	// Zone de dessin déplacée comme un bloc (poignée de la marge) : avant le premier paragraphe,
	// avec ses éléments ; sélectionnée ensuite. Un bloc ne peut pas entrer dans la zone.
	const NodeId bodyId = e.Document().Body(e.Document().Sections().front());
	const size_t items = e.Document().Find(drawing)->Children().size();
	REQUIRE(e.MoveBlock(drawing, bodyId, 0).IsOk());
	CHECK(e.Document().Find(bodyId)->Children().front() == drawing);
	CHECK(e.Document().Find(drawing)->Children().size() == items);
	CHECK(e.Selection().Primary() == drawing);
	e.LayoutNow();
	const NodeId following = e.Document().Find(bodyId)->Children()[1]; // « avant », désormais sous la zone
	REQUIRE(e.Document().Find(following)->Type() == NodeType::Paragraph);
	CHECK(e.BoxOf(drawing)->rect.Bottom() <= e.BoxOf(following)->rect.y + 1e-6);
	CHECK(e.MoveBlock(following, drawing, 0).IsError());
	REQUIRE(e.Undo().IsOk()); // le déplacement s'annule
	CHECK(e.Document().Find(bodyId)->Children().front() != drawing);
	CHECK(e.History().CanUndo());
}

TEST(Interface, Drafts) {
	// Brouillon : enregistré, relu, repris (modifié, rattaché au document d'origine), supprimé.
	const auto dir = Temp("drafts");
	std::filesystem::remove_all(dir);
	DraftStore store(dir);
	CHECK(!store.Load().has_value());
	Editor e;
	const uint64_t firstId = e.DocumentId();
	REQUIRE(e.TypeText("Texte non enregistré").IsOk());
	CHECK(e.Dirty());
	e.SetName("Lettre administrative");
	CHECK(e.Name() == "Lettre administrative");
	CHECK(e.Title() == "Lettre administrative •");
	REQUIRE(store.Save(e.Document(), {"Lettre\nadministrative", {}}).IsOk());
	CHECK(!std::filesystem::exists(store.DocumentFile().string() + ".tmp")); // fichiers temporaires renommés
	CHECK(!std::filesystem::exists(store.InfoFile().string() + ".tmp"));
	auto info = store.Load();
	REQUIRE(info.has_value());
	CHECK(info->name == "Lettre administrative"); // fin de ligne remplacée : format clé=valeur intact
	CHECK(info->source.empty());
	// Reprise sans document d'origine : « Enregistrer » demandera où.
	Editor resumed;
	REQUIRE(resumed.OpenDraft(store.DocumentFile(), info->source, info->name).IsOk());
	CHECK(resumed.Dirty());
	CHECK(resumed.Path().empty());
	CHECK(resumed.Name() == "Lettre administrative");
	CHECK(resumed.Editing());
	resumed.SelectStory();
	CHECK(resumed.Copy().plain == "Texte non enregistré");
	REQUIRE(resumed.TypeText("!").IsOk());
	REQUIRE(resumed.Undo().IsOk());
	CHECK(resumed.Dirty()); // annuler ne revient pas à un état enregistré
	// Reprise d'un document déjà enregistré : rattaché à son fichier.
	const auto original = dir / "lettre.docdb";
	REQUIRE(store.Save(e.Document(), {"lettre.docdb", original}).IsOk());
	info = store.Load();
	REQUIRE(info.has_value());
	CHECK(info->source == original);
	REQUIRE(resumed.OpenDraft(store.DocumentFile(), info->source, info->name).IsOk());
	CHECK(resumed.Path() == original);
	CHECK(resumed.Name() == "lettre.docdb");
	REQUIRE(resumed.Save(original).IsOk());
	CHECK(!resumed.Dirty());
	// Un nouvel enregistrement remplace le brouillon ; suppression.
	Editor other;
	REQUIRE(other.TypeText("Autre document").IsOk());
	REQUIRE(store.Save(other.Document(), {"Autre", {}}).IsOk());
	REQUIRE(resumed.OpenDraft(store.DocumentFile(), {}, "Autre").IsOk());
	resumed.SelectStory();
	CHECK(resumed.Copy().plain == "Autre document");
	store.Remove();
	CHECK(!store.Load().has_value());
	CHECK(resumed.OpenDraft(store.DocumentFile(), {}, "x").IsError());
	store.Remove(); // sans brouillon : sans effet
	// Numéro du document et nom : renouvelés à chaque remplacement.
	e.New();
	CHECK(e.DocumentId() > firstId);
	CHECK(e.Name() == Tr("Sans titre"));
	// Enregistrement de fond terminé après le remplacement du document : il ne s'applique pas
	// au nouveau document (ni chemin, ni état « enregistré »).
	REQUIRE(e.TypeText("Avant").IsOk());
	e.SaveAsync(dir / "avant.docdb");
	e.New();
	REQUIRE(e.TypeText("Après").IsOk());
	Settle(e);
	CHECK(e.Path().empty());
	CHECK(e.Dirty());
	CHECK(std::filesystem::exists(dir / "avant.docdb"));
	std::filesystem::remove_all(dir);
}

TEST(Interface, Templates) {
	// Modèles intégrés : tous construisent un document (le vierge en premier).
	const auto builtins = BuiltinTemplates();
	REQUIRE(builtins.size() >= 10);
	CHECK(builtins.front().key == "blank");
	for (const BuiltinTemplate &t : builtins) {
		Editor e;
		t.build(e);
		CHECK(!e.History().CanUndo()); // historique vierge
		CHECK(e.Document().FindAll(NodeType::Paragraph).size() >= 1);
		e.LayoutNow();
		CHECK(e.Current() && !e.Current()->layout.pages.empty());
	}
	// Identifiants (« --template= ») : la démonstration est un modèle intégré.
	const BuiltinTemplate *demo = FindBuiltinTemplate("demo");
	REQUIRE(demo != nullptr);
	CHECK(demo->name == "Démo");
	CHECK(FindBuiltinTemplate("letter") != nullptr);
	CHECK(FindBuiltinTemplate("inconnu") == nullptr);
	CHECK(FindBuiltinTemplate("") == nullptr);
	CHECK(BuiltinTemplateKeys().starts_with("blank, letter"));
	CHECK(BuiltinTemplateKeys().find("demo") != std::string::npos);
	std::set<std::string_view> keys;
	for (const BuiltinTemplate &t : builtins)
		CHECK(keys.insert(t.key).second); // identifiants uniques
	Editor fromDemo, reference;
	demo->build(fromDemo);
	BuildDemo(reference);
	CHECK(fromDemo.Document().FindAll(NodeType::Paragraph).size() == reference.Document().FindAll(NodeType::Paragraph).size());
	CHECK(fromDemo.Document().FindAll(NodeType::Table).size() == reference.Document().FindAll(NodeType::Table).size());
	// Modèles personnels : créer, lister, renommer, supprimer ; nouveau document depuis un modèle.
	CHECK(TemplateStore::ValidName("Mon modèle"));
	CHECK(!TemplateStore::ValidName(""));
	CHECK(!TemplateStore::ValidName("a/b"));
	CHECK(!TemplateStore::ValidName(" espace"));
	CHECK(!TemplateStore::ValidName(".cache"));
	const auto dir = Temp("templates");
	std::filesystem::remove_all(dir);
	TemplateStore store(dir);
	CHECK(store.List().empty());
	Editor source;
	REQUIRE(source.TypeText("Contenu du modèle").IsOk());
	REQUIRE(store.Create("Compte rendu maison", source.Document()).IsOk());
	CHECK(store.Create("Compte rendu maison", source.Document()).Error().code == ErrorCode::InvalidValue); // déjà pris
	CHECK(store.Create("a:b", source.Document()).IsError());
	REQUIRE(store.Create("Autre", source.Document()).IsOk());
	CHECK(store.List() == (std::vector<std::string>{"Autre", "Compte rendu maison"}));
	REQUIRE(store.Rename("Autre", "Zeta").IsOk());
	CHECK(store.Contains("Zeta"));
	CHECK(!store.Contains("Autre"));
	CHECK(store.Rename("Zeta", "Compte rendu maison").IsError()); // nom pris
	CHECK(store.Rename("absent", "x").Error().code == ErrorCode::NotFound);
	Editor fresh;
	REQUIRE(fresh.NewFromFile(store.PathOf("Compte rendu maison")).IsOk());
	CHECK(fresh.Path().empty()); // « Enregistrer » demandera où
	CHECK(!fresh.Dirty());
	CHECK(fresh.Editing());
	CHECK(fresh.Copy().Empty());
	fresh.SelectStory();
	CHECK(fresh.Copy().plain == "Contenu du modèle");
	REQUIRE(store.Remove("Zeta").IsOk());
	CHECK(store.List().size() == 1);
	CHECK(store.Remove("Zeta").Error().code == ErrorCode::NotFound);
	CHECK(fresh.NewFromFile(dir / "absent.docdb").IsError());
	std::filesystem::remove_all(dir);
}
