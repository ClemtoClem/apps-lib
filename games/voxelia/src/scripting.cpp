// Voxelia — liaisons entre le langage Script des mods et le jeu.
#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>

#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/trace.hpp>

#include "app.hpp"

namespace voxelia {

using script::Value;
using Args = std::span<const Value>;
using R = Result<Value, std::string>;

namespace {

const Value &Arg(Args a, size_t i) {
	static const Value nil;
	return i < a.size() ? a[i] : nil;
}
int IntArg(Args a, size_t i, int def = 0) { return i < a.size() ? int(std::floor(a[i].AsNumber(def))) : def; }

Value Vec(const math::FVector3 &v) { return Value(script::Map{{"x", Value(v.x)}, {"y", Value(v.y)}, {"z", Value(v.z)}}); }

// Conversions Script ↔ JSON (données persistantes des mods) et Script ↔ SQL.
dataio::NodePtr ToJson(const Value &v) {
	switch (v.Type()) {
	case script::ValueType::Bool: return dataio::Node::MakeBool(v.AsBool());
	case script::ValueType::Number: return dataio::Node::MakeFloat(v.AsNumber());
	case script::ValueType::Text: return dataio::Node::MakeString(std::string(v.AsString()));
	case script::ValueType::Array: {
		dataio::NodePtr a = dataio::Node::MakeArray();
		for (const Value &x : *v.AsList())
			a->Push(ToJson(x));
		return a;
	}
	case script::ValueType::Table: {
		dataio::NodePtr o = dataio::Node::MakeObject();
		for (const auto &[k, x] : *v.AsMap())
			o->Set(k, ToJson(x));
		return o;
	}
	default: return nullptr;
	}
}
Value FromJson(const dataio::NodePtr &j) {
	if (!j || j->IsNone())
		return Value();
	switch (j->type) {
	case dataio::NodeType::BOOL: return Value(j->AsBool());
	case dataio::NodeType::INT: return Value(double(j->AsInt()));
	case dataio::NodeType::FLOAT: return Value(j->AsNumber());
	case dataio::NodeType::STRING: return Value(std::string(j->AsString()));
	case dataio::NodeType::ARRAY: {
		script::List l;
		for (const dataio::NodePtr &x : j->AsArray())
			l.push_back(FromJson(x));
		return Value(std::move(l));
	}
	case dataio::NodeType::OBJECT: {
		script::Map m;
		for (const auto &[k, x] : j->AsObject())
			m.emplace(k, FromJson(x));
		return Value(std::move(m));
	}
	default: return Value();
	}
}
sql::Value ToSql(const Value &v) {
	switch (v.Type()) {
	case script::ValueType::Bool: return sql::Value(v.AsBool());
	case script::ValueType::Number: {
		const double d = v.AsNumber();
		return d == std::floor(d) && std::fabs(d) < 9e15 ? sql::Value(int64_t(d)) : sql::Value(d);
	}
	case script::ValueType::Text: return sql::Value(std::string(v.AsString()));
	case script::ValueType::Nil: return sql::Value();
	default: return sql::Value(dataio::WriteJson(ToJson(v), -1));
	}
}
Value FromSql(const sql::Value &v) {
	switch (v.Type()) {
	case sql::ColumnType::Int: return Value(double(v.AsInt()));
	case sql::ColumnType::Real: return Value(v.AsReal());
	case sql::ColumnType::Text: return Value(std::string(v.AsText()));
	case sql::ColumnType::Blob: return Value(std::format("<blob {} o>", v.AsBlob().size()));
	default: return Value();
	}
}

/// Fonctions de l'API Voxelia d'origine non reprises : neutres (nil), signalées une fois.
constexpr std::pair<const char *, const char *> kStubs[] = {
	{"voxel", "behavior"}, {"voxel", "generator"}, {"world", "biome"}, {"world", "drop"}, {"world", "load"}, {"world", "maze_shaft"},
	{"world", "set_state"}, {"world", "spawn_structure"}, {"world", "structure_set"},
	{"world", "default_dimension"}, {"entity", "add_xp"}, {"entity", "body"}, {"entity", "effect"},
	{"entity", "has_effect"}, 
	{"entity", "tags"}, {"entity", "travel"}, {"game", "ambient"}, {"game", "clear_highlight"},
	{"game", "highlight"}, {"game", "open_ui"}, {"game", "stop_ambient"},
	{"formula", "check"}, {"formula", "eval"}, {"structure", "bounds"}, {"structure", "box"}, {"structure", "copy"},
	{"structure", "delete"}, {"structure", "directory"}, {"structure", "duplicate"}, {"structure", "exists"}, {"structure", "forget"},
	{"structure", "hollow"}, {"structure", "info"}, {"structure", "list"}, {"structure", "load"}, {"structure", "mirror"},
	{"structure", "paste"}, {"structure", "plot"}, {"structure", "rotate"}, {"structure", "save"}, {"structure", "set_anchor"},
};

} // namespace

ScriptHost::ScriptHost(Game &game, const ModSettingsStore &modSettings) : m_game(game), m_modSettings(modSettings) {
	m_vm.SetPrint([this](std::string_view s) { m_game.Log(std::string(s)); });
	m_vm.SetStepLimit(5'000'000);
	BindApi();
}

Value ScriptHost::SettingValue(std::string_view id) const {
	// Paramètre déclaré dans mod.json : valeur choisie par le joueur, sinon défaut.
	// Pendant le chargement, le mod courant est prioritaire ; ensuite, recherche par id.
	if (!m_mods)
		return Value();
	auto lookup = [&](const voxel::ModInfo &mod) -> std::optional<Value> {
		for (const voxel::ModSetting &st : mod.settings)
			if (st.id == id)
				return FromJson(m_modSettings.Get(mod, st));
		return std::nullopt;
	};
	if (const voxel::ModInfo *cur = m_mods->Find(m_currentMod))
		if (auto v = lookup(*cur))
			return *v;
	for (const voxel::ModInfo &mod : m_mods->Mods())
		if (mod.enabled)
			if (auto v = lookup(mod))
				return *v;
	return Value();
}

Value ScriptHost::EntityValue(ecs::Entity e) const { return e ? Value(double(e.Bits())) : Value(); }

ecs::Entity ScriptHost::ToEntity(const Value &v) const {
	if (v.IsNumber())
		return ecs::Entity::FromBits(static_cast<uint64_t>(v.AsNumber()));
	if (v.IsMap())
		return ToEntity(v.Get("id"));
	return m_game.Player();
}

size_t ScriptHost::HookCount() const {
	size_t n = 0;
	for (const auto &name : m_hooks.Names())
		n += m_hooks.Listeners(name);
	return n;
}

void ScriptHost::BindApi() {
	Game &g = m_game;
	auto blockName = [&g](voxel::BlockId id) { return Value(g.GetContent().blocks.Get(id).name); };
	auto blockOf = [&g](const Value &v) { return g.GetContent().blocks.FindOrCreate(v.AsString()); };
	const auto fill = [&g, blockOf](Args a) -> R {
		const int x0 = IntArg(a, 0), y0 = IntArg(a, 1), z0 = IntArg(a, 2), x1 = IntArg(a, 3), y1 = IntArg(a, 4), z1 = IntArg(a, 5);
		const voxel::BlockId b = blockOf(Arg(a, 6));
		const int64_t volume = int64_t(std::abs(x1 - x0) + 1) * (std::abs(y1 - y0) + 1) * (std::abs(z1 - z0) + 1);
		if (volume > 2'000'000)
			return Err(std::format("volume trop grand ({} blocs)", volume));
		int64_t n = 0;
		for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
			for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z)
				for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
					n += g.World().SetBlock(x, y, z, b);
		return Ok(Value(double(n)));
	};
	const auto sphere = [&g, blockOf](Args a) -> R {
		const int cx = IntArg(a, 0), cy = IntArg(a, 1), cz = IntArg(a, 2), r = std::min(IntArg(a, 3), 64);
		const voxel::BlockId b = blockOf(Arg(a, 4));
		for (int y = -r; y <= r; ++y)
			for (int z = -r; z <= r; ++z)
				for (int x = -r; x <= r; ++x)
					if (x * x + y * y + z * z <= r * r)
						g.World().SetBlock(cx + x, cy + y, cz + z, b);
		return Ok(Value());
	};
	const auto playerPos = [&g](Args a) -> R {
		(void)a;
		const auto *t = g.Ecs().Get<voxel::Transform>(g.Player());
		return Ok(t ? Vec(t->position) : Value());
	};

	// ── voxel.* : enregistrement des mods ───────────────────────────────
	m_vm.RegisterModule("voxel", {
		{"on", [this](script::Interpreter &, Args a) -> R {
			 if (!Arg(a, 1).IsCallable())
				 return Err(std::string("voxel.on(nom, fn)"));
			 const Value fn = Arg(a, 1);
			 const std::string event(Arg(a, 0).AsString());
			 m_connections.push_back(m_hooks.On(event, [this, fn, event](const Value &payload) {
				 // Paramètres liés par nom : fn(x, y, z, block, actor) reçoit payload.x…
				 if (auto r = m_vm.CallNamed(fn, payload); !r)
					 m_game.Log(std::format("[{}] {}", event, r.Error().Format()));
				 else if (r.Value().Type() == script::ValueType::Bool && !r.Value().Truthy())
					 m_cancelled = true; // `return false` : action par défaut annulée
				 return false;
			 }));
			 return Ok(Value());
		 }},
		// voxel.interact(cible, fn(ev)) : clic gauche/droit sur un bloc ou une entité.
		// cible : « core:lamp », « #door » (étiquette), « core:pig » (genre d'entité),
		// « block », « entity » ou « * ». ev : button ("left"/"right"), x, y, z, block,
		// entity, kind, player, sneaking. `return false` annule l'action par défaut.
		{"interact", [this](script::Interpreter &, Args a) -> R {
			 if (!Arg(a, 1).IsCallable())
				 return Err(std::string("voxel.interact(cible, fn(ev))"));
			 m_interactions.push_back({std::string(Arg(a, 0).AsString()), m_currentMod, Arg(a, 1)});
			 return Ok(Value());
		 }},
		{"system", [this](script::Interpreter &, Args a) -> R {
			 const Value fn = a.size() > 1 ? Arg(a, 1) : Arg(a, 0);
			 if (!fn.IsCallable())
				 return Err(std::string("voxel.system(nom, fn(dt))"));
			 m_systems.push_back({std::string(Arg(a, 0).AsString()), fn, false});
			 return Ok(Value());
		 }},
		{"command", [this](script::Interpreter &, Args a) -> R {
			 const Value fn = a.empty() ? Value() : a.back();
			 if (!fn.IsCallable())
				 return Err(std::string("voxel.command(id, usage, description, fn)"));
			 m_commands.push_back({std::string(Arg(a, 0).AsString()), std::string(Arg(a, 1).AsString()), std::string(Arg(a, 2).AsString()), fn});
			 return Ok(Value());
		 }},
		{"setting", [this](script::Interpreter &, Args a) -> R {
			 Value v = SettingValue(Arg(a, 0).AsString());
			 return Ok(v.IsNil() && a.size() > 1 ? Arg(a, 1) : v);
		 }},
		{"emit", [&g, this](script::Interpreter &, Args a) -> R {
			 g.Emit(Arg(a, 0).AsString(), Arg(a, 1), m_currentMod.empty() ? "mod" : m_currentMod);
			 return Ok(Value());
		 }},
		{"skin", [&g, this](script::Interpreter &, Args a) -> R {
			 if (Arg(a, 0).IsNil())
				 return Err(std::string("voxel.skin(id, texture?)"));
			 const std::string mod = m_currentMod.empty() ? "core" : m_currentMod;
			 const std::string id = voxel::BlockRegistry::Qualify(std::string(Arg(a, 0).AsString()), mod);
			 std::string texture = Arg(a, 1).IsNil() ? id : voxel::BlockRegistry::Qualify(std::string(Arg(a, 1).AsString()), mod);
			 g.GetContent().RegisterSkin({id, SkinName(id), std::move(texture), mod});
			 return Ok(Value(id));
		 }},
		{"post_effect", [&g](script::Interpreter &, Args a) -> R {
			 // voxel.post_effect(nom, force) : définit un effet de post-traitement
			 // (« underwater », « heat », « glare », « stun », « suffocate »).
			 if (a.size() < 2)
				 return Err(std::string("voxel.post_effect(nom, force)"));
			 g.SetPostEffect(Arg(a, 0).AsString(), float(Arg(a, 1).AsNumber()));
			 return Ok(Value());
		 }},
	});

	// ── world.* : voxels, recherche d'entités par grille spatiale, données ──
	m_vm.RegisterModule("world", {
		{"get", [&g, blockName](script::Interpreter &, Args a) -> R { return Ok(blockName(g.World().GetBlock(IntArg(a, 0), IntArg(a, 1), IntArg(a, 2)))); }},
		{"set", [&g, blockOf](script::Interpreter &, Args a) -> R {
			 const uint8_t orient = a.size() > 4 ? uint8_t(IntArg(a, 4) & 0xFF) : uint8_t{0};
			 return Ok(Value(g.World().SetBlock(IntArg(a, 0), IntArg(a, 1), IntArg(a, 2), blockOf(Arg(a, 3)), orient)));
		 }},
		{"param", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(int(g.World().GetOrient(IntArg(a, 0), IntArg(a, 1), IntArg(a, 2))))); }},
		{"fill", [fill](script::Interpreter &, Args a) -> R { return fill(a); }},
		{"replace", [&g, blockOf](script::Interpreter &, Args a) -> R {
			 const voxel::BlockId from = blockOf(Arg(a, 6)), to = blockOf(Arg(a, 7));
			 int64_t n = 0;
			 for (int y = std::min(IntArg(a, 1), IntArg(a, 4)); y <= std::max(IntArg(a, 1), IntArg(a, 4)); ++y)
				 for (int z = std::min(IntArg(a, 2), IntArg(a, 5)); z <= std::max(IntArg(a, 2), IntArg(a, 5)); ++z)
					 for (int x = std::min(IntArg(a, 0), IntArg(a, 3)); x <= std::max(IntArg(a, 0), IntArg(a, 3)); ++x)
						 if (g.World().GetBlock(x, y, z) == from)
							 n += g.World().SetBlock(x, y, z, to);
			 return Ok(Value(double(n)));
		 }},
		{"count", [&g](script::Interpreter &, Args a) -> R {
			 core::RobinMap<voxel::BlockId, int> counts;
			 for (int y = std::min(IntArg(a, 1), IntArg(a, 4)); y <= std::max(IntArg(a, 1), IntArg(a, 4)); ++y)
				 for (int z = std::min(IntArg(a, 2), IntArg(a, 5)); z <= std::max(IntArg(a, 2), IntArg(a, 5)); ++z)
					 for (int x = std::min(IntArg(a, 0), IntArg(a, 3)); x <= std::max(IntArg(a, 0), IntArg(a, 3)); ++x)
						 ++counts[g.World().GetBlock(x, y, z)];
			 script::Map m;
			 for (const auto &e : counts)
				 m.emplace(g.GetContent().blocks.Get(e.key).name, Value(e.value));
			 return Ok(Value(std::move(m)));
		 }},
		{"surface", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(g.World().SurfaceHeight(IntArg(a, 0), IntArg(a, 1)))); }},
		{"ground", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(g.World().SurfaceHeight(IntArg(a, 0), IntArg(a, 1)))); }},
		{"structure", [&g](script::Interpreter &, Args a) -> R {
			 const std::string name = voxel::BlockRegistry::Qualify(std::string(Arg(a, 0).AsString()), "core");
			 const voxel::Structure *st = g.GetContent().mods.FindStructure(name);
			 if (!st)
				 return Err(std::format("structure inconnue : {}", name));
			 const size_t n = g.World().PlaceStructure(*st, IntArg(a, 1), IntArg(a, 2), IntArg(a, 3));
			 return Ok(Value(double(n)));
		 }},
		{"dimension", [](script::Interpreter &, Args) -> R { return Ok(Value("core:overworld")); }},
		{"time_here", [&g](script::Interpreter &, Args) -> R { return Ok(Value(g.TimeOfDay())); }},
		{"entities", [&g, this](script::Interpreter &, Args a) -> R {
			 // Recherche de voisinage par la grille spatiale des chunks (O(1) par chunk voisin).
			 std::vector<ecs::Entity> found;
			 const math::FVector3 p{float(Arg(a, 0).AsNumber()), float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber())};
			 g.World().QueryEntities(p, float(Arg(a, 3).AsNumber(16)), found, Arg(a, 4).AsString());
			 script::List l;
			 for (ecs::Entity e : found)
				 l.push_back(EntityValue(e));
			 return Ok(Value(std::move(l)));
		 }},
		{"spawn", [&g, this](script::Interpreter &, Args a) -> R {
			 voxel::EntityRecord r;
			 r.kind = Arg(a, 0).IsString() ? std::string(Arg(a, 0).AsString()) : std::string("core:creature");
			 r.position = {float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 r.wander = Arg(a, 4).IsNil() ? true : Arg(a, 4).Truthy();
			 return Ok(EntityValue(g.World().SpawnEntity(r)));
		 }},
		{"raycast", [&g](script::Interpreter &, Args a) -> R {
			 const math::FVector3 o{float(Arg(a, 0).AsNumber()), float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber())};
			 const math::FVector3 d{float(Arg(a, 3).AsNumber()), float(Arg(a, 4).AsNumber()), float(Arg(a, 5).AsNumber())};
			 auto hit = g.World().Raycast(o, d, float(Arg(a, 6).AsNumber(64)));
			 if (!hit)
				 return Ok(Value());
			 return Ok(Value(script::Map{{"x", Value(hit->block.x)}, {"y", Value(hit->block.y)}, {"z", Value(hit->block.z)},
										 {"block", Value(g.GetContent().blocks.Get(hit->id).name)}, {"distance", Value(hit->distance)}}));
		 }},
		{"data", [&g](script::Interpreter &, Args a) -> R {
			 const std::string v = g.World().Storage().GetMeta(std::format("data:{}", Arg(a, 0).AsString()));
			 if (v.empty())
				 return Ok(Value());
			 auto j = dataio::ParseJson(v);
			 return Ok(j ? FromJson(j.Value()) : Value());
		 }},
		{"set_data", [&g](script::Interpreter &, Args a) -> R {
			 g.World().Storage().SetMeta(std::format("data:{}", Arg(a, 0).AsString()), dataio::WriteJson(ToJson(Arg(a, 1)), -1));
			 return Ok(Value());
		 }},
	});

	// ── entity.* ──────────────────────────────────────────────────────────
	auto transform = [&g, this](const Value &v) { return g.Ecs().Get<voxel::Transform>(ToEntity(v)); };
	// ── Inventaire et état du joueur (entity.give/take/held/sneaking/armor) ──
	m_vm.RegisterModule("entity", {
		{"give", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value(0.0));
			 const ItemId item = g.GetContent().FindItem(Arg(a, 1).AsString());
			 if (item == 0)
				 return Err(std::format("objet inconnu : {}", Arg(a, 1).AsString()));
			 const int n = std::max(1, Arg(a, 2).AsInt(1));
			 return Ok(Value(double(n - g.Give(item, n))));
		 }},
		{"take", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value(0.0));
			 const ItemId item = g.GetContent().FindItem(Arg(a, 1).AsString());
			 return Ok(Value(double(item ? g.Take(item, std::max(1, Arg(a, 2).AsInt(1))) : 0)));
		 }},
		{"count", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value(0.0));
			 return Ok(Value(double(g.CountOf(g.GetContent().FindItem(Arg(a, 1).AsString())))));
		 }},
		{"held", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value());
			 const bool left = Arg(a, 1).AsString() == "left";
			 const ItemStack &s = g.Held(!left);
			 return Ok(s.Empty() ? Value() : Value(g.GetContent().ItemName(s.id)));
		 }},
		{"sneaking", [&g, this](script::Interpreter &, Args a) -> R { return Ok(Value(ToEntity(Arg(a, 0)) == g.Player() && g.Crouching())); }},
		{"armor", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value(0.0));
			 if (const auto slot = ParseArmorSlot(Arg(a, 1).AsString())) {
				 const ItemStack &s = g.ArmorPiece(*slot);
				 return Ok(s.Empty() ? Value() : Value(g.GetContent().ItemName(s.id)));
			 }
			 return Ok(Value(double(g.ArmorPoints())));
		 }},
		{"equip", [&g, this](script::Interpreter &, Args a) -> R {
			 if (ToEntity(Arg(a, 0)) != g.Player())
				 return Ok(Value(false));
			 return Ok(Value(g.EquipArmor(g.GetContent().FindItem(Arg(a, 1).AsString()))));
		 }},
	});
	m_vm.RegisterModule("player", {
		// player.emote("wave" | "wave_both" | "wave_small" | "equip_armor")
		{"emote", [&g](script::Interpreter &, Args a) -> R {
			 const auto gesture = voxel::ParseAvatarGesture(Arg(a, 0).AsString());
			 if (!gesture)
				 return Err(std::string("geste : wave, wave_both, wave_small, equip_armor"));
			 g.PlayGesture(*gesture);
			 return Ok(Value());
		 }},
		// player.swim_style() → nage courante ; player.swim_style("brasse") la change.
		{"swim_style", [&g](script::Interpreter &, Args a) -> R {
			 if (!a.empty()) {
				 const auto style = voxel::ParseSwimStyle(Arg(a, 0).AsString());
				 if (!style)
					 return Err(std::string("nage : crawl, breaststroke (brasse), backstroke (dos)"));
				 g.SetSwimStyle(*style);
			 }
			 return Ok(Value(voxel::SwimStyleId(g.SwimStyle())));
		 }},
		{"state", [&g](script::Interpreter &, Args) -> R { return Ok(Value(voxel::AvatarStateId(g.PlayerState()))); }},
		{"sit", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(g.SitOn({IntArg(a, 0), IntArg(a, 1), IntArg(a, 2)}))); }},
		{"sleep", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(g.SleepIn({IntArg(a, 0), IntArg(a, 1), IntArg(a, 2)}))); }},
		{"stand", [&g](script::Interpreter &, Args) -> R {
			 g.StandUp();
			 return Ok(Value());
		 }},
		// player.crouch(vrai/faux) : s'accroupir sans maintenir Maj.
		{"crouch", [&g](script::Interpreter &, Args a) -> R {
			 g.SetCrouchLock(a.empty() || Arg(a, 0).Truthy());
			 return Ok(Value());
		 }},
		// player.click("left" | "right") : clic sur l'objet visé (automatisation, tests).
		{"click", [&g](script::Interpreter &, Args a) -> R {
			 g.QueueClick(Arg(a, 0).AsString() == "right");
			 return Ok(Value());
		 }},
		// player.look(cap°, tangage°) : oriente le regard.
		{"look", [&g](script::Interpreter &, Args a) -> R {
			 g.SetLook(float(Arg(a, 0).AsNumber()) * math::DEG2RAD, float(Arg(a, 1).AsNumber()) * math::DEG2RAD);
			 return Ok(Value());
		 }},
		// player.inventory() : ouvre/ferme le menu d'inventaire.
		{"inventory", [&g](script::Interpreter &, Args a) -> R {
			 if (!a.empty()) // player.inventory("porte") : ouvre avec une recherche (créatif)
				 g.SetCreativeSearch(std::string(Arg(a, 0).AsString()));
			 g.ToggleInventory();
			 return Ok(Value(g.InventoryOpen()));
		 }},
		// player.gamemode("survival" | "creative")
		{"gamemode", [&g](script::Interpreter &, Args a) -> R {
			 WorldSettings w = g.WorldInfo();
			 if (!a.empty())
				 w.gameMode = Arg(a, 0).AsString() == "creative" ? 1 : 0;
			 g.ApplyWorldSettings(w);
			 return Ok(Value(g.Creative() ? "creative" : "survival"));
		 }},
		// player.camera("first" | "back" | "front" | "side")
		{"camera", [&g](script::Interpreter &, Args a) -> R {
			 const std::string_view m = Arg(a, 0).AsString();
			 if (m == "first")
				 g.SetCameraMode(CameraMode::FirstPerson);
			 else if (m == "back")
				 g.SetCameraMode(CameraMode::ThirdBack);
			 else if (m == "front")
				 g.SetCameraMode(CameraMode::ThirdFront);
			 else if (m == "side")
				 g.SetCameraMode(CameraMode::ThirdSide);
			 return Ok(Value());
		 }},
	});
	m_vm.RegisterModule("entity", {
		{"pos", [transform](script::Interpreter &, Args a) -> R {
			 const auto *t = transform(Arg(a, 0));
			 return Ok(t ? Vec(t->position) : Value());
		 }},
		{"set_pos", [&g, this](script::Interpreter &, Args a) -> R {
			 const ecs::Entity e = ToEntity(Arg(a, 0));
			 const math::FVector3 p{float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 if (e == g.Player())
				 g.Teleport(p);
			 else if (auto *t = g.Ecs().Get<voxel::Transform>(e))
				 t->position = p;
			 return Ok(Value());
		 }},
		{"players", [this](script::Interpreter &, Args) -> R { return Ok(Value(script::List{EntityValue(m_game.Player())})); }},
		{"nearest_player", [this](script::Interpreter &, Args) -> R { return Ok(EntityValue(m_game.Player())); }},
		{"exists", [&g, this](script::Interpreter &, Args a) -> R { return Ok(Value(g.Ecs().IsAlive(ToEntity(Arg(a, 0))))); }},
		{"kind", [&g, this](script::Interpreter &, Args a) -> R {
			 const auto *k = g.Ecs().Get<voxel::Kind>(ToEntity(Arg(a, 0)));
			 return Ok(k ? Value(k->id) : Value());
		 }},
		{"skin", [&g, this](script::Interpreter &, Args a) -> R {
			 const auto *s = g.Ecs().Get<voxel::Skin>(ToEntity(Arg(a, 0)));
			 return Ok(s ? Value(s->id) : Value());
		 }},
		{"set_skin", [&g, this](script::Interpreter &, Args a) -> R {
			 const ecs::Entity e = ToEntity(Arg(a, 0));
			 if (!g.Ecs().IsAlive(e))
				 return Ok(Value(false));
			 const std::string mod = m_currentMod.empty() ? "core" : m_currentMod;
			 const std::string qualified = voxel::BlockRegistry::Qualify(std::string(Arg(a, 1).AsString()), mod);
			 g.Ecs().Add(e, voxel::Skin{qualified});
			 return Ok(Value(true));
		 }},
		{"forward", [&g](script::Interpreter &, Args) -> R { return Ok(Vec(g.Camera().Forward())); }},
		{"look", [&g](script::Interpreter &, Args) -> R {
			 return Ok(Value(script::Map{{"yaw", Value(g.Camera().yaw)}, {"pitch", Value(g.Camera().pitch)}}));
		 }},
		{"velocity", [&g, this](script::Interpreter &, Args a) -> R {
			 const auto *v = g.Ecs().Get<voxel::Velocity>(ToEntity(Arg(a, 0)));
			 return Ok(v ? Vec(v->value) : Value());
		 }},
		{"set_velocity", [&g, this](script::Interpreter &, Args a) -> R {
			 if (auto *v = g.Ecs().Get<voxel::Velocity>(ToEntity(Arg(a, 0))))
				 v->value = {float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 return Ok(Value());
		 }},
		{"impulse", [&g, this](script::Interpreter &, Args a) -> R {
			 if (auto *v = g.Ecs().Get<voxel::Velocity>(ToEntity(Arg(a, 0))))
				 v->value += {float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 return Ok(Value());
		 }},
		{"move", [&g, this](script::Interpreter &, Args a) -> R {
			 if (auto *t = g.Ecs().Get<voxel::Transform>(ToEntity(Arg(a, 0))))
				 t->position += {float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 return Ok(Value());
		 }},
		{"gravity", [&g, this](script::Interpreter &, Args a) -> R {
			 const auto *b = g.Ecs().Get<voxel::Body>(ToEntity(Arg(a, 0)));
			 return Ok(Value(b && b->gravity));
		 }},
		{"set_gravity", [&g, this](script::Interpreter &, Args a) -> R {
			 if (auto *b = g.Ecs().Get<voxel::Body>(ToEntity(Arg(a, 0))))
				 b->gravity = Arg(a, 1).Truthy();
			 return Ok(Value());
		 }},
		{"damage", [&g, this](script::Interpreter &, Args a) -> R {
			 const ecs::Entity e = ToEntity(Arg(a, 0));
			 auto *h = g.Ecs().Get<voxel::Health>(e);
			 if (!h)
				 return Ok(Value());
			 h->current -= float(Arg(a, 1).AsNumber(1));
			 g.Emit("entity_damaged", Value(script::Map{{"entity", EntityValue(e)}, {"target", EntityValue(e)}, {"amount", Arg(a, 1)}, {"source", Arg(a, 2)}}));
			 if (h->current <= 0 && e != g.Player()) {
				 const auto *kind = g.Ecs().Get<voxel::Kind>(e);
				 g.Emit("entity_died", Value(script::Map{{"entity", EntityValue(e)}, {"e", EntityValue(e)}, {"kind", Value(kind ? kind->id : std::string())}}));
				 g.World().DespawnEntity(e);
			 }
			 return Ok(Value());
		 }},
		{"remove", [&g, this](script::Interpreter &, Args a) -> R {
			 const ecs::Entity e = ToEntity(Arg(a, 0));
			 if (e != g.Player())
				 g.World().DespawnEntity(e);
			 return Ok(Value());
		 }},
		{"prop", [&g, this](script::Interpreter &, Args a) -> R {
			 const auto *p = g.Ecs().Get<voxel::Props>(ToEntity(Arg(a, 0)));
			 if (!p)
				 return Ok(Value());
			 auto j = dataio::ParseJson(p->json);
			 return Ok(j ? FromJson((*j.Value())[Arg(a, 1).AsString()]) : Value());
		 }},
		{"set_prop", [&g, this](script::Interpreter &, Args a) -> R {
			 const ecs::Entity e = ToEntity(Arg(a, 0));
			 if (!g.Ecs().IsAlive(e))
				 return Ok(Value());
			 dataio::NodePtr j = dataio::Node::MakeObject();
			 if (const auto *p = g.Ecs().Get<voxel::Props>(e))
				 if (auto parsed = dataio::ParseJson(p->json))
					 j = parsed.Value();
			 j->Set(Arg(a, 1).AsString(), ToJson(Arg(a, 2)));
			 g.Ecs().Add(e, voxel::Props{dataio::WriteJson(j, -1)});
			 return Ok(Value());
		 }},
		{"count", [&g](script::Interpreter &, Args a) -> R {
			 const std::string_view kind = Arg(a, 0).AsString();
			 size_t n = 0;
			 g.Ecs().MakeQuery<const voxel::Kind>().Each([&](const voxel::Kind &k) { n += kind.empty() || k.id == kind; });
			 return Ok(Value(double(n)));
		 }},
		{"target", [&g](script::Interpreter &, Args) -> R {
			 auto hit = g.World().Raycast(g.Camera().position, g.Camera().Forward(), 8.f);
			 if (!hit)
				 return Ok(Value());
			 return Ok(Value(script::Map{{"x", Value(hit->block.x)}, {"y", Value(hit->block.y)}, {"z", Value(hit->block.z)},
										 {"block", Value(g.GetContent().blocks.Get(hit->id).name)}}));
		 }},
	});

	// ── game.* ────────────────────────────────────────────────────────────
	m_vm.RegisterModule("game", {
		// game.sound(id, x, y, z [, volume]) : son ponctuel spatialisé.
		{"sound", [&g](script::Interpreter &, Args a) -> R {
			 std::string id(Arg(a, 0).AsString());
			 if (!id.empty() && id.find(':') == std::string::npos)
				 id = "core:" + id;
			 math::FVector3 at{float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber()), float(Arg(a, 3).AsNumber())};
			 if (a.size() < 4)
				 at = g.Camera().position;
			 g.PlaySound(std::move(id), at, float(Arg(a, 4).AsNumber(1.0)));
			 return Ok(Value());
		 }},
		{"say", [&g](script::Interpreter &, Args a) -> R {
			 g.Say(a.size() > 1 && a[0].IsNumber() ? a[1].ToString() : Arg(a, 0).ToString());
			 return Ok(Value());
		 }},
		{"notify", [&g](script::Interpreter &, Args a) -> R {
			 g.Say(Arg(a, 0).ToString());
			 return Ok(Value());
		 }},
		{"log", [&g](script::Interpreter &, Args a) -> R {
			 g.Log(Arg(a, 0).ToString());
			 return Ok(Value());
		 }},
		{"time", [&g](script::Interpreter &, Args) -> R { return Ok(Value(g.TimeOfDay())); }},
		{"difficulty", [&g](script::Interpreter &, Args) -> R { return Ok(Value(double(g.Difficulty()))); }},
		{"set_time", [&g](script::Interpreter &, Args a) -> R {
			 g.SetTimeOfDay(float(Arg(a, 0).AsNumber()));
			 return Ok(Value());
		 }},
		{"mode", [&g](script::Interpreter &, Args) -> R { return Ok(Value(g.Flying() ? "creative" : "survival")); }},
		{"rule", [&g](script::Interpreter &, Args a) -> R {
			 auto v = g.World().Storage().GetConfig(std::format("rules.{}", Arg(a, 0).AsString()));
			 return Ok(v ? Value(*v) : Value());
		 }},
		{"tick", [&g](script::Interpreter &, Args) -> R { return Ok(Value(double(g.Tick()))); }},
		// game.explode(x, y, z, radius [, damage [, grief]]) : explosion physique (rayons,
		// résistance des blocs, dégâts et recul). Retourne la liste des TNT enchaînées.
		{"explode", [&g](script::Interpreter &, Args a) -> R {
			 const math::FVector3 c{float(Arg(a, 0).AsNumber()), float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber())};
			 const float radius = float(Arg(a, 3).AsNumber(3));
			 const float damage = float(Arg(a, 4).AsNumber(8));
			 const bool grief = Arg(a, 5).IsNil() || Arg(a, 5).AsBool();
			 std::vector<math::IVec3> chain = g.World().Explode(c, radius, damage, grief);
			 script::List l;
			 for (const math::IVec3 &p : chain) {
				 script::Map m;
				 m.emplace("x", Value(p.x));
				 m.emplace("y", Value(p.y));
				 m.emplace("z", Value(p.z));
				 l.push_back(Value(std::move(m)));
			 }
			 return Ok(Value(std::move(l)));
		 }},
	});

	// ── content.* : configuration fusionnée des mods (table SQL `config`) ──
	m_vm.RegisterModule("content", {
		{"config", [&g](script::Interpreter &, Args a) -> R {
			 auto v = g.World().Storage().GetConfig(Arg(a, 0).AsString());
			 if (!v)
				 return Ok(a.size() > 1 ? Arg(a, 1) : Value());
			 if (auto j = dataio::ParseJson(*v); j && !j.Value()->IsString())
				 return Ok(FromJson(j.Value()));
			 return Ok(Value(*v));
		 }},
		{"get", [&g](script::Interpreter &, Args a) -> R {
			 // content.get("block" | "item" | "entity" | "recipe", id) → définition JSON brute.
			 if (a.size() < 2)
				 return Ok(Value());
			 const std::string_view cat = Arg(a, 0).AsString();
			 const std::string key = std::string(cat) + ":" + voxel::BlockRegistry::Qualify(Arg(a, 1).AsString(), "core");
			 if (const auto it = g.GetContent().contentDefs.find(key); it != g.GetContent().contentDefs.end())
				 return Ok(FromJson(it->second));
			 return Ok(Value());
		 }},
		{"list", [&g](script::Interpreter &, Args a) -> R {
			 // content.list("entity" | "block" | "item" | "recipe" [, tag]) → identifiants qualifiés.
			 const std::string_view cat = Arg(a, 0).AsString();
			 const std::string_view tag = Arg(a, 1).AsString();
			 const std::string prefix = std::string(cat) + ":";
			 script::List l;
			 for (const auto &[key, def] : g.GetContent().contentDefs) {
				 if (!key.starts_with(prefix))
					 continue;
				 if (!tag.empty()) {
					 bool has = false;
					 if (const dataio::NodePtr t = def->Get("tags"); t && t->IsArray())
						 for (const dataio::NodePtr &x : t->AsArray())
							 if (x->AsString() == tag) {
								 has = true;
								 break;
							 }
					 if (!has)
						 continue;
				 }
				 l.emplace_back(key.substr(prefix.size()));
			 }
			 return Ok(Value(std::move(l)));
		 }},
	});

	// ── sql.* : requêtes sur la base du monde (tables des mods, journal…) ──
	m_vm.RegisterModule("sql", {
		{"query", [&g](script::Interpreter &, Args a) -> R {
			 std::vector<sql::Value> params;
			 for (size_t i = 1; i < a.size(); ++i)
				 params.push_back(ToSql(a[i]));
			 auto r = g.World().Storage().Db().Execute(Arg(a, 0).AsString(), params);
			 if (!r)
				 return Err(r.Error());
			 script::List rows;
			 for (const sql::Row &row : r.Value().rows) {
				 script::Map m;
				 for (size_t c = 0; c < row.size(); ++c)
					 m.emplace(r.Value().columns[c], FromSql(row[c]));
				 rows.emplace_back(std::move(m));
			 }
			 if (r.Value().columns.empty())
				 return Ok(Value(double(r.Value().affected)));
			 return Ok(Value(std::move(rows)));
		 }},
	});

	// Fonctions non reprises : présentes mais neutres.
	for (const auto &[mod, fn] : kStubs) {
		const std::string name = std::format("{}.{}", mod, fn);
		m_vm.RegisterModule(mod, {{fn, [name, warned = std::make_shared<bool>(false), &g](script::Interpreter &, Args) mutable -> R {
									   if (!*warned) {
										   *warned = true;
										   g.Log(std::format("(script) {} n'est pas disponible dans ce moteur : nil", name));
									   }
									   return Ok(Value());
								   }}});
	}

	// ── Fonctions de chaînes globales de l'API d'origine (ends_with(s, suffixe)…) ──
	m_vm.RegisterFunction("starts_with", [](script::Interpreter &, Args a) -> R { return Ok(Value(Arg(a, 0).AsString().starts_with(Arg(a, 1).AsString()))); });
	m_vm.RegisterFunction("ends_with", [](script::Interpreter &, Args a) -> R { return Ok(Value(Arg(a, 0).AsString().ends_with(Arg(a, 1).AsString()))); });
	m_vm.RegisterFunction("contains", [](script::Interpreter &, Args a) -> R {
		return Ok(Value(Arg(a, 0).AsString().find(Arg(a, 1).AsString()) != std::string_view::npos));
	});

	// ── Fonctions globales historiques (mod « welcome ») ─────────────────
	m_vm.RegisterFunction("pos", [playerPos](script::Interpreter &, Args a) -> R { return playerPos(a); });
	m_vm.RegisterFunction("get", [&g, blockName](script::Interpreter &, Args a) -> R { return Ok(blockName(g.World().GetBlock(IntArg(a, 0), IntArg(a, 1), IntArg(a, 2)))); });
	m_vm.RegisterFunction("set", [&g, blockOf](script::Interpreter &, Args a) -> R {
		return Ok(Value(g.World().SetBlock(IntArg(a, 0), IntArg(a, 1), IntArg(a, 2), blockOf(Arg(a, 3)))));
	});
	m_vm.RegisterFunction("fill", [fill](script::Interpreter &, Args a) -> R { return fill(a); });
	m_vm.RegisterFunction("sphere", [sphere](script::Interpreter &, Args a) -> R { return sphere(a); });
	m_vm.RegisterFunction("surface_height", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(g.World().SurfaceHeight(IntArg(a, 0), IntArg(a, 1)))); });
	m_vm.RegisterFunction("notify", [&g](script::Interpreter &, Args a) -> R {
		g.Say(Arg(a, 0).ToString());
		return Ok(Value());
	});
	m_vm.RegisterFunction("log", [&g](script::Interpreter &, Args a) -> R {
		g.Log(Arg(a, 0).ToString());
		return Ok(Value());
	});
	m_vm.RegisterFunction("tp", [&g](script::Interpreter &, Args a) -> R {
		g.Teleport({float(Arg(a, 0).AsNumber()), float(Arg(a, 1).AsNumber()), float(Arg(a, 2).AsNumber())});
		return Ok(Value());
	});
	m_vm.RegisterFunction("block_id", [&g](script::Interpreter &, Args a) -> R { return Ok(Value(int(g.GetContent().blocks.Find(Arg(a, 0).AsString())))); });
	m_vm.RegisterFunction("block_name", [&g](script::Interpreter &, Args a) -> R {
		return Ok(Value(g.GetContent().blocks.Get(voxel::BlockId(IntArg(a, 0))).name));
	});
}

void ScriptHost::LoadMods(const voxel::ModManager &mods) {
	DIAG_ZONE("ScriptHost::LoadMods");
	m_mods = &mods;
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		m_currentMod = mod.id;
		for (const fs::path &file : mod.scripts) {
			std::ifstream f(file, std::ios::binary);
			const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (auto r = m_vm.Run(src, (fs::path(mod.id) / file.filename()).string()); !r)
				m_game.Log(std::format("[mod {}] {}", mod.id, r.Error().Format()));
		}
	}
	m_currentMod.clear();
	m_game.Log(std::format("scripts : {} hook(s), {} système(s), {} commande(s)", HookCount(), m_systems.size(), m_commands.size()));
}

bool ScriptHost::Emit(std::string_view name, const Value &payload) {
	DIAG_ZONE("ScriptHost::Emit");
	const bool outer = m_cancelled;
	m_cancelled = false;
	m_hooks.Emit(name, payload);
	const bool cancelled = m_cancelled;
	m_cancelled = outer;
	return cancelled;
}

bool ScriptHost::Interact(std::span<const std::string> targets, const Value &event, std::string_view call) {
	DIAG_ZONE("ScriptHost::Interact");
	bool cancelled = false;
	auto run = [&](const Value &fn, std::string_view what) {
		auto r = m_vm.CallNamed(fn, event);
		if (!r)
			m_game.Log(std::format("[interact {}] {}", what, r.Error().Format()));
		else if (r.Value().Type() == script::ValueType::Bool && !r.Value().Truthy())
			cancelled = true;
	};
	// Copie : un gestionnaire peut en enregistrer d'autres.
	const std::vector<Interaction> handlers = m_interactions;
	for (const Interaction &h : handlers)
		if (std::ranges::find(targets, h.target) != targets.end())
			run(h.fn, h.target);
	if (!call.empty()) {
		const Value fn = m_vm.GetGlobal(call);
		if (fn.IsCallable()) {
			run(fn, call);
			cancelled = true; // la fonction du bloc remplace l'action par défaut
		} else {
			m_game.Log(std::format("[interact] fonction « {} » introuvable dans les scripts", call));
		}
	}
	return cancelled;
}

void ScriptHost::Tick(float dt) {
	DIAG_ZONE("ScriptHost::Tick");
	for (System &s : m_systems) {
		if (s.failed)
			continue;
		const Value args[] = {Value(dt)};
		if (auto r = m_vm.Call(s.fn, args); !r) {
			s.failed = true; // un système en erreur est désactivé (évite un flot d'erreurs à 20 Hz)
			m_game.Log(std::format("[système {}] désactivé : {}", s.name, r.Error().Format()));
		}
	}
}

std::string ScriptHost::RunSource(const std::string &source, const std::string &name) {
	auto r = m_vm.Run(source, name);
	if (!r)
		return r.Error().Format();
	return r.Value().IsNil() ? std::string("exécuté") : r.Value().ToString();
}

std::string ScriptHost::RunConsole(const std::string &line) {
	DIAG_ZONE("ScriptHost::RunConsole");
	const std::string trimmed(StringView(line).Trim());
	if (trimmed.empty())
		return {};
	if (trimmed[0] != '/') {
		auto r = m_vm.Run(trimmed, "console");
		if (!r)
			return r.Error().Format();
		return r.Value().IsNil() ? std::string() : r.Value().ToString();
	}
	const size_t sp = trimmed.find(' ');
	const std::string cmd = trimmed.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
	const std::string rest = sp == std::string::npos ? std::string() : std::string(StringView(trimmed.substr(sp + 1)).Trim());
	const auto words = rest.empty() ? std::vector<String>{} : String(rest).Split(' ');
	auto num = [&](size_t i, double def) { return i < words.size() ? std::strtod(words[i].c_str(), nullptr) : def; };
	voxel::World &w = m_game.World();
	static constexpr std::string_view kCheatCommands[] = {"tp", "fly", "time", "spawn"};
	if (!m_game.WorldInfo().cheats && std::ranges::find(kCheatCommands, cmd) != std::end(kCheatCommands))
		return std::format("/{} : commandes de triche désactivées dans les paramètres de ce monde", cmd);

	if (cmd == "help") {
		std::string s = "commandes : /help /tp x y z /fly /time t /seed /save /reload /stats /sql REQUÊTE /spawn type [n] /structure nom /structures /near [rayon] [type] /events [n] /chunks";
		for (const auto &c : m_commands)
			s += " " + (c.usage.empty() ? "/" + c.id : c.usage);
		return s;
	}
	if (cmd == "tp") {
		m_game.Teleport({float(num(0, 0)), float(num(1, 100)), float(num(2, 0))});
		return "téléporté";
	}
	if (cmd == "fly") {
		m_game.SetFlying(m_game.CanFly() && !m_game.Flying());
		return m_game.Flying() ? "vol activé" : "vol désactivé";
	}
	if (cmd == "time") {
		m_game.SetTimeOfDay(float(num(0, 0.3)));
		return std::format("heure = {:.2f}", m_game.TimeOfDay());
	}
	if (cmd == "seed")
		return std::format("graine : {}", w.Config().seed);
	if (cmd == "save") {
		m_game.Save();
		return "monde sauvegardé";
	}
	if (cmd == "reload") {
		m_game.RequestReloadScripts();
		return "scripts rechargés à chaud (différé à la prochaine image)";
	}
	if (cmd == "stats") {
		const auto s = w.Stats();
		const auto r = w.Storage().Regions().GetStats();
		return std::format("{} chunks, {} entités, régions ouvertes {}, enregistrements {}, {:.1f} Mo vivants, {:.1f} Mo morts, {} lectures, {} écritures",
						   s.loadedChunks, s.entities, r.regionsOpen, r.records, double(r.liveBytes) / 1048576.0, double(r.deadBytes) / 1048576.0,
						   r.reads, r.writes);
	}
	if (cmd == "sql") {
		auto r = w.Storage().Db().Execute(rest);
		if (!r)
			return r.Error();
		std::string out = std::format("[{}] {} ligne(s){}", r.Value().plan.empty() ? "-" : r.Value().plan, r.Value().rows.size(),
									  r.Value().affected ? std::format(", {} modifiée(s)", r.Value().affected) : "");
		for (size_t i = 0; i < r.Value().rows.size() && i < 20; ++i) {
			out += "\n  ";
			for (size_t c = 0; c < r.Value().rows[i].size(); ++c)
				out += std::format("{}={}  ", r.Value().columns[c], r.Value().rows[i][c].ToString());
		}
		return out;
	}
	if (cmd == "spawn") {
		const std::string kind = words.empty() ? "core:sheep" : std::string(words[0]);
		const size_t n = size_t(std::clamp(num(1, 1), 1.0, 500000.0));
		core::Stopwatch sw;
		const size_t spawned = m_game.SpawnCrowd(kind, n, std::min(24.f + std::sqrt(float(n)), 200.f));
		return std::format("{} « {} » créés en {:.1f} ms", spawned, kind, sw.Millis());
	}
	if (cmd == "structures") {
		std::string s = "structures :";
		for (const auto &st : m_game.GetContent().mods.Structures())
			s += " " + st.id;
		return s;
	}
	if (cmd == "structure") {
		if (words.empty())
			return "usage : /structure <nom> (voir /structures) — place la structure devant vous";
		const std::string name = voxel::BlockRegistry::Qualify(words[0], "core");
		const voxel::Structure *st = m_game.GetContent().mods.FindStructure(name);
		if (!st)
			return std::format("structure inconnue : {}", words[0]);
		const auto *t = m_game.Ecs().Get<voxel::Transform>(m_game.Player());
		const int x = t ? int(std::floor(t->position.x)) : 0;
		const int z = t ? int(std::floor(t->position.z)) : 0;
		const int y = m_game.World().SurfaceHeight(x, z) + 1;
		const size_t placed = m_game.World().PlaceStructure(*st, x, y, z);
		return std::format("structure « {} » placée ({} blocs)", words[0], placed);
	}
	if (cmd == "near") {
		std::vector<ecs::Entity> found;
		core::Stopwatch sw;
		const auto *t = m_game.Ecs().Get<voxel::Transform>(m_game.Player());
		w.QueryEntities(t->position, float(num(0, 32)), found, words.size() > 1 ? std::string_view(words[1]) : std::string_view{});
		return std::format("{} entité(s) à moins de {} m (grille spatiale, {:.3f} ms)", found.size(), num(0, 32), sw.Millis());
	}
	if (cmd == "events") {
		auto r = w.Storage().Db().Execute(std::format("SELECT tick, type, mod, data FROM events ORDER BY tick DESC LIMIT {}", int(num(0, 10))));
		if (!r)
			return r.Error();
		std::string out = "derniers événements :";
		for (const auto &row : r.Value().rows)
			out += std::format("\n  #{} {} [{}] {}", row[0].AsInt(), row[1].AsText(), row[2].AsText(), row[3].AsText());
		return out;
	}
	if (cmd == "chunks") {
		const auto *t = m_game.Ecs().Get<voxel::Transform>(m_game.Player());
		const math::IVec3 c = voxel::ChunkOfPosition(t->position);
		const int r = int(num(0, 8));
		const auto keys = w.Storage().SavedChunksInBox(c - math::IVec3{r, r, r}, c + math::IVec3{r, r, r});
		const auto ents = w.Storage().FindStoredEntities(c - math::IVec3{r, r, r}, c + math::IVec3{r, r, r});
		return std::format("{} chunk(s) sauvegardé(s) dans un rayon de {} chunks (index Morton), {} entité(s) stockée(s) hors mémoire", keys.size(),
						   r, ents.size());
	}
	for (const Command &c : m_commands) {
		if (c.id != cmd)
			continue;
		script::List argList;
		for (const auto &word : words)
			argList.emplace_back(std::string(word));
		const Value args[] = {EntityValue(m_game.Player()), Value(std::move(argList))};
		auto r = m_vm.Call(c.fn, args);
		if (!r)
			return r.Error().Format();
		return r.Value().IsNil() ? std::string() : r.Value().ToString();
	}
	return std::format("commande inconnue : /{} (voir /help)", cmd);
}

} // namespace voxelia
