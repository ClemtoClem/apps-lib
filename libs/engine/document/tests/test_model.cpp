// Tests du modèle d'engine.document : catalogue des propriétés, arborescence (création,
// suppression, déplacement, cycles, enfants admis), groupes (multiples, hiérarchiques,
// priorité / proximité), cascade des propriétés, cache, instantanés, sous-arbres, notifications.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <document/document.hpp>

using namespace doc;

namespace {

struct Fixture {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);

	NodeId Paragraph(std::string_view text = "texte") {
		const NodeId p = d.CreateNode(NodeType::Paragraph, body).Value();
		const NodeId t = d.CreateNode(NodeType::Text, p).Value();
		(void)d.Set(t, props::Text, text);
		return p;
	}
};

} // namespace

// ── Catalogue ──

TEST(Properties, CatalogueIsCompleteAndOrdered) {
	const auto all = AllProperties();
	REQUIRE(all.size() == kPropertyCount);
	for (size_t i = 0; i < all.size(); ++i) {
		CHECK(size_t(all[i].id) == i);
		CHECK(!all[i].name.empty());
		CHECK(TypeOf(all[i].defaultValue) == all[i].type);
		CHECK(FindProperty(all[i].name) == all[i].id);
	}
	CHECK(Info(PropertyId::FontSize).inheritable);
	CHECK(!Info(PropertyId::BackgroundColor).inheritable);
	CHECK(Info(PropertyId::Bold).category == PropertyCategory::Visual);
	CHECK(Info(PropertyId::KeepTogether).category == PropertyCategory::Functional);
	CHECK(Info(PropertyId::MarginTop).category == PropertyCategory::Structural);
	CHECK_NEAR(std::get<double>(Info(PropertyId::FontSize).defaultValue), 11.0, 1e-9);
}

TEST(Properties, ValidationRejectsWrongTypeAndRange) {
	CHECK(Validate(PropertyId::FontSize, 12.0).IsOk());
	CHECK(Validate(PropertyId::FontSize, int64_t(12)).Error().code == ErrorCode::TypeMismatch);
	CHECK(Validate(PropertyId::FontSize, 0.0).Error().code == ErrorCode::InvalidValue);
	CHECK(Validate(PropertyId::FontSize, std::nan("")).Error().code == ErrorCode::InvalidValue);
	CHECK(Validate(PropertyId::Opacity, 1.5).Error().code == ErrorCode::InvalidValue);
	CHECK(Validate(PropertyId::HAlign, int64_t(HorizontalAlign::Justify)).IsOk());
	CHECK(Validate(PropertyId::HAlign, int64_t(7)).Error().code == ErrorCode::InvalidValue);
	CHECK(Validate(PropertyId::TextColor, Color::RED()).IsOk());
}

TEST(Properties, PropertyMapStaysSorted) {
	PropertyMap m;
	CHECK(m.Set(PropertyId::Opacity, 0.5));
	CHECK(m.Set(PropertyId::Visible, false));
	CHECK(m.Set(PropertyId::FontSize, 9.0));
	CHECK(!m.Set(PropertyId::FontSize, 9.0)); // inchangé
	CHECK_EQ(m.Size(), size_t(3));
	PropertyId previous{};
	bool first = true;
	for (const auto &[id, value] : m) {
		CHECK(first || previous < id);
		previous = id;
		first = false;
	}
	CHECK(m.Erase(PropertyId::Visible));
	CHECK(!m.Erase(PropertyId::Visible));
	CHECK(m.Find(PropertyId::Visible) == nullptr);
}

TEST(Types, PageFormatsInPoints) {
	CHECK_NEAR(StandardSize(PageFormat::A4).width, 595.276, 1e-3);
	CHECK_NEAR(StandardSize(PageFormat::A4).height, 841.89, 1e-2);
	CHECK_NEAR(StandardSize(PageFormat::A3).height, 1190.55, 1e-2);
	CHECK_NEAR(StandardSize(PageFormat::A5).width, 419.53, 1e-2);
	CHECK_NEAR(StandardSize(PageFormat::Letter).height, 792.0, 1e-9);
	CHECK_NEAR(StandardSize(PageFormat::Legal).height, 1008.0, 1e-9);
	CHECK_NEAR(Millimeters(25.4), 72.0, 1e-9);
	CHECK_NEAR(ToPixels(72.0, 96.0), 96.0, 1e-9);
}

// ── Arborescence ──

TEST(Tree, DefaultStructure) {
	Document d;
	const Node *root = d.Find(d.Root());
	REQUIRE(root != nullptr);
	CHECK(root->Type() == NodeType::Document);
	CHECK(d.Find(d.GroupsRoot())->Type() == NodeType::Groups);
	const auto sections = d.Sections();
	REQUIRE(sections.size() == 1);
	CHECK(d.Body(sections[0]) != kNoNode);
	// Groupe automatique des pages de la section.
	const auto *pg = d.Explicit(sections[0], PropertyId::PageGroup);
	REQUIRE(pg != nullptr);
	CHECK(d.Find(NodeId(std::get<int64_t>(*pg)))->Type() == NodeType::Group);
}

TEST(Tree, CreateValidatesChildTypes) {
	Fixture f;
	CHECK(f.d.CreateNode(NodeType::Paragraph, f.body).IsOk());
	CHECK(f.d.CreateNode(NodeType::Text, f.body).Error().code == ErrorCode::InvalidParent);
	CHECK(f.d.CreateNode(NodeType::TableCell, f.body).Error().code == ErrorCode::InvalidParent);
	CHECK(f.d.CreateNode(NodeType::Body, f.section).Error().code == ErrorCode::Protected);
	CHECK(f.d.CreateNode(NodeType::Page, f.section).Error().code == ErrorCode::Unsupported);
	CHECK(f.d.CreateNode(NodeType::Paragraph, 9999).Error().code == ErrorCode::NotFound);
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	CHECK(f.d.CreateNode(NodeType::Table, cell).IsOk()); // tableau imbriqué
	CHECK(f.d.CreateNode(NodeType::TableCell, table).Error().code == ErrorCode::InvalidParent);
}

TEST(Tree, IdsAreStableAndNeverReused) {
	Fixture f;
	const NodeId a = f.Paragraph();
	const NodeId b = f.Paragraph();
	CHECK(b > a);
	REQUIRE(f.d.DeleteNode(b).IsOk());
	const NodeId c = f.Paragraph();
	CHECK(c != b);
	CHECK(c > b);
	CHECK(!f.d.Contains(b));
}

TEST(Tree, InsertAtPositionAndMove) {
	Fixture f;
	const NodeId a = f.Paragraph("a"), b = f.Paragraph("b"), c = f.Paragraph("c");
	const NodeId first = f.d.CreateNode(NodeType::Paragraph, f.body, 0).Value();
	auto children = f.d.Find(f.body)->Children();
	REQUIRE(children.size() == 4);
	CHECK(children[0] == first);
	// Déplacer `a` après `c` dans le même parent (position exprimée avant le retrait).
	REQUIRE(f.d.MoveNode(a, f.body, 4).IsOk());
	children = f.d.Find(f.body)->Children();
	const std::vector<NodeId> expected{first, b, c, a};
	CHECK(std::vector<NodeId>(children.begin(), children.end()) == expected);
	CHECK(f.d.Find(a)->Parent() == f.body);
}

TEST(Tree, MoveRejectsCyclesAndInvalidParents) {
	Fixture f;
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId inner = f.d.CreateNode(NodeType::Table, cell).Value();
	(void)inner;
	CHECK(f.d.MoveNode(table, cell).Error().code == ErrorCode::Cycle);
	CHECK(f.d.MoveNode(table, table).Error().code == ErrorCode::Cycle);
	CHECK(f.d.MoveNode(row, f.body).Error().code == ErrorCode::InvalidParent);
	CHECK(f.d.MoveNode(f.body, f.section).Error().code == ErrorCode::Protected);
	// Le tableau imbriqué remonte dans le corps : autorisé.
	CHECK(f.d.MoveNode(inner, f.body).IsOk());
	CHECK(f.d.Find(inner)->Parent() == f.body);
}

TEST(Tree, StructuralNodesAreProtected) {
	Fixture f;
	CHECK(f.d.DeleteNode(f.d.Root()).Error().code == ErrorCode::Protected);
	CHECK(f.d.DeleteNode(f.d.GroupsRoot()).Error().code == ErrorCode::Protected);
	CHECK(f.d.DeleteNode(f.body).Error().code == ErrorCode::Protected);
	CHECK(f.d.DeleteNode(f.section).Error().code == ErrorCode::Protected); // dernière section
	const NodeId second = f.d.CreateNode(NodeType::Section, f.d.Root()).Value();
	CHECK(f.d.Body(second) != kNoNode);
	CHECK(f.d.DeleteNode(f.section).IsOk());
	CHECK(f.d.Sections().size() == 1);
}

TEST(Tree, HeaderVariantsAndStructuralMoves) {
	Fixture f;
	CHECK(f.d.CreateNode(NodeType::Header, f.section).IsOk());
	CHECK(f.d.CreateNode(NodeType::Header, f.section).IsOk()); // variantes multiples admises
	const NodeId other = f.d.CreateNode(NodeType::Section, f.d.Root()).Value();
	CHECK(f.d.MoveNode(f.d.Body(other), f.section).Error().code == ErrorCode::Protected);
}

TEST(Tree, VisitSearchAndAncestors) {
	Fixture f;
	const NodeId p = f.Paragraph("bonjour");
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(p, props::Name, "intro").IsOk());
	CHECK(f.d.FindByName("intro") == std::vector<NodeId>{p});
	CHECK(f.d.FindAll(NodeType::Text) == std::vector<NodeId>{t});
	CHECK(f.d.AncestorOfType(t, NodeType::Section) == f.section);
	CHECK(f.d.IsAncestor(f.body, t));
	CHECK(!f.d.IsAncestor(t, f.body));
	CHECK_EQ(f.d.Depth(t), size_t(4)); // Document > Section > Body > Paragraph > Text
	std::vector<NodeId> order;
	f.d.Visit(f.section, [&](const Node &n) {
		order.push_back(n.Id());
		return n.Type() != NodeType::Paragraph; // ne pas descendre sous les paragraphes
	});
	CHECK(std::ranges::find(order, p) != order.end());
	CHECK(std::ranges::find(order, t) == order.end());
	CHECK(order.front() == f.section);
}

// ── Propriétés et cascade ──

TEST(Cascade, ExplicitParentDocumentDefault) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];

	Resolved r = f.d.Resolve(t, PropertyId::FontSize);
	CHECK(r.source == ValueSource::Default);
	CHECK_NEAR(std::get<double>(r.value), 11.0, 1e-9);

	REQUIRE(f.d.Set(f.d.Root(), props::FontSize, 12.0).IsOk());
	r = f.d.Resolve(t, PropertyId::FontSize);
	CHECK(r.source == ValueSource::Document);
	CHECK(r.origin == f.d.Root());

	REQUIRE(f.d.Set(p, props::FontSize, 14.0).IsOk());
	r = f.d.Resolve(t, PropertyId::FontSize);
	CHECK(r.source == ValueSource::Parent);
	CHECK(r.origin == p);
	CHECK_NEAR(f.d.Get(t, props::FontSize), 14.0, 1e-9);

	REQUIRE(f.d.Set(t, props::FontSize, 16.0).IsOk());
	r = f.d.Resolve(t, PropertyId::FontSize);
	CHECK(r.source == ValueSource::Explicit);
	CHECK_NEAR(f.d.Get(t, props::FontSize), 16.0, 1e-9);

	REQUIRE(f.d.ClearProperty(t, PropertyId::FontSize).IsOk());
	CHECK_NEAR(f.d.Get(t, props::FontSize), 14.0, 1e-9);
}

TEST(Cascade, NonInheritablePropertiesDoNotFlowToChildren) {
	Fixture f;
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId para = f.d.CreateNode(NodeType::Paragraph, cell).Value();
	REQUIRE(f.d.Set(cell, props::BackgroundColor, Color::YELLOW()).IsOk());
	CHECK(f.d.Get(para, props::BackgroundColor) == Color::TRANSPARENT());
	CHECK(f.d.Resolve(para, PropertyId::BackgroundColor).source == ValueSource::Default);
	// Document : une propriété non héritable n'atteint que les types qu'elle cible.
	REQUIRE(f.d.Set(f.d.Root(), props::BorderWidth, 2.0).IsOk());
	CHECK_NEAR(f.d.Get(cell, props::BorderWidth), 2.0, 1e-9); // cible : cellule
	CHECK(f.d.Resolve(row, PropertyId::BorderWidth).source == ValueSource::Default); // ligne non ciblée
}

TEST(Cascade, LocalPropertiesNeverCascade) {
	Fixture f;
	const NodeId p = f.Paragraph("contenu");
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(f.d.Root(), props::Name, "Mon document").IsOk());
	const NodeId g = f.d.CreateGroup("g").Value();
	REQUIRE(f.d.Set(g, props::Text, "texte du groupe").IsOk());
	REQUIRE(f.d.AddToGroup(t, g).IsOk());
	CHECK(f.d.Get(p, props::Name).empty()); // le nom du document ne se propage pas
	CHECK(f.d.Resolve(p, PropertyId::Name).source == ValueSource::Default);
	CHECK(f.d.Get(t, props::Text) == std::string("contenu"));
	REQUIRE(f.d.ClearProperty(t, PropertyId::Text).IsOk());
	CHECK(f.d.Get(t, props::Text).empty()); // ni celui du groupe
	CHECK(!Info(PropertyId::Name).cascades);
	CHECK(Info(PropertyId::FontSize).cascades);
}

TEST(Cascade, InapplicablePropertyIsRefused) {
	Fixture f;
	const NodeId p = f.Paragraph();
	CHECK(f.d.Set(p, props::Text, "x").Error().code == ErrorCode::InvalidValue);
	CHECK(f.d.Set(p, props::Opacity, 0.5).Error().code == ErrorCode::InvalidValue);
	// Les groupes et le document portent n'importe quelle propriété.
	const NodeId g = f.d.CreateGroup("g").Value();
	CHECK(f.d.Set(g, props::Opacity, 0.5).IsOk());
	CHECK(f.d.Set(f.d.Root(), props::Opacity, 0.5).IsOk());
}

TEST(Cascade, PageSetupInheritsThroughSection) {
	Fixture f;
	const NodeId page = f.d.AddPage(f.section).Value();
	CHECK(f.d.Get(page, props::Format) == PageFormat::A4);
	REQUIRE(f.d.Set(f.d.Root(), props::PageOrientation, Orientation::Landscape).IsOk());
	CHECK(f.d.Get(page, props::PageOrientation) == Orientation::Landscape);
	REQUIRE(f.d.Set(f.section, props::Format, PageFormat::Letter).IsOk());
	CHECK(f.d.Get(page, props::Format) == PageFormat::Letter);
	REQUIRE(f.d.Set(page, props::PageOrientation, Orientation::Portrait).IsOk());
	CHECK(f.d.Get(page, props::PageOrientation) == Orientation::Portrait);
}

// ── Groupes ──

TEST(Groups, MemberReceivesGroupValues) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	const NodeId titles = f.d.CreateGroup("Titres", 10).Value();
	REQUIRE(f.d.Set(titles, props::FontSize, 20.0).IsOk());
	REQUIRE(f.d.Set(titles, props::Bold, true).IsOk());
	REQUIRE(f.d.AddToGroup(p, titles).IsOk());
	const Resolved r = f.d.Resolve(p, PropertyId::FontSize);
	CHECK(r.source == ValueSource::Group);
	CHECK(r.origin == titles);
	CHECK(f.d.Get(t, props::Bold)); // hérité du paragraphe, lui-même membre
	CHECK(f.d.Resolve(t, PropertyId::Bold).source == ValueSource::Parent);
	CHECK(f.d.Find(titles)->Members().size() == 1);
	CHECK(f.d.AddToGroup(p, titles).Error().code == ErrorCode::Nothing);
	// Explicite > groupe.
	REQUIRE(f.d.Set(p, props::FontSize, 9.0).IsOk());
	CHECK_NEAR(f.d.Get(p, props::FontSize), 9.0, 1e-9);
	// Retrait du groupe.
	REQUIRE(f.d.RemoveFromGroup(p, titles).IsOk());
	CHECK(!f.d.Get(p, props::Bold));
	CHECK(f.d.Find(titles)->Members().empty());
}

TEST(Groups, MultipleMembershipPriorityAndRecency) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId low = f.d.CreateGroup("bas", 1).Value();
	const NodeId high = f.d.CreateGroup("haut", 5).Value();
	REQUIRE(f.d.Set(low, props::TextColor, Color::RED()).IsOk());
	REQUIRE(f.d.Set(high, props::TextColor, Color::BLUE()).IsOk());
	REQUIRE(f.d.Set(low, props::Italic, true).IsOk());
	REQUIRE(f.d.AddToGroup(p, high).IsOk());
	REQUIRE(f.d.AddToGroup(p, low).IsOk());
	CHECK(f.d.Get(p, props::TextColor) == Color::BLUE()); // priorité
	CHECK(f.d.Get(p, props::Italic));					 // seul `low` la définit
	// Priorités égales : l'appartenance la plus récente l'emporte.
	REQUIRE(f.d.Set(low, props::Priority, int64_t(5)).IsOk());
	CHECK(f.d.Get(p, props::TextColor) == Color::RED());
	CHECK(f.d.GroupOrder(p) == (std::vector<NodeId>{low, high}));
}

TEST(Groups, HierarchyAndPriorityVersusProximity) {
	Fixture f;
	const NodeId p = f.Paragraph();
	// parent (priorité 10) > enfant (priorité 0) ; p appartient à l'enfant seulement.
	const NodeId parent = f.d.CreateGroup("parent", 10).Value();
	const NodeId child = f.d.CreateGroup("enfant", 0, parent).Value();
	REQUIRE(f.d.Set(parent, props::FontSize, 30.0).IsOk());
	REQUIRE(f.d.Set(parent, props::Underline, true).IsOk());
	REQUIRE(f.d.Set(child, props::FontSize, 15.0).IsOk());
	REQUIRE(f.d.AddToGroup(p, child).IsOk());
	CHECK(f.d.Get(p, props::Underline)); // le groupe ancêtre s'applique

	CHECK(f.d.Resolution() == GroupResolution::PriorityFirst);
	CHECK_NEAR(f.d.Get(p, props::FontSize), 30.0, 1e-9); // priorité d'abord
	CHECK(f.d.Resolve(p, PropertyId::FontSize).origin == parent);

	f.d.SetResolution(GroupResolution::ProximityFirst);
	CHECK_NEAR(f.d.Get(p, props::FontSize), 15.0, 1e-9); // proximité d'abord
	CHECK(f.d.Resolve(p, PropertyId::FontSize).origin == child);

	// Propriété d'un groupe : explicite, puis groupes ancêtres.
	CHECK(f.d.Resolve(child, PropertyId::Underline).source == ValueSource::Group);
	CHECK(f.d.Resolve(child, PropertyId::Underline).origin == parent);
}

TEST(Groups, GroupTargetsAndMembershipRules) {
	Fixture f;
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId g = f.d.CreateGroup("fond").Value();
	REQUIRE(f.d.Set(g, props::BackgroundColor, Color::GREEN()).IsOk());
	REQUIRE(f.d.AddToGroup(cell, g).IsOk());
	REQUIRE(f.d.AddToGroup(row, g).IsOk());
	CHECK(f.d.Get(cell, props::BackgroundColor) == Color::GREEN());
	CHECK(f.d.Get(row, props::BackgroundColor) == Color::TRANSPARENT()); // ligne non ciblée
	CHECK(f.d.AddToGroup(g, g).Error().code == ErrorCode::InvalidValue);
	CHECK(f.d.AddToGroup(cell, table).Error().code == ErrorCode::InvalidValue);
}

TEST(Groups, PagesJoinSectionGroup) {
	Fixture f;
	const NodeId group = NodeId(std::get<int64_t>(*f.d.Explicit(f.section, PropertyId::PageGroup)));
	const NodeId p1 = f.d.AddPage(f.section).Value();
	const NodeId p2 = f.d.AddPage(f.section).Value();
	CHECK(f.d.Find(p1)->Generated());
	REQUIRE(f.d.Find(p1)->Groups().size() == 1);
	CHECK(f.d.Find(p1)->Groups()[0].automatic);
	REQUIRE(f.d.Set(group, props::PageOrientation, Orientation::Landscape).IsOk());
	CHECK(f.d.Get(p1, props::PageOrientation) == Orientation::Landscape);
	CHECK(f.d.Get(p2, props::PageOrientation) == Orientation::Landscape);
	CHECK(f.d.Resolve(p2, PropertyId::PageOrientation).source == ValueSource::Group);
	REQUIRE(f.d.RemovePage(p2).IsOk());
	CHECK(f.d.Find(group)->Members().size() == 1);
	const NodeId para = f.Paragraph();
	CHECK(f.d.RemovePage(para).Error().code == ErrorCode::InvalidValue);
}

TEST(Groups, DeletingGroupDropsMembershipsAndRestoreIsExact) {
	Fixture f;
	const NodeId a = f.Paragraph("a"), b = f.Paragraph("b");
	const NodeId g1 = f.d.CreateGroup("g1").Value();
	const NodeId g2 = f.d.CreateGroup("g2").Value();
	const NodeId sub = f.d.CreateGroup("sous", 0, g2).Value();
	REQUIRE(f.d.AddToGroup(a, g1).IsOk());
	REQUIRE(f.d.AddToGroup(a, g2).IsOk());
	REQUIRE(f.d.AddToGroup(a, sub).IsOk());
	REQUIRE(f.d.AddToGroup(b, g2).IsOk());
	REQUIRE(f.d.Set(g2, props::Italic, true).IsOk());
	CHECK(f.d.Get(b, props::Italic));

	auto removed = f.d.RemoveSubtree(g2); // g2 et son sous-groupe
	REQUIRE(removed.IsOk());
	CHECK(!f.d.Contains(g2));
	CHECK(!f.d.Contains(sub));
	CHECK(f.d.Find(a)->Groups().size() == 1);
	CHECK(!f.d.Get(b, props::Italic));

	REQUIRE(f.d.InsertSubtree(removed.Value()).IsOk());
	CHECK(f.d.Get(b, props::Italic));
	const auto groups = f.d.Find(a)->Groups();
	REQUIRE(groups.size() == 3);
	CHECK(groups[0].group == g1);
	CHECK(groups[1].group == g2);
	CHECK(groups[2].group == sub);
	CHECK(f.d.Find(g2)->Members().size() == 2);
	CHECK(f.d.Find(sub)->Parent() == g2);
}

// ── Sous-arbres, verrous ──

TEST(Subtree, RemoveAndReinsertKeepsIdsPositionsAndMemberships) {
	Fixture f;
	const NodeId a = f.Paragraph("a");
	const NodeId table = f.d.CreateNode(NodeType::Table, f.body).Value();
	const NodeId row = f.d.CreateNode(NodeType::TableRow, table).Value();
	const NodeId cell = f.d.CreateNode(NodeType::TableCell, row).Value();
	const NodeId b = f.Paragraph("b");
	const NodeId g = f.d.CreateGroup("g").Value();
	REQUIRE(f.d.AddToGroup(b, g).IsOk());
	REQUIRE(f.d.AddToGroup(cell, g).IsOk());
	REQUIRE(f.d.Set(cell, props::BackgroundColor, Color::RED()).IsOk());
	const size_t count = f.d.NodeCount();

	auto removed = f.d.RemoveSubtree(table);
	REQUIRE(removed.IsOk());
	CHECK_EQ(f.d.NodeCount(), count - 3);
	CHECK(f.d.Find(g)->Members().size() == 1);
	REQUIRE(f.d.InsertSubtree(removed.Value()).IsOk());
	CHECK_EQ(f.d.NodeCount(), count);
	const auto children = f.d.Find(f.body)->Children();
	const std::vector<NodeId> expected{a, table, b};
	CHECK(std::vector<NodeId>(children.begin(), children.end()) == expected);
	CHECK(f.d.Get(cell, props::BackgroundColor) == Color::RED());
	const auto members = f.d.Find(g)->Members();
	const std::vector<NodeId> expectedMembers{b, cell};
	CHECK(std::vector<NodeId>(members.begin(), members.end()) == expectedMembers);
	CHECK(f.d.InsertSubtree(removed.Value()).Error().code == ErrorCode::InvalidValue); // déjà présent
}

TEST(Locks, LockedNodesRefuseChanges) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(p, props::Locked, true).IsOk());
	CHECK(f.d.Get(t, props::Locked)); // héritable
	CHECK(f.d.Set(t, props::Text, "x").Error().code == ErrorCode::Locked);
	CHECK(f.d.DeleteNode(t).Error().code == ErrorCode::Locked);
	CHECK(f.d.CreateNode(NodeType::Text, p).Error().code == ErrorCode::Locked);
	CHECK(f.d.MoveNode(p, f.body, 0).Error().code == ErrorCode::Locked);
	// Un nœud verrouillé dans un sous-arbre empêche sa suppression.
	const NodeId q = f.Paragraph();
	const NodeId qt = f.d.Find(q)->Children()[0];
	REQUIRE(f.d.Set(qt, props::Locked, true).IsOk());
	CHECK(f.d.DeleteNode(q).Error().code == ErrorCode::Locked);
	// Déverrouiller reste possible.
	REQUIRE(f.d.Set(p, props::Locked, false).IsOk());
	CHECK(f.d.Set(t, props::Text, "x").IsOk());
}

TEST(Locks, NonEditableTextIsReadOnly) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	REQUIRE(f.d.Set(p, props::Editable, false).IsOk());
	CHECK(f.d.Set(t, props::Text, "y").Error().code == ErrorCode::Locked);
	CHECK(f.d.Set(t, props::Bold, true).IsOk()); // la mise en forme reste possible
}

// ── Cache, révisions, instantanés, notifications ──

TEST(Cache, InvalidatedByStyleChanges) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	PropertyCache cache;
	CHECK_NEAR(cache.Get(f.d, t, props::FontSize), 11.0, 1e-9);
	CHECK_NEAR(cache.Get(f.d, t, props::FontSize), 11.0, 1e-9);
	CHECK_EQ(cache.Hits(), uint64_t(1));
	CHECK_EQ(cache.Misses(), uint64_t(1));
	const NodeId g = f.d.CreateGroup("g").Value();
	REQUIRE(f.d.Set(g, props::FontSize, 18.0).IsOk());
	REQUIRE(f.d.AddToGroup(p, g).IsOk());
	CHECK_NEAR(cache.Get(f.d, t, props::FontSize), 18.0, 1e-9);
	REQUIRE(f.d.Set(g, props::FontSize, 8.0).IsOk());
	CHECK_NEAR(cache.Get(f.d, t, props::FontSize), 8.0, 1e-9);
	f.d.SetResolution(GroupResolution::ProximityFirst);
	CHECK_NEAR(cache.Get(f.d, t, props::FontSize), 8.0, 1e-9);
	CHECK_EQ(cache.Misses(), uint64_t(4)); // chaque changement de style a forcé un recalcul
	CHECK_EQ(cache.Size(), size_t(1));
}

TEST(Cache, LocalChangesKeepStyleRevision) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	PropertyCache cache;
	CHECK(cache.Get(f.d, t, props::Text) == std::string("texte"));
	const uint64_t style = f.d.StyleRevision();
	REQUIRE(f.d.Set(t, props::Text, "modifié").IsOk());		 // non héritable, nœud ordinaire
	REQUIRE(f.d.Set(p, props::SpaceAfter, 3.0).IsOk());
	CHECK_EQ(f.d.StyleRevision(), style);
	CHECK(cache.Get(f.d, t, props::Text) == std::string("modifié")); // révision du nœud
	REQUIRE(f.d.Set(p, props::FontSize, 20.0).IsOk());				 // héritable
	CHECK(f.d.StyleRevision() > style);
	const NodeId g = f.d.CreateGroup("g").Value();
	const uint64_t beforeGroup = f.d.StyleRevision();
	REQUIRE(f.d.Set(g, props::SpaceAfter, 1.0).IsOk()); // groupe : touche ses membres
	CHECK(f.d.StyleRevision() > beforeGroup);
}

TEST(Cache, RevisionsPropagateToAncestors) {
	Fixture f;
	const NodeId p = f.Paragraph();
	const NodeId t = f.d.Find(p)->Children()[0];
	const uint64_t bodyBefore = f.d.Find(f.body)->Revision();
	const uint64_t otherBefore = f.d.Find(f.d.GroupsRoot())->Revision();
	REQUIRE(f.d.Set(t, props::Text, "modifié").IsOk());
	CHECK(f.d.Find(t)->Revision() > bodyBefore);
	CHECK(f.d.Find(p)->Revision() == f.d.Find(t)->Revision());
	CHECK(f.d.Find(f.body)->Revision() == f.d.Find(t)->Revision());
	CHECK(f.d.Find(f.d.GroupsRoot())->Revision() == otherBefore);
}

TEST(Snapshot, IsImmutableAndSharesUntouchedNodes) {
	Fixture f;
	const NodeId p = f.Paragraph("avant");
	const NodeId t = f.d.Find(p)->Children()[0];
	const NodeId other = f.Paragraph("autre");
	const Snapshot snap = f.d.TakeSnapshot();
	REQUIRE(f.d.Set(t, props::Text, "après").IsOk());
	REQUIRE(f.d.DeleteNode(other).IsOk());
	f.Paragraph("nouveau");

	CHECK(snap->Get(t, props::Text) == std::string("avant"));
	CHECK(f.d.Get(t, props::Text) == std::string("après"));
	CHECK(snap->Contains(other));
	CHECK(!f.d.Contains(other));
	CHECK(snap->Find(f.body)->Children().size() == 2);
	CHECK(f.d.Find(f.body)->Children().size() == 2);
	CHECK(snap->Stamp() == f.d.Stamp());
	CHECK(snap->Revision() < f.d.Revision());
	// Nœud non modifié : même objet partagé (copie sur écriture).
	CHECK(snap->Find(f.d.GroupsRoot()) == f.d.Find(f.d.GroupsRoot()));
	CHECK(snap->Find(t) != f.d.Find(t));
}

TEST(Notifications, ChangesAreSignalled) {
	Fixture f;
	std::vector<Change> changes;
	auto connection = f.d.Changed().Connect([&](const Change &c) { changes.push_back(c); });
	const NodeId p = f.d.CreateNode(NodeType::Paragraph, f.body).Value();
	REQUIRE(f.d.Set(p, props::SpaceAfter, 12.0).IsOk());
	REQUIRE(f.d.Set(p, props::SpaceAfter, 12.0).IsOk()); // inchangé : pas de notification
	const NodeId g = f.d.CreateGroup("g").Value();
	REQUIRE(f.d.AddToGroup(p, g).IsOk());
	REQUIRE(f.d.DeleteNode(p).IsOk());
	REQUIRE(changes.size() == 5);
	CHECK(changes[0].kind == ChangeKind::Created);
	CHECK(changes[0].node == p);
	CHECK(changes[1].kind == ChangeKind::Property);
	CHECK(changes[1].property == PropertyId::SpaceAfter);
	CHECK(changes[2].kind == ChangeKind::Created);
	CHECK(changes[3].kind == ChangeKind::Membership);
	CHECK(changes[4].kind == ChangeKind::Deleted);
	// Le groupe perd le membre supprimé.
	CHECK(f.d.Find(g)->Members().empty());
}

TEST(Resources, ImageResourceIsReferencedByImages) {
	Fixture f;
	const ResourceId r = f.d.AddResource({2, 1, {255, 0, 0, 255, 0, 0, 255, 255}});
	const NodeId image = f.d.CreateNode(NodeType::Image, f.body).Value();
	CHECK(f.d.Set(image, props::Resource, int64_t(r)).IsOk());
	CHECK(f.d.Set(image, props::Resource, int64_t(r + 10)).Error().code == ErrorCode::NotFound);
	REQUIRE(f.d.FindResource(r) != nullptr);
	CHECK_EQ(f.d.FindResource(r)->width, uint32_t(2));
	CHECK(f.d.Resources() == std::vector<ResourceId>{r});
}
