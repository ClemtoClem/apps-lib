#include "sandbox/game/session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <variant>
#include <iterator>
#include <fstream>

#include <script/script.hpp>

#include <math/color.hpp>

#include "sandbox/game/sound.hpp"
#include "sandbox/modeling/triangulate.hpp"

namespace sandbox::game {

namespace {

using physics::kWorld;
using physics::UserId;
constexpr UserId kPlayer = ~UserId(0);
constexpr math::FVector3 kPlayerHalf{0.3f, 0.9f, 0.3f};
constexpr float kReach = 3.f;		  ///< portée de la main (touche E)
constexpr size_t kMaxMessages = 8;

sdl3::LinearColor Linear(const modeling::Color &c) {
	return sdl3::Srgb(uint8_t(std::clamp(c.r, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.g, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.b, 0.f, 1.f) * 255.f));
}

/// Triangles d'un modèle, transformés dans le repère du monde.
std::vector<math::FVector3> WorldTriangles(const modeling::Model &model, const math::FMatrix4 &transform) {
	const modeling::MeshData mesh = modeling::BuildMesh(model);
	std::vector<math::FVector3> out;
	out.reserve(mesh.indices.size());
	for (const uint32_t i : mesh.indices)
		out.push_back(transform.TransformPoint(mesh.positions[i]));
	return out;
}

} // namespace

// ── Script d'un groupe ───────────────────────────────────────────────────────

/// Un interpréteur par script : celui d'un groupe (fonctions `self.*` visant ce groupe) ou
script::Value ListOf(const math::FVector3 &v) { return script::Value(script::List{script::Value(v.x), script::Value(v.y), script::Value(v.z)}); }

/// Bases natives des classes de script (Behaviour, PhysicsBody, Light3D, AudioSource, Scene) :
/// chaque méthode agit sur le groupe (ou la scène) de l'instance.
struct GroupHandle {
	GameSession *game = nullptr;
	EntityId id = 0;
};
struct SceneHandle {
	GameSession *game = nullptr;
};
void RegisterEngineClasses(script::Interpreter &interp, GameSession *game = nullptr) {
	using script::Value;
	using R = Result<Value, std::string>;
	// Instance créée par un script (`Porte()`) : pas de groupe derrière.
	const auto ready = [](const auto &h) -> bool { return h.game != nullptr; };
	const auto orphan = [] { return R(Err(std::string("instance sans groupe (créée par le script)"))); };
	const auto behaviour = script::ClassBinder<GroupHandle>(interp, "Behaviour")
		.Method("name", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(Value(g.game->NameOf(g.id)))) : orphan(); })
		.Method("position", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(ListOf(g.game->CenterOf(g.id)))) : orphan(); })
		.Method("move", [=](GroupHandle &g, float dx, float dy, float dz) -> R {
			if (!ready(g))
				return orphan();
			g.game->MoveEntity(g.id, {dx, dy, dz});
			return Ok(Value());
		})
		.Method("set_position", [=](GroupHandle &g, float x, float y, float z) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetEntityCenter(g.id, {x, y, z});
			return Ok(Value());
		})
		.Method("rotate", [=](GroupHandle &g, float degrees) -> R {
			if (!ready(g))
				return orphan();
			g.game->RotateEntity(g.id, degrees, false);
			return Ok(Value());
		})
		.Method("turn", [=](GroupHandle &g, float degrees) -> R {
			if (!ready(g))
				return orphan();
			g.game->RotateEntity(g.id, degrees, true);
			return Ok(Value());
		})
		.Method("show", [=](GroupHandle &g) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetVisible(g.id, true);
			return Ok(Value());
		})
		.Method("hide", [=](GroupHandle &g) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetVisible(g.id, false);
			return Ok(Value());
		})
		.Method("prop", [=](GroupHandle &g, std::string_view key) -> R { return ready(g) ? R(Ok(g.game->PropOf(g.id, key))) : orphan(); })
		.Method("world_position", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(ListOf(g.game->OriginOf(g.id)))) : orphan(); })
		.Method("path", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(Value(g.game->PathOf(g.id)))) : orphan(); })
		.Method("child", [=](GroupHandle &g, std::string_view name) -> R {
			return ready(g) ? R(Ok(Value(g.game->PathOf(g.id) + "/" + std::string(name)))) : orphan();
		})
		.Method("set_scale", [=](GroupHandle &g, float x, float y, float z) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetEntityScale(g.id, {x, y, z});
			return Ok(Value());
		})
		.Method("set_rotation", [=](GroupHandle &g, float x, float y, float z) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetEntityRotation(g.id, {x, y, z});
			return Ok(Value());
		})
		.Method("distance", [=](GroupHandle &g) -> R {
			return ready(g) ? R(Ok(Value(double(g.game->CenterOf(g.id).Distance(g.game->PlayerFeet()))))) : orphan();
		})
							   .Class();
	// Comme l'ancien moteur : `Sway("Skull")` attache un comportement au groupe nommé.
	behaviour->nativeMethods.insert_or_assign("init", [game](script::Interpreter &, script::Instance &inst, std::span<const Value> args) -> R {
		if (!game || args.empty() || !args[0].IsString())
			return Ok(Value());
		const EntityId id = game->FindByName(args[0].AsString());
		if (!id)
			return Err(std::format("groupe « {} » introuvable", args[0].AsString()));
		for (auto &[native, state] : inst.states) // toutes les bases du groupe visent ce nœud
			if (native->name != "Scene" && native->name != "Widget")
				state = std::make_shared<GroupHandle>(GroupHandle{game, id});
		return Ok(Value());
	});
	script::ClassBinder<GroupHandle>(interp, "PhysicsBody")
		.Method("push", [=](GroupHandle &g, float vx, float vy, float vz) -> R {
			if (!ready(g))
				return orphan();
			g.game->PushEntity(g.id, {vx, vy, vz});
			return Ok(Value());
		})
		.Method("velocity", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(ListOf(g.game->VelocityOf(g.id)))) : orphan(); })
		.Method("set_mass", [=](GroupHandle &g, float mass) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetMass(g.id, mass);
			return Ok(Value());
		});
	script::ClassBinder<GroupHandle>(interp, "Light3D")
		.Method("light", [=](GroupHandle &g, std::optional<bool> on) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetLight(g.id, on.value_or(true));
			return Ok(Value());
		})
		.Method("intensity", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(Value(double(g.game->LightIntensityOf(g.id))))) : orphan(); })
		.Method("range", [=](GroupHandle &g) -> R { return ready(g) ? R(Ok(Value(double(g.game->LightRangeOf(g.id))))) : orphan(); })
		.Method("set_intensity", [=](GroupHandle &g, float v) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetLightIntensity(g.id, v);
			return Ok(Value());
		})
		.Method("set_range", [=](GroupHandle &g, float v) -> R {
			if (!ready(g))
				return orphan();
			g.game->SetLightRange(g.id, v);
			return Ok(Value());
		})
		.Method("set_color", [=](GroupHandle &g, std::string_view color) -> R {
			if (!ready(g))
				return orphan();
			const auto c = ParseColor(color);
			if (!c)
				return Err(std::format("couleur « {} » invalide (#rrggbb attendu)", color));
			g.game->SetLightColor(g.id, *c);
			return Ok(Value());
		});
	script::ClassBinder<GroupHandle>(interp, "AudioSource")
		.Method("play", [=](GroupHandle &g, std::optional<std::string> file) -> R {
			if (!ready(g))
				return orphan();
			g.game->PlaySound(g.id, file.value_or(std::string()));
			return Ok(Value());
		})
		.Method("stop", [=](GroupHandle &g) -> R {
			if (!ready(g))
				return orphan();
			g.game->StopSound(g.id);
			return Ok(Value());
		});
	const auto named = [](SceneHandle &s, std::string_view group) { return s.game ? s.game->FindByName(group) : EntityId(0); };
	script::ClassBinder<SceneHandle>(interp, "Scene")
		.Method("say", [=](SceneHandle &s, Value text) -> R {
			if (!ready(s))
				return orphan();
			s.game->ScriptSay(text.ToString(), 0);
			return Ok(Value());
		})
		.Method("time", [=](SceneHandle &s) -> R { return ready(s) ? R(Ok(Value(s.game->Time()))) : orphan(); })
		.Method("state", [=](SceneHandle &s) -> R { return ready(s) ? R(Ok(s.game->State())) : orphan(); })
		.Method("load", [=](SceneHandle &s, std::string_view scene) -> R {
			if (!ready(s))
				return orphan();
			s.game->RequestScene(std::string(scene));
			return Ok(Value());
		})
		.Method("quit", [=](SceneHandle &s) -> R {
			if (!ready(s))
				return orphan();
			s.game->RequestQuit();
			return Ok(Value());
		})
		.Method("set_player_speed", [=](SceneHandle &s, float walk, std::optional<float> run) -> R {
			if (!ready(s))
				return orphan();
			s.game->SetPlayerSpeed(walk, run.value_or(walk * 1.8f));
			return Ok(Value());
		})
		.Method("set_jump", [=](SceneHandle &s, float speed) -> R {
			if (!ready(s))
				return orphan();
			s.game->SetJumpSpeed(speed);
			return Ok(Value());
		})
		.Method("set_gravity", [=](SceneHandle &s, float g) -> R {
			if (!ready(s))
				return orphan();
			s.game->SetGravity(g);
			return Ok(Value());
		})
		.Method("player", [=](SceneHandle &s) -> R { return ready(s) ? R(Ok(ListOf(s.game->PlayerFeet()))) : orphan(); })
		.Method("position", [=](SceneHandle &s, std::string_view group) -> R {
			const EntityId id = named(s, group);
			return id ? R(Ok(ListOf(s.game->CenterOf(id)))) : R(Ok(Value()));
		})
		.Method("move", [=](SceneHandle &s, std::string_view group, float dx, float dy, float dz) -> R {
			if (const EntityId id = named(s, group))
				s.game->MoveEntity(id, {dx, dy, dz});
			return Ok(Value());
		})
		.Method("turn", [=](SceneHandle &s, std::string_view group, float degrees) -> R {
			if (const EntityId id = named(s, group))
				s.game->RotateEntity(id, degrees, true);
			return Ok(Value());
		})
		.Method("visible", [=](SceneHandle &s, std::string_view group, std::optional<bool> on) -> R {
			if (const EntityId id = named(s, group))
				s.game->SetVisible(id, on.value_or(true));
			return Ok(Value());
		})
		.Method("light", [=](SceneHandle &s, std::string_view group, std::optional<bool> on) -> R {
			if (const EntityId id = named(s, group))
				s.game->SetLight(id, on.value_or(true));
			return Ok(Value());
		});
}

/// celui de la scène (`id` nul, sans `self`). Liaisons typées (`script::Binder`).
class EntityScript {
public:
	EntityScript(GameSession &game, EntityId id, std::string name) : m_game(game), m_id(id), m_name(std::move(name)) {
		using script::Value;
		m_interp.SetStepLimit(2'000'000); // une boucle infinie ne fige pas la partie
		m_interp.SetPrint([this](std::string_view s) { m_game.ScriptSay(m_name + " : " + std::string(s), m_id); });
		if (m_id != 0)
			script::Binder(m_interp, "self")
				.Fn("name", [this] { return m_name; })
				.Fn("position", [this] { return Vec(m_game.CenterOf(m_id)); })
				.Fn("move", [this](float dx, float dy, float dz) { m_game.MoveEntity(m_id, {dx, dy, dz}); })
				.Fn("set_position", [this](float x, float y, float z) { m_game.SetEntityCenter(m_id, {x, y, z}); })
				.Fn("rotate", [this](float degrees) { m_game.RotateEntity(m_id, degrees, false); })
				.Fn("turn", [this](float degrees) { m_game.RotateEntity(m_id, degrees, true); })
				.Fn("push", [this](float vx, float vy, float vz) { m_game.PushEntity(m_id, {vx, vy, vz}); })
				.Fn("velocity", [this] { return Vec(m_game.VelocityOf(m_id)); })
				.Fn("show", [this] { m_game.SetVisible(m_id, true); })
				.Fn("hide", [this] { m_game.SetVisible(m_id, false); })
				.Fn("play", [this](std::optional<std::string> file) { m_game.PlaySound(m_id, file.value_or(std::string())); })
				.Fn("stop", [this] { m_game.StopSound(m_id); })
				.Fn("light", [this](std::optional<bool> on) { m_game.SetLight(m_id, on.value_or(true)); });
		script::Binder(m_interp, "game")
			.Fn("say", [this](Value text) { m_game.ScriptSay(text.ToString(), m_id); })
			.Fn("time", [this] { return m_game.Time(); })
			.Fn("state", [this] { return m_game.State(); }) // état partagé de la partie (comme Scene.state())
			.Fn("player", [this] { return Vec(m_game.PlayerFeet()); })
			.Fn("distance", [this] { return double(m_game.CenterOf(m_id).Distance(m_game.PlayerFeet())); })
			.Fn("position",
				[this](std::string_view group) -> Value {
					const EntityId other = m_game.FindByName(group);
					return other ? Value(script::List{Value(m_game.CenterOf(other).x), Value(m_game.CenterOf(other).y), Value(m_game.CenterOf(other).z)})
								 : Value();
				})
			.Fn("move",
				[this](std::string_view group, float dx, float dy, float dz) {
					if (const EntityId other = m_game.FindByName(group))
						m_game.MoveEntity(other, {dx, dy, dz});
				})
			.Fn("turn",
				[this](std::string_view group, float degrees) {
					if (const EntityId other = m_game.FindByName(group))
						m_game.RotateEntity(other, degrees, true);
				})
			.Fn("visible",
				[this](std::string_view group, std::optional<bool> on) {
					if (const EntityId other = m_game.FindByName(group))
						m_game.SetVisible(other, on.value_or(true));
				})
			.Fn("light",
				[this](std::string_view group, std::optional<bool> on) {
					if (const EntityId other = m_game.FindByName(group))
						m_game.SetLight(other, on.value_or(true));
				});
		script::Binder(m_interp, "input").Fn("key", [this](std::string_view key) { return m_game.KeyDown(key); });
		RegisterEngineClasses(m_interp, &m_game);
		script::Binder(m_interp, "editor")
			.Fn("log", [this](Value text) { m_game.ScriptSay(text.ToString(), m_id); })
			.Fn("time", [this] { return m_game.Time(); });
		// Caméra du joueur.
		script::Binder(m_interp, "camera")
			.Fn("position", [this] { return ListOf(m_game.Camera().position); })
			.Fn("forward", [this] {
				const sdl3::Camera3D cam = m_game.Camera();
				return ListOf((cam.target - cam.position).Normalized());
			})
			.Fn("set",
				[this](float x, float y, float z, float tx, float ty, float tz) { m_game.SetCamera({x, y, z}, {tx, ty, tz}); })
			.Fn("reset", [this] { m_game.ResetCamera(); });
		// Nœuds par référence (chemin « Salle/Torche 1/Flamme » ou nom), comme l'ancien moteur.
		const auto nodeOf = [this](std::string_view ref) { return m_game.Resolve(ref); };
		script::Binder(m_interp, "node")
			.Fn("exists", [nodeOf](std::string_view ref) { return nodeOf(ref) != 0; })
			.Fn("position", [this, nodeOf](std::string_view ref) -> Value {
				const EntityId node = nodeOf(ref);
				return node ? ListOf(m_game.OriginOf(node)) : Value();
			})
			.Fn("world_position", [this, nodeOf](std::string_view ref) -> Value {
				const EntityId node = nodeOf(ref);
				return node ? ListOf(m_game.OriginOf(node)) : Value();
			})
			.Fn("set_position", [this, nodeOf](std::string_view ref, float x, float y, float z) {
				if (const EntityId node = nodeOf(ref))
					m_game.MoveEntity(node, math::FVector3{x, y, z} - m_game.OriginOf(node));
			})
			.Fn("set_rotation", [this, nodeOf](std::string_view ref, float x, float y, float z) {
				if (const EntityId node = nodeOf(ref))
					m_game.SetEntityRotation(node, {x, y, z});
			})
			.Fn("set_scale", [this, nodeOf](std::string_view ref, float x, float y, float z) {
				if (const EntityId node = nodeOf(ref))
					m_game.SetEntityScale(node, {x, y, z});
			})
			.Fn("prop", [this, nodeOf](std::string_view ref, std::string_view key) -> Value {
				const EntityId node = nodeOf(ref);
				return node ? m_game.PropOf(node, key) : Value();
			})
			.Fn("path", [this, nodeOf](std::string_view ref) -> Value {
				const EntityId node = nodeOf(ref);
				return node ? Value(m_game.PathOf(node)) : Value();
			})
			.Fn("visible", [this, nodeOf](std::string_view ref, std::optional<bool> on) {
				if (const EntityId node = nodeOf(ref))
					m_game.SetVisible(node, on.value_or(true));
			})
			.Fn("create_group", [this](std::string_view groupName) { return Value(double(m_game.CreateGroup(std::string(groupName)))); })
			.Fn("rotation", [this, nodeOf](std::string_view ref) -> Value {
				const EntityId node = nodeOf(ref);
				return node ? ListOf(m_game.RotationOf(node)) : Value();
			})
			.Fn("set_color", [this, nodeOf](std::string_view ref, float, float, float) {
				(void)nodeOf(ref); // teinte : rendu du document (à venir), la méthode existe sans erreur
			});
		// Scène : nœuds créés et retirés à l'exécution (`scene.spawn`, `scene.remove`…).
		script::Binder(m_interp, "scene")
			.Fn("exists", [this](std::string_view ref) { return m_game.Resolve(ref) != 0; })
			.Fn("find_tag", [this](std::string_view tag) -> Value {
				script::List list;
				for (const std::string &tagged : m_game.FindByTag(tag))
					list.push_back(Value(tagged));
				return Value(std::move(list));
			})
			.Fn("spawn", [this](Value options) -> Value {
				const std::string nodeName = std::string(options.Get("name").AsString());
				const std::string parent = std::string(options.Get("parent").AsString());
				const std::string tag = std::string(options.Get("tag").AsString());
				const bool visible = options.Get("visible").Truthy();
				math::FVector3 pos;
				if (const script::List *p = options.Get("pos").AsList(); p && p->size() >= 3)
					pos = {float((*p)[0].AsNumber()), float((*p)[1].AsNumber()), float((*p)[2].AsNumber())};
				return Value(double(m_game.SpawnNode(nodeName, parent, pos, visible, tag)));
			})
			.Fn("remove", [this](std::string_view ref) {
				if (const EntityId node = m_game.Resolve(ref))
					m_game.RemoveEntity(node);
			});
		// Sons : `audio.play(fichier, {volume, loop, position})` rend une voix (0 : muet).
		m_interp.RegisterModule(
			"audio", {
						 {"play",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  SoundSystem *sound = m_game.Sound();
							  if (!sound || a.empty())
								  return Ok(Value(0.0));
							  const Value options = a.size() > 1 ? a[1] : Value();
							  const float volume = float(options.Get("volume").AsNumber(1.0));
							  const bool loop = options.Get("loop").Truthy();
							  const float pitch = float(options.Get("pitch").AsNumber(1.0));
							  std::optional<math::FVector3> at;
							  if (const script::List *p = options.Get("position").AsList(); p && p->size() >= 3)
								  at = math::FVector3{float((*p)[0].AsNumber()), float((*p)[1].AsNumber()), float((*p)[2].AsNumber())};
							  const int voice = sound->Play(std::string(a[0].AsString()), volume, loop, at);
							  if (voice != 0 && pitch != 1.f)
								  sound->SetPitch(voice, pitch);
							  return Ok(Value(double(voice)));
						  }},
						 {"stop",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  if (SoundSystem *sound = m_game.Sound(); sound && !a.empty())
								  sound->Stop(a[0].AsInt());
							  return Ok(Value());
						  }},
						 {"volume",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  if (SoundSystem *sound = m_game.Sound(); sound && a.size() > 1)
								  sound->SetVolume(a[0].AsInt(), float(a[1].AsNumber()));
							  return Ok(Value());
						  }},
						 {"pitch",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  if (SoundSystem *sound = m_game.Sound(); sound && a.size() > 1)
								  sound->SetPitch(a[0].AsInt(), float(a[1].AsNumber()));
							  return Ok(Value());
						  }},
						 {"pause",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  if (SoundSystem *sound = m_game.Sound(); sound && !a.empty())
								  sound->SetPause(a[0].AsInt(), true);
							  return Ok(Value());
						  }},
						 {"resume",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  if (SoundSystem *sound = m_game.Sound(); sound && !a.empty())
								  sound->SetPause(a[0].AsInt(), false);
							  return Ok(Value());
						  }},
						 {"playing",
						  [this](script::Interpreter &, std::span<const Value> a) -> Result<Value, std::string> {
							  SoundSystem *sound = m_game.Sound();
							  return Ok(Value(sound && !a.empty() && sound->IsPlaying(a[0].AsInt())));
						  }},
						 {"preload", [](script::Interpreter &, std::span<const Value>) -> Result<Value, std::string> { return Ok(Value()); }},
					 });
		// Comportements créés par le script et attachés à un groupe (`Sway("Skull")`) : animés eux aussi.
		m_interp.SetInstanceHook([this](const Value &object) {
			const script::Instance *o = object.AsInstance();
			if (!o || m_creatingSelf || object.Equals(m_self) || !o->cls->Derives("Behaviour"))
				return; // l'instance du groupe lui-même est déjà animée par ses rappels
			for (const auto &[native, state] : o->states)
				if (native->name == "Behaviour" && state && static_cast<const GroupHandle *>(state.get())->id != 0)
					m_spawned.push_back({object, false});
		});
		m_interp.SetModuleLoader([this](std::string_view module) { return m_game.LoadModule(module); });
		script::Binder(m_interp, "input").Fn("set_mouse_mode", [this](std::string_view mode) { m_game.SetMouseFree(mode != "captured"); });
		if (m_game.Hook())
			m_game.Hook()(m_interp, m_id); // liaisons de l'application (interface…)
		// Calque 2D : éléments du document par leur nom (un nom inconnu est sans effet).
		script::Binder(m_interp, "hud")
			.Fn("text",
				[this](std::string_view item, Value text) {
					if (Item2D *i = m_game.HudItem(item))
						i->text = String(text.ToString());
				})
			.Fn("show",
				[this](std::string_view item, std::optional<bool> on) {
					if (Item2D *i = m_game.HudItem(item))
						i->visible = on.value_or(true);
				})
			.Fn("move",
				[this](std::string_view item, float x, float y) {
					if (Item2D *i = m_game.HudItem(item))
						i->position = {x, y};
				})
			.Fn("size",
				[this](std::string_view item, float w, float h) {
					if (Item2D *i = m_game.HudItem(item))
						i->size = {w, h};
				})
			.Fn("color",
				[this](std::string_view item, std::string_view color) -> Result<Value, std::string> {
					const auto c = ParseColor(color);
					if (!c)
						return Err(std::format("couleur « {} » invalide (#rrggbb attendu)", color));
					if (Item2D *i = m_game.HudItem(item))
						(i->shape == Shape2D::Line ? i->stroke : i->fill) = *c;
					return Ok(Value());
				})
			.Fn("position", [this](std::string_view item) -> Value {
				const Item2D *i = m_game.HudItem(item);
				return i ? Value(script::List{Value(double(i->position.x)), Value(double(i->position.y))}) : Value();
			});
		// Dessin immédiat : visible jusqu'au pas suivant (jauges, viseurs, compteurs).
		const auto colorOr = [](const std::optional<std::string> &text, modeling::Color fallback) -> Result<modeling::Color, std::string> {
			if (!text)
				return Ok(fallback);
			if (const auto c = ParseColor(*text))
				return Ok(*c);
			return Err(std::format("couleur « {} » invalide (#rrggbb attendu)", *text));
		};
		const auto draw = [this, colorOr](Shape2D shape, math::FVector2 at, math::FVector2 size, const std::optional<std::string> &color,
										  modeling::Color fallback) -> Result<Item2D, std::string> {
			auto c = colorOr(color, fallback);
			if (c.IsError())
				return Err(c.Error());
			Item2D item;
			item.shape = shape;
			item.position = at;
			item.size = size;
			item.cornerRadius = 0.f;
			(shape == Shape2D::Line ? item.stroke : item.fill) = c.Value();
			return Ok(std::move(item));
		};
		const auto emit = [this](Result<Item2D, std::string> item) -> Result<Value, std::string> {
			if (item.IsError())
				return Err(item.Error());
			m_game.Draw2D(std::move(item.Value()));
			return Ok(Value());
		};
		script::Binder(m_interp, "draw2d")
			.Fn("rect",
				[draw, emit](float x, float y, float w, float h, std::optional<std::string> color) {
					return emit(draw(Shape2D::Rect, {x, y}, {w, h}, color, {1.f, 1.f, 1.f, 1.f}));
				})
			.Fn("circle",
				[draw, emit](float x, float y, float radius, std::optional<std::string> color) {
					return emit(draw(Shape2D::Circle, {x, y}, {radius * 2.f, radius * 2.f}, color, {1.f, 1.f, 1.f, 1.f}));
				})
			.Fn("line",
				[draw, emit](float x1, float y1, float x2, float y2, std::optional<std::string> color, std::optional<float> width) {
					auto item = draw(Shape2D::Line, {x1, y1}, {x2 - x1, y2 - y1}, color, {1.f, 1.f, 1.f, 1.f});
					if (item.IsOk())
						item.Value().strokeWidth = width.value_or(2.f);
					return emit(std::move(item));
				})
			.Fn("text", [draw, emit](float x, float y, Value text, std::optional<float> size, std::optional<std::string> color) {
				auto item = draw(Shape2D::Text, {x, y}, {0.f, 0.f}, color, {1.f, 1.f, 1.f, 1.f});
				if (item.IsOk()) {
					item.Value().text = String(text.ToString());
					item.Value().fontSize = size.value_or(24.f);
				}
				return emit(std::move(item));
			});
		// Dégradé de couleurs (`math::ColorGradient`) : `gradient.sample(["#…", "#…"], t)` → « #rrggbb ».
		script::Binder(m_interp, "gradient")
			.Fn("sample", [](Value stops, float t) -> std::string {
				math::ColorGradient g;
				if (const script::List *list = stops.AsList(); list)
					for (size_t i = 0; i < list->size(); ++i)
						if (const auto c = math::Color::FromHex((*list)[i].AsString())) {
							const float pos = list->size() > 1 ? float(i) / float(list->size() - 1) : 0.f;
							g.AddPoint({pos, *c});
						}
				const math::Color col = g.Sample(std::clamp(t, 0.f, 1.f));
				return std::format("#{:02x}{:02x}{:02x}", unsigned(col.r), unsigned(col.g), unsigned(col.b));
			});
	}

	/// Exécute le source (définitions) ; faux et message en cas d'erreur.
	/// Exécute le source, instancie la classe du groupe (ou de la scène) ; faux et message en cas
	/// d'erreur. Sans classe : les fonctions libres `on_*` servent de rappels.
	bool Load(std::string_view source, const ScriptParams &params = {}) {
		auto r = m_interp.Run(source, m_name);
		if (r.IsError()) {
			Fail(r.Error());
			return false;
		}
		// Classes du script dérivées des bases du moteur ; la plus dérivée est celle du groupe.
		const std::string_view root = m_id != 0 ? "Behaviour" : "Scene";
		std::vector<script::Value> candidates;
		for (const script::Value &c : m_interp.Classes()) {
			const script::ClassDef &def = *c.AsClass();
			const bool engine = m_id != 0 ? def.Derives("Behaviour") || def.Derives("PhysicsBody") || def.Derives("Light3D") || def.Derives("AudioSource")
										  : def.Derives("Scene");
			if (engine)
				candidates.push_back(c);
		}
		std::erase_if(candidates, [&](const script::Value &c) { // une classe parente d'une autre candidate n'est pas « la » classe
			return std::ranges::any_of(candidates, [&](const script::Value &d) { return !d.Equals(c) && d.AsClass()->Derives(c.AsClass()->name); });
		});
		if (candidates.size() > 1) {
			std::string names;
			for (const script::Value &c : candidates)
				names += (names.empty() ? "" : ", ") + c.AsClass()->name;
			Fail(script::ScriptError{std::format("plusieurs classes {} possibles : {}", root, names), m_name, 0, 0});
			return false;
		}
		const auto hook = [&](std::string_view name) { return m_self.IsNil() ? m_interp.GetGlobal(name) : m_interp.Method(m_self, name); };
		if (candidates.size() == 1) {
			m_creatingSelf = true;
			auto object = m_interp.Instantiate(candidates[0], {}, [this, &params](script::Instance &o) {
				for (const ScriptParam &p : params) // paramètres réglés dans l'inspecteur, avant init
					o.fields.insert_or_assign(std::string(p.name), std::visit(
																	   [](const auto &v) -> script::Value {
																		   if constexpr (std::is_same_v<std::decay_t<decltype(v)>, String>)
																			   return script::Value(std::string(v));
																		   else
																			   return script::Value(v);
																	   },
																	   p.value));
				for (auto &[native, state] : o.states) // chaque base du moteur vise ce groupe
					if (native->name == "Scene")
						state = std::make_shared<SceneHandle>(SceneHandle{&m_game});
					else
						state = std::make_shared<GroupHandle>(GroupHandle{&m_game, m_id});
			});
			m_creatingSelf = false;
			if (object.IsError()) {
				Fail(object.Error());
				return false;
			}
			m_self = std::move(object.Value());
			m_class = candidates[0].AsClass()->name;
		}
		m_onStart = hook("OnStart");
		m_onUpdate = hook("OnUpdate");
		m_onTouch = hook("OnTouch");
		m_onUse = hook("OnUse");
		m_onTrigger = hook("OnTrigger");
		return true;
	}
	[[nodiscard]] const std::string &ClassName() const noexcept { return m_class; }

	[[nodiscard]] EntityId Id() const noexcept { return m_id; }
	void Start() { Call(m_onStart, {}); }
	void Update(float dt) {
		Call(m_onUpdate, std::array{script::Value(double{dt})});
		for (size_t i = 0; i < m_spawned.size() && !m_failed; ++i) { // comportements attachés à la volée
			const script::Value object = m_spawned[i].first;
			if (!m_spawned[i].second) {
				m_spawned[i].second = true;
				Call(m_interp.Method(object, "OnStart"), {});
			}
			Call(m_interp.Method(object, "OnUpdate"), std::array{script::Value(double{dt})});
		}
	}
	/// Contact : `OnTouch(autre)` d'un groupe, `OnTouch(groupe, autre)` de la scène.
	void Touch(const std::string &self, const std::string &other) {
		if (m_id != 0)
			Call(m_onTouch, std::array{script::Value(other)});
		else
			Call(m_onTouch, std::array{script::Value(self), script::Value(other)});
	}
	/// Zone : `OnTrigger(objet, évènement)` d'un groupe, `OnTrigger(zone, objet, évènement)` de la scène.
	void Trigger(const std::string &zone, const std::string &other, const std::string &event) {
		if (m_id != 0)
			Call(m_onTrigger, std::array{script::Value(other), script::Value(event)});
		else
			Call(m_onTrigger, std::array{script::Value(zone), script::Value(other), script::Value(event)});
	}
	bool Use() {
		if (!m_onUse.IsCallable())
			return false;
		Call(m_onUse, {});
		return true;
	}

private:
	static std::vector<double> Vec(const math::FVector3 &v) { return {v.x, v.y, v.z}; }
	void Call(const script::Value &fn, std::span<const script::Value> args) {
		if (m_failed || !fn.IsCallable())
			return;
		if (auto r = m_interp.Call(fn, args); r.IsError())
			Fail(r.Error());
	}
	void Fail(const script::ScriptError &e) {
		m_failed = true; // un script en erreur est arrêté, la partie continue
		m_game.ScriptSay("Script « " + m_name + " » : " + e.Format(), m_id, true, e.line);
	}

	script::Interpreter m_interp;
	GameSession &m_game;
	EntityId m_id;
	std::string m_name;
	bool m_failed = false;
	script::Value m_onStart, m_onUpdate, m_onTouch, m_onUse, m_onTrigger;
	script::Value m_self; ///< instance de la classe du groupe (nil : script sans classe)
	std::vector<std::pair<script::Value, bool>> m_spawned; ///< comportements attachés par le script (démarré ?)
	bool m_creatingSelf = false;						   ///< instanciation de la classe du groupe en cours
	std::string m_class;

};

// ── Session ──────────────────────────────────────────────────────────────────

GameSession::GameSession(const Document &doc, const math::FVector3 &eye, const math::FVector3 &target, SoundSystem *sound, ModuleLoader modules,
						 ScriptHook hook)
	: m_settings(doc.Game()), m_sound(sound), m_overlay(doc.Overlay()), m_modules(std::move(modules)), m_hook(std::move(hook)) {
	m_physics.AddTriangles(WorldTriangles(doc.Root(), math::FMatrix4::Identity()), kWorld);
	for (const Entity &e : doc.Entities()) {
		if (!doc.IsVisible(e))
			continue;
		Actor a;
		a.id = e.id;
		a.name = std::string(e.name);
		a.transform = e.transform;
		a.local = e.model.Bounds();
		a.behavior = e.behavior;
		a.lightOn = e.behavior.light;
		a.parent = e.parent;
		a.scale = {math::FVector3{e.transform.m[0][0], e.transform.m[0][1], e.transform.m[0][2]}.Length(),
				   math::FVector3{e.transform.m[1][0], e.transform.m[1][1], e.transform.m[1][2]}.Length(),
				   math::FVector3{e.transform.m[2][0], e.transform.m[2][1], e.transform.m[2][2]}.Length()};
		// Mobile : scripté, dynamique, ou descendant d'un tel groupe (il le suivra).
		bool scripted = !e.behavior.script.IsEmpty();
		for (const Entity *up = doc.Find(e.parent); up && !scripted; up = doc.Find(up->parent))
			scripted = !up->behavior.script.IsEmpty() || up->behavior.body == BodyKind::Dynamic;
		const math::FAABB box = WorldBox(a);
		if (e.behavior.body == BodyKind::Solid && !scripted) {
			m_physics.AddTriangles(WorldTriangles(e.model, e.transform), e.id); // décor exact
		} else if (e.behavior.body == BodyKind::Solid) {
			a.mesh = m_physics.AddMesh(WorldTriangles(e.model, math::FMatrix4::Identity()), e.transform, e.id); // forme exacte, mobile
		} else if (box.min.x <= box.max.x) {
			physics::Body b;
			b.center = box.Center();
			b.half = math::Max(box.Extent(), math::FVector3{0.02f, 0.02f, 0.02f});
			b.dynamic = e.behavior.body == BodyKind::Dynamic;
			b.ghost = e.behavior.body == BodyKind::Ghost;
			b.mass = e.behavior.mass;
			b.bounce = e.behavior.bounce;
			b.friction = e.behavior.friction;
			b.user = e.id;
			a.body = m_physics.AddBody(b);
			a.bodyOffset = b.center - math::FVector3{e.transform.m[3][0], e.transform.m[3][1], e.transform.m[3][2]};
		}
		m_actors.push_back(std::move(a));
	}
	// Scripts (après la création de tous les groupes : game.position(nom) les trouve tous) ;
	// celui de la scène d'abord.
	if (!m_settings.script.IsEmpty())
		if (auto s = std::make_unique<EntityScript>(*this, 0, "scène"); s->Load(m_settings.script, m_settings.params))
			m_scripts.push_back(std::move(s));
	for (const Entity &e : doc.Entities())
		if (doc.IsVisible(e) && !e.behavior.script.IsEmpty()) {
			auto s = std::make_unique<EntityScript>(*this, e.id, std::string(e.name));
			if (s->Load(e.behavior.script, e.behavior.params))
				m_scripts.push_back(std::move(s));
		}
	// Joueur : posé sous la caméra de l'éditeur (sur le décor s'il y en a dessous).
	math::FVector3 feet = eye - math::FVector3{0.f, m_settings.eyeHeight, 0.f};
	if (const auto hit = m_physics.Raycast({eye, {0.f, -1.f, 0.f}}, 1000.f))
		feet = hit->point;
	m_spawn = feet + math::FVector3{0.f, kPlayerHalf.y + 0.02f, 0.f};
	physics::Body player;
	player.center = m_spawn;
	player.half = kPlayerHalf;
	player.mass = 75.f;
	player.bounce = 0.f;
	player.friction = 0.f; // vitesse imposée par les commandes
	player.stepHeight = m_settings.stepHeight;
	player.user = kPlayer;
	m_player = m_physics.AddBody(player);
	const math::FVector3 dir = (target - eye).Normalized();
	m_yaw = std::atan2(dir.x, -dir.z);
	m_pitch = std::clamp(std::asin(std::clamp(dir.y, -1.f, 1.f)), -0.4f, 0.4f);
	const math::FAABB bounds = doc.Bounds();
	m_floor = (bounds.min.y <= bounds.max.y ? bounds.min.y : 0.f) - 30.f;
}

GameSession::~GameSession() {
	if (m_sound)
		for (const Actor &a : m_actors)
			if (a.voice)
				m_sound->Stop(a.voice);
}

GameSession::Actor *GameSession::Find(EntityId id) {
	const auto it = std::ranges::find(m_actors, id, &Actor::id);
	return it == m_actors.end() ? nullptr : &*it;
}

const GameSession::Actor *GameSession::Find(EntityId id) const { return const_cast<GameSession *>(this)->Find(id); }

math::FAABB GameSession::WorldBox(const Actor &a) const {
	math::FAABB box{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
	if (a.local.min.x > a.local.max.x)
		return box;
	for (int i = 0; i < 8; ++i) {
		const math::FVector3 p =
			a.transform.TransformPoint({i & 1 ? a.local.max.x : a.local.min.x, i & 2 ? a.local.max.y : a.local.min.y, i & 4 ? a.local.max.z : a.local.min.z});
		box.min = math::Min(box.min, p);
		box.max = math::Max(box.max, p);
	}
	return box;
}

void GameSession::SyncBody(Actor &a) {
	if (a.mesh >= 0)
		m_physics.SetTransform(a.mesh, a.transform);
	if (a.body < 0)
		return;
	const math::FAABB box = WorldBox(a);
	physics::Body &b = m_physics.GetBody(a.body);
	b.center = box.Center();
	b.half = math::Max(box.Extent(), math::FVector3{0.02f, 0.02f, 0.02f});
	a.bodyOffset = b.center - math::FVector3{a.transform.m[3][0], a.transform.m[3][1], a.transform.m[3][2]};
}

void GameSession::SetEntityRotation(EntityId id, const math::FVector3 &degrees) {
	if (Actor *a = Find(id)) {
		const math::FMatrix4 before = a->transform;
		const math::FVector3 t{a->transform.m[3][0], a->transform.m[3][1], a->transform.m[3][2]};
		a->transform = math::FMatrix4::Translation(t) * math::FMatrix4::Rotation({0.f, 1.f, 0.f}, degrees.y * math::DEG2RAD) *
					   math::FMatrix4::Rotation({1.f, 0.f, 0.f}, degrees.x * math::DEG2RAD) * math::FMatrix4::Rotation({0.f, 0.f, 1.f}, degrees.z * math::DEG2RAD) *
					   math::FMatrix4::Scale(a->scale);
		SyncBody(*a);
		CarryChildren(*a, a->transform * before.Inverse());
	}
}

void GameSession::SetEntityScale(EntityId id, const math::FVector3 &scale) {
	if (Actor *a = Find(id)) {
		const math::FMatrix4 before = a->transform;
		math::FMatrix4 m = a->transform;
		for (int c = 0; c < 3; ++c) { // colonnes : axes tournés, remis à la nouvelle échelle
			const math::FVector3 axis = math::FVector3{m.m[c][0], m.m[c][1], m.m[c][2]}.Normalized() * scale[c];
			m.m[c][0] = axis.x;
			m.m[c][1] = axis.y;
			m.m[c][2] = axis.z;
		}
		a->transform = m;
		a->scale = scale;
		SyncBody(*a);
		CarryChildren(*a, a->transform * before.Inverse());
	}
}

math::FVector3 GameSession::OriginOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? math::FVector3{a->transform.m[3][0], a->transform.m[3][1], a->transform.m[3][2]} : math::FVector3{};
}

std::string GameSession::PathOf(EntityId id) const {
	std::string path;
	int guard = 0;
	for (const Actor *a = Find(id); a && guard < 256; a = Find(a->parent), ++guard)
		path = path.empty() ? a->name : a->name + "/" + path;
	return path;
}

EntityId GameSession::Resolve(std::string_view ref) const {
	if (ref.empty())
		return 0;
	EntityId suffix = 0;
	for (const Actor &a : m_actors) {
		const std::string path = PathOf(a.id);
		if (path == ref)
			return a.id;
		if (!suffix && path.size() > ref.size() && path.ends_with(ref) && path[path.size() - ref.size() - 1] == '/')
			suffix = a.id; // fin de chemin (« Torche 1/Flamme »)
	}
	return suffix ? suffix : FindByName(ref);
}

script::Value GameSession::PropOf(EntityId id, std::string_view key) const {
	if (const Actor *a = Find(id))
		for (const ScriptParam &p : a->behavior.params)
			if (std::string_view(p.name) == key)
				return std::visit(
					[](const auto &v) -> script::Value {
						if constexpr (std::is_same_v<std::decay_t<decltype(v)>, String>)
							return script::Value(std::string(v));
						else
							return script::Value(v);
					},
					p.value);
	return {};
}

float GameSession::LightIntensityOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? a->behavior.lightIntensity : 0.f;
}

float GameSession::LightRangeOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? a->behavior.lightRange : 0.f;
}

void GameSession::SetPlayerSpeed(float walk, float run) {
	m_settings.walkSpeed = std::max(0.f, walk);
	m_settings.runSpeed = std::max(m_settings.walkSpeed, run);
}

void GameSession::SetLightIntensity(EntityId id, float intensity) {
	if (Actor *a = Find(id))
		a->behavior.lightIntensity = std::max(0.f, intensity);
}

void GameSession::SetLightRange(EntityId id, float range) {
	if (Actor *a = Find(id))
		a->behavior.lightRange = std::max(0.1f, range);
}

void GameSession::SetLightColor(EntityId id, const modeling::Color &color) {
	if (Actor *a = Find(id))
		a->behavior.lightColor = color;
}

void GameSession::SetMass(EntityId id, float mass) {
	if (Actor *a = Find(id); a && a->body >= 0)
		m_physics.GetBody(a->body).mass = std::max(0.01f, mass);
}

Result<std::vector<std::pair<std::string, script::Value>>, std::string> ScriptExports(std::string_view source, bool scene,
																						const std::function<Result<std::string, std::string>(std::string_view)> &modules) {
	script::Interpreter probe; // définitions seulement : aucune méthode n'est appelée
	probe.SetStepLimit(200'000);
	probe.SetPrint([](std::string_view) {});
	RegisterEngineClasses(probe);
	for (const char *module : {"game", "input", "hud", "draw2d", "self", "math"})
		if (probe.GetGlobal(module).IsNil())
			probe.SetGlobal(module, script::Value(script::Map{}));
	if (modules)
		probe.SetModuleLoader(modules);
	if (auto r = probe.Run(source, "paramètres"); r.IsError())
		return Err(r.Error().Format());
	for (const script::Value &c : probe.Classes()) {
		const script::ClassDef &def = *c.AsClass();
		const bool engine = scene ? def.Derives("Scene") : def.Derives("Behaviour") || def.Derives("PhysicsBody") || def.Derives("Light3D") || def.Derives("AudioSource");
		const bool parentOfOther = std::ranges::any_of(probe.Classes(), [&](const script::Value &d) { return !d.Equals(c) && d.AsClass()->Derives(def.name); });
		if (engine && !parentOfOther) {
			auto params = probe.Exports(c);
			if (params.IsError())
				return Err(params.Error().Format());
			return Ok(std::move(params.Value()));
		}
	}
	return Ok(std::vector<std::pair<std::string, script::Value>>{});
}

std::unordered_map<std::string, std::string> ExportComments(std::string_view source) {
	std::unordered_map<std::string, std::string> out;
	size_t pos = 0;
	while ((pos = source.find("export let ", pos)) != std::string_view::npos) {
		pos += 11; // « export let »
		while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t'))
			++pos;
		const size_t nameStart = pos;
		while (pos < source.size() && (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_'))
			++pos;
		const std::string name(source.substr(nameStart, pos - nameStart));
		// Commentaire « # … » sur la même ligne, hors des chaînes de caractères.
		const size_t nl = source.find('\n', pos);
		const size_t lineEnd = nl == std::string_view::npos ? source.size() : nl;
		size_t hash = std::string_view::npos;
		bool inString = false;
		for (size_t i = pos; i < lineEnd; ++i) {
			if (source[i] == '"')
				inString = !inString;
			else if (source[i] == '#' && !inString) {
				hash = i;
				break;
			}
		}
		if (!name.empty() && hash != std::string_view::npos) {
			std::string_view comment = source.substr(hash + 1, nl == std::string_view::npos ? std::string_view::npos : nl - hash - 1);
			while (!comment.empty() && (comment.front() == ' ' || comment.front() == '\t'))
				comment.remove_prefix(1);
			out[name] = std::string(comment);
		}
	}
	return out;
}

Result<std::string, std::string> GameSession::LoadModule(std::string_view name) const {
	if (!m_modules)
		return Err(std::string("aucun projet : pas de modules de script"));
	return m_modules(name);
}

std::string GameSession::ClassOf(EntityId id) const {
	for (const auto &s : m_scripts)
		if (s->Id() == id)
			return s->ClassName();
	return {};
}

void GameSession::Say(std::string text, bool error) {
	GameMessage m;
	m.text = std::move(text);
	m.time = m_time;
	m.error = error;
	m_journal.push_back(m);
	m_messages.push_back(std::move(m));
	while (m_messages.size() > kMaxMessages)
		m_messages.pop_front();
}

void GameSession::ScriptSay(std::string text, EntityId source, bool error, int line) {
	Say(std::move(text), error);
	m_journal.back().source = m_messages.back().source = source;
	m_journal.back().script = m_messages.back().script = true;
	m_journal.back().line = m_messages.back().line = line;
}

EntityId GameSession::FindByName(std::string_view name) const {
	const auto it = std::ranges::find(m_actors, name, &Actor::name);
	return it == m_actors.end() ? 0 : it->id;
}

EntityId GameSession::SpawnNode(std::string name, std::string_view parent, const math::FVector3 &pos, bool visible, std::string tag) {
	Actor a;
	a.id = m_nextRuntimeId++;
	a.name = std::move(name);
	a.parent = parent.empty() ? 0 : Resolve(parent);
	a.transform = math::FMatrix4::Translation(pos);
	a.local = math::FAABB{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}}; // repère : ni maillage ni corps
	a.visible = visible;
	a.runtime = true;
	a.tag = std::move(tag);
	m_actors.push_back(std::move(a));
	return m_actors.back().id;
}

EntityId GameSession::CreateGroup(std::string name) { return SpawnNode(std::move(name), {}, math::FVector3{}, true, {}); }

void GameSession::RemoveEntity(EntityId id) {
	std::vector<EntityId> toRemove;
	std::function<void(EntityId)> collect = [&](EntityId p) {
		for (const Actor &c : m_actors)
			if (c.parent == p) {
				toRemove.push_back(c.id);
				collect(c.id);
			}
	};
	collect(id);
	toRemove.push_back(id);
	for (EntityId i : toRemove)
		std::erase_if(m_actors, [i](const Actor &x) { return x.id == i; });
}

std::vector<std::string> GameSession::FindByTag(std::string_view tag) const {
	std::vector<std::string> names;
	for (const Actor &a : m_actors)
		if (a.tag == tag)
			names.push_back(a.name);
	return names;
}

math::FVector3 GameSession::RotationOf(EntityId id) const {
	const Actor *a = Find(id);
	if (!a)
		return {};
	// Lacet (Y) : l'axe Z tourné (colonne 2) vaut (sin, 0, cos) pour une rotation pure.
	const float yaw = std::atan2(a->transform.m[2][0], a->transform.m[2][2]) * math::RAD2DEG;
	return {0.f, yaw, 0.f};
}

std::string GameSession::NameOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? a->name : std::string("décor");
}

math::FVector3 GameSession::CenterOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? WorldBox(*a).Center() : math::FVector3{};
}

void GameSession::MoveEntity(EntityId id, const math::FVector3 &delta) {
	if (Actor *a = Find(id)) {
		const math::FMatrix4 move = math::FMatrix4::Translation(delta);
		a->transform = move * a->transform;
		SyncBody(*a);
		CarryChildren(*a, move);
	}
}

void GameSession::CarryChildren(const Actor &a, const math::FMatrix4 &delta) {
	for (Actor &child : m_actors)
		if (child.parent == a.id) {
			child.transform = delta * child.transform;
			SyncBody(child);
			CarryChildren(child, delta);
		}
}

void GameSession::SetEntityCenter(EntityId id, const math::FVector3 &center) { MoveEntity(id, center - CenterOf(id)); }

void GameSession::RotateEntity(EntityId id, float degrees, bool aroundOrigin) {
	if (Actor *a = Find(id)) {
		const math::FVector3 c =
			aroundOrigin ? math::FVector3{a->transform.m[3][0], a->transform.m[3][1], a->transform.m[3][2]} : WorldBox(*a).Center();
		const math::FMatrix4 turn =
			math::FMatrix4::Translation(c) * math::FMatrix4::Rotation({0.f, 1.f, 0.f}, degrees * math::DEG2RAD) * math::FMatrix4::Translation(-c);
		a->transform = turn * a->transform;
		SyncBody(*a);
		CarryChildren(*a, turn);
	}
}

void GameSession::PushEntity(EntityId id, const math::FVector3 &velocity) {
	if (Actor *a = Find(id); a && a->body >= 0)
		m_physics.GetBody(a->body).velocity += velocity;
}

math::FVector3 GameSession::VelocityOf(EntityId id) const {
	const Actor *a = Find(id);
	return a && a->body >= 0 ? m_physics.GetBody(a->body).velocity : math::FVector3{};
}

void GameSession::SetVisible(EntityId id, bool visible) {
	if (Actor *a = Find(id)) {
		a->visible = visible;
		if (a->body >= 0) // caché : traversable (le décor exact, lui, reste solide)
			m_physics.GetBody(a->body).ghost = !visible || a->behavior.body == BodyKind::Ghost;
		if (a->mesh >= 0)
			m_physics.SetMeshEnabled(a->mesh, visible);
	}
}

void GameSession::SetLight(EntityId id, bool on) {
	if (Actor *a = Find(id))
		a->lightOn = on;
}

void GameSession::PlaySound(EntityId id, const std::string &file) {
	Actor *a = Find(id);
	if (!a || !m_sound)
		return;
	const std::string clip = file.empty() ? std::string(a->behavior.sound) : file;
	if (clip.empty())
		return;
	if (a->voice)
		m_sound->Stop(a->voice);
	a->voice = m_sound->Play(clip, a->behavior.volume, a->behavior.loop && file.empty(),
							 a->behavior.spatial ? std::optional<math::FVector3>(CenterOf(id)) : std::nullopt);
}

void GameSession::StopSound(EntityId id) {
	if (Actor *a = Find(id); a && a->voice && m_sound) {
		m_sound->Stop(a->voice);
		a->voice = 0;
	}
}

bool GameSession::KeyDown(std::string_view name) const { return m_input.keys.contains(std::string(name)); }

void GameSession::TriggerSound(Actor &a, SoundTrigger trigger) {
	if (a.behavior.sound.IsEmpty() || a.behavior.soundTrigger != trigger || !m_sound)
		return;
	if (a.behavior.loop && a.voice && m_sound->IsPlaying(a.voice))
		return; // une boucle en cours n'est pas relancée
	PlaySound(a.id, std::string());
}

void GameSession::Touched(Actor &a, UserId other) {
	const std::string name = other == kPlayer ? std::string("joueur") : NameOf(other);
	for (auto &s : m_scripts)
		if (s->Id() == a.id || s->Id() == 0)
			s->Touch(a.name, name);
	TriggerSound(a, SoundTrigger::Touch);
}

void GameSession::Triggered(Actor &zone, UserId other) {
	if (zone.behavior.triggerOnce && zone.triggered)
		return;
	const Actor *mover = Find(other);
	if (other != kPlayer && (!mover || mover->behavior.body != BodyKind::Dynamic))
		return; // seuls le joueur et les objets dynamiques déclenchent
	zone.triggered = true;
	const std::string name = other == kPlayer ? std::string("joueur") : NameOf(other), event(zone.behavior.triggerEvent);
	for (auto &s : m_scripts)
		if (s->Id() == zone.id || s->Id() == 0)
			s->Trigger(zone.name, name, event);
}

void GameSession::Used(Actor &a) {
	bool handled = false;
	for (auto &s : m_scripts)
		if (s->Id() == a.id)
			handled |= s->Use();
	TriggerSound(a, SoundTrigger::Use);
	if (!handled && (a.behavior.sound.IsEmpty() || a.behavior.soundTrigger != SoundTrigger::Use) && a.behavior.body == BodyKind::Dynamic) {
		// Objet sans action : la main le pousse.
		const math::FVector3 dir = math::DirectionFromYawPitch(m_yaw, 0.f);
		PushEntity(a.id, dir * 3.f + math::FVector3{0.f, 1.5f, 0.f});
	}
}

sdl3::Camera3D GameSession::Camera() const {
	if (m_freeCamera)
		return *m_freeCamera; // caméra libre posée par un script
	sdl3::Camera3D cam;
	const physics::Body &p = m_physics.GetBody(m_player);
	cam.position = p.center + math::FVector3{0.f, m_settings.eyeHeight - p.half.y, 0.f};
	cam.target = cam.position + math::DirectionFromYawPitch(m_yaw, m_pitch);
	cam.fovY = 70.f * math::DEG2RAD;
	cam.zNear = 0.05f;
	cam.zFar = 2000.f;
	return cam;
}

void GameSession::SetCamera(const math::FVector3 &position, const math::FVector3 &target) {
	sdl3::Camera3D cam;
	cam.position = position;
	cam.target = target;
	cam.fovY = 70.f * math::DEG2RAD;
	cam.zNear = 0.05f;
	cam.zFar = 2000.f;
	m_freeCamera = cam;
}

math::FVector3 GameSession::PlayerFeet() const {
	const physics::Body &p = m_physics.GetBody(m_player);
	return p.center - math::FVector3{0.f, p.half.y, 0.f};
}

bool GameSession::PlayerGrounded() const { return m_physics.GetBody(m_player).grounded; }

math::FMatrix4 GameSession::TransformOf(EntityId id) const {
	const Actor *a = Find(id);
	return a ? a->transform : math::FMatrix4::Identity();
}

bool GameSession::IsVisible(EntityId id) const {
	const Actor *a = Find(id);
	return a && a->visible;
}

void AddLamp(const Behavior &b, const math::FMatrix4 &transform, const math::FVector3 &center, sdl3::Lighting &lighting) {
	if (b.lightKind == LightKind::Spot) {
		sdl3::SpotLight spot;
		spot.position = center;
		spot.direction = transform.TransformDirection({0.f, -1.f, 0.f}).Normalized();
		spot.color = Linear(b.lightColor);
		spot.intensity = b.lightIntensity;
		spot.range = b.lightRange;
		spot.angle = std::clamp(b.spotAngle, 1.f, 89.f) * math::DEG2RAD;
		spot.penumbra = std::clamp(b.penumbra, 0.f, 1.f);
		lighting.spots.push_back(spot);
	} else {
		lighting.points.push_back({center, Linear(b.lightColor), b.lightIntensity, b.lightRange});
	}
}

void GameSession::Lights(sdl3::Lighting &lighting) const {
	for (const Actor &a : m_actors)
		if (a.visible && a.lightOn)
			AddLamp(a.behavior, a.transform, WorldBox(a).Center(), lighting);
}

void GameSession::Update(float dt, const GameInput &input) {
	m_input = input;
	if (!m_started) {
		m_started = true;
		for (Actor &a : m_actors)
			TriggerSound(a, SoundTrigger::Start);
		for (auto &s : m_scripts)
			s->Start();
	}
	m_time += double(dt);
	// Regard et déplacement du joueur.
	m_yaw += input.lookX * 0.0025f;
	m_pitch = std::clamp(m_pitch - input.lookY * 0.0025f, -1.45f, 1.45f);
	physics::Body &player = m_physics.GetBody(m_player);
	const math::FVector3 forward{std::sin(m_yaw), 0.f, -std::cos(m_yaw)}, right{std::cos(m_yaw), 0.f, std::sin(m_yaw)};
	math::FVector3 wish = forward * input.forward + right * input.strafe;
	if (wish.LengthSq() > 1.f)
		wish = wish.Normalized();
	const math::FVector3 target = wish * (input.run ? m_settings.runSpeed : m_settings.walkSpeed);
	const float control = player.grounded ? 1.f : std::min(1.f, dt * 2.f); // peu de contrôle en l'air
	player.velocity.x += (target.x - player.velocity.x) * control;
	player.velocity.z += (target.z - player.velocity.z) * control;
	if (input.jump && player.grounded)
		player.velocity.y = m_settings.jumpSpeed;
	m_drawing.clear();
	for (auto &s : m_scripts)
		s->Update(dt);
	m_drawn = std::move(m_drawing); // les dessins de ce pas remplacent ceux du précédent
	m_physics.Step(dt, {0.f, -m_settings.gravity, 0.f});
	// Les groupes dynamiques suivent leur corps.
	for (Actor &a : m_actors)
		if (a.body >= 0 && a.behavior.body == BodyKind::Dynamic) {
			const math::FVector3 t = m_physics.GetBody(a.body).center - a.bodyOffset;
			const math::FVector3 was{a.transform.m[3][0], a.transform.m[3][1], a.transform.m[3][2]};
			a.transform.m[3][0] = t.x;
			a.transform.m[3][1] = t.y;
			a.transform.m[3][2] = t.z;
			if ((t - was).LengthSq() > 0.f)
				CarryChildren(a, math::FMatrix4::Translation(t - was));
		}
	// Nouveaux contacts : OnTouch et sons « au contact ». Un corps reçoit ses propres
	// évènements ; le décor et les maillages (sans corps) sont prévenus par celui qui les touche.
	for (const physics::ContactEvent &e : m_physics.Events()) {
		if (!e.began)
			continue;
		if (Actor *self = e.body != m_player ? Find(e.self) : nullptr; self && self->body == e.body) {
			Touched(*self, e.other);
			if (self->behavior.body == BodyKind::Ghost && self->visible)
				Triggered(*self, e.other);
		}
		if (Actor *other = Find(e.other); other && other->body < 0)
			Touched(*other, e.self);
	}
	// Chute hors du modèle : retour au départ.
	if (m_physics.GetBody(m_player).center.y < m_floor) {
		physics::Body &p = m_physics.GetBody(m_player);
		p.center = m_spawn;
		p.velocity = {};
		Say("Retour au point de départ");
	}
	// Visée et action (E).
	const sdl3::Camera3D cam = Camera();
	const math::FVector3 look = (cam.target - cam.position).Normalized();
	m_aimed = 0;
	if (const auto hit = m_physics.Raycast({cam.position, look}, kReach, m_player); hit && hit->user != kWorld && hit->user != kPlayer && Find(hit->user))
		m_aimed = hit->user;
	if (input.use && !m_usePrev && m_aimed)
		Used(*Find(m_aimed));
	m_usePrev = input.use;
	// Sons : l'auditeur suit la tête, les voix spatiales suivent leur groupe.
	if (m_sound) {
		m_sound->SetListener(cam.position, right);
		for (const Actor &a : m_actors)
			if (a.voice && a.behavior.spatial)
				m_sound->SetPosition(a.voice, WorldBox(a).Center());
	}
}

} // namespace sandbox::game
