// Tests de persistance d'engine.document (.docdb) : aller-retour complet (arborescence,
// propriétés de tous types, groupes et ordre des appartenances, ressources, politique de
// groupes, compteurs d'identifiants), mise en page identique après rechargement, fichiers
// corrompus, contrôles d'intégrité, versions et migrations, enregistrement d'un instantané
// sur un autre fil.
#include <cpp-maker/test.hpp>

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

#include <document/layout.hpp>
#include <document/persistence.hpp>
#include <sql/database.hpp>

using namespace doc;

namespace {

std::filesystem::path Temp(std::string_view name) {
	return std::filesystem::temp_directory_path() / std::format("apps-lib-docdb-{}", name);
}

struct Sample {
	Document d;
	NodeId section = d.Sections().front();
	NodeId body = d.Body(section);
	NodeId para = kNoNode, text = kNoNode, table = kNoNode, image = kNoNode, high = kNoNode, low = kNoNode, child = kNoNode;
	NodeId deleted = kNoNode;

	Sample() {
		para = d.CreateNode(NodeType::Paragraph, body).Value();
		text = d.CreateNode(NodeType::Text, para).Value();
		(void)d.Set(text, props::Text, "Été « accentué » — 10 €");
		(void)d.Set(text, props::Bold, true);
		(void)d.Set(text, props::TextColor, Color(10, 20, 30, 200));
		(void)d.Set(para, props::HAlign, HorizontalAlign::Justify);
		(void)d.Set(para, props::LineSpacing, 1.5);
		(void)d.Set(para, props::MinimumLines, int64_t(3));
		(void)d.Set(para, props::Name, "intro");
		table = d.CreateNode(NodeType::Table, body).Value();
		for (int r = 0; r < 2; ++r) {
			const NodeId row = d.CreateNode(NodeType::TableRow, table).Value();
			for (int c = 0; c < 2; ++c) {
				const NodeId cell = d.CreateNode(NodeType::TableCell, row).Value();
				const NodeId p = d.CreateNode(NodeType::Paragraph, cell).Value();
				(void)d.Set(d.CreateNode(NodeType::Text, p).Value(), props::Text, std::format("{}{}", r, c));
			}
		}
		const ResourceId res = d.AddResource({2, 1, {1, 2, 3, 4, 5, 6, 7, 8}});
		image = d.CreateNode(NodeType::Image, body).Value();
		(void)d.Set(image, props::Resource, int64_t(res));
		const NodeId header = d.CreateNode(NodeType::Header, section).Value();
		(void)d.Set(header, props::Variant, HeaderFooterVariant::First);
		const NodeId hp = d.CreateNode(NodeType::Paragraph, header).Value();
		(void)d.Set(d.CreateNode(NodeType::Text, hp).Value(), props::Field, FieldKind::PageNumber);
		high = d.CreateGroup("haut", 10).Value();
		low = d.CreateGroup("bas", -2).Value();
		child = d.CreateGroup("enfant", 0, high).Value();
		(void)d.Set(high, props::FontSize, 14.0);
		(void)d.Set(child, props::Italic, true);
		(void)d.AddToGroup(para, low);
		(void)d.AddToGroup(para, child);
		(void)d.AddToGroup(table, child);
		(void)d.AddToGroup(image, high);
		(void)d.Set(d.Root(), props::PageOrientation, Orientation::Landscape);
		d.SetResolution(GroupResolution::ProximityFirst);
		deleted = d.CreateNode(NodeType::Paragraph, body).Value(); // identifiant à ne jamais réutiliser
		(void)d.DeleteNode(deleted);
		LayoutResult layout = Layout(d);
		(void)SyncPages(d, layout);
	}
};

/// Comparaison complète de deux vues (nœuds, ordre, propriétés, appartenances, ressources).
void CheckSame(const DocumentView &a, const DocumentView &b) {
	CHECK_EQ(a.NodeCount(), b.NodeCount());
	CHECK_EQ(a.Root(), b.Root());
	CHECK_EQ(a.GroupsRoot(), b.GroupsRoot());
	CHECK(a.Resolution() == b.Resolution());
	CHECK_EQ(a.NextId(), b.NextId());
	a.Visit(a.Root(), [&](const Node &n) {
		const Node *m = b.Find(n.Id());
		REQUIRE(m != nullptr);
		CHECK(m->Type() == n.Type());
		CHECK_EQ(m->Parent(), n.Parent());
		CHECK(m->Generated() == n.Generated());
		CHECK(std::ranges::equal(m->Children(), n.Children()));
		CHECK(std::ranges::equal(m->Groups(), n.Groups()));
		CHECK(std::ranges::equal(m->Members(), n.Members()));
		CHECK(m->Properties() == n.Properties());
	});
	CHECK(a.Resources() == b.Resources());
	for (ResourceId r : a.Resources()) {
		CHECK_EQ(a.FindResource(r)->width, b.FindResource(r)->width);
		CHECK(a.FindResource(r)->rgba == b.FindResource(r)->rgba);
	}
}

/// Base écrite depuis l'échantillon, à altérer par les tests d'intégrité.
std::unique_ptr<sql::Database> SampleDatabase(const Sample &s) {
	auto db = std::make_unique<sql::Database>();
	(void)WriteDatabase(s.d, *db);
	return db;
}

sql::RowId FindRow(const sql::Table &t, std::string_view column, int64_t value) {
	const int c = t.GetSchema().Find(column);
	sql::RowId found = sql::kNoRow;
	t.Scan([&](sql::RowId r) {
		if (t.GetInt(r, c) == value) {
			found = r;
			return false;
		}
		return true;
	});
	return found;
}

sql::RowId FindPropertyRow(const sql::Table &t, NodeId node, std::string_view name) {
	const int n = t.GetSchema().Find("node"), k = t.GetSchema().Find("name");
	sql::RowId found = sql::kNoRow;
	t.Scan([&](sql::RowId r) {
		if (t.GetInt(r, n) == int64_t(node) && t.GetText(r, k) == name) {
			found = r;
			return false;
		}
		return true;
	});
	return found;
}

ErrorCode CodeOf(sql::Database &db) {
	auto r = ReadDatabase(db);
	return r ? ErrorCode::Nothing : r.Error().code;
}

} // namespace

TEST(Persistence, RoundTripKeepsEverything) {
	Sample s;
	const auto path = Temp("roundtrip.docdb");
	REQUIRE(SaveDocument(s.d, path).IsOk());
	CHECK(!std::filesystem::exists(path.string() + ".tmp"));
	auto loaded = LoadDocument(path);
	REQUIRE(loaded.IsOk());
	const Document &d = loaded.Value();
	CheckSame(s.d, d);
	// Cascade et valeurs de tous types identiques.
	CHECK(d.Get(s.text, props::Text) == std::string("Été « accentué » — 10 €"));
	CHECK(d.Get(s.text, props::TextColor) == Color(10, 20, 30, 200));
	CHECK(d.Get(s.para, props::HAlign) == HorizontalAlign::Justify);
	CHECK(d.Get(s.para, props::Italic)); // groupe enfant
	CHECK(d.Resolve(s.image, PropertyId::FontSize).origin == s.high);
	CHECK(d.FindByName("intro") == std::vector<NodeId>{s.para});
	// Mise en page identique.
	const LayoutResult a = Layout(s.d), b = Layout(d);
	REQUIRE(a.pages.size() == b.pages.size());
	CHECK_NEAR(a.pages[0].geometry.width, b.pages[0].geometry.width, 1e-9);
	CHECK_EQ(a.pages[0].lines.size(), b.pages[0].lines.size());
	CHECK(b.pages[0].page == a.pages[0].page);
	// Identifiants jamais réutilisés, document nouveau (identité distincte) et modifiable.
	Document reloaded = std::move(loaded.Value());
	CHECK(reloaded.Stamp() != s.d.Stamp());
	const NodeId fresh = reloaded.CreateNode(NodeType::Paragraph, reloaded.Body(reloaded.Sections()[0])).Value();
	CHECK(fresh > s.deleted);
	std::filesystem::remove(path);
}

TEST(Persistence, SaveIsAtomicAndFilesAreChecked) {
	Sample s;
	const auto path = Temp("atomic.docdb");
	REQUIRE(SaveDocument(s.d, path).IsOk());
	const auto size = std::filesystem::file_size(path);
	// Échec d'écriture : erreur, rien d'abandonné.
	CHECK(SaveDocument(s.d, "/dossier/inexistant/x.docdb").Error().code == ErrorCode::Io);
	CHECK(LoadDocument(Temp("absent.docdb")).Error().code == ErrorCode::Io);
	// Octet altéré : somme de contrôle fausse.
	{
		std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
		f.seekp(std::streamoff(size / 2));
		char c = 0;
		f.seekg(std::streamoff(size / 2));
		f.get(c);
		f.seekp(std::streamoff(size / 2));
		f.put(char(c ^ 0x5A));
	}
	CHECK(LoadDocument(path).Error().code == ErrorCode::Corrupt);
	// Fichier tronqué.
	std::filesystem::resize_file(path, size / 3);
	CHECK(LoadDocument(path).Error().code == ErrorCode::Corrupt);
	std::filesystem::remove(path);
}

TEST(Persistence, IntegrityChecksRejectInconsistentDatabases) {
	Sample s;
	{ // Enfant non admis : paragraphe sous un TextNode.
		auto db = SampleDatabase(s);
		sql::Table &nodes = *db->GetTable("nodes");
		REQUIRE(nodes.Set(FindRow(nodes, "id", int64_t(s.para)), nodes.GetSchema().Find("parent"), sql::Value(int64_t(s.text))));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Parent absent.
		auto db = SampleDatabase(s);
		sql::Table &nodes = *db->GetTable("nodes");
		REQUIRE(nodes.Set(FindRow(nodes, "id", int64_t(s.table)), nodes.GetSchema().Find("parent"), sql::Value(int64_t(99999))));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Cycle : deux groupes parents l'un de l'autre, détachés de l'arborescence.
		auto db = SampleDatabase(s);
		sql::Table &nodes = *db->GetTable("nodes");
		const int parent = nodes.GetSchema().Find("parent");
		REQUIRE(nodes.Set(FindRow(nodes, "id", int64_t(s.high)), parent, sql::Value(int64_t(s.child))));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Propriété inconnue.
		auto db = SampleDatabase(s);
		sql::Table &p = *db->GetTable("properties");
		REQUIRE(p.Set(0, p.GetSchema().Find("name"), sql::Value("Inexistante")));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Type de valeur incohérent.
		auto db = SampleDatabase(s);
		sql::Table &p = *db->GetTable("properties");
		REQUIRE(p.Set(0, p.GetSchema().Find("kind"), sql::Value(int64_t(PropertyType::Text) + 10)));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Valeur hors domaine.
		auto db = SampleDatabase(s);
		sql::Table &p = *db->GetTable("properties");
		const sql::RowId row = FindPropertyRow(p, s.high, "FontSize");
		REQUIRE(row != sql::kNoRow);
		REQUIRE(p.Set(row, p.GetSchema().Find("rval"), sql::Value(-5.0)));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Appartenance vers un nœud qui n'est pas un groupe.
		auto db = SampleDatabase(s);
		sql::Table &m = *db->GetTable("memberships");
		REQUIRE(m.Set(0, m.GetSchema().Find("grp"), sql::Value(int64_t(s.table))));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Ressource référencée absente.
		auto db = SampleDatabase(s);
		sql::Table &r = *db->GetTable("resources");
		r.Clear();
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Pixels incohérents avec les dimensions.
		auto db = SampleDatabase(s);
		sql::Table &r = *db->GetTable("resources");
		REQUIRE(r.Set(0, r.GetSchema().Find("width"), sql::Value(int64_t(3))));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Table manquante.
		auto db = SampleDatabase(s);
		REQUIRE(db->DropTable("memberships"));
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Pas un document.
		auto db = SampleDatabase(s);
		db->GetTable("meta")->Upsert(sql::Row{sql::Value("format"), sql::Value("autre")});
		CHECK(CodeOf(*db) == ErrorCode::Corrupt);
	}
	{ // Base de destination non vide.
		auto db = SampleDatabase(s);
		CHECK(WriteDatabase(s.d, *db).Error().code == ErrorCode::InvalidValue);
	}
}

TEST(Persistence, VersionsAndMigrations) {
	Sample s;
	{ // Format plus récent que le moteur.
		auto db = SampleDatabase(s);
		db->GetTable("meta")->Upsert(sql::Row{sql::Value("version"), sql::Value(std::to_string(kSchemaVersion + 1))});
		CHECK(CodeOf(*db) == ErrorCode::Unsupported);
	}
	{ // Format ancien sans migration connue.
		auto db = SampleDatabase(s);
		db->GetTable("meta")->Upsert(sql::Row{sql::Value("version"), sql::Value("0")});
		CHECK(CodeOf(*db) == ErrorCode::Unsupported);
	}
	{ // Migration 0 → 1 : la « version 0 » nommait la politique de groupes « mode ».
		auto db = SampleDatabase(s);
		sql::Table &meta = *db->GetTable("meta");
		meta.Upsert(sql::Row{sql::Value("version"), sql::Value("0")});
		meta.Erase(meta.FindByKey(sql::Value("resolution")));
		meta.Upsert(sql::Row{sql::Value("mode"), sql::Value("proximity")});
		static const Migration kRename{0, [](sql::Database &base) -> Status {
										   sql::Table *t = base.GetTable("meta");
										   const sql::RowId r = t->FindByKey(sql::Value("mode"));
										   if (r == sql::kNoRow)
											   return Failure(ErrorCode::Corrupt, "clé « mode » absente");
										   t->Upsert(sql::Row{sql::Value("resolution"), t->Get(r, 1)});
										   t->Erase(r);
										   return Success();
									   }};
		auto migrated = ReadDatabase(*db, std::span<const Migration>(&kRename, 1));
		REQUIRE(migrated.IsOk());
		CHECK(migrated.Value().Resolution() == GroupResolution::ProximityFirst);
		CHECK(db->GetTable("meta")->Get(db->GetTable("meta")->FindByKey(sql::Value("version")), 1).AsText() == "1");
		CheckSame(s.d, migrated.Value());
	}
	CHECK(BuiltinMigrations().empty());
}

TEST(Persistence, SnapshotSavedOnAnotherThread) {
	Sample s;
	const Snapshot snap = s.d.TakeSnapshot();
	const auto path = Temp("thread.docdb");
	Status saved = Success();
	std::thread worker([&] { saved = SaveDocument(*snap, path); });
	for (int i = 0; i < 300; ++i) // l'édition continue pendant l'enregistrement
		(void)s.d.CreateNode(NodeType::Paragraph, s.body);
	(void)s.d.Set(s.text, props::Text, "modifié après l'instantané");
	worker.join();
	REQUIRE(saved.IsOk());
	auto loaded = LoadDocument(path);
	REQUIRE(loaded.IsOk());
	CheckSame(*snap, loaded.Value());
	CHECK(loaded.Value().Get(s.text, props::Text) == std::string("Été « accentué » — 10 €"));
	std::filesystem::remove(path);
}
