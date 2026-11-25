// Tests d'édition d'engine.document : sélection (règles, modes, groupe, type, zone, inversion,
// élagage, contexte) et commandes (création, suppression, déplacement, propriétés avec fusion
// des frappes, groupes, duplication), transactions, historique (annuler, rétablir, sauts,
// limite, état enregistré, notifications).
#include <cpp-maker/test.hpp>

#include <vector>

#include <document/commands.hpp>
#include <document/selection.hpp>

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
	std::vector<NodeId> Children(NodeId id) const {
		const auto c = d.Find(id)->Children();
		return {c.begin(), c.end()};
	}
};

std::vector<NodeId> Ids(std::span<const NodeId> s) { return {s.begin(), s.end()}; }

} // namespace

// ── Sélection ──

TEST(Selection, RulesAndModes) {
	Fixture f;
	const NodeId a = f.Para("a"), b = f.Para("b"), c = f.Para("c");
	CHECK(!Selection::CanSelect(f.d, f.d.Root()));
	CHECK(!Selection::CanSelect(f.d, f.body));
	CHECK(!Selection::CanSelect(f.d, 9999));
	REQUIRE(f.d.Set(b, props::Selectable, false).IsOk());
	REQUIRE(f.d.Set(c, props::Visible, false).IsOk());
	CHECK(!Selection::CanSelect(f.d, b));
	CHECK(!Selection::CanSelect(f.d, c));
	CHECK(Selection::CanSelect(f.d, c, true)); // depuis l'arborescence
	CHECK(!Selection::CanSelect(f.d, f.d.Find(c)->Children()[0])); // ancêtre masqué

	Selection s;
	CHECK_EQ(s.Select(f.d, a), size_t(1));
	const uint64_t rev = s.Revision();
	CHECK_EQ(s.Select(f.d, a), size_t(0)); // inchangé
	CHECK_EQ(s.Revision(), rev);
	const NodeId t = f.d.Find(a)->Children()[0];
	s.Select(f.d, t, SelectMode::Add);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{a, t}));
	CHECK(s.Primary() == t);
	s.Select(f.d, a, SelectMode::Toggle);
	CHECK(Ids(s.Nodes()) == std::vector<NodeId>{t});
	s.Select(f.d, t, SelectMode::Remove);
	CHECK(s.Empty());
	s.Select(f.d, a);
	s.Select(f.d, b); // non sélectionnable : remplacement par une sélection vide
	CHECK(s.Empty());
	const std::vector<NodeId> ids{a, b, c, a};
	s.SelectIds(f.d, ids, SelectMode::Replace, true);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{a, c}));
}

TEST(Selection, GroupsTypesAreasAndInversion) {
	Fixture f;
	const NodeId a = f.Para("a"), b = f.Para("b"), c = f.Para("c"), d = f.Para("d");
	const NodeId g = f.d.CreateGroup("g").Value();
	const NodeId sub = f.d.CreateGroup("sous", 0, g).Value();
	REQUIRE(f.d.AddToGroup(a, g).IsOk());
	REQUIRE(f.d.AddToGroup(c, sub).IsOk());
	Selection s;
	s.SelectGroup(f.d, g);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{a, c}));
	// SelectViaGroup : cliquer un membre sélectionne tout son groupe, le membre cliqué en principal.
	REQUIRE(f.d.AddToGroup(b, g).IsOk());
	REQUIRE(f.d.Set(b, props::SelectViaGroup, true).IsOk());
	s.Select(f.d, b);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{a, b}));
	CHECK(s.Primary() == b);
	s.SelectType(f.d, NodeType::Paragraph);
	CHECK_EQ(s.Size(), size_t(4));
	s.SelectType(f.d, NodeType::Text, c);
	CHECK_EQ(s.Size(), size_t(1));
	// Zone : blocs entièrement contenus.
	const LayoutResult layout = Layout(f.d);
	const auto &boxes = layout.pages[0].boxes;
	Rect area = boxes[1].rect; // b
	area.w += 1;
	area.h = boxes[2].rect.Bottom() - area.y + 1; // b et c
	s.SelectArea(f.d, layout, 0, area);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{b, c}));
	// Inversion parmi les frères du nœud principal.
	s.Invert(f.d);
	CHECK(Ids(s.Nodes()) == (std::vector<NodeId>{a, d}));
	s.Clear();
	s.Invert(f.d);
	CHECK_EQ(s.Size(), size_t(4));
	// Élagage après suppression.
	REQUIRE(f.d.DeleteNode(d).IsOk());
	CHECK_EQ(s.Prune(f.d), size_t(1));
	CHECK(!s.Contains(d));
}

TEST(Selection, ContextForTheInterface) {
	Fixture f;
	const NodeId p = f.Para("texte");
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId inCell = f.Para("cellule", cell);
	const NodeId image = f.d.CreateNode(NodeType::Image, f.body).Value();
	const NodeId header = f.d.CreateNode(NodeType::Header, f.section).Value();
	const NodeId inHeader = f.Para("en-tête", header);
	Selection s;
	CHECK(s.Context(f.d).Empty());
	s.Select(f.d, p);
	SelectionContext c = s.Context(f.d);
	CHECK(c.text && !c.table && !c.mixed);
	CHECK(c.primaryType == NodeType::Paragraph);
	s.Select(f.d, f.d.Find(inCell)->Children()[0]);
	c = s.Context(f.d);
	CHECK(c.text && c.table);
	CHECK(c.enclosingTable == table);
	s.Select(f.d, image);
	s.Select(f.d, p, SelectMode::Add);
	c = s.Context(f.d);
	CHECK(c.image && c.text && c.mixed);
	CHECK_EQ(c.count, size_t(2));
	s.Select(f.d, inHeader);
	CHECK(s.Context(f.d).headerFooter);
	REQUIRE(f.d.Set(f.body, props::Locked, true).IsOk());
	s.Select(f.d, p);
	CHECK(s.Context(f.d).locked); // verrou hérité
}

// ── Commandes et historique ──

TEST(History, CreateUndoRedoKeepsIdsValid) {
	Fixture f;
	History h(f.d);
	auto create = CreateNode(NodeType::Paragraph, f.body);
	Command *raw = create.get();
	REQUIRE(h.Execute(std::move(create)).IsOk());
	const NodeId p = CreatedId(*raw);
	REQUIRE(p != kNoNode);
	REQUIRE(h.Execute(SetProperty(p, props::SpaceAfter, 20.0)).IsOk());
	REQUIRE(h.Execute(CreateNode(NodeType::Text, p)).IsOk());
	CHECK_EQ(h.Position(), size_t(3));
	REQUIRE(h.Undo().IsOk());
	REQUIRE(h.Undo().IsOk());
	REQUIRE(h.Undo().IsOk());
	CHECK(!f.d.Contains(p));
	CHECK(!h.CanUndo());
	REQUIRE(h.JumpTo(3).IsOk()); // rétablit tout : mêmes identifiants
	CHECK(f.d.Contains(p));
	CHECK_NEAR(f.d.Get(p, props::SpaceAfter), 20.0, 1e-9);
	CHECK_EQ(f.d.Find(p)->Children().size(), size_t(1));
	CHECK(h.Undo().IsOk());
	CHECK(h.UndoName() == "Modifier SpaceAfter");
	CHECK(h.RedoName() == "Insérer Text");
}

TEST(History, DeleteMoveAndExactRestoration) {
	Fixture f;
	const NodeId a = f.Para("a"), b = f.Para("b"), c = f.Para("c");
	const NodeId g = f.d.CreateGroup("g").Value();
	const NodeId other = f.d.CreateGroup("autre").Value();
	REQUIRE(f.d.AddToGroup(a, other).IsOk());
	REQUIRE(f.d.AddToGroup(b, g).IsOk());
	REQUIRE(f.d.AddToGroup(c, g).IsOk());
	History h(f.d);
	REQUIRE(h.Execute(DeleteNode(b)).IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{a, c}));
	REQUIRE(h.Undo().IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{a, b, c}));
	CHECK(Ids(f.d.Find(g)->Members()) == (std::vector<NodeId>{b, c}));
	// Déplacements dans le même parent (vers l'avant, vers l'arrière) et vers un autre parent.
	REQUIRE(h.Execute(MoveNode(a, f.body, 3)).IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{b, c, a}));
	REQUIRE(h.Undo().IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{a, b, c}));
	REQUIRE(h.Execute(MoveNode(c, f.body, 0)).IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{c, a, b}));
	REQUIRE(h.Undo().IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{a, b, c}));
	const NodeId header = f.d.CreateNode(NodeType::Header, f.section).Value();
	REQUIRE(h.Execute(MoveNode(b, header)).IsOk());
	REQUIRE(h.Undo().IsOk());
	CHECK(f.Children(f.body) == (std::vector<NodeId>{a, b, c}));
	// Retrait d'un groupe annulé : rang restauré (la récence départage la cascade).
	REQUIRE(f.d.AddToGroup(c, other).IsOk());
	REQUIRE(f.d.Set(g, props::Italic, true).IsOk());
	REQUIRE(f.d.Set(other, props::Italic, false).IsOk());
	CHECK(!f.d.Get(c, props::Italic)); // « autre » ajouté en dernier
	REQUIRE(h.Execute(RemoveFromGroup(c, g)).IsOk());
	REQUIRE(h.Undo().IsOk());
	CHECK(Ids(f.d.Find(g)->Members()) == (std::vector<NodeId>{b, c}));
	CHECK(!f.d.Get(c, props::Italic));
	CHECK(f.d.Find(c)->Groups()[0].group == g);
}

TEST(History, PropertiesAndTypingMerge) {
	Fixture f;
	const NodeId p = f.Para("");
	const NodeId t = f.d.Find(p)->Children()[0];
	History h(f.d);
	REQUIRE(h.Execute(SetProperty(t, props::Text, "B")).IsOk());
	REQUIRE(h.Execute(SetProperty(t, props::Text, "Bo")).IsOk());
	REQUIRE(h.Execute(SetProperty(t, props::Text, "Bon")).IsOk());
	CHECK_EQ(h.Entries().size(), size_t(1)); // frappe continue fusionnée
	h.BreakMerge();
	REQUIRE(h.Execute(SetProperty(t, props::Text, "Bonjour")).IsOk());
	CHECK_EQ(h.Entries().size(), size_t(2));
	REQUIRE(h.Undo().IsOk());
	CHECK(f.d.Get(t, props::Text) == std::string("Bon"));
	REQUIRE(h.Undo().IsOk());
	CHECK(f.d.Get(t, props::Text) == std::string(""));
	// Valeur explicite précédente restaurée, ou héritage rétabli.
	REQUIRE(h.Execute(SetProperty(p, props::FontSize, 14.0)).IsOk());
	REQUIRE(h.Execute(SetProperty(p, props::FontSize, 18.0)).IsOk());
	REQUIRE(h.Execute(ClearProperty(p, PropertyId::FontSize)).IsOk());
	CHECK_NEAR(f.d.Get(p, props::FontSize), 11.0, 1e-9);
	REQUIRE(h.Undo().IsOk());
	CHECK_NEAR(f.d.Get(p, props::FontSize), 18.0, 1e-9);
	REQUIRE(h.Undo().IsOk());
	CHECK_NEAR(f.d.Get(p, props::FontSize), 14.0, 1e-9);
	REQUIRE(h.Undo().IsOk());
	CHECK(f.d.Explicit(p, PropertyId::FontSize) == nullptr);
	// Commande refusée (verrou) : rien d'inscrit.
	REQUIRE(f.d.Set(p, props::Locked, true).IsOk());
	const size_t entries = h.Entries().size();
	CHECK(h.Execute(SetProperty(t, props::Bold, true)).Error().code == ErrorCode::Locked);
	CHECK_EQ(h.Entries().size(), entries);
}

TEST(History, GroupUngroupAndDuplicate) {
	Fixture f;
	const NodeId a = f.Para("a"), b = f.Para("b");
	History h(f.d);
	auto group = GroupNodes("Titres", {a, b}, 5);
	Command *raw = group.get();
	REQUIRE(h.Execute(std::move(group)).IsOk());
	const NodeId g = CreatedId(*raw);
	REQUIRE(f.d.Contains(g));
	CHECK(Ids(f.d.Find(g)->Members()) == (std::vector<NodeId>{a, b}));
	CHECK_EQ(f.d.Get(g, props::Priority), int64_t(5));
	REQUIRE(h.Undo().IsOk());
	CHECK(!f.d.Contains(g));
	CHECK(f.d.Find(a)->Groups().empty());
	REQUIRE(h.Redo().IsOk());
	CHECK(Ids(f.d.Find(g)->Members()) == (std::vector<NodeId>{a, b}));
	REQUIRE(h.Execute(Ungroup(g)).IsOk());
	CHECK(f.d.Find(b)->Groups().empty());
	REQUIRE(h.Undo().IsOk());
	CHECK(f.d.Find(b)->InGroup(g));
	CHECK(h.Execute(GroupNodes("refusé", {a, f.d.Root()})).IsError()); // échec : rien ne reste
	CHECK_EQ(f.d.AllGroups().size(), size_t(2)); // groupe de pages + « Titres »
	// Duplication : nouveaux identifiants, propriétés et appartenances copiées, juste après.
	REQUIRE(f.d.Set(a, props::SpaceAfter, 9.0).IsOk());
	auto dup = Duplicate(a);
	raw = dup.get();
	REQUIRE(h.Execute(std::move(dup)).IsOk());
	const NodeId copy = CreatedId(*raw);
	CHECK(copy != a);
	CHECK(f.Children(f.body)[1] == copy);
	CHECK_NEAR(f.d.Get(copy, props::SpaceAfter), 9.0, 1e-9);
	CHECK(f.d.Find(copy)->InGroup(g));
	const NodeId copyText = f.d.Find(copy)->Children()[0];
	CHECK(copyText != f.d.Find(a)->Children()[0]);
	CHECK(f.d.Get(copyText, props::Text) == std::string("a"));
	REQUIRE(h.Undo().IsOk());
	CHECK(!f.d.Contains(copy));
	REQUIRE(h.Redo().IsOk());
	CHECK(f.d.Contains(copy) && f.d.Contains(copyText));
}

TEST(History, TransactionsAndComposites) {
	Fixture f;
	History h(f.d);
	h.BeginTransaction("Insérer un tableau");
	auto table = CreateNode(NodeType::Table, f.body);
	Command *raw = table.get();
	REQUIRE(h.Execute(std::move(table)).IsOk());
	const NodeId t = CreatedId(*raw);
	auto row = CreateNode(NodeType::TableRow, t);
	Command *rawRow = row.get();
	REQUIRE(h.Execute(std::move(row)).IsOk());
	REQUIRE(h.Execute(CreateNode(NodeType::TableCell, CreatedId(*rawRow))).IsOk());
	CHECK(!h.CanUndo()); // pas d'annulation pendant une transaction
	REQUIRE(h.Commit().IsOk());
	CHECK_EQ(h.Entries().size(), size_t(1));
	CHECK(h.Entries()[0].name == "Insérer un tableau");
	REQUIRE(h.Undo().IsOk());
	CHECK(!f.d.Contains(t));
	REQUIRE(h.Redo().IsOk());
	CHECK_EQ(f.d.SubtreeOf(t).size(), size_t(3));
	// Annulation d'une transaction.
	h.BeginTransaction("abandon");
	REQUIRE(h.Execute(CreateNode(NodeType::Paragraph, f.body)).IsOk());
	CHECK(h.Execute(CreateNode(NodeType::Text, f.body)).IsError()); // refusée, la transaction continue
	REQUIRE(h.Rollback().IsOk());
	CHECK_EQ(f.Children(f.body).size(), size_t(1));
	CHECK_EQ(h.Entries().size(), size_t(1));
	h.BeginTransaction("vide");
	REQUIRE(h.Commit().IsOk());
	CHECK_EQ(h.Entries().size(), size_t(1));
	// Composite atomique : échec de la deuxième commande, la première est annulée.
	std::vector<CommandPtr> parts;
	parts.push_back(CreateNode(NodeType::Paragraph, f.body));
	parts.push_back(CreateNode(NodeType::Text, f.body));
	CHECK(h.Execute(Composite("atomique", std::move(parts))).IsError());
	CHECK_EQ(f.Children(f.body).size(), size_t(1));
}

TEST(History, BranchesLimitDirtyStateAndSignals) {
	Fixture f;
	History h(f.d);
	size_t signals = 0;
	auto connection = h.Changed().Connect([&] { ++signals; });
	CHECK(!h.IsDirty());
	REQUIRE(h.Execute(CreateNode(NodeType::Paragraph, f.body)).IsOk());
	CHECK(h.IsDirty());
	h.MarkClean();
	CHECK(!h.IsDirty());
	REQUIRE(h.Execute(CreateNode(NodeType::Paragraph, f.body)).IsOk());
	CHECK(h.IsDirty());
	REQUIRE(h.Undo().IsOk());
	CHECK(!h.IsDirty()); // revenu à l'état enregistré
	REQUIRE(h.Undo().IsOk());
	CHECK(h.IsDirty());
	// Nouvelle branche : l'entrée rétablissable et l'état enregistré sont abandonnés.
	REQUIRE(h.Execute(CreateNode(NodeType::Shape, f.body)).IsOk());
	CHECK(!h.CanRedo());
	CHECK_EQ(h.Entries().size(), size_t(1));
	CHECK(h.IsDirty());
	REQUIRE(h.Undo().IsOk());
	CHECK(h.IsDirty());
	CHECK(h.Undo().Error().code == ErrorCode::Nothing);
	CHECK(h.JumpTo(9).IsError());
	CHECK_EQ(signals, size_t(6));
	// Limite : les entrées les plus anciennes sont oubliées.
	h.Clear();
	h.SetLimit(3);
	for (int i = 0; i < 5; ++i)
		REQUIRE(h.Execute(CreateNode(NodeType::Paragraph, f.body)).IsOk());
	CHECK_EQ(h.Entries().size(), size_t(3));
	CHECK_EQ(h.Position(), size_t(3));
}
