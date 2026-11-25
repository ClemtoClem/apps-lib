#include "sandbox/document/document.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <dataio/documents/json.hpp>

#include "sandbox/document/scene_tree.hpp"
#include "sandbox/modeling/io.hpp"
#include "sandbox/modeling/operations.hpp"

namespace sandbox {

namespace {

/// Révisions uniques pour tout le programme : un document restauré (annulation) n'a jamais la
/// même révision qu'un état déjà affiché.
uint64_t NextRevision() {
	static std::atomic<uint64_t> counter{1};
	return ++counter;
}

constexpr std::string_view kFormat = "sandbox.scene"; ///< fichiers `.scene` (« sandbox.document » encore lu)
constexpr int kVersion = 1;

dataio::NodePtr Vec3Json(const math::FVector3 &v) {
	dataio::NodePtr a = dataio::Node::MakeArray();
	a->Push(dataio::Node::MakeFloat(v.x));
	a->Push(dataio::Node::MakeFloat(v.y));
	a->Push(dataio::Node::MakeFloat(v.z));
	return a;
}

math::FVector3 JsonVec3(const dataio::NodePtr &j, math::FVector3 fallback = {}) {
	return j->IsArray() && j->Size() == 3 ? math::FVector3{(*j)[0]->AsFloat(), (*j)[1]->AsFloat(), (*j)[2]->AsFloat()} : fallback;
}

dataio::NodePtr MatrixJson(const math::FMatrix4 &m) {
	dataio::NodePtr a = dataio::Node::MakeArray();
	for (int i = 0; i < 16; ++i)
		a->Push(dataio::Node::MakeFloat(m.Data()[i]));
	return a;
}

math::FMatrix4 JsonMatrix(const dataio::NodePtr &j) {
	math::FMatrix4 m = math::FMatrix4::Identity();
	if (j->IsArray() && j->Size() == 16)
		for (int i = 0; i < 16; ++i)
			(&m.m[0][0])[i] = (*j)[size_t(i)]->AsFloat();
	return m;
}

dataio::NodePtr ModelJson(const modeling::Model &model) {
	auto parsed = dataio::ParseJson(modeling::Serialize(model));
	return parsed ? std::move(parsed.Value()) : nullptr;
}

Result<modeling::Model, String> JsonModel(const dataio::NodePtr &j) {
	if (j->IsNone())
		return Ok(modeling::Model{});
	return modeling::Deserialize(String(dataio::WriteJson(j, -1)));
}

constexpr std::string_view kBodyNames[] = {"solid", "dynamic", "ghost"};
constexpr std::string_view kTriggerNames[] = {"start", "touch", "use", "script"};
constexpr std::string_view kLightNames[] = {"point", "spot"};

dataio::NodePtr ColorJson(const modeling::Color &c) {
	dataio::NodePtr a = dataio::Node::MakeArray();
	a->Push(dataio::Node::MakeFloat(c.r));
	a->Push(dataio::Node::MakeFloat(c.g));
	a->Push(dataio::Node::MakeFloat(c.b));
	return a;
}

modeling::Color JsonColor(const dataio::NodePtr &j, const modeling::Color &fallback) {
	return j->IsArray() && j->Size() >= 3 ? modeling::Color{(*j)[0]->AsFloat(), (*j)[1]->AsFloat(), (*j)[2]->AsFloat(), 1.f} : fallback;
}

template <typename E, size_t N> E EnumFrom(std::string_view name, const std::string_view (&names)[N], E fallback) {
	for (size_t i = 0; i < N; ++i)
		if (names[i] == name)
			return E(i);
	return fallback;
}

dataio::NodePtr ParamsJson(const ScriptParams &params) {
	dataio::NodePtr o = dataio::Node::MakeObject();
	for (const ScriptParam &p : params)
		std::visit(
			[&](const auto &v) {
				using V = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<V, String>)
					o->Set(std::string(p.name), dataio::Node::MakeString(std::string(v)));
				else if constexpr (std::is_same_v<V, bool>)
					o->Set(std::string(p.name), dataio::Node::MakeBool(v));
				else
					o->Set(std::string(p.name), dataio::Node::MakeFloat(v));
			},
			p.value);
	return o;
}

ScriptParams JsonParams(const dataio::NodePtr &j) {
	ScriptParams out;
	if (!j->IsObject())
		return out;
	for (const auto &[name, v] : j->AsObject()) {
		if (v->IsBool())
			out.push_back({String(name), bool(v->AsBool())});
		else if (v->IsNumber())
			out.push_back({String(name), v->AsNumber()});
		else if (v->IsString())
			out.push_back({String(name), String(v->AsString())});
	}
	return out;
}

dataio::NodePtr BehaviorJson(const Behavior &b) {
	dataio::NodePtr o = dataio::Node::MakeObject();
	o->Set("body", dataio::Node::MakeString(kBodyNames[size_t(b.body)]));
	o->Set("mass", dataio::Node::MakeFloat(b.mass));
	o->Set("bounce", dataio::Node::MakeFloat(b.bounce));
	o->Set("friction", dataio::Node::MakeFloat(b.friction));
	if (!b.sound.IsEmpty()) {
		o->Set("sound", dataio::Node::MakeString(std::string(b.sound)));
		o->Set("volume", dataio::Node::MakeFloat(b.volume));
		o->Set("loop", dataio::Node::MakeBool(b.loop));
		o->Set("spatial", dataio::Node::MakeBool(b.spatial));
		o->Set("soundTrigger", dataio::Node::MakeString(kTriggerNames[size_t(b.soundTrigger)]));
	}
	if (b.light) {
		dataio::NodePtr c = dataio::Node::MakeArray();
		c->Push(dataio::Node::MakeFloat(b.lightColor.r));
		c->Push(dataio::Node::MakeFloat(b.lightColor.g));
		c->Push(dataio::Node::MakeFloat(b.lightColor.b));
		o->Set("light", dataio::Node::MakeBool(true));
		o->Set("lightColor", std::move(c));
		o->Set("lightIntensity", dataio::Node::MakeFloat(b.lightIntensity));
		o->Set("lightRange", dataio::Node::MakeFloat(b.lightRange));
		if (b.lightKind == LightKind::Spot) {
			o->Set("lightKind", dataio::Node::MakeString(std::string(kLightNames[size_t(b.lightKind)])));
			o->Set("spotAngle", dataio::Node::MakeFloat(b.spotAngle));
			o->Set("penumbra", dataio::Node::MakeFloat(b.penumbra));
		}
	}
	if (!b.triggerEvent.IsEmpty())
		o->Set("triggerEvent", dataio::Node::MakeString(std::string(b.triggerEvent)));
	if (b.triggerOnce)
		o->Set("triggerOnce", dataio::Node::MakeBool(true));
	if (!b.params.empty())
		o->Set("params", ParamsJson(b.params));
	if (!b.scriptFile.IsEmpty())
		o->Set("scriptFile", dataio::Node::MakeString(std::string(b.scriptFile))); // contenu dans son fichier
	else if (!b.script.IsEmpty())
		o->Set("script", dataio::Node::MakeString(std::string(b.script)));
	return o;
}

Behavior JsonBehavior(const dataio::NodePtr &j) {
	Behavior b;
	if (!j->IsObject())
		return b;
	b.body = EnumFrom((*j)["body"]->AsString(), kBodyNames, BodyKind::Solid);
	b.mass = (*j)["mass"]->AsFloat(b.mass);
	b.bounce = (*j)["bounce"]->AsFloat(b.bounce);
	b.friction = (*j)["friction"]->AsFloat(b.friction);
	b.sound = String((*j)["sound"]->AsString());
	b.volume = (*j)["volume"]->AsFloat(b.volume);
	b.loop = (*j)["loop"]->AsBool(b.loop);
	b.spatial = (*j)["spatial"]->AsBool(b.spatial);
	b.soundTrigger = EnumFrom((*j)["soundTrigger"]->AsString(), kTriggerNames, SoundTrigger::Start);
	b.light = (*j)["light"]->AsBool(false);
	if (const dataio::NodePtr c = (*j)["lightColor"]; c->IsArray() && c->Size() == 3)
		b.lightColor = {(*c)[0]->AsFloat(), (*c)[1]->AsFloat(), (*c)[2]->AsFloat(), 1.f};
	b.lightIntensity = (*j)["lightIntensity"]->AsFloat(b.lightIntensity);
	b.lightRange = (*j)["lightRange"]->AsFloat(b.lightRange);
	b.lightKind = EnumFrom((*j)["lightKind"]->AsString(), kLightNames, LightKind::Point);
	b.spotAngle = (*j)["spotAngle"]->AsFloat(b.spotAngle);
	b.penumbra = (*j)["penumbra"]->AsFloat(b.penumbra);
	b.script = String((*j)["script"]->AsString());
	b.scriptFile = String((*j)["scriptFile"]->AsString());
	b.triggerEvent = String((*j)["triggerEvent"]->AsString());
	b.triggerOnce = (*j)["triggerOnce"]->AsBool(false);
	b.params = JsonParams((*j)["params"]);
	return b;
}

math::FAABB Empty() { return {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}}; }

void Grow(math::FAABB &box, const math::FVector3 &p) {
	box.min = math::Min(box.min, p);
	box.max = math::Max(box.max, p);
}

} // namespace

Document::Document() : m_revision(NextRevision()) {
	m_tags.push_back({"Sans balise", true});
	// Palette de départ : couleurs franches et matières courantes.
	static constexpr struct {
		const char *name;
		modeling::Color color;
	} kPalette[] = {
		{"Blanc cassé", {0.93f, 0.92f, 0.88f, 1.f}}, {"Brique", {0.66f, 0.30f, 0.22f, 1.f}},	{"Bois clair", {0.78f, 0.62f, 0.42f, 1.f}},
		{"Ardoise", {0.30f, 0.33f, 0.38f, 1.f}},	 {"Béton", {0.62f, 0.62f, 0.60f, 1.f}},		{"Herbe", {0.36f, 0.56f, 0.28f, 1.f}},
		{"Eau", {0.25f, 0.52f, 0.78f, 1.f}},		 {"Verre", {0.70f, 0.86f, 0.95f, 0.45f}},	{"Cuivre", {0.78f, 0.47f, 0.30f, 1.f}},
		{"Jaune", {0.95f, 0.80f, 0.25f, 1.f}},		 {"Rouge", {0.85f, 0.22f, 0.22f, 1.f}},		{"Bleu", {0.22f, 0.42f, 0.85f, 1.f}},
	};
	for (const auto &p : kPalette)
		m_materials.push_back({p.name, p.color, 1.f});
	SyncMaterials();
}

void Document::Touch() noexcept { m_revision = NextRevision(); }

Entity *Document::Find(EntityId id) noexcept {
	const auto it = std::ranges::find(m_entities, id, &Entity::id);
	return it == m_entities.end() ? nullptr : &*it;
}

const Entity *Document::Find(EntityId id) const noexcept { return const_cast<Document *>(this)->Find(id); }

modeling::Model *Document::ModelOf(EntityId id) noexcept {
	if (id == kRootId)
		return &m_root;
	Entity *e = Find(id);
	return e ? &e->model : nullptr;
}

math::FMatrix4 Document::TransformOf(EntityId id) const noexcept {
	const Entity *e = id == kRootId ? nullptr : Find(id);
	return e ? e->transform : math::FMatrix4::Identity();
}

Entity &Document::AddEntity(String name, modeling::Model model, const math::FMatrix4 &transform) {
	Entity e;
	e.id = m_nextId++;
	e.name = name.IsEmpty() ? UniqueName("Groupe") : std::move(name);
	e.model = std::move(model);
	e.model.Materials() = m_materials;
	e.transform = transform;
	m_entities.push_back(std::move(e));
	Touch();
	return m_entities.back();
}

bool Document::RemoveEntity(EntityId id) {
	if (!Find(id))
		return false;
	std::vector<EntityId> gone = Descendants(id); // le groupe part avec ses descendants
	gone.push_back(id);
	std::erase_if(m_entities, [&](const Entity &e) { return std::ranges::find(gone, e.id) != gone.end(); });
	Touch();
	return true;
}

std::vector<EntityId> Document::Children(EntityId parent) const {
	std::vector<EntityId> out;
	for (const Entity &e : m_entities)
		if (e.parent == parent)
			out.push_back(e.id);
	return out;
}

std::vector<EntityId> Document::Descendants(EntityId id) const {
	std::vector<EntityId> out;
	for (const EntityId child : Children(id)) {
		out.push_back(child);
		const std::vector<EntityId> below = Descendants(child);
		out.insert(out.end(), below.begin(), below.end());
	}
	return out;
}

bool Document::IsAncestor(EntityId ancestor, EntityId id) const noexcept {
	if (ancestor == 0)
		return id != 0;
	int guard = 0;
	for (const Entity *e = Find(id); e && e->parent != 0 && guard < 1024; e = Find(e->parent), ++guard)
		if (e->parent == ancestor)
			return true;
	return false;
}

EntityId Document::TopUnder(EntityId id, EntityId context) const noexcept {
	int guard = 0;
	for (const Entity *e = Find(id); e && guard < 1024; e = Find(e->parent), ++guard)
		if (e->parent == context)
			return e->id;
	return 0;
}

bool Document::Reparent(EntityId id, EntityId parent) {
	Entity *e = Find(id);
	if (!e || id == parent || (parent != 0 && (!Find(parent) || IsAncestor(id, parent))))
		return false; // un groupe ne peut pas devenir enfant de lui-même ou de son descendant
	e->parent = parent;
	Touch();
	return true;
}

void Document::SetTransform(EntityId id, const math::FMatrix4 &world) {
	Entity *e = Find(id);
	if (!e)
		return;
	const math::FMatrix4 delta = world * e->transform.Inverse();
	e->transform = world;
	for (const EntityId d : Descendants(id))
		if (Entity *child = Find(d))
			child->transform = delta * child->transform;
	Touch();
}

EntityId Document::GroupEntities(std::span<const EntityId> ids, String name) {
	std::vector<EntityId> members;
	math::FAABB box = Empty();
	for (const EntityId id : ids)
		if (const Entity *e = Find(id); e && std::ranges::none_of(ids, [&](EntityId other) { return other != id && IsAncestor(other, id); })) {
			members.push_back(id); // un descendant d'un groupe choisi suit déjà son ancêtre
			const math::FAABB b = WorldBounds(*e);
			if (b.min.x <= b.max.x) {
				Grow(box, b.min);
				Grow(box, b.max);
			}
		}
	if (members.empty())
		return 0;
	// Parent commun conservé ; sinon le nouveau groupe est à la racine.
	EntityId parent = Find(members.front())->parent;
	for (const EntityId id : members)
		if (Find(id)->parent != parent)
			parent = 0;
	const math::FVector3 origin = box.min.x <= box.max.x ? math::FVector3{(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f}
														 : math::FVector3{};
	Entity &group = AddEntity(name.IsEmpty() ? UniqueName("Groupe") : std::move(name), modeling::Model{}, math::FMatrix4::Translation(origin));
	group.parent = parent;
	const EntityId gid = group.id;
	for (const EntityId id : members)
		(void)Reparent(id, gid);
	return gid;
}

bool Document::IsLocked(const Entity &e) const noexcept {
	int guard = 0;
	for (const Entity *x = &e; x && guard < 1024; x = Find(x->parent), ++guard)
		if (x->locked)
			return true;
	return false;
}

EntityId Document::MakeGroup(const modeling::Selection &selection, String name) {
	if (selection.IsEmpty())
		return 0;
	SyncMaterials();
	modeling::Model group = modeling::ExtractFaces(m_root, selection.faces);
	for (modeling::EdgeId e : selection.edges) // arêtes libres sélectionnées (non bordées par une face prise)
		if (m_root.IsEdgeAlive(e))
			(void)group.AddEdge(m_root.Position(m_root.Edges()[e].a), m_root.Position(m_root.Edges()[e].b));
	if (group.IsEmpty())
		return 0;
	// Origine du groupe : coin bas de sa boîte, pour qu'il tourne et s'échelonne autour de lui.
	const math::FAABB box = group.Bounds();
	const math::FVector3 origin{box.min.x, box.min.y, box.min.z};
	group.Transform(math::FMatrix4::Translation(-origin));
	// Les arêtes de bord des faces emportées partent avec elles, sauf si une face restante s'y appuie.
	std::vector<modeling::EdgeId> borders;
	for (modeling::FaceId f : selection.faces)
		if (m_root.IsAlive(f))
			for (modeling::EdgeId e : m_root.EdgesOfFace(f))
				borders.push_back(e);
	modeling::Erase(m_root, selection.edges, selection.faces);
	for (modeling::EdgeId e : borders)
		if (m_root.IsEdgeAlive(e) && m_root.FacesOfEdge(e).empty())
			m_root.RemoveEdge(e);
	m_root.RemoveOrphanVertices();
	return AddEntity(std::move(name), std::move(group), math::FMatrix4::Translation(origin)).id;
}

bool Document::Explode(EntityId id) {
	Entity *e = Find(id);
	if (!e)
		return false;
	SyncMaterials();
	// Géométrie : dans le modèle du parent (repère local), sinon dans la géométrie libre ;
	// les enfants passent au parent.
	const EntityId parent = e->parent;
	if (Entity *p = Find(parent))
		(void)p->model.Append(e->model, p->transform.Inverse() * e->transform);
	else
		(void)m_root.Append(e->model, e->transform);
	for (const EntityId child : Children(id))
		Find(child)->parent = parent;
	return RemoveEntity(id);
}

EntityId Document::Duplicate(EntityId id, const math::FVector3 &offset) {
	const Entity *e = Find(id);
	if (!e)
		return 0;
	Entity copy = *e; // `e` peut être invalidé par l'ajout
	const std::vector<EntityId> children = Children(id);
	Entity &added = AddEntity(UniqueName(std::string_view(copy.name)), std::move(copy.model), math::FMatrix4::Translation(offset) * copy.transform);
	added.tag = copy.tag;
	added.behavior = copy.behavior;
	added.component = copy.component; // copie d'une instance : nouvelle instance du composant
	added.parent = copy.parent;
	added.type = copy.type;
	const EntityId newId = added.id;
	for (const EntityId child : children) // sous-arbre copié, mêmes noms
		if (const EntityId c = Duplicate(child, offset)) {
			Find(c)->parent = newId;
			Find(c)->name = Find(child)->name;
		}
	return newId;
}

ComponentId Document::MakeComponent(EntityId id) {
	Entity *e = Find(id);
	if (!e)
		return 0;
	if (e->component)
		return e->component;
	ComponentId next = 1;
	for (const ComponentDef &c : m_components)
		next = std::max(next, c.id + 1);
	m_components.push_back({next, e->name});
	e->component = next;
	Touch();
	return next;
}

bool Document::MakeUnique(EntityId id) {
	Entity *e = Find(id);
	if (!e || !e->component)
		return false;
	const ComponentId was = e->component;
	e->component = 0;
	if (InstanceCount(was) == 0) // plus d'instance : définition retirée
		std::erase_if(m_components, [was](const ComponentDef &c) { return c.id == was; });
	Touch();
	return true;
}

const ComponentDef *Document::FindComponent(ComponentId id) const noexcept {
	const auto it = std::ranges::find(m_components, id, &ComponentDef::id);
	return it == m_components.end() ? nullptr : &*it;
}

size_t Document::InstanceCount(ComponentId id) const noexcept {
	return id == 0 ? 0 : size_t(std::ranges::count(m_entities, id, &Entity::component));
}

bool Document::SyncInstances(EntityId source) {
	const Entity *src = Find(source);
	if (!src || !src->component)
		return false;
	bool changed = false;
	for (Entity &e : m_entities)
		if (e.component == src->component && e.id != source) {
			e.model = src->model;
			changed = true;
		}
	if (changed)
		Touch(); // nouvelle révision : les maillages des instances sont reconstruits
	return changed;
}

math::FAABB Document::WorldBounds(const Entity &e) const {
	math::FAABB box = Empty();
	const math::FAABB local = e.model.Bounds();
	if (local.min.x > local.max.x)
		return box;
	for (int i = 0; i < 8; ++i)
		Grow(box, e.transform.TransformPoint({i & 1 ? local.max.x : local.min.x, i & 2 ? local.max.y : local.min.y, i & 4 ? local.max.z : local.min.z}));
	return box;
}

math::FAABB Document::Bounds() const {
	math::FAABB box = Empty();
	if (const math::FAABB r = m_root.Bounds(); r.min.x <= r.max.x) {
		Grow(box, r.min);
		Grow(box, r.max);
	}
	for (const Entity &e : m_entities)
		if (IsVisible(e))
			if (const math::FAABB b = WorldBounds(e); b.min.x <= b.max.x) {
				Grow(box, b.min);
				Grow(box, b.max);
			}
	return box;
}

String Document::UniqueName(std::string_view base) const {
	// « Groupe 2 » → base « Groupe ».
	std::string stem(base);
	if (const size_t space = stem.rfind(' '); space != std::string::npos && stem.find_first_not_of("0123456789", space + 1) == std::string::npos)
		stem.resize(space);
	for (int n = 1;; ++n) {
		String candidate = n == 1 ? String(stem) : String::Format("{} {}", stem, n);
		if (std::ranges::none_of(m_entities, [&](const Entity &e) { return e.name == candidate; }))
			return candidate;
	}
}

uint32_t Document::AddMaterial(String name, modeling::Color color) {
	m_materials.push_back({std::move(name), color, 1.f});
	SyncMaterials();
	Touch();
	return uint32_t(m_materials.size() - 1);
}

void Document::SetMaterial(uint32_t index, const modeling::Material &material) {
	if (index >= m_materials.size())
		return;
	m_materials[index] = material;
	SyncMaterials();
	Touch();
}

void Document::SetMaterialColor(uint32_t index, modeling::Color color) {
	if (index >= m_materials.size())
		return;
	m_materials[index].color = color;
	SyncMaterials();
	Touch();
}

void Document::SyncMaterials() {
	m_root.Materials() = m_materials;
	for (Entity &e : m_entities)
		e.model.Materials() = m_materials;
}

bool Document::IsVisible(const Entity &e) const noexcept {
	int guard = 0;
	for (const Entity *x = &e; x && guard < 1024; x = Find(x->parent), ++guard) // un parent masqué masque ses descendants
		if (!x->visible || (x->tag < m_tags.size() && !m_tags[x->tag].visible))
			return false;
	return true;
}

String Document::ToJson() const {
	dataio::NodePtr doc = dataio::Node::MakeObject();
	doc->Set("format", dataio::Node::MakeString(kFormat));
	doc->Set("version", dataio::Node::MakeInt(kVersion));
	dataio::NodePtr materials = dataio::Node::MakeArray();
	for (const modeling::Material &m : m_materials) {
		dataio::NodePtr color = dataio::Node::MakeArray();
		color->Push(dataio::Node::MakeFloat(m.color.r));
		color->Push(dataio::Node::MakeFloat(m.color.g));
		color->Push(dataio::Node::MakeFloat(m.color.b));
		color->Push(dataio::Node::MakeFloat(m.color.a));
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("name", dataio::Node::MakeString(std::string(m.name)));
		o->Set("color", std::move(color));
		o->Set("textureScale", dataio::Node::MakeFloat(m.textureScale));
		if (m.kind != modeling::MaterialKind::Plastic)
			o->Set("kind", dataio::Node::MakeString(std::string(modeling::MaterialKindName(m.kind))));
		if (m.roughness != 0.85f)
			o->Set("roughness", dataio::Node::MakeFloat(m.roughness));
		if (m.kind == modeling::MaterialKind::Emissive)
			o->Set("strength", dataio::Node::MakeFloat(m.strength));
		materials->Push(std::move(o));
	}
	doc->Set("materials", std::move(materials));
	dataio::NodePtr tags = dataio::Node::MakeArray();
	for (const Tag &t : m_tags) {
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("name", dataio::Node::MakeString(std::string(t.name)));
		o->Set("visible", dataio::Node::MakeBool(t.visible));
		tags->Push(std::move(o));
	}
	doc->Set("tags", std::move(tags));
	dataio::NodePtr views = dataio::Node::MakeArray();
	for (const SceneView &v : m_views) {
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("name", dataio::Node::MakeString(std::string(v.name)));
		o->Set("eye", Vec3Json(v.eye));
		o->Set("target", Vec3Json(v.target));
		o->Set("fov", dataio::Node::MakeFloat(v.fovDegrees));
		o->Set("orthographic", dataio::Node::MakeBool(v.orthographic));
		o->Set("orthoHeight", dataio::Node::MakeFloat(v.orthoHeight));
		views->Push(std::move(o));
	}
	doc->Set("views", std::move(views));
	doc->Set("root", ModelJson(m_root));
	dataio::NodePtr entities = dataio::Node::MakeArray();
	for (const Entity &e : m_entities) {
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("id", dataio::Node::MakeInt(int64_t(e.id)));
		o->Set("name", dataio::Node::MakeString(std::string(e.name)));
		if (e.parent)
			o->Set("parent", dataio::Node::MakeInt(int64_t(e.parent)));
		if (std::string_view(e.type) != "Node3D")
			o->Set("type", dataio::Node::MakeString(std::string(e.type)));
		o->Set("transform", MatrixJson(e.transform));
		o->Set("tag", dataio::Node::MakeInt(int64_t(e.tag)));
		o->Set("visible", dataio::Node::MakeBool(e.visible));
		o->Set("locked", dataio::Node::MakeBool(e.locked));
		if (e.component)
			o->Set("component", dataio::Node::MakeInt(int64_t(e.component))); // géométrie dans « components »
		else
			o->Set("model", ModelJson(e.model));
		if (e.behavior != Behavior{})
			o->Set("behavior", BehaviorJson(e.behavior));
		entities->Push(std::move(o));
	}
	doc->Set("entities", std::move(entities));
	dataio::NodePtr components = dataio::Node::MakeArray();
	for (const ComponentDef &c : m_components) {
		const auto it = std::ranges::find(m_entities, c.id, &Entity::component);
		if (it == m_entities.end())
			continue; // définition sans instance
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("id", dataio::Node::MakeInt(int64_t(c.id)));
		o->Set("name", dataio::Node::MakeString(std::string(c.name)));
		o->Set("model", ModelJson(it->model));
		components->Push(std::move(o));
	}
	if (components->Size() > 0)
		doc->Set("components", std::move(components));
	dataio::NodePtr game = dataio::Node::MakeObject();
	game->Set("gravity", dataio::Node::MakeFloat(m_game.gravity));
	game->Set("walkSpeed", dataio::Node::MakeFloat(m_game.walkSpeed));
	game->Set("runSpeed", dataio::Node::MakeFloat(m_game.runSpeed));
	game->Set("jumpSpeed", dataio::Node::MakeFloat(m_game.jumpSpeed));
	game->Set("eyeHeight", dataio::Node::MakeFloat(m_game.eyeHeight));
	game->Set("stepHeight", dataio::Node::MakeFloat(m_game.stepHeight));
	if (!m_game.params.empty())
		game->Set("params", ParamsJson(m_game.params));
	if (!m_game.scriptFile.IsEmpty())
		game->Set("scriptFile", dataio::Node::MakeString(std::string(m_game.scriptFile)));
	else if (!m_game.script.IsEmpty())
		game->Set("script", dataio::Node::MakeString(std::string(m_game.script)));
	doc->Set("game", std::move(game));
	if (m_environment != EnvironmentSettings{}) {
		const EnvironmentSettings &e = m_environment;
		dataio::NodePtr env = dataio::Node::MakeObject();
		env->Set("zenith", ColorJson(e.zenith));
		env->Set("horizon", ColorJson(e.horizon));
		env->Set("ground", ColorJson(e.ground));
		env->Set("sunColor", ColorJson(e.sunColor));
		env->Set("sunHour", dataio::Node::MakeFloat(e.sunHour));
		env->Set("sunIntensity", dataio::Node::MakeFloat(e.sunIntensity));
		env->Set("ambient", dataio::Node::MakeFloat(e.ambient));
		env->Set("fog", ColorJson(e.fog));
		env->Set("fogDensity", dataio::Node::MakeFloat(e.fogDensity));
		env->Set("exposure", dataio::Node::MakeFloat(e.exposure));
		doc->Set("environment", std::move(env));
	}
	if (!m_overlay.items.empty() || !m_overlay.tilemap.Empty()) {
		static constexpr std::string_view kShapes[] = {"rect", "circle", "line", "text", "image"};
		dataio::NodePtr overlay = dataio::Node::MakeObject();
		dataio::NodePtr ref = dataio::Node::MakeArray();
		ref->Push(dataio::Node::MakeFloat(m_overlay.reference.x));
		ref->Push(dataio::Node::MakeFloat(m_overlay.reference.y));
		overlay->Set("reference", std::move(ref));
		dataio::NodePtr items = dataio::Node::MakeArray();
		for (const Item2D &i : m_overlay.items) {
			dataio::NodePtr o = dataio::Node::MakeObject();
			const auto pair = [](const math::FVector2 &v) {
				dataio::NodePtr a = dataio::Node::MakeArray();
				a->Push(dataio::Node::MakeFloat(v.x));
				a->Push(dataio::Node::MakeFloat(v.y));
				return a;
			};
			o->Set("id", dataio::Node::MakeInt(int64_t(i.id)));
			o->Set("name", dataio::Node::MakeString(std::string(i.name)));
			o->Set("shape", dataio::Node::MakeString(std::string(kShapes[size_t(i.shape)])));
			o->Set("position", pair(i.position));
			o->Set("size", pair(i.size));
			o->Set("fill", dataio::Node::MakeString(std::string(FormatColor(i.fill))));
			o->Set("stroke", dataio::Node::MakeString(std::string(FormatColor(i.stroke))));
			o->Set("strokeWidth", dataio::Node::MakeFloat(i.strokeWidth));
			o->Set("cornerRadius", dataio::Node::MakeFloat(i.cornerRadius));
			if (!i.text.IsEmpty())
				o->Set("text", dataio::Node::MakeString(std::string(i.text)));
			o->Set("fontSize", dataio::Node::MakeFloat(i.fontSize));
			if (!i.image.IsEmpty())
				o->Set("image", dataio::Node::MakeString(std::string(i.image)));
			o->Set("z", dataio::Node::MakeInt(int64_t(i.z)));
			o->Set("visible", dataio::Node::MakeBool(i.visible));
			items->Push(std::move(o));
		}
		overlay->Set("items", std::move(items));
		// Calque de tuiles.
		if (!m_overlay.tilemap.Empty()) {
			dataio::NodePtr tm = dataio::Node::MakeObject();
			dataio::NodePtr origin = dataio::Node::MakeArray();
			origin->Push(dataio::Node::MakeFloat(m_overlay.tilemap.origin.x));
			origin->Push(dataio::Node::MakeFloat(m_overlay.tilemap.origin.y));
			tm->Set("origin", std::move(origin));
			tm->Set("tileSize", dataio::Node::MakeFloat(m_overlay.tilemap.tileSize));
			tm->Set("width", dataio::Node::MakeInt(m_overlay.tilemap.width));
			tm->Set("height", dataio::Node::MakeInt(m_overlay.tilemap.height));
			dataio::NodePtr terrain = dataio::Node::MakeArray();
			for (int8_t t : m_overlay.tilemap.terrain)
				terrain->Push(dataio::Node::MakeInt(int64_t(t)));
			tm->Set("terrain", std::move(terrain));
			dataio::NodePtr palette = dataio::Node::MakeArray();
			for (const modeling::Color &c : m_overlay.tilemap.palette)
				palette->Push(dataio::Node::MakeString(std::string(FormatColor(c))));
			tm->Set("palette", std::move(palette));
			overlay->Set("tilemap", std::move(tm));
		}
		doc->Set("overlay", std::move(overlay));
	}
	return String(dataio::WriteJson(doc, 1));
}

Result<Document, String> Document::FromJson(std::string_view json) {
	auto parsed = dataio::ParseJson(json);
	if (!parsed)
		return Err(String("document : JSON invalide (" + parsed.Error().Format() + ")"));
	const dataio::NodePtr &j = parsed.Value();
	// Ancien nom, ou projet d'un seul fichier (scène complète dans le `.project`) : encore lus.
	if (const std::string_view format = (*j)["format"]->AsString();
		format != kFormat && format != "sandbox.document" && !(format == "sandbox.project" && (*j)["root"]->IsObject()))
		return Err(String::Format("scène : format « {} » attendu", kFormat));
	Document doc;
	if (const dataio::NodePtr &materials = (*j)["materials"]; materials->IsArray() && materials->Size() > 0) {
		doc.m_materials.clear();
		for (const dataio::NodePtr &m : materials->AsArray()) {
			const dataio::NodePtr &c = (*m)["color"];
			modeling::Material mat{String((*m)["name"]->AsString()), {(*c)[0]->AsFloat(0.8f), (*c)[1]->AsFloat(0.8f), (*c)[2]->AsFloat(0.8f), (*c)[3]->AsFloat(1.f)},
								   (*m)["textureScale"]->AsFloat(1.f)};
			mat.kind = modeling::MaterialKindFromName((*m)["kind"]->AsString());
			mat.roughness = (*m)["roughness"]->AsFloat(0.85f);
			mat.strength = (*m)["strength"]->AsFloat(4.f);
			doc.m_materials.push_back(std::move(mat));
		}
	}
	if (const dataio::NodePtr &tags = (*j)["tags"]; tags->IsArray() && tags->Size() > 0) {
		doc.m_tags.clear();
		for (const dataio::NodePtr &t : tags->AsArray())
			doc.m_tags.push_back({String((*t)["name"]->AsString()), (*t)["visible"]->AsBool(true)});
	}
	for (const dataio::NodePtr &v : (*j)["views"]->AsArray())
		doc.m_views.push_back({String((*v)["name"]->AsString()), JsonVec3((*v)["eye"], {8.f, 6.f, 10.f}), JsonVec3((*v)["target"]), (*v)["fov"]->AsFloat(35.f),
							   (*v)["orthographic"]->AsBool(), (*v)["orthoHeight"]->AsFloat(10.f)});
	auto root = JsonModel((*j)["root"]);
	if (!root)
		return Err(String("document : géométrie libre : " + root.Error()));
	doc.m_root = std::move(root.Value());
	std::unordered_map<ComponentId, modeling::Model> definitions;
	for (const dataio::NodePtr &c : (*j)["components"]->AsArray()) {
		auto model = JsonModel((*c)["model"]);
		if (!model)
			return Err(String::Format("document : composant « {} » : {}", (*c)["name"]->AsString(), model.Error()));
		const ComponentId id = ComponentId(std::max<int64_t>(1, (*c)["id"]->AsInt()));
		doc.m_components.push_back({id, String((*c)["name"]->AsString())});
		definitions.emplace(id, std::move(model.Value()));
	}
	std::vector<std::pair<int64_t, EntityId>> savedIds;	   // identifiant écrit → identifiant chargé
	std::vector<std::pair<EntityId, int64_t>> savedParents; // groupe chargé → parent écrit
	for (const dataio::NodePtr &e : (*j)["entities"]->AsArray()) {
		const ComponentId component = ComponentId(std::max<int64_t>(0, (*e)["component"]->AsInt()));
		const auto definition = definitions.find(component);
		auto model = definition != definitions.end() ? Result<modeling::Model, String>(Ok(definition->second)) : JsonModel((*e)["model"]);
		if (!model)
			return Err(String::Format("document : groupe « {} » : {}", (*e)["name"]->AsString(), model.Error()));
		Entity &added = doc.AddEntity(String((*e)["name"]->AsString()), std::move(model.Value()), JsonMatrix((*e)["transform"]));
		added.tag = uint32_t(std::max<int64_t>(0, (*e)["tag"]->AsInt()));
		added.visible = (*e)["visible"]->AsBool(true);
		added.locked = (*e)["locked"]->AsBool();
		added.behavior = JsonBehavior((*e)["behavior"]);
		added.component = definition != definitions.end() ? component : 0;
		if (const std::string_view type = (*e)["type"]->AsString(); !type.empty())
			added.type = String(type);
		savedIds.emplace_back((*e)["id"]->AsInt(), added.id);
		savedParents.emplace_back(added.id, (*e)["parent"]->AsInt());
	}
	for (const auto &[id, savedParent] : savedParents) // parents, une fois tous les groupes créés
		if (savedParent > 0)
			if (const auto it = std::ranges::find(savedIds, savedParent, &std::pair<int64_t, EntityId>::first); it != savedIds.end())
				(void)doc.Reparent(id, it->second);
	if (const dataio::NodePtr &g = (*j)["game"]; g->IsObject()) {
		GameSettings &s = doc.m_game;
		s.gravity = (*g)["gravity"]->AsFloat(s.gravity);
		s.walkSpeed = (*g)["walkSpeed"]->AsFloat(s.walkSpeed);
		s.runSpeed = (*g)["runSpeed"]->AsFloat(s.runSpeed);
		s.jumpSpeed = (*g)["jumpSpeed"]->AsFloat(s.jumpSpeed);
		s.eyeHeight = (*g)["eyeHeight"]->AsFloat(s.eyeHeight);
		s.stepHeight = (*g)["stepHeight"]->AsFloat(s.stepHeight);
		s.script = String((*g)["script"]->AsString());
		s.scriptFile = String((*g)["scriptFile"]->AsString());
		s.params = JsonParams((*g)["params"]);
	}
	if (const dataio::NodePtr &g = (*j)["environment"]; g->IsObject()) {
		EnvironmentSettings &e = doc.m_environment;
		e.zenith = JsonColor((*g)["zenith"], e.zenith);
		e.horizon = JsonColor((*g)["horizon"], e.horizon);
		e.ground = JsonColor((*g)["ground"], e.ground);
		e.sunColor = JsonColor((*g)["sunColor"], e.sunColor);
		e.sunHour = (*g)["sunHour"]->AsFloat(e.sunHour);
		e.sunIntensity = (*g)["sunIntensity"]->AsFloat(e.sunIntensity);
		e.ambient = (*g)["ambient"]->AsFloat(e.ambient);
		e.fog = JsonColor((*g)["fog"], e.fog);
		e.fogDensity = (*g)["fogDensity"]->AsFloat(e.fogDensity);
		e.exposure = (*g)["exposure"]->AsFloat(e.exposure);
	}
	if (const dataio::NodePtr &o = (*j)["overlay"]; o->IsObject()) {
		static constexpr std::string_view kShapes[] = {"rect", "circle", "line", "text", "image"};
		Layer2D &layer = doc.m_overlay;
		if (const dataio::NodePtr &r = (*o)["reference"]; r->IsArray() && r->Size() == 2)
			layer.reference = {std::max(1.f, (*r)[0]->AsFloat(1280.f)), std::max(1.f, (*r)[1]->AsFloat(720.f))};
		for (const dataio::NodePtr &e : (*o)["items"]->AsArray()) {
			Item2D i;
			i.id = uint32_t(std::max<int64_t>(1, (*e)["id"]->AsInt()));
			i.name = String((*e)["name"]->AsString());
			i.shape = EnumFrom((*e)["shape"]->AsString(), kShapes, Shape2D::Rect);
			const auto pair = [](const dataio::NodePtr &a, math::FVector2 fallback) {
				return a->IsArray() && a->Size() == 2 ? math::FVector2{(*a)[0]->AsFloat(), (*a)[1]->AsFloat()} : fallback;
			};
			i.position = pair((*e)["position"], i.position);
			i.size = pair((*e)["size"], i.size);
			i.fill = ParseColor((*e)["fill"]->AsString()).value_or(i.fill);
			i.stroke = ParseColor((*e)["stroke"]->AsString()).value_or(i.stroke);
			i.strokeWidth = (*e)["strokeWidth"]->AsFloat(i.strokeWidth);
			i.cornerRadius = (*e)["cornerRadius"]->AsFloat(i.cornerRadius);
			i.text = String((*e)["text"]->AsString());
			i.fontSize = (*e)["fontSize"]->AsFloat(i.fontSize);
			i.image = String((*e)["image"]->AsString());
			i.z = int((*e)["z"]->AsInt());
			i.visible = (*e)["visible"]->AsBool(true);
			layer.items.push_back(std::move(i));
		}
		// Calque de tuiles.
		if (const dataio::NodePtr &tm = (*o)["tilemap"]; tm->IsObject()) {
			TilemapLayer &t = layer.tilemap;
			const auto pair = [](const dataio::NodePtr &a, math::FVector2 fallback) {
				return a->IsArray() && a->Size() == 2 ? math::FVector2{(*a)[0]->AsFloat(), (*a)[1]->AsFloat()} : fallback;
			};
			t.origin = pair((*tm)["origin"], t.origin);
			t.tileSize = (*tm)["tileSize"]->AsFloat(32.f);
			t.width = int((*tm)["width"]->AsInt());
			t.height = int((*tm)["height"]->AsInt());
			t.terrain.clear();
			for (const dataio::NodePtr &v : (*tm)["terrain"]->AsArray())
				t.terrain.push_back(int8_t(v->AsInt()));
			t.palette.clear();
			for (const dataio::NodePtr &v : (*tm)["palette"]->AsArray())
				t.palette.push_back(ParseColor(v->AsString()).value_or(modeling::Color{}));
		}
	}
	doc.SyncMaterials();
	return Ok(std::move(doc));
}


namespace {

Option<String> WriteText(const std::filesystem::path &path, std::string_view text, const dataio::FileOptions &options = {}) {
	if (auto error = dataio::WriteFile(path, text, options))
		return Some(String(*error));
	return Option<String>(NONE);
}

Option<String> ReadText(const std::filesystem::path &path) {
	if (auto text = dataio::ReadFile(path))
		return Some(String(std::move(*text)));
	return Option<String>(NONE);
}

/// Scripts à fichier du document : (fichier, contenu).
template <typename Doc, typename Fn> void ForEachScriptFile(Doc &doc, Fn &&fn) {
	if (!doc.Game().scriptFile.IsEmpty())
		fn(doc.Game().scriptFile, doc.Game().script);
	for (auto &e : doc.Entities())
		if (!e.behavior.scriptFile.IsEmpty())
			fn(e.behavior.scriptFile, e.behavior.script);
}

} // namespace

Option<String> Document::SaveObject(EntityId id, const std::filesystem::path &file, String description) const {
	const Entity *e = Find(id);
	if (!e)
		return Some(String("objet : groupe introuvable"));
	dataio::NodePtr o = dataio::Node::MakeObject();
	o->Set("format", dataio::Node::MakeString(std::string("sandbox.object")));
	o->Set("version", dataio::Node::MakeInt(int64_t(1)));
	o->Set("name", dataio::Node::MakeString(std::string(e->name)));
	o->Set("description", dataio::Node::MakeString(std::string(description)));
	dataio::NodePtr palette = dataio::Node::MakeArray(); // palette du modèle : rapprochée par nom à l'insertion
	for (const modeling::Material &m : m_materials) {
		dataio::NodePtr c = dataio::Node::MakeObject();
		c->Set("name", dataio::Node::MakeString(std::string(m.name)));
		dataio::NodePtr rgba = dataio::Node::MakeArray();
		rgba->Push(dataio::Node::MakeFloat(m.color.r));
		rgba->Push(dataio::Node::MakeFloat(m.color.g));
		rgba->Push(dataio::Node::MakeFloat(m.color.b));
		rgba->Push(dataio::Node::MakeFloat(m.color.a));
		c->Set("color", std::move(rgba));
		if (m.kind != modeling::MaterialKind::Plastic)
			c->Set("kind", dataio::Node::MakeString(std::string(modeling::MaterialKindName(m.kind))));
		palette->Push(std::move(c));
	}
	o->Set("materials", std::move(palette));
	o->Set("model", ModelJson(e->model));
	Behavior behavior = e->behavior;
	behavior.scriptFile = String(); // script inclus : l'objet se suffit à lui-même
	if (behavior != Behavior{})
		o->Set("behavior", BehaviorJson(behavior));
	return WriteText(file, std::string(dataio::WriteJson(o, 1)));
}

Result<EntityId, String> Document::InsertObject(const std::filesystem::path &file, const math::FVector3 &at) {
	const Option<String> text = ReadText(file);
	if (!text)
		return Err(String::Format("impossible de lire {}", file.string()));
	auto parsed = dataio::ParseJson(*text);
	if (!parsed)
		return Err(String("objet : JSON invalide (" + parsed.Error().Format() + ")"));
	const dataio::NodePtr &j = parsed.Value();
	if ((*j)["format"]->AsString() != "sandbox.object")
		return Err(String("objet : format « sandbox.object » attendu"));
	if (IsSceneTree(j)) { // ancien format : arbre importé puis greffé (identifiants, parents et matériaux rapprochés)
		auto sub = ImportSceneTree(j, file);
		if (sub.IsError())
			return Err(sub.Error());
		for (const modeling::Material &m : sub.Value().Materials())
			if (std::ranges::find(m_materials, m.name, &modeling::Material::name) == m_materials.end())
				m_materials.push_back(m);
		std::vector<std::pair<EntityId, EntityId>> ids; // importé → ajouté
		EntityId root = 0;
		for (const Entity &src : sub.Value().Entities()) {
			modeling::Model placed;
			placed.Materials() = m_materials;
			(void)placed.Append(src.model);
			Entity &added = AddEntity(src.parent ? src.name : UniqueName(std::string_view(src.name)), std::move(placed),
									  math::FMatrix4::Translation(at) * src.transform);
			added.type = src.type;
			added.behavior = src.behavior;
			ids.emplace_back(src.id, added.id);
			if (!root)
				root = added.id;
		}
		for (const Entity &src : sub.Value().Entities())
			if (src.parent)
				for (const auto &[from, to] : ids)
					if (from == src.parent)
						Find(std::ranges::find(ids, src.id, &std::pair<EntityId, EntityId>::first)->second)->parent = to;
		SyncMaterials();
		Touch();
		return root ? Result<EntityId, String>(Ok(root)) : Result<EntityId, String>(Err(String("objet vide")));
	}
	auto model = JsonModel((*j)["model"]);
	if (!model)
		return Err(String("objet : " + model.Error()));
	std::vector<modeling::Material> palette;
	for (const dataio::NodePtr &m : (*j)["materials"]->AsArray()) {
		const dataio::NodePtr &c = (*m)["color"];
		modeling::Material mat{String((*m)["name"]->AsString()), {(*c)[0]->AsFloat(0.8f), (*c)[1]->AsFloat(0.8f), (*c)[2]->AsFloat(0.8f), (*c)[3]->AsFloat(1.f)}};
		mat.kind = modeling::MaterialKindFromName((*m)["kind"]->AsString());
		palette.push_back(std::move(mat));
	}
	model.Value().Materials() = palette;
	// Matériaux absents de la scène : ajoutés ; index des faces rapprochés par nom (Append).
	for (const modeling::Material &m : palette)
		if (std::ranges::find(m_materials, m.name, &modeling::Material::name) == m_materials.end())
			m_materials.push_back(m);
	modeling::Model placed;
	placed.Materials() = m_materials;
	(void)placed.Append(model.Value());
	const math::FAABB box = placed.Bounds();
	const math::FVector3 base = box.min.x <= box.max.x ? math::FVector3{(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f}
													   : math::FVector3{};
	const std::string name = std::string((*j)["name"]->AsString()).empty() ? file.stem().string() : std::string((*j)["name"]->AsString());
	Entity &e = AddEntity(UniqueName(name), std::move(placed), math::FMatrix4::Translation(at - base));
	e.behavior = JsonBehavior((*j)["behavior"]);
	SyncMaterials();
	Touch();
	return Ok(e.id);
}

std::span<const std::pair<std::string_view, EnvironmentSettings>> EnvironmentPresets() {
	static const std::array<std::pair<std::string_view, EnvironmentSettings>, 4> kPresets = [] {
		EnvironmentSettings day; // valeurs par défaut : ciel clair d'après-midi
		EnvironmentSettings sunset = day;
		sunset.zenith = {0.26f, 0.33f, 0.55f, 1.f};
		sunset.horizon = {0.98f, 0.62f, 0.38f, 1.f};
		sunset.ground = {0.38f, 0.30f, 0.26f, 1.f};
		sunset.sunColor = {1.f, 0.62f, 0.36f, 1.f};
		sunset.sunHour = 17.5f;
		sunset.sunIntensity = 1.6f;
		sunset.ambient = 0.7f;
		EnvironmentSettings night = day;
		night.zenith = {0.02f, 0.03f, 0.08f, 1.f};
		night.horizon = {0.07f, 0.09f, 0.16f, 1.f};
		night.ground = {0.04f, 0.04f, 0.05f, 1.f};
		night.sunColor = {0.62f, 0.70f, 1.f, 1.f}; // lune
		night.sunHour = 20.f;
		night.sunIntensity = 0.25f;
		night.ambient = 0.25f;
		night.exposure = 1.3f;
		EnvironmentSettings mist = day;
		mist.zenith = {0.70f, 0.73f, 0.76f, 1.f};
		mist.horizon = {0.82f, 0.84f, 0.85f, 1.f};
		mist.sunIntensity = 1.1f;
		mist.fog = {0.80f, 0.82f, 0.84f, 1.f};
		mist.fogDensity = 0.06f;
		return std::array<std::pair<std::string_view, EnvironmentSettings>, 4>{
			{{"Jour", day}, {"Couchant", sunset}, {"Nuit", night}, {"Brume", mist}}};
	}();
	return kPresets;
}

Option<String> Document::Save(const String &path, const dataio::FileOptions &options) const {
	if (auto error = WriteText(path.c_str(), ToJson(), options))
		return error;
	const std::filesystem::path folder = std::filesystem::path(path.c_str()).parent_path();
	Option<String> failure = NONE;
	ForEachScriptFile(*this, [&](const String &file, const String &source) {
		if (auto error = WriteText(folder / file.c_str(), source); error && !failure)
			failure = std::move(error);
	});
	return failure;
}

Result<Document, String> Document::Load(const String &path) {
	const Option<String> text = ReadText(path.c_str());
	if (!text)
		return Err(String::Format("impossible de lire {}", path));
	// Ancien sandbox : arbre de nœuds (« scene.tree ») importé en groupes imbriqués.
	auto parsed = dataio::ParseJson(*text);
	Result<Document, String> doc = parsed && IsSceneTree(parsed.Value()) ? ImportSceneTree(parsed.Value(), path.c_str()) : FromJson(*text);
	if (doc.IsError())
		return doc;
	const std::filesystem::path folder = std::filesystem::path(path.c_str()).parent_path();
	ForEachScriptFile(doc.Value(), [&](const String &file, String &source) {
		if (auto content = ReadText(folder / file.c_str()))
			source = std::move(*content);
		else
			source = String::Format("# Fichier introuvable : {} (créé à l'enregistrement)\n", file);
	});
	return doc;
}

void History::Push(const Document &before, String label) {
	m_undo.push_back({before, std::move(label)});
	if (m_undo.size() > m_capacity)
		m_undo.pop_front();
	m_redo.clear();
}

bool History::Undo(Document &doc) {
	if (m_undo.empty())
		return false;
	m_redo.push_back({std::move(doc), m_undo.back().label});
	doc = std::move(m_undo.back().state);
	m_undo.pop_back();
	doc.Touch();
	return true;
}

bool History::Redo(Document &doc) {
	if (m_redo.empty())
		return false;
	m_undo.push_back({std::move(doc), m_redo.back().label});
	doc = std::move(m_redo.back().state);
	m_redo.pop_back();
	doc.Touch();
	return true;
}

bool History::Revert(Document &doc) {
	if (m_undo.empty())
		return false;
	doc = std::move(m_undo.back().state);
	m_undo.pop_back();
	doc.Touch();
	return true;
}

void History::Clear() noexcept {
	m_undo.clear();
	m_redo.clear();
}

std::vector<String> History::UndoLabels() const {
	std::vector<String> out;
	out.reserve(m_undo.size());
	for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it)
		out.push_back(it->label);
	return out;
}

std::vector<String> History::RedoLabels() const {
	std::vector<String> out;
	out.reserve(m_redo.size());
	for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it)
		out.push_back(it->label);
	return out;
}

} // namespace sandbox
