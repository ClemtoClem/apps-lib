// Persistance .docdb : écriture d'une vue dans une base foundation.sql, lecture avec migrations
// et contrôle d'intégrité, instantané fichier atomique.
#include "document/persistence.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>

#include <archive/archive_deflate.hpp>
#include <sql/database.hpp>

namespace doc {

namespace {

using sql::ColumnDef;
using sql::ColumnType;
using sql::Row;
using sql::Schema;
using sql::Value;

constexpr std::string_view kRgba8 = "rgba8";

Err<Error> Corrupt(std::string message) { return Failure(ErrorCode::Corrupt, std::move(message)); }

uint32_t PackColor(const Color &c) { return uint32_t(c.r) << 24 | uint32_t(c.g) << 16 | uint32_t(c.b) << 8 | c.a; }
Color UnpackColor(uint32_t v) { return Color(uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)); }

Schema MakeSchema(std::initializer_list<ColumnDef> columns) { return Schema{std::vector<ColumnDef>(columns)}; }

/// Accès aux colonnes par nom (un fichier migré peut ordonner ses colonnes autrement).
struct Columns {
	const sql::Table *table = nullptr;
	std::vector<int> index;

	static DocResult<Columns> Of(const sql::Database &db, std::string_view name, std::initializer_list<std::string_view> wanted) {
		Columns c;
		c.table = db.GetTable(name);
		if (!c.table)
			return Corrupt(std::format("table « {} » absente", name));
		for (std::string_view w : wanted) {
			const int i = c.table->GetSchema().Find(w);
			if (i < 0)
				return Corrupt(std::format("colonne « {}.{} » absente", name, w));
			c.index.push_back(i);
		}
		return Ok(std::move(c));
	}
	[[nodiscard]] Value Get(sql::RowId r, size_t k) const { return table->Get(r, index[k]); }
};

std::map<std::string, std::string, std::less<>> ReadMeta(const sql::Database &db) {
	std::map<std::string, std::string, std::less<>> meta;
	if (const sql::Table *t = db.GetTable("meta")) {
		const int key = t->GetSchema().Find("key"), value = t->GetSchema().Find("value");
		if (key >= 0 && value >= 0)
			t->Scan([&](sql::RowId r) {
				meta.emplace(std::string(t->GetText(r, key)), std::string(t->GetText(r, value)));
				return true;
			});
	}
	return meta;
}

std::optional<int64_t> ParseInt(std::string_view s) {
	int64_t v = 0;
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
	if (ec != std::errc() || ptr != s.data() + s.size())
		return std::nullopt;
	return v;
}

Status SetMeta(sql::Database &db, std::string_view key, std::string value) {
	sql::Table *t = db.GetTable("meta");
	if (!t)
		return Corrupt("table « meta » absente");
	t->Upsert(Row{Value(key), Value(std::move(value))});
	return Success();
}

} // namespace

// ── Écriture ──

Status WriteDatabase(const DocumentView &view, sql::Database &db) {
	if (!db.TableNames().empty())
		return Failure(ErrorCode::InvalidValue, "la base de destination n'est pas vide");
	const bool created = db.CreateTable("meta", MakeSchema({{"key", ColumnType::Text, true}, {"value", ColumnType::Text}})) &&
						 db.CreateTable("nodes", MakeSchema({{"id", ColumnType::Int, true},
															 {"type", ColumnType::Text},
															 {"parent", ColumnType::Int, false, true},
															 {"position", ColumnType::Int},
															 {"generated", ColumnType::Int}})) &&
						 db.CreateTable("properties", MakeSchema({{"id", ColumnType::Int, true},
																  {"node", ColumnType::Int, false, true},
																  {"name", ColumnType::Text},
																  {"kind", ColumnType::Int},
																  {"ival", ColumnType::Int},
																  {"rval", ColumnType::Real},
																  {"tval", ColumnType::Text}})) &&
						 db.CreateTable("memberships", MakeSchema({{"id", ColumnType::Int, true},
																   {"node", ColumnType::Int, false, true},
																   {"grp", ColumnType::Int, false, true},
																   {"position", ColumnType::Int},
																   {"member_position", ColumnType::Int},
																   {"automatic", ColumnType::Int}})) &&
						 db.CreateTable("resources", MakeSchema({{"id", ColumnType::Int, true},
																 {"kind", ColumnType::Text},
																 {"width", ColumnType::Int},
																 {"height", ColumnType::Int},
																 {"data", ColumnType::Blob}}));
	if (!created)
		return Failure(ErrorCode::Io, "création des tables impossible");
	sql::Table &meta = *db.GetTable("meta");
	sql::Table &nodes = *db.GetTable("nodes");
	sql::Table &properties = *db.GetTable("properties");
	sql::Table &memberships = *db.GetTable("memberships");
	sql::Table &resources = *db.GetTable("resources");

	const auto put = [&](std::string_view k, std::string v) { meta.Upsert(Row{Value(k), Value(std::move(v))}); };
	put("format", std::string(kFormatName));
	put("version", std::to_string(kSchemaVersion));
	put("next_id", std::to_string(view.NextId()));
	put("resolution", view.Resolution() == GroupResolution::PriorityFirst ? "priority" : "proximity");

	int64_t propertyRow = 1, membershipRow = 1;
	ResourceId maxResource = 0;
	std::string error;
	bool ok = true;
	view.Visit(view.Root(), [&](const Node &n) {
		const Node *parent = view.Find(n.Parent());
		const int64_t position = parent ? int64_t(parent->IndexOfChild(n.Id())) : 0;
		ok = ok && nodes.Insert(Row{Value(n.Id()), Value(NodeTypeName(n.Type())), Value(n.Parent()), Value(position), Value(n.Generated())}, &error);
		for (const auto &[id, value] : n.Properties()) {
			Row row{Value(propertyRow++), Value(n.Id()), Value(Info(id).name), Value(int64_t(TypeOf(value))), Value(), Value(), Value()};
			std::visit(Overloaded{[&](bool b) { row[4] = Value(b); }, [&](int64_t i) { row[4] = Value(i); },
								  [&](double d) { row[5] = Value(d); }, [&](const Color &c) { row[4] = Value(int64_t(PackColor(c))); },
								  [&](const std::string &s) { row[6] = Value(s); }},
					   value);
			ok = ok && properties.Insert(row, &error);
		}
		const auto groups = n.Groups();
		for (size_t gi = 0; gi < groups.size(); ++gi) {
			const Node *g = view.Find(groups[gi].group);
			const auto members = g ? g->Members() : std::span<const NodeId>{};
			const auto mi = int64_t(std::ranges::find(members, n.Id()) - members.begin());
			ok = ok && memberships.Insert(Row{Value(membershipRow++), Value(n.Id()), Value(groups[gi].group), Value(int64_t(gi)), Value(mi),
											  Value(groups[gi].automatic)},
										  &error);
		}
	});
	for (ResourceId id : view.Resources()) {
		const ImageResource &img = *view.FindResource(id);
		maxResource = std::max(maxResource, id);
		ok = ok && resources.Insert(Row{Value(id), Value(kRgba8), Value(img.width), Value(img.height), Value(archive::ZlibCompress(img.rgba))},
									&error);
	}
	put("next_resource", std::to_string(maxResource + 1));
	if (!ok)
		return Failure(ErrorCode::Io, std::format("écriture de la base : {}", error));
	return Success();
}

// ── Lecture ──

std::span<const Migration> BuiltinMigrations() noexcept { return {}; }

/// Reconstruit un document depuis les tables, avec contrôle d'intégrité.
class DocumentBuilder {
public:
	static DocResult<Document> Build(const sql::Database &db, const std::map<std::string, std::string, std::less<>> &meta) {
		Document d{Document::Empty{}};
		auto nodes = Columns::Of(db, "nodes", {"id", "type", "parent", "position", "generated"});
		if (!nodes)
			return Err(nodes.Error());
		struct Placement {
			NodeId parent;
			int64_t position;
			NodeId id;
		};
		std::vector<Placement> placements;
		NodeId maxId = 0;
		std::optional<std::string> error;
		nodes->table->Scan([&](sql::RowId r) {
			const int64_t id = nodes->Get(r, 0).AsInt(), parent = nodes->Get(r, 2).AsInt();
			const auto type = FindNodeType(nodes->Get(r, 1).AsText());
			if (id <= 0 || parent < 0 || !type) {
				error = std::format("nœud invalide (id {}, type « {} »)", id, nodes->Get(r, 1).AsText());
				return false;
			}
			auto node = std::make_shared<Node>(NodeId(id), *type);
			node->m_parent = NodeId(parent);
			node->m_generated = nodes->Get(r, 4).AsBool();
			node->m_revision = 1;
			d.m_nodes.InsertOrAssign(NodeId(id), std::move(node));
			placements.push_back({NodeId(parent), nodes->Get(r, 3).AsInt(), NodeId(id)});
			maxId = std::max(maxId, NodeId(id));
			return true;
		});
		if (error)
			return Corrupt(*error);

		// Arborescence : enfants ordonnés par position (puis identifiant), parents et types contrôlés.
		std::ranges::sort(placements, [](const Placement &a, const Placement &b) {
			return std::tie(a.parent, a.position, a.id) < std::tie(b.parent, b.position, b.id);
		});
		for (const Placement &p : placements) {
			if (p.parent == kNoNode) {
				if (d.m_root != kNoNode)
					return Corrupt("plusieurs racines");
				d.m_root = p.id;
				continue;
			}
			std::shared_ptr<Node> *parent = d.m_nodes.Find(p.parent);
			if (!parent)
				return Corrupt(std::format("nœud {} : parent {} absent", p.id, p.parent));
			const NodeType childType = d.Find(p.id)->Type();
			if (!CanContain((*parent)->Type(), childType))
				return Corrupt(std::format("{} {} ne peut pas contenir {} {}", NodeTypeName((*parent)->Type()), p.parent,
										   NodeTypeName(childType), p.id));
			if (TypeInfo(childType).uniqueInParent && d.ChildOfType(p.parent, childType) != kNoNode)
				return Corrupt(std::format("{} {} : {} en double", NodeTypeName((*parent)->Type()), p.parent, NodeTypeName(childType)));
			(*parent)->m_children.push_back(p.id);
		}
		if (d.m_root == kNoNode || d.Find(d.m_root)->Type() != NodeType::Document)
			return Corrupt("racine Document absente");
		size_t reached = 0;
		d.Visit(d.m_root, [&](const Node &) { ++reached; });
		if (reached != d.m_nodes.Size())
			return Corrupt(std::format("{} nœud(s) hors de l'arborescence (cycle ou orphelin)", d.m_nodes.Size() - reached));
		d.m_groupsRoot = d.ChildOfType(d.m_root, NodeType::Groups);
		if (d.m_groupsRoot == kNoNode)
			return Corrupt("conteneur des groupes absent");
		const auto sections = d.Sections();
		if (sections.empty())
			return Corrupt("aucune section");
		for (NodeId s : sections)
			if (d.Body(s) == kNoNode)
				return Corrupt(std::format("section {} sans corps", s));

		// Ressources.
		auto resources = Columns::Of(db, "resources", {"id", "kind", "width", "height", "data"});
		if (!resources)
			return Err(resources.Error());
		ResourceId maxResource = 0;
		resources->table->Scan([&](sql::RowId r) {
			const int64_t id = resources->Get(r, 0).AsInt(), w = resources->Get(r, 2).AsInt(), h = resources->Get(r, 3).AsInt();
			if (id <= 0 || resources->Get(r, 1).AsText() != kRgba8 || w <= 0 || h <= 0 || w > 65535 || h > 65535) {
				error = std::format("ressource {} invalide", id);
				return false;
			}
			const size_t expected = size_t(w) * size_t(h) * 4;
			auto pixels = archive::ZlibDecompress(resources->Get(r, 4).AsBlob(), expected);
			if (!pixels || pixels.Value().size() != expected) {
				error = std::format("ressource {} : pixels illisibles", id);
				return false;
			}
			d.m_resources.InsertOrAssign(ResourceId(id), std::make_shared<const ImageResource>(
															 ImageResource{uint32_t(w), uint32_t(h), std::move(pixels.Value())}));
			maxResource = std::max(maxResource, ResourceId(id));
			return true;
		});
		if (error)
			return Corrupt(*error);

		// Propriétés.
		auto props = Columns::Of(db, "properties", {"node", "name", "kind", "ival", "rval", "tval"});
		if (!props)
			return Err(props.Error());
		props->table->Scan([&](sql::RowId r) {
			const NodeId nodeId = NodeId(props->Get(r, 0).AsInt());
			std::shared_ptr<Node> *node = d.m_nodes.Find(nodeId);
			const auto id = FindProperty(props->Get(r, 1).AsText());
			if (!node || !id) {
				error = std::format("propriété « {} » du nœud {} : inconnue ou orpheline", props->Get(r, 1).AsText(), nodeId);
				return false;
			}
			const PropertyInfo &info = Info(*id);
			if (props->Get(r, 2).AsInt() != int64_t(info.type)) {
				error = std::format("propriété {} du nœud {} : type incohérent", info.name, nodeId);
				return false;
			}
			PropertyValue value;
			switch (info.type) {
			case PropertyType::Bool: value = props->Get(r, 3).AsBool(); break;
			case PropertyType::Int: value = props->Get(r, 3).AsInt(); break;
			case PropertyType::Real: value = props->Get(r, 4).AsReal(); break;
			case PropertyType::Rgba: value = UnpackColor(uint32_t(props->Get(r, 3).AsInt())); break;
			case PropertyType::Text: value = std::string(props->Get(r, 5).AsText()); break;
			}
			const NodeType type = (*node)->Type();
			if (auto valid = Validate(*id, value); !valid) {
				error = std::format("nœud {} : {}", nodeId, valid.Error().message);
				return false;
			}
			if (!AppliesTo(*id, type) && type != NodeType::Group && type != NodeType::Document) {
				error = std::format("propriété {} inapplicable à {} {}", info.name, NodeTypeName(type), nodeId);
				return false;
			}
			if ((*node)->m_properties.Contains(*id)) {
				error = std::format("propriété {} du nœud {} en double", info.name, nodeId);
				return false;
			}
			(*node)->m_properties.Set(*id, std::move(value));
			return true;
		});
		if (error)
			return Corrupt(*error);

		// Appartenances (ordre des groupes d'un nœud et des membres d'un groupe restitué).
		auto links = Columns::Of(db, "memberships", {"node", "grp", "position", "member_position", "automatic"});
		if (!links)
			return Err(links.Error());
		struct Link {
			NodeId node, group;
			int64_t position, memberPosition;
			bool automatic;
		};
		std::vector<Link> all;
		links->table->Scan([&](sql::RowId r) {
			all.push_back({NodeId(links->Get(r, 0).AsInt()), NodeId(links->Get(r, 1).AsInt()), links->Get(r, 2).AsInt(), links->Get(r, 3).AsInt(),
						   links->Get(r, 4).AsBool()});
			return true;
		});
		for (const Link &l : all) {
			const Node *n = d.Find(l.node), *g = d.Find(l.group);
			if (!n || !g || g->Type() != NodeType::Group || n->Type() == NodeType::Document || n->Type() == NodeType::Groups ||
				n->Type() == NodeType::Group)
				return Corrupt(std::format("appartenance invalide {} → {}", l.node, l.group));
		}
		std::ranges::sort(all, [](const Link &a, const Link &b) { return std::tie(a.node, a.position, a.group) < std::tie(b.node, b.position, b.group); });
		for (const Link &l : all) {
			Node &n = **d.m_nodes.Find(l.node);
			if (n.InGroup(l.group))
				return Corrupt(std::format("appartenance {} → {} en double", l.node, l.group));
			n.m_groups.push_back({l.group, l.automatic});
		}
		std::ranges::sort(all, [](const Link &a, const Link &b) {
			return std::tie(a.group, a.memberPosition, a.node) < std::tie(b.group, b.memberPosition, b.node);
		});
		for (const Link &l : all)
			(*d.m_nodes.Find(l.group))->m_members.push_back(l.node);

		// Références croisées portées par des propriétés.
		for (const auto &[id, node] : d.m_nodes) {
			if (const auto *r = node->m_properties.Find(PropertyId::Resource))
				if (const int64_t res = std::get<int64_t>(*r); res != 0 && !d.FindResource(ResourceId(res)))
					return Corrupt(std::format("image {} : ressource {} absente", id, res));
			if (const auto *pg = node->m_properties.Find(PropertyId::PageGroup)) {
				const Node *g = d.Find(NodeId(std::get<int64_t>(*pg)));
				if (!g || g->Type() != NodeType::Group)
					return Corrupt(std::format("section {} : groupe de pages invalide", id));
			}
		}

		// Compteurs : jamais en deçà des identifiants présents (un identifiant n'est jamais réutilisé).
		const auto meta64 = [&](std::string_view key) -> int64_t {
			const auto it = meta.find(key);
			return it == meta.end() ? 0 : ParseInt(it->second).value_or(0);
		};
		d.m_nextId = std::max<NodeId>(maxId + 1, NodeId(std::max<int64_t>(0, meta64("next_id"))));
		d.m_nextResource = std::max<ResourceId>(maxResource + 1, ResourceId(std::max<int64_t>(0, meta64("next_resource"))));
		const auto res = meta.find("resolution");
		d.m_resolution = res != meta.end() && res->second == "proximity" ? GroupResolution::ProximityFirst : GroupResolution::PriorityFirst;
		d.m_revision = 1;
		d.m_styleRevision = 1;
		return Ok(std::move(d));
	}
};

DocResult<Document> ReadDatabase(sql::Database &db, std::span<const Migration> migrations, int targetVersion) {
	auto meta = ReadMeta(db);
	const auto format = meta.find("format");
	if (format == meta.end() || format->second != kFormatName)
		return Corrupt("ce fichier n'est pas un document .docdb");
	const auto versionText = meta.find("version");
	const auto parsed = versionText == meta.end() ? std::nullopt : ParseInt(versionText->second);
	if (!parsed || *parsed < 0)
		return Corrupt("version du schéma absente ou illisible");
	int version = int(*parsed);
	if (version > targetVersion)
		return Failure(ErrorCode::Unsupported,
					   std::format("document au format {} : ce moteur lit jusqu'au format {}", version, targetVersion));
	while (version < targetVersion) {
		const auto it = std::ranges::find(migrations, version, &Migration::from);
		if (it == migrations.end() || !it->apply)
			return Failure(ErrorCode::Unsupported, std::format("aucune migration depuis le format {}", version));
		if (auto applied = it->apply(db); !applied)
			return Err(applied.Error());
		++version;
		if (auto s = SetMeta(db, "version", std::to_string(version)); !s)
			return Err(s.Error());
	}
	meta = ReadMeta(db);
	return DocumentBuilder::Build(db, meta);
}

// ── Fichiers ──

Status SaveDocument(const DocumentView &view, const std::filesystem::path &path) {
	sql::Database db;
	if (auto s = WriteDatabase(view, db); !s)
		return s;
	if (!db.SaveSnapshot(path.string()))
		return Failure(ErrorCode::Io, std::format("écriture de {} impossible", path.string()));
	return Success();
}

DocResult<Document> LoadDocument(const std::filesystem::path &path) {
	sql::Database db;
	if (auto loaded = db.LoadSnapshot(path.string()); !loaded) {
		const bool missing = !std::filesystem::exists(path);
		return Failure(missing ? ErrorCode::Io : ErrorCode::Corrupt, loaded.Error());
	}
	return ReadDatabase(db);
}

} // namespace doc
