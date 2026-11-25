// Voxelia — session de jeu : joueur, interactions, systèmes ECS, streaming, rendu.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <format>

#include <core/clock.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/trace.hpp>

#include "app.hpp"

namespace voxelia {

namespace {
constexpr float kTickRate = 20.f;

sdl3::Color KindColor(std::string_view kind) {
	const uint64_t h = core::HashString(kind);
	return sdl3::Rgba(uint8_t(90 + (h & 0x7f)), uint8_t(90 + ((h >> 8) & 0x7f)), uint8_t(90 + ((h >> 16) & 0x7f)), 255);
}

/// Vecteur local (repère du corps, avant = −Z) tourné par le cap `yaw` (même convention
/// que `RotationY` : +X → droite, −Z → avant du joueur).
math::FVector3 YawRotate(const math::FVector3 &v, float yaw) noexcept {
	const float c = std::cos(yaw), s = std::sin(yaw);
	return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

/// Entité la plus proche touchée par le rayon (boîtes englobantes), à moins de `maxDist`.
std::optional<std::pair<ecs::Entity, float>> PickEntityRay(ecs::World &ecs, const math::FVector3 &o, const math::FVector3 &d, float maxDist,
														   ecs::Entity skip) {
	std::optional<std::pair<ecs::Entity, float>> best;
	ecs.MakeQuery<const voxel::Transform, const voxel::Body>().Each([&](ecs::Entity e, const voxel::Transform &t, const voxel::Body &b) {
		if (e == skip || (t.position - o).LengthSq() > (maxDist + 4.f) * (maxDist + 4.f))
			return;
		const math::FVector3 mn = t.position - b.halfExtents, mx = t.position + b.halfExtents;
		float tmin = 0.f, tmax = maxDist;
		for (int a = 0; a < 3; ++a) {
			if (std::fabs(d[a]) < 1e-8f) {
				if (o[a] < mn[a] || o[a] > mx[a])
					return;
				continue;
			}
			float t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
			if (t1 > t2)
				std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return;
		}
		if (!best || tmin < best->second)
			best = std::pair{e, tmin};
	});
	return best;
}

/// Pixels 16×16 (moyenne par blocs) d'une couche de texture de bloc.
void LayerThumbnail(const std::vector<uint8_t> &layers, uint16_t layer, uint8_t *out16, int outPitch) {
	constexpr int kStep = voxel::kTextureSize / 16;
	const uint8_t *px = layers.data() + size_t(layer) * voxel::kTextureSize * voxel::kTextureSize * 4;
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			uint32_t acc[4] = {};
			for (int sy = 0; sy < kStep; ++sy)
				for (int sx = 0; sx < kStep; ++sx) {
					const uint8_t *q = px + (size_t(y * kStep + sy) * voxel::kTextureSize + size_t(x * kStep + sx)) * 4;
					for (int k = 0; k < 4; ++k)
						acc[k] += q[k];
				}
			uint8_t *o = out16 + size_t(y) * size_t(outPitch) + size_t(x) * 4;
			for (int k = 0; k < 4; ++k)
				o[k] = uint8_t(acc[k] / uint32_t(kStep * kStep));
		}
}

/// Matrice de rotation autour de Y (colonne-majeure).
math::FMatrix4 RotationY(float yaw) {
	math::FMatrix4 m = math::FMatrix4::Identity();
	m.m[0][0] = std::cos(yaw);
	m.m[0][2] = std::sin(yaw);
	m.m[2][0] = -std::sin(yaw);
	m.m[2][2] = std::cos(yaw);
	return m;
}

/// Rotation d'Euler (radians, X puis Y puis Z — convention de `voxel::RotateEulerXYZ`).
math::FMatrix4 RotationXYZ(const math::FVector3 &r) {
	const float cx = std::cos(r.x), sx = std::sin(r.x);
	const float cy = std::cos(r.y), sy = std::sin(r.y);
	const float cz = std::cos(r.z), sz = std::sin(r.z);
	math::FMatrix4 m = math::FMatrix4::Identity();
	// Rz · Ry · Rx (colonne-majeure ; Rx appliquée en premier).
	m.m[0][0] = cy * cz;
	m.m[0][1] = cy * sz;
	m.m[0][2] = -sy;
	m.m[1][0] = sx * sy * cz - cx * sz;
	m.m[1][1] = sx * sy * sz + cx * cz;
	m.m[1][2] = sx * cy;
	m.m[2][0] = cx * sy * cz + sx * sz;
	m.m[2][1] = cx * sy * sz - sx * cz;
	m.m[2][2] = cx * cy;
	return m;
}

/// Nom d'animation d'un état de l'avatar (cherché dans les animations du rig édité).
const char *AvatarAnimName(voxel::AvatarState s) {
	switch (s) {
	case voxel::AvatarState::Walk: return "walk";
	case voxel::AvatarState::Crouch: return "crouch";
	case voxel::AvatarState::Sit: return "sit";
	case voxel::AvatarState::Sleep: return "sleep";
	case voxel::AvatarState::Swim: return "swim";
	case voxel::AvatarState::FreeFall: return "freefall";
	case voxel::AvatarState::Fly: return "fly";
	default: return "idle";
	}
}
} // namespace

Game::Game(Content &content, Settings &settings, const ModSettingsStore &modSettings, jobs::ThreadPool &pool, voxel::Renderer &renderer,
		   ui::Context &ui)
	: m_content(content), m_settings(settings), m_modSettings(modSettings), m_pool(pool), m_renderer(renderer), m_ui(ui) {
	m_ecs.SetPool(pool);
}

Game::~Game() {
	if (m_world && m_options.persistent)
		Save();
	m_scripts.reset();
	m_world.reset();
	m_renderer.Clear();
}

bool Game::Open(const fs::path &worldDir, const WorldSettings &world, GameOptions options) {
	m_options = options;
	m_worldSettings = world;
	m_worldDir = worldDir;
	voxel::WorldConfig cfg;
	cfg.directory = worldDir;
	cfg.seed = world.seed;
	cfg.terrain = world.Terrain();
	cfg.structures = [this](std::string_view id) { return m_content.mods.FindStructure(id); };
	m_world = std::make_unique<voxel::World>(m_content.blocks, m_ecs, m_pool, cfg);
	if (!m_world->Open())
		return false;
	ApplySettings();
	m_world->SetMeshContext(m_content.meshContext);
	m_content.mods.SyncToDatabase(m_world->Storage());
	// Inventaire : barre d'outils (9) + 3 rangées ; départ avec les blocs courants.
	m_slots.assign(kInventorySlots, ItemStack{});
	for (size_t i = 0; i < m_content.hotbarCandidates.size() && i < kHotbarSlots; ++i)
		m_slots[i] = ItemStack{ItemId(m_content.hotbarCandidates[i]), world.gameMode == 1 ? 1 : 32};
	m_world->onBlockChanged = [this](const math::IVec3 &p, voxel::BlockId oldId, voxel::BlockId newId) {
		m_world->Storage().LogEvent(m_tickCount, voxel::MakeChunkKey(voxel::ChunkOfVoxel(p.x, p.y, p.z)),
									newId == voxel::kAir ? "block_broken" : "block_placed", "game",
									std::format("{},{},{} {} -> {}", p.x, p.y, p.z, m_content.blocks.Get(oldId).name, m_content.blocks.Get(newId).name));
	};

	// Systèmes ECS, dans l'ordre d'exécution de chaque image.
	m_schedule.Add("world.streaming", [this](ecs::World &, float dt) {
		if (m_cameraOverride) // caméra libre de l'éditeur : le monde se charge autour d'elle
			m_world->Update(m_cameraOverride->position, dt);
		else if (const auto *t = m_ecs.Get<voxel::Transform>(m_player))
			m_world->Update(t->position, dt);
	});
	m_schedule.Add("entities.simulate", [this](ecs::World &, float dt) {
		if (const auto *t = m_ecs.Get<voxel::Transform>(m_player))
			m_world->SimulateEntities(t->position, dt);
	});
	// Joints (attaches, charnières, glissières, poulies, cordes) après l'intégration physique.
	m_schedule.Add("mechanics.joints", [this](ecs::World &ecs, float dt) { m_mechanics = voxel::SolveJoints(ecs, dt); });
	m_schedule.Add("entities.grid", [this](ecs::World &, float) { m_world->UpdateEntityGrid(); });

	CreatePlayer();
	// Inventaire et réglages de l'avatar sauvegardés avec le monde.
	{
		const std::string saved = m_world->Storage().GetMeta("inventory");
		for (const auto &entry : String(saved).Split(';')) {
			const auto kv = String(entry).Split('=');
			if (kv.size() != 2)
				continue;
			const auto item = String(kv[1]).Split('*');
			const ItemId id = m_content.FindItem(item[0]);
			const ItemStack st{id, id && item.size() > 1 ? std::max(1, std::atoi(item[1].c_str())) : 0};
			const std::string &slot = kv[0];
			if (slot.starts_with('a') && slot.size() == 2 && slot[1] >= '0' && slot[1] < '4')
				m_armor[size_t(slot[1] - '0')] = st;
			else if (slot == "o")
				m_offhand = st;
			else if (const int i = std::atoi(slot.c_str()); i >= 0 && size_t(i) < m_slots.size() && !slot.empty() && std::isdigit(slot[0]))
				m_slots[size_t(i)] = st;
		}
		const auto avatar = String(m_world->Storage().GetMeta("avatar")).Split(',');
		if (!avatar.empty())
			if (auto style = voxel::ParseSwimStyle(avatar[0]))
				m_swimStyle = *style;
		if (avatar.size() > 1)
			m_cameraMode = CameraMode(std::clamp(std::atoi(avatar[1].c_str()), 0, int(CameraMode::Count) - 1));
	}
	// Avatar du joueur (skin + objets tenus + armure) : maillage posé à chaque image.
	RefreshAvatarTexture();
	m_timeOfDay = std::strtof(m_world->Storage().GetMeta("time", std::to_string(world.startTime)).c_str(), nullptr);
	if (m_options.cinematic) {
		m_flying = true;
		m_pitch = -0.32f;
	}
	if (!CanFly())
		m_flying = false;
	// Paramètres effectifs des mods → table `config` (lus par voxel.setting et content.config).
	for (const voxel::ModInfo &mod : m_content.mods.Mods())
		for (const voxel::ModSetting &st : mod.settings) {
			const dataio::NodePtr v = m_modSettings.Get(mod, st);
			m_world->Storage().SetConfig(std::format("{}.{}", mod.id, st.id), v->IsString() ? std::string(v->AsString()) : dataio::WriteJson(v, -1),
										 mod.id);
		}
	m_worldSettings.lastPlayed = NowText();
	if (m_options.persistent)
		m_worldSettings.Save(worldDir);
	if (m_options.scripts) {
		m_scripts = std::make_unique<ScriptHost>(*this, m_modSettings);
		m_scripts->LoadMods(m_content.mods);
		Say(std::format("Monde « {} » — graine {}", m_worldSettings.name, m_world->Config().seed), m_ui.Style().accent);
	}
	return true;
}

void Game::ApplySettings() {
	if (!m_world)
		return;
	voxel::WorldConfig &c = m_world->Config();
	if (m_options.preview) { // aperçu du menu : un petit volume autour de la surface, rien de plus
		c.viewDistance = 5;
		c.verticalDistance = 2;
		c.simulationDistance = 1;
		c.entityQueryRadius = 0;
	} else {
		c.viewDistance = std::clamp(m_settings.viewDistance, 2, 32);
		c.verticalDistance = std::clamp(m_settings.verticalDistance, 1, 8);
		c.simulationDistance = std::clamp(m_settings.simulationDistance, 1, 16);
		c.entityQueryRadius = std::clamp(m_settings.entityQueryRadius, 0, 8);
	}
	c.maxPendingLoads = std::clamp(m_settings.maxPendingLoads, 4, 256);
	c.maxPendingMeshes = std::clamp(m_settings.maxPendingMeshes, 2, 128);
	m_renderer.SetUploadBudget(size_t(std::clamp(m_settings.uploadBudgetMB, 1, 256)) << 20);
	m_camera.fovY = m_settings.fov * math::DEG2RAD;
	m_camera.zFar = float(c.viewDistance + 2) * voxel::kChunkSize * 1.5f;
}

void Game::CreatePlayer() {
	math::FVector3 pos{0.5f, 0, 0.5f};
	const std::string saved = m_world->Storage().GetMeta("player");
	bool restored = false;
	if (!saved.empty()) {
		const auto parts = String(saved).Split(',');
		if (parts.size() >= 5) {
			pos = {std::strtof(parts[0].c_str(), nullptr), std::strtof(parts[1].c_str(), nullptr), std::strtof(parts[2].c_str(), nullptr)};
			m_yaw = std::strtof(parts[3].c_str(), nullptr);
			m_pitch = std::strtof(parts[4].c_str(), nullptr);
			m_flying = parts.size() < 6 || parts[5] == "1";
			restored = true;
		}
	}
	if (!restored)
		pos.y = float(m_world->Terrain().SurfaceHeight(0, 0)) + 2.5f;
	m_player = m_ecs.Spawn(voxel::Transform{pos, m_yaw}, voxel::Velocity{}, voxel::Body{{0.3f, 0.9f, 0.3f}}, voxel::Health{20, 20},
						   voxel::Kind{"core:player"}, voxel::Skin{m_settings.playerSkin}, voxel::Transient{}, voxel::GridCell{});
}

void Game::SetPlayerSkin(std::string_view id) {
	if (m_player)
		m_ecs.Add(m_player, voxel::Skin{std::string(id)});
	UploadPlayerSkin();
}

void Game::UploadPlayerSkin() {
	m_avatarTextureKey.clear(); // force la reconstruction de la texture de l'avatar
	RefreshAvatarTexture();
}

void Game::ReloadScripts() {
	if (!m_options.scripts)
		return;
	DIAG_ZONE("Game::ReloadScripts");
	m_scripts = std::make_unique<ScriptHost>(*this, m_modSettings);
	m_scripts->LoadMods(m_content.mods);
	Log("scripts des mods rechargés à chaud");
}

void Game::ApplyWorldSettings(const WorldSettings &w) {
	const bool modeChanged = w.gameMode != m_worldSettings.gameMode;
	m_worldSettings.name = w.name;
	m_worldSettings.description = w.description;
	m_worldSettings.gameMode = w.gameMode;
	m_worldSettings.cheats = w.cheats;
	m_worldSettings.dayCycle = w.dayCycle;
	m_worldSettings.dayLengthMinutes = w.dayLengthMinutes;
	if (modeChanged || !CanFly())
		m_flying = CanFly() && w.gameMode == 1;
}

void Game::Teleport(const math::FVector3 &p) {
	if (auto *t = m_ecs.Get<voxel::Transform>(m_player)) {
		t->position = p;
		m_ecs.Get<voxel::Velocity>(m_player)->value = {};
	}
}

voxel::BlockId Game::SelectedBlock() const noexcept {
	const ItemStack &s = Held(true);
	return !s.Empty() && IsBlockItem(s.id) ? voxel::BlockId(s.id) : voxel::kAir;
}

void Game::Say(std::string text, sdl3::Color c) {
	m_ui.Notify(text, c);
	Log(std::move(text));
}

void Game::Log(std::string text) {
	core::log::Info("{}", text);
	m_console.push_back(std::move(text));
	while (m_console.size() > 400)
		m_console.pop_front();
}

bool Game::Emit(std::string_view type, const script::Value &payload, std::string_view mod) {
	DIAG_ZONE("Game::Emit");
	voxel::ChunkKey chunk = 0;
	if (const auto *t = m_ecs.Get<voxel::Transform>(m_player))
		chunk = voxel::ChunkKeyOfPosition(t->position);
	m_world->Storage().LogEvent(m_tickCount, chunk, type, mod, payload.ToString());
	return m_scripts && m_scripts->Emit(type, payload);
}

std::string Game::ExecuteConsole(const std::string &line) {
	Log("> " + line);
	std::string out = m_scripts ? m_scripts->RunConsole(line) : "";
	if (!out.empty())
		Log(out);
	return out;
}

size_t Game::SpawnCrowd(std::string_view kind, size_t count, float radius) {
	const auto *t = m_ecs.Get<voxel::Transform>(m_player);
	if (!t)
		return 0;
	// Plafond d'entités vivantes autour du joueur (réglage « maxEntities »).
	if (m_settings.maxEntities > 0) {
		const size_t total = m_ecs.EntityCount();
		const size_t room = total >= size_t(m_settings.maxEntities) ? 0 : size_t(m_settings.maxEntities) - total;
		count = std::min(count, room);
	}
	math::Rng rng(static_cast<uint64_t>(m_tickCount) * 7919u + count);
	size_t spawned = 0;
	for (size_t i = 0; i < count; ++i) {
		const float a = rng.NextFloat() * math::TAU, d = std::sqrt(rng.NextFloat()) * radius;
		const int x = math::FastFloor(t->position.x + std::cos(a) * d), z = math::FastFloor(t->position.z + std::sin(a) * d);
		voxel::EntityRecord r;
		r.kind = std::string(kind);
		r.position = {float(x) + 0.5f, float(m_world->SurfaceHeight(x, z)) + 1.2f, float(z) + 0.5f};
		r.halfExtents = {0.35f, 0.45f, 0.35f};
		r.yaw = std::atan2(t->position.x - r.position.x, -(t->position.z - r.position.z)); // face au joueur
		r.wander = true;
		m_world->SpawnEntity(r);
		++spawned;
	}
	return spawned;
}

// ── Mise à jour ───────────────────────────────────────────────────────────
void Game::Update(const PlayerInput &in, float dt) {
	DIAG_ZONE("Game::Update");
	core::Stopwatch sw;
	if (m_reloadPending) {
		m_reloadPending = false;
		ReloadScripts();
	}
	UpdatePlayer(in, dt);
	Interact(in, dt);
	// Pause (Studio) : aucun tick sauf les pas demandés ; le streaming continue.
	const bool stepping = m_paused && m_stepRequests > 0;
	float simDt = dt;
	if (m_paused) {
		m_tickAccumulator = 0;
		simDt = stepping ? 1.f / kTickRate : 0.f;
		if (stepping) {
			FixedTick(1.f / kTickRate);
			--m_stepRequests;
		}
	} else {
		m_stepRequests = 0;
		m_tickAccumulator += dt;
		int ticks = 0;
		while (m_tickAccumulator >= 1.f / kTickRate && ticks < 5) {
			FixedTick(1.f / kTickRate);
			m_tickAccumulator -= 1.f / kTickRate;
			++ticks;
		}
	}
	m_schedule.SetEnabled("entities.simulate", !m_paused || stepping);
	m_schedule.SetEnabled("mechanics.joints", !m_paused || stepping);
	if (m_content.blocks.Count() != m_content.syncedBlocks) { // blocs créés par un script
		if (m_content.SyncNewBlocks())
			m_renderer.SetBlockTextures(m_content.textureLayers, m_content.layerCount);
		m_world->SetMeshContext(m_content.meshContext);
	}
	m_schedule.Run(m_ecs, simDt);
	PumpMeshes();
	if (!m_worldLoadEmitted && m_world->Stats().pendingLoads == 0 && m_world->LoadedChunkCount() > 0) {
		m_worldLoadEmitted = true;
		Emit("player_join", script::Value(script::Map{{"player", script::Value(double(m_player.Bits()))}}));
		Emit("world_load", script::Value());
	}
	m_lastUpdateMs = sw.Millis();
}

void Game::FixedTick(float dt) {
	DIAG_ZONE("Game::FixedTick");
	++m_tickCount;
	if (m_worldSettings.dayCycle) {
		m_timeOfDay += dt / (float(std::max(1, m_worldSettings.dayLengthMinutes)) * 60.f);
		m_timeOfDay -= std::floor(m_timeOfDay);
	}
	// Faim et respiration (une fois par seconde).
	if (m_tickCount % static_cast<int64_t>(kTickRate) == 0 && !m_options.cinematic) {
		if (!m_flying)
			m_hunger = std::max(0.f, m_hunger - 0.04f); // la faim décroît lentement
		const int ex = math::FastFloor(m_camera.position.x), ey = math::FastFloor(m_camera.position.y), ez = math::FastFloor(m_camera.position.z);
		const bool inWater = m_world->IsLoaded(ex, ey, ez) &&
							 m_content.blocks.MaterialOf(m_world->GetBlock(ex, ey, ez)) == voxel::Material::Liquid &&
							 !m_content.blocks.Get(m_world->GetBlock(ex, ey, ez)).HasTag("hot");
		m_air = inWater ? std::max(0.f, m_air - 1.f) : std::min(10.f, m_air + 1.f);
		// Asphyxie (noyade) ou famine : dégâts.
		if (m_air <= 0.f || m_hunger <= 0.f)
			if (auto *h = m_ecs.Get<voxel::Health>(m_player))
				h->current = std::max(0.f, h->current - 1.f);
	}
	if (m_scripts && m_worldLoadEmitted)
		m_scripts->Tick(dt);
	if (m_tickCount % (static_cast<int64_t>(kTickRate) * 60) == 0)
		m_world->Storage().TrimEvents(50'000);
}

void Game::UpdatePlayer(const PlayerInput &in, float dt) {
	DIAG_ZONE("Game::UpdatePlayer");
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	auto *v = m_ecs.Get<voxel::Velocity>(m_player);
	auto *b = m_ecs.Get<voxel::Body>(m_player);
	if (!t || !v || !b)
		return;
	const float sens = m_settings.sensitivity / 1000.f;
	m_yaw += in.lookDX * sens;
	m_pitch = std::clamp(m_pitch - in.lookDY * sens * (m_settings.invertY ? -1.f : 1.f), -1.55f, 1.55f);
	if (m_options.cinematic)
		m_yaw += dt * 0.035f; // panoramique lent derrière les menus
	t->yaw = m_yaw;
	if (in.cycleSwimStyle) {
		m_swimStyle = voxel::SwimStyle((uint8_t(m_swimStyle) + 1) % uint8_t(voxel::SwimStyle::Count));
		Say(std::format("Nage : {}", voxel::SwimStyleLabel(m_swimStyle)));
	}
	if (in.cycleCamera)
		m_cameraMode = CameraMode((uint8_t(m_cameraMode) + 1) % uint8_t(CameraMode::Count));
	if (in.gesture > 0 && in.gesture < int(voxel::AvatarGesture::Count))
		PlayGesture(voxel::AvatarGesture(in.gesture));
	if (in.swapHands)
		std::swap(m_slots[size_t(m_hotbarIndex) % kHotbarSlots], m_offhand);

	const math::FVector3 forward{std::sin(m_yaw), 0, -std::cos(m_yaw)};
	const math::FVector3 right{std::cos(m_yaw), 0, std::sin(m_yaw)};
	math::FVector3 wish = forward * in.moveZ + right * in.moveX;
	if (wish.LengthSq() > 1.f)
		wish = wish.Normalized();
	const bool loaded = m_world->IsLoaded(math::FastFloor(t->position.x), math::FastFloor(t->position.y - 1.f), math::FastFloor(t->position.z));
	auto liquidAt = [&](const math::FVector3 &p) {
		const int x = math::FastFloor(p.x), y = math::FastFloor(p.y), z = math::FastFloor(p.z);
		return m_world->IsLoaded(x, y, z) && m_content.blocks.Get(m_world->GetBlock(x, y, z)).IsLiquid();
	};
	const bool seated = m_seat.has_value() || m_bed.has_value();
	const bool swimming = !m_flying && !seated && liquidAt(t->position);

	if (seated) {
		// Assis / couché : immobile ; sauter ou s'accroupir pour se relever.
		v->value = {};
		b->gravity = false;
		if (in.jump || in.crouch || in.moveZ != 0.f || in.moveX != 0.f)
			StandUp();
		m_crouchLock = false;
	} else if (m_flying) {
		(void)SetCrouch(false);
		const float speed = in.sprint ? m_settings.sprintFlySpeed : m_settings.flySpeed;
		v->value = wish * speed;
		v->value.y = (in.jump ? speed : 0.f) - (in.descend ? speed : 0.f);
		b->gravity = false;
	} else if (swimming) {
		(void)SetCrouch(false);
		UpdateSwimming(in, dt, v->value, *b);
		// Ambiance de nage (éclaboussures) tant qu'on avance sous l'eau.
		m_swimSoundTimer -= dt;
		if (m_swimSoundTimer <= 0.f && m_horizontalSpeed > 0.5f) {
			m_swimSoundTimer = 1.1f;
			PlaySound("core:water", m_camera.position, 0.4f);
		}
	} else {
		(void)SetCrouch(in.crouch || m_crouchLock);
		const float speed = m_crouching ? 1.6f : (in.sprint ? 7.f : 4.3f);
		const float accel = b->onGround ? 12.f : 3.f;
		v->value.x += (wish.x * speed - v->value.x) * std::min(1.f, accel * dt);
		v->value.z += (wish.z * speed - v->value.z) * std::min(1.f, accel * dt);
		v->value.y = std::max(v->value.y - 28.f * dt, -60.f);
		if (in.jump && b->onGround)
			v->value.y = m_crouching ? 6.f : 8.6f;
		b->gravity = true;
		// Accroupi : on ne tombe pas du bord d'un bloc.
		if (m_crouching && b->onGround) {
			auto supported = [&](float px, float pz) {
				const float feet = t->position.y - b->halfExtents.y - 0.08f;
				for (const float ox : {-b->halfExtents.x + 0.02f, b->halfExtents.x - 0.02f})
					for (const float oz : {-b->halfExtents.z + 0.02f, b->halfExtents.z - 0.02f}) {
						const int x = math::FastFloor(px + ox), y = math::FastFloor(feet), z = math::FastFloor(pz + oz);
						if (m_content.blocks.IsSolid(m_world->GetBlock(x, y, z)))
							return true;
					}
				return false;
			};
			if (!supported(t->position.x + v->value.x * dt, t->position.z))
				v->value.x = 0;
			if (!supported(t->position.x, t->position.z + v->value.z * dt))
				v->value.z = 0;
		}
	}
	if (!seated && (loaded || m_flying))
		m_world->MoveBody(t->position, v->value, *b, dt);
	// Auto-accroupissement : la tête est dans un sous-bloc (porte, dalle) mais la position
	// accroupie est libre → on s'accroupit pour passer (sinon, on reste « piégé »).
	if (!m_crouching && !m_flying && !swimming && !seated) {
		const float headY = t->position.y + b->halfExtents.y - 0.02f;
		if (m_world->PointInSolid({t->position.x, headY, t->position.z}) && !m_world->PointInSolid({t->position.x, t->position.y + 0.72f, t->position.z}))
			(void)SetCrouch(true);
	}
	m_horizontalSpeed = swimming ? v->value.Length() : std::sqrt(v->value.x * v->value.x + v->value.z * v->value.z);
	// Chute libre : départ mémorisé tant qu'on est au sol, dans l'eau ou en vol.
	if (b->onGround || swimming || m_flying || seated)
		m_fallStartY = t->position.y;
	m_fallStartY = std::max(m_fallStartY, t->position.y);

	if (in.hotbarSelect >= 0)
		m_hotbarIndex = in.hotbarSelect % int(kHotbarSlots);
	if (in.hotbarDelta != 0)
		m_hotbarIndex = (m_hotbarIndex + in.hotbarDelta + int(kHotbarSlots) * 4) % int(kHotbarSlots);
	UpdateAvatar(in, dt);
}

void Game::UpdateSwimming(const PlayerInput &in, float dt, math::FVector3 &v, voxel::Body &b) {
	b.gravity = false;
	const float cp = std::cos(m_pitch);
	const math::FVector3 look{std::sin(m_yaw) * cp, std::sin(m_pitch), -std::cos(m_yaw) * cp};
	const math::FVector3 right{std::cos(m_yaw), 0, std::sin(m_yaw)};
	math::FVector3 dir = look * in.moveZ + right * in.moveX;
	if (dir.LengthSq() > 1.f)
		dir = dir.Normalized();
	float speed = 2.8f;
	switch (m_swimStyle) {
	case voxel::SwimStyle::Crawl: speed = 3.6f; break;
	case voxel::SwimStyle::Backstroke: speed = 2.9f; break;
	default: { // brasse : poussée au moment du coup de jambes (cycle de 1,4 s)
		const float c = m_animTime / 1.4f - std::floor(m_animTime / 1.4f);
		speed = 1.6f + 2.4f * std::max(0.f, std::sin((c - 0.6f) * 6.2832f * 1.5f));
		break;
	}
	}
	if (in.sprint)
		speed *= 1.3f;
	math::FVector3 target = dir * speed;
	const auto *t = m_ecs.Get<voxel::Transform>(m_player);
	const int hx = math::FastFloor(t->position.x), hz = math::FastFloor(t->position.z);
	const int eyeY = math::FastFloor(t->position.y + b.halfExtents.y * 0.55f);
	const bool headUnder = m_world->IsLoaded(hx, eyeY, hz) && m_content.blocks.Get(m_world->GetBlock(hx, eyeY, hz)).IsLiquid();
	if (in.jump) {
		if (!headUnder) { // tête déjà hors de l'eau : sortir d'un saut
			v.y = 6.5f;
			return;
		}
		target.y = 3.2f;
	} else if (in.crouch)
		target.y = -3.f;
	else if (std::fabs(in.moveZ) + std::fabs(in.moveX) < 0.01f || !headUnder)
		target.y = headUnder ? 1.4f : std::min(target.y, 0.f) - 0.15f; // flottaison : la tête reste hors de l'eau
	v += (target - v) * std::min(1.f, 3.5f * dt);
}

void Game::Interact(const PlayerInput &input, float dt) {
	DIAG_ZONE("Game::Interact");
	PlayerInput in = input;
	if (m_pendingClick) { // clic simulé (player.click)
		(m_pendingClick == 2 ? in.secondary : in.primary) = true;
		(m_pendingClick == 2 ? m_prevSecondary : m_prevPrimary) = false;
		m_pendingClick = 0;
	}
	const bool pressedL = in.primary && !m_prevPrimary, pressedR = in.secondary && !m_prevSecondary;
	m_prevPrimary = in.primary;
	m_prevSecondary = in.secondary;
	if (!in.primary)
		m_leftCancelled = false;
	const float reach = Creative() ? 8.f : 5.f;
	const math::FVector3 eye = m_cameraMode == CameraMode::FirstPerson ? m_camera.position : m_bodyOrigin + YawRotate(voxel::AvatarEye(m_pose), m_bodyYaw);
	const math::FVector3 dir = math::DirectionFromYawPitch(m_yaw, m_pitch);
	m_target = m_world->Raycast(eye, dir, reach);
	m_targetEntity = PickEntityRay(m_ecs, eye, dir, reach, m_player);
	if (m_targetEntity && m_target && m_target->distance < m_targetEntity->second)
		m_targetEntity.reset(); // un bloc cache l'entité
	if (m_targetEntity)
		m_target.reset();
	m_breakCooldown = std::max(0.f, m_breakCooldown - 1.f / 60.f);

	// Indication sous le viseur : action du clic droit sur l'objet visé.
	m_actionHint.clear();
	if (m_targetEntity) {
		m_actionHint = "Clic gauche : frapper  ·  Clic droit : interagir";
	} else if (m_target) {
		const voxel::BlockDef &def = m_content.blocks.Get(m_target->id);
		if (!def.interact.toggle.empty())
			m_actionHint = def.HasTag("door") ? "Clic droit : ouvrir / fermer"
											 : (def.light > 0 || def.name.find("lamp") != std::string::npos ? "Clic droit : allumer / éteindre"
																											: "Clic droit : basculer");
		else if (def.interact.action == "sit")
			m_actionHint = "Clic droit : s'asseoir";
		else if (def.interact.action == "sleep")
			m_actionHint = "Clic droit : se coucher";
		else if (!def.interact.call.empty())
			m_actionHint = "Clic droit : utiliser";
	}
	if (m_seat || m_bed)
		m_actionHint = m_bed ? "Saut ou Maj : se lever" : "Saut ou Maj : se relever";

	if (pressedL)
		m_swing = 0.f;
	// ── Clic gauche : frapper une entité, fonction du mod, ou casser le bloc ──
	if (pressedL && m_targetEntity) {
		ClickEntity(m_targetEntity->first, false);
		return;
	}
	if (in.primary && m_target) {
		if (pressedL)
			m_leftCancelled = ClickBlock(*m_target, false);
		if (m_leftCancelled) {
			StopMining();
		} else if (Creative()) {
			const voxel::BlockId old = BreakBlock(m_target->block);
			if (old != voxel::kAir && m_swing >= 1.f)
				m_swing = 0.f;
			m_breakCooldown = 0.18f;
		} else {
			MineBlock(m_target->block, dt);
		}
		return;
	}
	StopMining();
	// ── Clic droit : interaction (porte, lampe, chaise, lit, bouton, entité), armure, pose ──
	const bool useKey = in.use && m_breakCooldown <= 0.f;
	if (pressedR && m_targetEntity) {
		ClickEntity(m_targetEntity->first, true);
		m_breakCooldown = 0.25f;
		return;
	}
	// Accroupi, le clic droit pose le bloc au lieu d'interagir (E interagit toujours).
	if ((pressedR || useKey) && m_target && (!m_crouching || useKey)) {
		if (ClickBlock(*m_target, true) || useKey) {
			m_breakCooldown = 0.25f;
			return;
		}
	}
	if (pressedR) {
		const ItemStack &held = Held(true);
		if (const ItemDef *item = m_content.Item(held.id); item && !item->armorSlot.empty()) {
			EquipArmor(held.id);
			return;
		}
	}
	if (in.secondary && m_target && m_breakCooldown <= 0.f && SelectedBlock() != voxel::kAir) {
		const math::IVec3 q = m_target->block + m_target->normal;
		const voxel::Body *body = m_ecs.Get<voxel::Body>(m_player);
		const voxel::Transform *t = m_ecs.Get<voxel::Transform>(m_player);
		const math::FAABB player{t->position - body->halfExtents, t->position + body->halfExtents};
		const math::FAABB cell{q.ToFloat(), q.ToFloat() + math::FVector3(1.f)};
		m_breakCooldown = 0.18f;
		if (m_content.blocks.IsSolid(SelectedBlock()) && player.Intersects(cell) && !m_flying)
			return;
		const voxel::BlockId id = SelectedBlock();
		if (PlaceBlock(q, id)) {
			m_swing = 0.f;
			if (!Creative())
				Take(ItemId(id), 1);
		}
	}
}

script::Value Game::ClickPayload(bool right) const {
	script::Map m{{"button", script::Value(right ? "right" : "left")},
				  {"player", script::Value(double(m_player.Bits()))},
				  {"actor", script::Value(double(m_player.Bits()))},
				  {"sneaking", script::Value(m_crouching)}};
	const ItemStack &held = Held(true);
	m["held"] = held.Empty() ? script::Value() : script::Value(m_content.ItemName(held.id));
	return script::Value(std::move(m));
}

bool Game::ClickBlock(const voxel::RayHit &hit, bool right) {
	const voxel::BlockDef &def = m_content.blocks.Get(hit.id);
	const math::IVec3 root = RootOf(hit.block, hit.id);
	script::Value ev = ClickPayload(right);
	script::Map &m = *ev.AsMap();
	m["x"] = script::Value(hit.block.x);
	m["y"] = script::Value(hit.block.y);
	m["z"] = script::Value(hit.block.z);
	m["root_x"] = script::Value(root.x);
	m["root_y"] = script::Value(root.y);
	m["root_z"] = script::Value(root.z);
	m["nx"] = script::Value(hit.normal.x);
	m["ny"] = script::Value(hit.normal.y);
	m["nz"] = script::Value(hit.normal.z);
	m["block"] = script::Value(def.name);
	// Fonctions des mods : évènement générique, puis gestionnaires ciblés et fonction du bloc.
	bool cancelled = Emit("block_click", ev);
	if (m_scripts) {
		std::vector<std::string> targets = {def.name, "block", "*"};
		for (const std::string &tag : def.tags)
			targets.push_back("#" + tag);
		cancelled = m_scripts->Interact(targets, ev, right ? std::string_view(def.interact.call) : std::string_view()) || cancelled;
	}
	if (!right)
		return cancelled;
	// Compatibilité : « block_use » (TNT, aiguillages…) ; `return false` = utilisation prise en charge.
	if (!cancelled)
		cancelled = Emit("block_use", ev);
	if (cancelled)
		return true;
	// Actions par défaut déclarées par le bloc.
	if (!def.interact.toggle.empty()) {
		const voxel::BlockDef &rootDef = m_content.blocks.Get(m_world->GetBlock(root.x, root.y, root.z));
		const std::string toName = rootDef.interact.toggle.empty() ? def.interact.toggle : rootDef.interact.toggle;
		const std::string sound = rootDef.interact.sound.empty() ? def.interact.sound : rootDef.interact.sound;
		const std::string fromName = rootDef.name;
		const std::vector<voxel::BlockPart> oldParts = rootDef.parts;
		const voxel::BlockId to = m_content.blocks.Find(toName);
		if (to == voxel::kAir)
			return true;
		for (const voxel::BlockPart &part : oldParts) // anciennes parties (remplacées ci-dessous)
			m_world->SetBlock(root.x + part.offset[0], root.y + part.offset[1], root.z + part.offset[2], voxel::kAir);
		m_world->SetBlock(root.x, root.y, root.z, to);
		for (const voxel::BlockPart &part : m_content.blocks.Get(to).parts)
			if (const voxel::BlockId pid = m_content.blocks.Find(part.block); pid != voxel::kAir)
				m_world->SetBlock(root.x + part.offset[0], root.y + part.offset[1], root.z + part.offset[2], pid);
		if (!sound.empty())
			PlaySound(sound, root.ToFloat() + math::FVector3(0.5f));
		Emit("block_toggled", script::Value(script::Map{{"x", script::Value(root.x)}, {"y", script::Value(root.y)}, {"z", script::Value(root.z)},
														{"from", script::Value(fromName)}, {"to", script::Value(toName)}}));
		m_swing = 0.f;
		return true;
	}
	if (def.interact.action == "sit") {
		SitOn(hit.block);
		return true;
	}
	if (def.interact.action == "sleep") {
		SleepIn(hit.block);
		return true;
	}
	return !def.interact.Empty();
}

bool Game::ClickEntity(ecs::Entity e, bool right) {
	const auto *kind = m_ecs.Get<voxel::Kind>(e);
	const auto *t = m_ecs.Get<voxel::Transform>(e);
	const std::string kindId = kind ? kind->id : std::string();
	script::Value ev = ClickPayload(right);
	script::Map &m = *ev.AsMap();
	const script::Value entity(double(e.Bits()));
	m["entity"] = entity;
	m["e"] = entity;
	m["kind"] = script::Value(kindId);
	if (t) {
		m["x"] = script::Value(t->position.x);
		m["y"] = script::Value(t->position.y);
		m["z"] = script::Value(t->position.z);
	}
	m_swing = 0.f;
	bool cancelled = Emit("entity_click", ev);
	if (m_scripts) {
		const std::string targets[] = {kindId.empty() ? std::string("?") : kindId, "entity", "*"};
		cancelled = m_scripts->Interact(targets, ev) || cancelled;
	}
	if (cancelled || right || !m_ecs.IsAlive(e))
		return cancelled;
	// Clic gauche par défaut : coup (dégâts et recul). L'arme tenue fixe les dégâts.
	if (auto *h = m_ecs.Get<voxel::Health>(e)) {
		const ItemStack &held = Held(true);
		const ItemDef *item = m_content.Item(held.id);
		const float damage = item && item->IsTool() ? float(item->tool->damage) : 1.f;
		h->current -= damage;
		if (auto *v = m_ecs.Get<voxel::Velocity>(e))
			v->value += math::FVector3{std::sin(m_yaw) * 6.f, 4.5f, -std::cos(m_yaw) * 6.f};
		const bool dead = h->current <= 0.f;
		Emit("entity_damaged", script::Value(script::Map{{"entity", entity}, {"target", entity}, {"amount", script::Value(damage)},
														 {"source", script::Value(double(m_player.Bits()))}}));
		if (dead && m_ecs.IsAlive(e)) {
			Emit("entity_died", script::Value(script::Map{{"entity", entity}, {"e", entity}, {"kind", script::Value(kindId)}}));
			m_world->DespawnEntity(e);
		}
	}
	return true;
}

math::IVec3 Game::RootOf(const math::IVec3 &p, voxel::BlockId id) const {
	const voxel::BlockDef &def = m_content.blocks.Get(id);
	return def.isPart ? math::IVec3{p.x + def.partOf[0], p.y + def.partOf[1], p.z + def.partOf[2]} : p;
}

// Son d'un matériau de bloc : « core:stone_hit », « core:wood_footstep »… (vide si aucun).
static std::string MaterialSound(std::string_view group, std::string_view event) {
	if (group.empty())
		return {};
	return std::format("core:{}_{}", group, event);
}

// Son de casse d'un matériau : « _dug » s'il existe, sinon le son générique « break ».
static std::string DigSound(std::string_view group) {
	static constexpr std::string_view kDug[] = {"stone", "gravel", "sand", "snow", "wood", "leaves", "glass", "ceramic", "metal"};
	if (!group.empty() && std::ranges::find(kDug, group) != std::end(kDug))
		return std::format("core:{}_dug", group);
	return "core:break";
}

bool Game::PlaceBlock(const math::IVec3 &p, voxel::BlockId id) {
	const voxel::BlockDef &def = m_content.blocks.Get(id);
	// ── Orientation de placement ──────────────────────────────────────────
	uint8_t orient = def.orientDefault;
	if (def.orientAuto && !m_crouching) { // accroupi : pose verticale (pas de face au joueur)
		if (const auto *t = m_ecs.Get<voxel::Transform>(m_player)) {
			const float dx = t->position.x - (float(p.x) + 0.5f);
			const float dz = t->position.z - (float(p.z) + 0.5f);
			orient = voxel::OrientFromYaw(voxel::YawFromDirection(dx, dz));
		}
	}
	auto contains = [](const std::vector<uint8_t> &v, uint8_t o) { return std::find(v.begin(), v.end(), o) != v.end(); };
	if (!def.orientAllowed.empty() && !contains(def.orientAllowed, orient))
		orient = def.orientAllowed.front(); // orientation hors liste : première autorisée
	if (contains(def.orientForbidden, orient))
		orient = def.orientDefault; // orientation interdite : repli sur le défaut
	// ──────────────────────────────────────────────────────────────────────
	auto replaceable = [&](int x, int y, int z) {
		const voxel::BlockId b = m_world->GetBlock(x, y, z);
		return b == voxel::kAir || m_content.blocks.Get(b).IsLiquid();
	};
	for (const voxel::BlockPart &part : def.parts)
		if (!replaceable(p.x + part.offset[0], p.y + part.offset[1], p.z + part.offset[2])) {
			m_ui.Notify("Pas assez de place pour poser ce bloc");
			return false;
		}
	if (!m_world->SetBlock(p.x, p.y, p.z, id, orient))
		return false;
	for (const voxel::BlockPart &part : def.parts)
		if (const voxel::BlockId pid = m_content.blocks.Find(part.block); pid != voxel::kAir)
			m_world->SetBlock(p.x + part.offset[0], p.y + part.offset[1], p.z + part.offset[2], pid, orient);
	Emit("block_placed", script::Value(script::Map{{"x", script::Value(p.x)}, {"y", script::Value(p.y)}, {"z", script::Value(p.z)},
												   {"block", script::Value(def.name)}, {"player", script::Value(double(m_player.Bits()))},
												   {"actor", script::Value(double(m_player.Bits()))}}));
	// Son de pose selon le matériau du bloc.
	const std::string place = MaterialSound(def.soundGroup, "place");
	PlaySound(place.empty() ? "core:place" : place, m_camera.position, 0.7f);
	return true;
}

voxel::BlockId Game::BreakBlock(const math::IVec3 &p) {
	const voxel::BlockId hitId = m_world->GetBlock(p.x, p.y, p.z);
	if (hitId == voxel::kAir)
		return voxel::kAir;
	const math::IVec3 root = RootOf(p, hitId);
	voxel::BlockId rootId = m_world->GetBlock(root.x, root.y, root.z);
	if (rootId == voxel::kAir)
		rootId = hitId;
	const voxel::BlockDef rootDef = m_content.blocks.Get(rootId);
	if (!m_world->SetBlock(root.x, root.y, root.z, voxel::kAir) && rootId == hitId && root.x == p.x && root.y == p.y && root.z == p.z)
		return voxel::kAir;
	for (const voxel::BlockPart &part : rootDef.parts) {
		const math::IVec3 q{root.x + part.offset[0], root.y + part.offset[1], root.z + part.offset[2]};
		if (m_content.blocks.Get(m_world->GetBlock(q.x, q.y, q.z)).isPart)
			m_world->SetBlock(q.x, q.y, q.z, voxel::kAir);
	}
	if (root.x != p.x || root.y != p.y || root.z != p.z) // la partie visée elle-même
		m_world->SetBlock(p.x, p.y, p.z, voxel::kAir);
	if (m_seat && root.x == m_seat->x && root.y == m_seat->y && root.z == m_seat->z)
		StandUp();
	Emit("block_broken", script::Value(script::Map{{"x", script::Value(root.x)}, {"y", script::Value(root.y)}, {"z", script::Value(root.z)},
												   {"block", script::Value(rootDef.name)}, {"player", script::Value(double(m_player.Bits()))},
												   {"actor", script::Value(double(m_player.Bits()))}}));
	// Objet récupéré : la racine (porte entière) ; un état non ramassable (porte ouverte,
	// lampe éteinte) rend sa forme de base.
	if (!rootDef.item && !rootDef.interact.toggle.empty())
		if (const voxel::BlockId base = m_content.blocks.Find(rootDef.interact.toggle); base != voxel::kAir && m_content.blocks.Get(base).item)
			return base;
	return rootId;
}

void Game::MineBlock(const math::IVec3 &p, float dt) {
	const voxel::BlockId hitId = m_world->GetBlock(p.x, p.y, p.z);
	if (hitId == voxel::kAir) {
		StopMining();
		return;
	}
	if (m_miningBlock != p) { // nouvelle cible : on repart de zéro
		m_miningBlock = p;
		m_miningProgress = 0.f;
		m_hitSoundTimer = 0.f;
	}
	const voxel::BlockDef &def = m_content.blocks.Get(hitId);
	const ItemStack &held = Held(true);
	const ItemDef *item = m_content.Item(held.id);
	const float speed = item ? item->MiningSpeed(def.bestTool) : 1.f;
	const float breakSeconds = std::max(0.12f, def.hardness * 0.7f / speed);
	m_miningProgress += dt / breakSeconds;
	// Son « hit » périodique selon le matériau.
	m_hitSoundTimer -= dt;
	if (m_hitSoundTimer <= 0.f) {
		m_hitSoundTimer = 0.22f;
		const std::string hit = MaterialSound(def.soundGroup, "hit");
		PlaySound(hit.empty() ? "core:step" : hit, m_camera.position, 0.7f);
	}
	if (m_miningProgress >= 1.f) { // bloc cassé
		const std::string group = def.soundGroup;
		StopMining();
		const voxel::BlockId old = BreakBlock(p);
		if (old != voxel::kAir && !Creative())
			Give(ItemId(old), 1);
		if (m_swing >= 1.f)
			m_swing = 0.f;
		PlaySound(DigSound(group), m_camera.position, 0.9f);
		if (old != voxel::kAir)
			PlaySound("core:pickup", m_camera.position, 0.6f);
	}
}

void Game::StopMining() noexcept {
	m_miningBlock = math::IVec3{};
	m_miningProgress = 0.f;
	m_hitSoundTimer = 0.f;
}

bool Game::SetCrouch(bool crouch) {
	if (crouch == m_crouching)
		return true;
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	auto *b = m_ecs.Get<voxel::Body>(m_player);
	if (!t || !b)
		return false;
	constexpr float kStand = 0.9f, kCrouch = 0.75f, kDelta = kStand - kCrouch;
	if (!crouch) {
		// Se relever : il faut de la place au-dessus de la tête.
		const float top = t->position.y + kCrouch + kDelta * 2.f;
		for (const float ox : {-b->halfExtents.x + 0.02f, b->halfExtents.x - 0.02f})
			for (const float oz : {-b->halfExtents.z + 0.02f, b->halfExtents.z - 0.02f})
				if (m_content.blocks.IsSolid(m_world->GetBlock(math::FastFloor(t->position.x + ox), math::FastFloor(top), math::FastFloor(t->position.z + oz))))
					return false;
		b->halfExtents.y = kStand;
		t->position.y += kDelta;
	} else {
		b->halfExtents.y = kCrouch;
		t->position.y -= kDelta;
	}
	m_crouching = crouch;
	return true;
}

bool Game::SitOn(const math::IVec3 &block) {
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	if (!t)
		return false;
	StandUp();
	(void)SetCrouch(false);
	const voxel::BlockDef &def = m_content.blocks.Get(m_world->GetBlock(block.x, block.y, block.z));
	float seatTop = 0.f; // haut de l'assise, relatif au centre de la cellule
	const std::vector<voxel::BlockBox> &boxes = def.collision.empty() ? def.boxes : def.collision;
	if (!boxes.empty()) {
		seatTop = -0.5f;
		for (const voxel::BlockBox &box : boxes)
			seatTop = std::max(seatTop, box[4]);
	}
	// Orientation : dos au dossier (géométrie au-dessus de l'assise), sinon dans l'axe du regard.
	{
		float sx = 0.f, sz = 0.f, n = 0.f;
		for (const voxel::BlockTriangle &tri : def.mesh)
			for (int k = 0; k < 3; ++k)
				if (tri.position[size_t(k * 3 + 1)] > seatTop + 0.15f) {
					sx += tri.position[size_t(k * 3)];
					sz += tri.position[size_t(k * 3 + 2)];
					n += 1.f;
				}
		for (const voxel::BlockBox &box : def.boxes)
			if (box[4] > seatTop + 0.15f) {
				sx += (box[0] + box[3]) * 0.5f;
				sz += (box[2] + box[5]) * 0.5f;
				n += 1.f;
			}
		if (n > 0.f && sx * sx + sz * sz > 0.0025f * n * n) {
			const float fx = -sx / n, fz = -sz / n; // face opposée au dossier
			m_yaw = std::atan2(fx, -fz);
		}
	}
	m_seat = block;
	m_restAnchor = {float(block.x) + 0.5f, float(block.y) + 0.5f + seatTop + 0.27f, float(block.z) + 0.5f};
	t->position = m_restAnchor;
	if (auto *v = m_ecs.Get<voxel::Velocity>(m_player))
		v->value = {};
	m_ui.Notify(std::format("Assis : {} — sauter pour se relever", def.displayName));
	Emit("player_sit", script::Value(script::Map{{"x", script::Value(block.x)}, {"y", script::Value(block.y)}, {"z", script::Value(block.z)},
												 {"block", script::Value(def.name)}}));
	return true;
}

bool Game::SleepIn(const math::IVec3 &block) {
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	if (!t)
		return false;
	const voxel::BlockId id = m_world->GetBlock(block.x, block.y, block.z);
	const math::IVec3 root = RootOf(block, id);
	const voxel::BlockDef &rootDef = m_content.blocks.Get(m_world->GetBlock(root.x, root.y, root.z));
	math::IVec3 head{root.x, root.y, root.z + 1};
	if (!rootDef.parts.empty())
		head = {root.x + rootDef.parts[0].offset[0], root.y + rootDef.parts[0].offset[1], root.z + rootDef.parts[0].offset[2]};
	StandUp();
	(void)SetCrouch(false);
	float top = 0.f;
	for (const voxel::BlockBox &box : rootDef.boxes)
		top = std::max(top, box[4]);
	if (rootDef.boxes.empty())
		top = 0.5f;
	m_bed = root;
	m_sleepTime = 0.f;
	math::FVector3 d{float(head.x - root.x), 0.f, float(head.z - root.z)};
	if (d.LengthSq() < 0.5f)
		d = {0.f, 0.f, 1.f};
	m_lieDirection = d.Normalized();
	m_restAnchor = {(float(root.x + head.x) + 1.f) * 0.5f, float(root.y) + 0.5f + top + 0.14f, (float(root.z + head.z) + 1.f) * 0.5f};
	t->position = m_restAnchor;
	if (auto *v = m_ecs.Get<voxel::Velocity>(m_player))
		v->value = {};
	// Regard vers les pieds, un peu relevé.
	m_yaw = std::atan2(-m_lieDirection.x, m_lieDirection.z);
	m_pitch = 0.35f;
	const bool night = m_timeOfDay < 0.23f || m_timeOfDay > 0.77f;
	m_ui.Notify(night ? "Bonne nuit…" : "On ne peut dormir que la nuit : vous vous reposez un instant");
	Emit("player_sleep", script::Value(script::Map{{"x", script::Value(root.x)}, {"y", script::Value(root.y)}, {"z", script::Value(root.z)},
												   {"night", script::Value(night)}}));
	return true;
}

void Game::StandUp() {
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	auto *b = m_ecs.Get<voxel::Body>(m_player);
	if (!t || !b || (!m_seat && !m_bed))
		return;
	if (m_bed) {
		const math::IVec3 r = *m_bed;
		t->position = {float(r.x) + 0.5f, m_restAnchor.y + b->halfExtents.y + 0.05f, float(r.z) + 0.5f};
	} else {
		t->position = {m_restAnchor.x, m_restAnchor.y - 0.27f + b->halfExtents.y + 0.05f, m_restAnchor.z};
	}
	m_seat.reset();
	m_bed.reset();
	m_fallStartY = t->position.y;
}

void Game::PlayGesture(voxel::AvatarGesture g) {
	if (g == voxel::AvatarGesture::None || g >= voxel::AvatarGesture::Count)
		return;
	m_gesture = g;
	m_gestureTime = 0.f;
	Emit("player_gesture", script::Value(script::Map{{"gesture", script::Value(voxel::AvatarGestureId(g))}}));
}

void Game::UpdateAvatar(const PlayerInput &in, float dt) {
	(void)in;
	auto *t = m_ecs.Get<voxel::Transform>(m_player);
	auto *v = m_ecs.Get<voxel::Velocity>(m_player);
	auto *b = m_ecs.Get<voxel::Body>(m_player);
	if (!t || !v || !b)
		return;
	using S = voxel::AvatarState;
	const int cx = math::FastFloor(t->position.x), cy = math::FastFloor(t->position.y), cz = math::FastFloor(t->position.z);
	const bool inLiquid = m_world->IsLoaded(cx, cy, cz) && m_content.blocks.Get(m_world->GetBlock(cx, cy, cz)).IsLiquid();
	S state = S::Stand;
	if (m_bed)
		state = S::Sleep;
	else if (m_seat)
		state = S::Sit;
	else if (m_flying)
		state = S::Fly;
	else if (inLiquid)
		state = S::Swim;
	else if (!b->onGround && (m_fallStartY - t->position.y > 4.f || m_avatarState == S::FreeFall) && v->value.y < -10.f)
		state = S::FreeFall;
	else if (m_crouching)
		state = S::Crouch;
	else if (m_horizontalSpeed > 0.3f)
		state = S::Walk;
	m_avatarState = state;
	m_animTime += dt;
	m_entityAnimTime += dt;
	m_walkPhase += m_horizontalSpeed * dt * (state == S::Crouch ? 3.4f : 2.3f);
	// Pas : son de marche selon le matériau sous les pieds.
	if (state == S::Walk || state == S::Crouch) {
		m_stepDistance += m_horizontalSpeed * dt;
		if (m_stepDistance >= (state == S::Crouch ? 1.6f : 1.35f)) {
			m_stepDistance = 0.f;
			const int fx = math::FastFloor(m_bodyOrigin.x), fz = math::FastFloor(m_bodyOrigin.z);
			const int fy = math::FastFloor(m_bodyOrigin.y - 0.95f);
			const std::string group = m_content.blocks.Get(m_world->GetBlock(fx, fy, fz)).soundGroup;
			const std::string step = MaterialSound(group, "footstep");
			PlaySound(step.empty() ? "core:step" : step, m_camera.position, 0.5f);
		}
	} else {
		m_stepDistance = 0.f;
	}
	if (m_gesture != voxel::AvatarGesture::None) {
		m_gestureTime += dt;
		if (m_gestureTime >= voxel::AvatarGestureDuration(m_gesture))
			m_gesture = voxel::AvatarGesture::None;
	}
	m_swing = std::min(1.f, m_swing + dt * 3.2f);

	voxel::AvatarInput ai;
	ai.state = state;
	ai.swim = m_swimStyle;
	ai.time = m_animTime;
	ai.walkPhase = m_walkPhase;
	ai.speed = m_horizontalSpeed;
	ai.lookPitch = m_pitch;
	ai.firstPerson = m_cameraMode == CameraMode::FirstPerson && !m_cameraOverride;
	ai.gesture = m_gesture;
	ai.gestureTime = m_gestureTime;
	ai.swing = m_swing;
	ai.holdingRight = !Held(true).Empty();
	ai.holdingLeft = !m_offhand.Empty();
	m_pose = voxel::BlendPose(m_pose, voxel::ComputeAvatarPose(ai), 1.f - std::exp(-dt * 14.f));

	// Sommeil : la nuit passe après quelques secondes au lit.
	if (m_bed) {
		m_sleepTime += dt;
		const bool night = m_timeOfDay < 0.23f || m_timeOfDay > 0.77f;
		if (night && m_sleepTime > 4.f) {
			m_timeOfDay = 0.26f;
			Say("Bonjour ! Une nouvelle journée commence.", m_ui.Style().accent);
			Emit("player_wake", script::Value());
			StandUp();
		} else if (!night && m_sleepTime > 6.f) {
			StandUp();
		}
	}

	// Position du corps et de l'œil (caméra « sur le visage », au niveau des yeux).
	const math::FVector3 eyeLocal = voxel::AvatarEye(m_pose);
	math::FVector3 eye;
	if (state == S::Sleep) {
		m_bodyYaw = std::atan2(-m_lieDirection.x, m_lieDirection.z);
		m_bodyOrigin = m_restAnchor;
		eye = m_bodyOrigin + YawRotate(eyeLocal, m_bodyYaw);
	} else if (state == S::Sit) {
		m_bodyYaw = m_yaw;
		m_bodyOrigin = m_restAnchor;
		eye = m_bodyOrigin + YawRotate(eyeLocal, m_bodyYaw);
	} else if (state == S::Swim || state == S::FreeFall) {
		// L'œil reste dans la boîte de collision ; le corps (allongé) s'étend derrière lui.
		m_bodyYaw = m_yaw;
		eye = t->position + math::FVector3{0.f, b->halfExtents.y * 0.55f, 0.f};
		m_bodyOrigin = eye - YawRotate(eyeLocal, m_bodyYaw);
	} else {
		m_bodyYaw = m_yaw;
		m_bodyOrigin = t->position + math::FVector3{0.f, 0.9f - b->halfExtents.y, 0.f};
		// L'œil reste dans la boîte de collision (pas de caméra dans un mur quand on se penche).
		math::FVector3 off = YawRotate(eyeLocal, m_bodyYaw);
		const float horizontal = std::sqrt(off.x * off.x + off.z * off.z), limit = b->halfExtents.x * 0.85f;
		if (horizontal > limit) {
			off.x *= limit / horizontal;
			off.z *= limit / horizontal;
		}
		eye = m_bodyOrigin + off; // œil à l'avant du visage : en baissant les yeux, on voit torse, jambes et pieds
	}
	m_camera.yaw = m_yaw;
	m_camera.pitch = m_pitch;
	m_camera.position = eye;
	if (m_cameraMode != CameraMode::FirstPerson) {
		const math::FVector3 look = math::DirectionFromYawPitch(m_yaw, m_pitch);
		math::FVector3 away = look;
		float dist = 3.f;
		if (m_cameraMode == CameraMode::ThirdBack) {
			away = look * -1.f;
			dist = 4.f;
		} else if (m_cameraMode == CameraMode::ThirdSide) { // à droite du personnage, regard vers lui
			away = math::FVector3{std::cos(m_yaw), 0.f, std::sin(m_yaw)};
			dist = 3.5f;
		}
		// Recul de la caméra jusqu'au premier bloc plein (les blocs à forme — lit, chaise,
		// porte — et la cellule de l'œil ne l'arrêtent pas).
		float travelled = 0.f;
		for (int tries = 0; tries < 6; ++tries) {
			const auto hit = m_world->Raycast(eye + away * travelled, away, dist + 0.3f - travelled);
			if (!hit)
				break;
			if (m_content.blocks.Get(hit->id).HasShape() || hit->distance < 0.02f) {
				travelled += hit->distance + 0.05f;
				continue;
			}
			dist = std::max(0.3f, travelled + hit->distance - 0.3f);
			break;
		}
		m_camera.position = eye + away * dist;
		if (m_cameraMode == CameraMode::ThirdFront) {
			m_camera.yaw = m_yaw + math::PI;
			m_camera.pitch = -m_pitch;
		} else if (m_cameraMode == CameraMode::ThirdSide) {
			m_camera.yaw = m_yaw - math::PI * 0.5f;
			m_camera.pitch = -0.12f;
		}
	}
	if (m_cameraOverride) {
		m_camera.position = m_cameraOverride->position;
		m_camera.yaw = m_cameraOverride->yaw;
		m_camera.pitch = m_cameraOverride->pitch;
	}
	for (OneShot &o : m_oneShots)
		o.remaining -= dt;
	std::erase_if(m_oneShots, [](const OneShot &o) { return o.remaining <= 0.f; });
}

// ── Inventaire ────────────────────────────────────────────────────────────

void Game::ToggleInventory() noexcept {
	m_inventoryOpen = !m_inventoryOpen;
	if (!m_inventoryOpen && !m_cursor.Empty()) { // la pile du curseur retourne dans l'inventaire
		const int rest = Give(m_cursor.id, m_cursor.count);
		m_cursor = rest > 0 ? ItemStack{m_cursor.id, rest} : ItemStack{};
	}
}

int Game::Give(ItemId item, int count) {
	if (item == 0 || count <= 0 || m_slots.empty())
		return count;
	const int maxStack = m_content.MaxStack(item);
	for (int pass = 0; pass < 2 && count > 0; ++pass) // piles existantes, puis cases vides
		for (ItemStack &s : m_slots) {
			if (count <= 0)
				break;
			if (pass == 0 && s.id == item && !s.Empty() && s.count < maxStack) {
				const int n = std::min(count, maxStack - s.count);
				s.count += n;
				count -= n;
			} else if (pass == 1 && s.Empty()) {
				const int n = std::min(count, maxStack);
				s = {item, n};
				count -= n;
			}
		}
	if (count > 0)
		m_ui.Notify("Inventaire plein");
	return count;
}

int Game::Take(ItemId item, int count) {
	int taken = 0;
	auto from = [&](ItemStack &s) {
		if (s.id != item || s.Empty() || taken >= count)
			return;
		const int n = std::min(count - taken, s.count);
		s.count -= n;
		taken += n;
		if (s.count <= 0)
			s = {};
	};
	if (m_slots.empty())
		return 0;
	from(m_slots[size_t(m_hotbarIndex) % kHotbarSlots]); // d'abord la main
	for (ItemStack &s : m_slots)
		from(s);
	from(m_offhand);
	return taken;
}

int Game::CountOf(ItemId item) const noexcept {
	int n = 0;
	for (const ItemStack &s : m_slots)
		if (s.id == item && !s.Empty())
			n += s.count;
	if (m_offhand.id == item && !m_offhand.Empty())
		n += m_offhand.count;
	return n;
}

const ItemStack &Game::Held(bool rightHand) const noexcept {
	if (!rightHand)
		return m_offhand;
	if (m_slots.empty())
		return m_noItem;
	return m_slots[size_t(m_hotbarIndex) % kHotbarSlots];
}

bool Game::EquipArmor(ItemId item) {
	const ItemDef *def = m_content.Item(item);
	if (!def)
		return false;
	const auto slot = ParseArmorSlot(def->armorSlot);
	if (!slot)
		return false;
	// La pièce vient du curseur (inventaire ouvert), sinon de l'inventaire ; en créatif, elle est offerte.
	if (m_cursor.id == item && !m_cursor.Empty()) {
		if (--m_cursor.count <= 0)
			m_cursor = {};
	} else if (Take(item, 1) == 0 && !Creative()) {
		return false;
	}
	ItemStack old = std::exchange(m_armor[size_t(*slot)], ItemStack{item, 1});
	if (!old.Empty())
		Give(old.id, old.count);
	PlayGesture(voxel::AvatarGesture::EquipArmor);
	PlaySound("core:equip", m_camera.position, 0.8f);
	const int points = ArmorPoints();
	m_ui.Notify(std::format("{} équipé — armure : {} point{}", def->displayName, points, points > 1 ? "s" : ""));
	Emit("armor_equipped", script::Value(script::Map{{"item", script::Value(def->name)}, {"slot", script::Value(def->armorSlot)}}));
	return true;
}

int Game::ArmorPoints() const noexcept {
	int n = 0;
	for (const ItemStack &s : m_armor)
		if (const ItemDef *d = m_content.Item(s.id); d && !s.Empty())
			n += d->armorPoints;
	return n;
}

void Game::PlaySound(std::string sound, const math::FVector3 &at, float volume) {
	if (sound.empty())
		return;
	m_oneShots.push_back({(uint64_t(1) << 62) | m_nextSoundKey++, std::move(sound), at, volume, 3.f});
}

// ── Avatar : texture (skin + objets tenus + armure) et maillage posé ─────────

void Game::RefreshAvatarTexture() {
	const auto *skin = m_ecs.Get<voxel::Skin>(m_player);
	const ItemStack &right = Held(true), &left = m_offhand;
	std::string key = std::format("{}|{}|{}", skin ? skin->id : std::string(), right.Empty() ? 0u : right.id, left.Empty() ? 0u : left.id);
	for (const ItemStack &a : m_armor)
		key += std::format("|{}", a.Empty() ? 0u : a.id);
	if (key == m_avatarTextureKey)
		return;
	m_avatarTextureKey = key;
	// Atlas 128×64 : skin (64×64) à gauche ; objets tenus et couleurs d'armure à droite.
	constexpr int kW = 128, kH = 64;
	std::vector<uint8_t> px(size_t(kW) * kH * 4, 0);
	auto put = [&](int x, int y, const uint8_t *rgba) { std::memcpy(&px[(size_t(y) * kW + size_t(x)) * 4], rgba, 4); };
	if (skin && !skin->id.empty())
		if (auto path = m_content.SkinTexturePath(skin->id))
			if (auto img = sdl3::LoadImageRgba(*path)) {
				const sdl3::Surface &skinImg = img.Value();
				if (skinImg.GetWidth() == 64 && (skinImg.GetHeight() == 64 || skinImg.GetHeight() == 32)) {
					for (int y = 0; y < skinImg.GetHeight(); ++y)
						for (int x = 0; x < 64; ++x)
							put(x, y, &skinImg.Pixels()[(size_t(y) * 64 + size_t(x)) * 4]);
					if (skinImg.GetHeight() == 32) // ancien format : bras et jambe gauches = droits
						for (int y = 0; y < 16; ++y)
							for (int x = 0; x < 16; ++x) {
								uint8_t leg[4], arm[4];
								std::memcpy(leg, &px[(size_t(16 + y) * kW + size_t(x)) * 4], 4);
								std::memcpy(arm, &px[(size_t(16 + y) * kW + size_t(40 + x)) * 4], 4);
								put(16 + x, 48 + y, leg);
								put(32 + x, 48 + y, arm);
							}
				}
			}
	auto item = [&](const ItemStack &s, int row) {
		if (s.Empty())
			return;
		uint8_t *side = &px[(size_t(row) * kW + 64) * 4];
		uint8_t *top = &px[(size_t(row) * kW + 80) * 4];
		if (IsBlockItem(s.id)) {
			const voxel::BlockDef &def = m_content.blocks.Get(voxel::BlockId(s.id));
			LayerThumbnail(m_content.textureLayers, def.layers[size_t(voxel::Face::NegZ)], side, kW * 4);
			LayerThumbnail(m_content.textureLayers, def.layers[size_t(voxel::Face::PosY)], top, kW * 4);
		} else if (const ItemDef *d = m_content.Item(s.id); d && d->icon.size() == 16 * 16 * 4) {
			for (int y = 0; y < 16; ++y)
				std::memcpy(side + size_t(y) * kW * 4, &d->icon[size_t(y) * 16 * 4], 16 * 4);
		}
	};
	item(right, 0);
	item(left, 16);
	for (size_t a = 0; a < m_armor.size(); ++a) {
		const ItemDef *d = m_content.Item(m_armor[a].id);
		if (!d || m_armor[a].Empty())
			continue;
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x) {
				const float shade = 0.82f + 0.18f * float((x * 7 + y * 13) % 5) / 4.f; // grain discret
				const uint8_t c[4] = {uint8_t(float(d->color.r) * shade), uint8_t(float(d->color.g) * shade), uint8_t(float(d->color.b) * shade), 255};
				put(96 + int(a) * 8 + x, y, c);
			}
	}
	m_avatarAtlas = std::move(px);
	m_renderer.SetSkinTexture(m_avatarAtlas.data(), kW, kH);
}

void Game::EnsureAvatarRig() {
	const voxel::ModInfo *core = m_content.mods.Find("core");
	fs::path path = core ? core->directory / "models" / "player_avatar.json" : fs::path{};
	if (path != m_avatarRigPath) {
		m_avatarRigPath = path;
		m_avatarRig.reset();
		m_avatarRigMtime = {};
		m_avatarModelHandle = 0;
	}
	if (path.empty())
		return;
	std::error_code ec;
	const auto mtime = fs::last_write_time(path, ec);
	if (ec || mtime == m_avatarRigMtime)
		return;
	m_avatarRigMtime = mtime;
	m_avatarRig.reset();
	if (auto parsed = dataio::LoadJsonFile(path.string()))
		if (auto rig = voxel::ParseRig(parsed.Value()))
			m_avatarRig = std::move(rig.Value());
	// Nouveau contenu : le rig (s'il existe) sera re-téléversé à la prochaine image.
	m_avatarModelHandle = 0;
}

std::vector<voxel::PartOffset> Game::AvatarRigPose() const {
	const voxel::Model &m = *m_avatarRig;
	// Animation nommée selon l'état, puis « idle », puis « stand ».
	const char *primary = AvatarAnimName(m_avatarState);
	for (const char *candidate : {primary, "idle", "stand"}) {
		for (const voxel::Animation &a : m.animations)
			if (a.name == candidate)
				return voxel::SamplePose(m, &a, m_animTime);
	}
	// Pose procédurale convertie (radians → degrés) pour les parties standard.
	std::vector<voxel::PartOffset> pose(m.parts.size());
	constexpr float kRad2Deg = 180.f / 3.14159265358979f;
	for (size_t i = 0; i < m.parts.size(); ++i)
		for (size_t p = 0; p < voxel::kAvatarParts; ++p)
			if (m.parts[i].name == voxel::kAvatarPartRigNames[p])
				pose[i].rotation = m_pose.rotation[p] * kRad2Deg;
	return pose;
}

uint64_t Game::EntityModel(std::string_view kind) {
	if (const auto it = m_entityModels.find(kind); it != m_entityModels.end())
		return it->second;
	uint64_t handle = 0;
	if (const EntityRig *rig = m_content.FindEntityRig(kind)) {
		if (const auto mit = m_content.rigModels.find(rig->model); mit != m_content.rigModels.end())
			if (auto skin = m_content.EntitySkinImage(*rig); skin && skin->GetWidth() > 0)
				handle = m_renderer.UploadModel(kind, skin->ToRgba8(), skin->GetWidth(), skin->GetHeight());
	}
	m_entityModels[std::string(kind)] = handle;
	return handle;
}

void Game::ReloadEntityModels() {
	for (const auto &[kind, handle] : m_entityModels)
		if (handle)
			m_renderer.ReleaseModel(handle);
	m_entityModels.clear();
}

void Game::UploadAvatarMesh() {
	RefreshAvatarTexture();
	EnsureAvatarRig();
	const bool firstPerson = m_cameraMode == CameraMode::FirstPerson && !m_cameraOverride;
	// Boîtes attachées (objets tenus, armure) : mêmes pour l'avatar procédural et le rig.
	std::vector<voxel::AvatarBox> extras;
	auto heldBox = [&](const ItemStack &s, bool rightHand, float row) {
		if (s.Empty())
			return;
		const std::array<float, 4> side{64.f, row, 80.f, row + 16.f}, top{80.f, row, 96.f, row + 16.f};
		extras.push_back(voxel::AvatarHeldBox(rightHand, !IsBlockItem(s.id), side, top));
	};
	heldBox(Held(true), true, 0.f);
	heldBox(m_offhand, false, 16.f);
	using P = voxel::AvatarPart;
	auto armor = [&](ArmorSlot slot, std::initializer_list<P> parts, float inflate) {
		if (m_armor[size_t(slot)].Empty())
			return;
		const float u = 96.f + float(slot) * 8.f;
		for (P part : parts) {
			voxel::AvatarBox box = voxel::AvatarPartBox(part, inflate);
			for (auto &f : box.uv)
				f = {u + 1.f, 1.f, u + 7.f, 7.f};
			if (slot == ArmorSlot::Feet) // bottes : bas des jambes
				box.max.y = box.min.y + 0.24f;
			extras.push_back(box);
		}
	};
	armor(ArmorSlot::Head, {P::Head}, 0.05f);
	armor(ArmorSlot::Chest, {P::Body, P::ArmRight, P::ArmLeft}, 0.045f);
	armor(ArmorSlot::Legs, {P::LegRight, P::LegLeft}, 0.035f);
	armor(ArmorSlot::Feet, {P::LegRight, P::LegLeft}, 0.055f);
	if (firstPerson) // la tête (et ce qui s'y attache) est masquée en vue subjective
		std::erase_if(extras, [](const voxel::AvatarBox &b) { return b.part == P::Head; });

	// Avatar édité (rig) : forme + animations ; le maillage suit la pose à chaque image.
	if (m_avatarRig && !m_avatarRig->parts.empty()) {
		const std::vector<voxel::PartOffset> pose = AvatarRigPose();
		// Tête masquée en vue subjective.
		int headPart = -1;
		if (firstPerson)
			for (size_t i = 0; i < m_avatarRig->parts.size(); ++i)
				if (m_avatarRig->parts[i].name == "head") {
					headPart = int(i);
					break;
				}
		const bool headWasVisible = headPart >= 0 && m_avatarRig->parts[size_t(headPart)].visible;
		if (headPart >= 0)
			m_avatarRig->parts[size_t(headPart)].visible = false;
		std::vector<voxel::ModelVertex> verts;
		voxel::BuildModelMesh(*m_avatarRig, pose, verts);
		if (headPart >= 0)
			m_avatarRig->parts[size_t(headPart)].visible = headWasVisible;
		// Le rig (textureWidth 64) est posé sur l'atlas 128×64 : peau dans la moitié gauche.
		const float su = float(m_avatarRig->textureWidth) / 128.f;
		const float sv = float(m_avatarRig->textureHeight) / 64.f;
		for (voxel::ModelVertex &v : verts) {
			v.u *= su;
			v.v *= sv;
		}
		// Objets tenus et armure (UV déjà normalisés sur l'atlas 128×64).
		voxel::BuildAvatarExtrasMesh(*m_avatarRig, pose, extras, 128.f, 64.f, verts);
		if (!m_avatarModelHandle)
			m_avatarModelHandle = m_renderer.UploadModel("avatar", m_avatarAtlas, 128, 64);
		static_assert(sizeof(voxel::ModelVertex) == sizeof(voxel::ModelVertex));
		m_renderer.UpdateModelMesh(m_avatarModelHandle,
								   std::span<const voxel::ModelVertex>(reinterpret_cast<const voxel::ModelVertex *>(verts.data()), verts.size()));
		return;
	}

	voxel::AvatarMeshOptions opt;
	opt.head = !firstPerson;
	opt.textureWidth = 128.f;
	opt.textureHeight = 64.f;
	if (!opt.head)
		opt.armThickness = 0.72f; // première personne : bras moins envahissants
	opt.extras = std::move(extras);
	std::vector<voxel::AvatarVertex> verts;
	verts.reserve(600);
	voxel::BuildAvatarMesh(m_pose, opt, verts);
	static_assert(sizeof(voxel::AvatarVertex) == sizeof(voxel::ModelVertex));
	m_avatarMesh.resize(verts.size());
	std::memcpy(m_avatarMesh.data(), verts.data(), verts.size() * sizeof(voxel::ModelVertex));
	m_renderer.SetModelMesh(m_avatarMesh);
}

void Game::CollectLights(const math::FVector3 &eye, std::vector<voxel::PointLight> &out) {
	out.clear();
	std::vector<std::pair<float, voxel::PointLight>> all;
	m_ecs.MakeQuery<const voxel::LightEmitter, const voxel::Transform>().Each([&](const voxel::LightEmitter &l, const voxel::Transform &t) {
		if (!l.enabled || l.intensity <= 0)
			return;
		const math::FVector3 p = t.position + voxel::RotateYaw(l.offset, t.yaw);
		const float d = (p - eye).Length() - l.range;
		voxel::PointLight light;
		light.position = p;
		light.range = l.range;
		light.color = l.color;
		light.intensity = l.intensity;
		light.type = static_cast<voxel::PointLight::Type>(std::min<uint8_t>(l.type, 2));
		light.direction = voxel::RotateYaw(l.direction, t.yaw).Normalized();
		const float inner = std::cos(l.coneAngle * 0.7f * math::DEG2RAD);
		const float outer = std::cos(l.coneAngle * math::DEG2RAD);
		light.innerCos = inner;
		light.outerCos = outer;
		light.radius = 0.5f;
		all.push_back({d, light});
	});
	const size_t n = std::min(all.size(), voxel::kMaxPointLights);
	std::ranges::partial_sort(all, all.begin() + std::ptrdiff_t(n), {}, &std::pair<float, voxel::PointLight>::first);
	for (size_t i = 0; i < n; ++i)
		out.push_back(all[i].second);
}

void Game::CollectSounds(const math::FVector3 &listener, std::vector<SoundSource> &out) {
	out.clear();
	m_ecs.MakeQuery<const voxel::SoundEmitter, const voxel::Transform>().Each(
		[&](ecs::Entity e, const voxel::SoundEmitter &s, const voxel::Transform &t) {
			if (!s.enabled || s.sound.empty() || (t.position - listener).Length() > s.range * 1.1f)
				return;
			out.push_back({e.Bits(), s.sound, t.position, s.volume, s.range, s.loop});
		});
	for (const OneShot &o : m_oneShots) // sons ponctuels (portes, lampes, armure…)
		out.push_back({o.key, o.sound, o.position, o.volume, 24.f, false});
}

void Game::SetPostEffect(std::string_view name, float strength) {
	m_postEffects[std::string(name)] = std::clamp(strength, 0.f, 1.f);
}

void Game::PumpMeshes() {
	DIAG_ZONE("Game::PumpMeshes");
	m_world->DrainUnloaded(m_unloadScratch);
	for (voxel::ChunkKey k : m_unloadScratch)
		m_renderer.RemoveChunk(k);
	m_world->DrainMeshes(m_meshScratch);
	for (voxel::MeshResult &r : m_meshScratch) {
		std::vector<voxel::ChunkVertex> opaque(r.mesh.opaque.size()), translucent(r.mesh.translucent.size());
		static_assert(sizeof(voxel::ChunkVertex) == sizeof(voxel::ChunkVertex));
		std::memcpy(opaque.data(), r.mesh.opaque.data(), opaque.size() * sizeof(voxel::ChunkVertex));
		std::memcpy(translucent.data(), r.mesh.translucent.data(), translucent.size() * sizeof(voxel::ChunkVertex));
		std::vector<voxel::DetailVertex> detail(r.mesh.detail.size());
		static_assert(sizeof(voxel::DetailVertex) == sizeof(voxel::DetailVertex));
		std::memcpy(detail.data(), r.mesh.detail.data(), detail.size() * sizeof(voxel::DetailVertex));
		m_renderer.QueueChunkMesh(r.key, r.coord * voxel::kChunkSize, std::move(opaque), std::move(translucent), std::move(detail));
	}
}

void Game::Save() {
	DIAG_ZONE("Game::Save");
	if (const auto *t = m_ecs.Get<voxel::Transform>(m_player))
		m_world->Storage().SetMeta("player", std::format("{},{},{},{},{},{}", t->position.x, t->position.y, t->position.z, m_yaw, m_pitch,
														 m_flying ? 1 : 0));
	m_world->Storage().SetMeta("time", std::to_string(m_timeOfDay));
	{
		std::string inv;
		auto put = [&](std::string_view slot, const ItemStack &st) {
			if (!st.Empty())
				inv += std::format("{}={}*{};", slot, m_content.ItemName(st.id), st.count);
		};
		for (size_t i = 0; i < m_slots.size(); ++i)
			put(std::to_string(i), m_slots[i]);
		for (size_t i = 0; i < m_armor.size(); ++i)
			put(std::format("a{}", i), m_armor[i]);
		put("o", m_offhand);
		m_world->Storage().SetMeta("inventory", inv);
		m_world->Storage().SetMeta("avatar", std::format("{},{}", voxel::SwimStyleId(m_swimStyle), int(m_cameraMode)));
	}
	m_worldSettings.playSeconds += double(m_tickCount - m_savedTick) / double(kTickRate);
	m_savedTick = m_tickCount;
	m_worldSettings.lastPlayed = NowText();
	m_worldSettings.Save(m_worldDir);
	m_world->SaveAll();
}

// ── Rendu ─────────────────────────────────────────────────────────────────
void Game::Render(app::Application &ctx) {
	DIAG_ZONE("Game::Render");
	const float sun = std::sin((m_timeOfDay - 0.25f) * math::TAU) * 0.5f + 0.5f; // 0 minuit, 1 midi
	const float daylight = std::clamp(0.18f + sun * 1.1f, 0.18f, 1.f);
	voxel::SceneParams params;
	params.daylight = std::clamp(daylight * m_settings.brightness, 0.05f, 1.6f);
	params.skyColor = {math::Lerp(0.02f, 0.55f, sun), math::Lerp(0.03f, 0.74f, sun), math::Lerp(0.08f, 0.95f, sun), 1.f};
	params.fogEnd = float(m_world->Config().viewDistance) * voxel::kChunkSize;
	params.fogStart = params.fogEnd * std::clamp(m_settings.fogStart, 0.05f, 0.98f);
	params.lodInterval = std::max(m_settings.textureLodInterval, 1.f);
	params.reflectionBounces = std::max(m_settings.reflectionBounces, 0.f);
	// Le personnage ne voit pas à travers les blocs opaques : la tête dans un bloc
	// plein n'affiche que du noir.
	{
		const int ex = math::FastFloor(m_camera.position.x), ey = math::FastFloor(m_camera.position.y), ez = math::FastFloor(m_camera.position.z);
		if (m_world->IsLoaded(ex, ey, ez)) {
			const voxel::BlockId head = m_world->GetBlock(ex, ey, ez);
			params.blackout = head != voxel::kAir && m_content.blocks.IsOpaque(head);
		}
	}
	CollectLights(m_camera.position, params.lights);
	// Grille d'opacité 3D (ombres portées) autour de la caméra : les blocs opaques
	// arrêtent la lumière, le feuillage/verre/eau la filtrent partiellement.
	{
		constexpr int kShadowVol = 48;
		std::array<uint8_t, size_t(kShadowVol) * kShadowVol * kShadowVol> opacity{};
		const int cx = math::FastFloor(m_camera.position.x), cy = math::FastFloor(m_camera.position.y), cz = math::FastFloor(m_camera.position.z);
		const int ox = cx - kShadowVol / 2, oy = cy - kShadowVol / 2, oz = cz - kShadowVol / 2;
		for (int y = 0; y < kShadowVol; ++y)
			for (int z = 0; z < kShadowVol; ++z)
				for (int x = 0; x < kShadowVol; ++x) {
					const int wx = ox + x, wy = oy + y, wz = oz + z;
					uint8_t op = 0;
					if (m_world->IsLoaded(wx, wy, wz)) {
						const voxel::BlockId b = m_world->GetBlock(wx, wy, wz);
						if (b != voxel::kAir)
							switch (m_content.blocks.MaterialOf(b)) {
							case voxel::Material::Opaque: op = 255; break;
							case voxel::Material::Cutout: op = 140; break;	   // feuillage : filtre
							case voxel::Material::Translucent: op = 90; break; // verre
							case voxel::Material::Liquid: op = 40; break;	   // eau / lave
							default: break;
							}
					}
					opacity[size_t(y) * kShadowVol * kShadowVol + size_t(z) * kShadowVol + size_t(x)] = op;
				}
		m_renderer.SetShadowVolume(opacity, kShadowVol, {float(cx), float(cy), float(cz)});
	}

	m_boxes.clear();
	std::vector<voxel::ModelDraw> rigDraws; // entités (et avatar) rendues par leur rig
	if (const auto *pt = m_ecs.Get<voxel::Transform>(m_player); pt && m_settings.showEntities) {
		m_world->ForEachEntityNear(pt->position, std::clamp(m_settings.entityRenderRadius, 0, 6), [&](ecs::Entity e) {
			if (e == m_player)
				return;
			const auto *t = m_ecs.Get<voxel::Transform>(e);
			const auto *b = m_ecs.Get<voxel::Body>(e);
			const auto *k = m_ecs.Get<voxel::Kind>(e);
			if (!t || !b)
				return;
			const std::string_view kindId = k ? std::string_view(k->id) : std::string_view{};
			// Rendu par le rig du kind s'il existe (modèle articulé + skin).
			const voxel::Model *model = nullptr;
			float scale = 1.f;
			if (const EntityRig *rig = m_content.FindEntityRig(kindId)) {
				if (const auto mit = m_content.rigModels.find(rig->model); mit != m_content.rigModels.end()) {
					model = &mit->second;
					scale = rig->scale;
				}
			}
			if (model) {
				const uint64_t handle = EntityModel(kindId);
				if (handle) {
					std::vector<voxel::ModelVertex> verts;
					const auto *vel = m_ecs.Get<voxel::Velocity>(e);
					const float speed = vel ? std::sqrt(vel->value.x * vel->value.x + vel->value.z * vel->value.z) : 0.f;
					if (speed > 0.3f) {
						voxel::BuildModelMesh(*model, voxel::SamplePose(*model, nullptr, 0, m_entityAnimTime), verts);
					} else {
						const voxel::Animation *idle = nullptr;
						for (const voxel::Animation &a : model->animations)
							if (a.name == "idle")
								idle = &a;
						std::vector<voxel::PartOffset> pose = idle ? voxel::SamplePose(*model, idle, m_entityAnimTime) : std::vector<voxel::PartOffset>{};
						voxel::BuildModelMesh(*model, pose, verts);
					}
					static_assert(sizeof(voxel::ModelVertex) == sizeof(voxel::ModelVertex));
					m_renderer.UpdateModelMesh(handle, std::span<const voxel::ModelVertex>(reinterpret_cast<const voxel::ModelVertex *>(verts.data()), verts.size()));
					const math::FVector3 s{scale, scale, scale};
					// Rigs des mods : face +Z ; le jeu fait face −Z → demi-tour.
					const math::FMatrix4 world = math::FMatrix4::Translation(t->position - math::FVector3{0, b->halfExtents.y, 0}) *
												 RotationY(t->yaw + math::PI) * math::FMatrix4::Scale(s);
					rigDraws.push_back({handle, world});
					return;
				}
			}
			// Repli : boîte colorée.
			const sdl3::Color c = KindColor(kindId);
			m_boxes.push_back({t->position, b->halfExtents, c.r, c.g, c.b, 255});
		});
	}
	std::optional<voxel::BoxInstance> outline;
	if (m_target) {
		const math::FVector3 center = m_target->block.ToFloat() + math::FVector3(0.5f);
		outline = voxel::BoxInstance{center, math::FVector3(0.502f), 20, 20, 20, 230};
	}
	m_renderer.Render(ctx.Commands(), ctx.SceneColor(), ctx.SceneDepth(), ctx.Aspect(), m_camera, params, m_boxes, outline ? &*outline : nullptr);

	// Avatar du joueur (skin animée) — à la première personne, la caméra est sur le
	// visage : corps, bras, jambes et objets tenus visibles, sans la tête.
	// L'aperçu du menu (cinématique) ne montre que le terrain : pas d'entité.
	if (const auto *t = m_ecs.Get<voxel::Transform>(m_player); t && !m_options.preview) {
		(void)t;
		UploadAvatarMesh();
		if (m_avatarRig && !m_avatarRig->parts.empty() && m_avatarModelHandle) {
			const math::FMatrix4 model = math::FMatrix4::Translation(m_bodyOrigin) * RotationY(m_bodyYaw) * RotationXYZ(m_pose.rootRotation) *
										math::FMatrix4::Translation(m_pose.rootOffset);
			rigDraws.push_back({m_avatarModelHandle, model});
		} else {
			const math::FMatrix4 model = math::FMatrix4::Translation(m_bodyOrigin) * RotationY(m_bodyYaw);
			m_renderer.RenderModel(ctx.Commands(), ctx.SceneColor().Get(), ctx.SceneDepth().Get(), ctx.Aspect(), m_camera, params, model);
		}
	}
	if (!rigDraws.empty())
		m_renderer.RenderModels(ctx.Commands(), ctx.SceneColor().Get(), ctx.SceneDepth().Get(), ctx.Aspect(), m_camera, params, rigDraws);

	// ── Post-traitement plein écran ──────────────────────────────────────
	voxel::PostParams post;
	post.effects2.y = float(m_tickCount) / 60.f; // temps (ondulations)
	post.effects2.z = float(ctx.Width());
	post.effects2.w = float(ctx.Height());
	{
		const int ex = math::FastFloor(m_camera.position.x), ey = math::FastFloor(m_camera.position.y), ez = math::FastFloor(m_camera.position.z);
		if (m_world->IsLoaded(ex, ey, ez)) {
			const voxel::BlockId hb = m_world->GetBlock(ex, ey, ez);
			const voxel::BlockDef &hd = m_content.blocks.Get(hb);
			if (hd.IsLiquid()) {
				post.effects2.x = 1.f;			  // noyade : asphyxie
				if (hd.HasTag("hot"))
					post.effects.y = 1.f;		  // lave : chaleur
				else
					post.effects.x = 1.f;		  // eau : sous-marin
			} else if (m_world->PointInSolid(m_camera.position)) {
				post.effects2.x = 1.f;			  // tête dans une boîte de collision (porte, dalle…)
			} else if (hd.HasTag("hot")) {
				post.effects.y = 1.f;			  // magma : chaleur
			}
		}
		// Éblouissement : source forte dans le champ de vision.
		const math::FVector3 fwd = m_camera.Forward();
		for (const voxel::PointLight &l : params.lights) {
			const math::FVector3 dir = (l.position - m_camera.position).Normalized();
			const float cosAng = dir.x * fwd.x + dir.y * fwd.y + dir.z * fwd.z;
			if (cosAng > 0.6f) {
				post.effects.z = std::clamp((cosAng - 0.6f) / 0.4f * std::min(l.intensity * 0.5f, 1.f), 0.f, 1.f);
				break;
			}
		}
	}
	// Effets définis par les scripts de mods (ils peuvent amplifier les effets).
	auto scriptEffect = [&](const char *name) -> float {
		const auto it = m_postEffects.find(name);
		return it == m_postEffects.end() ? 0.f : it->second;
	};
	post.effects.x = std::max(post.effects.x, scriptEffect("underwater"));
	post.effects.y = std::max(post.effects.y, scriptEffect("heat"));
	post.effects.z = std::max(post.effects.z, scriptEffect("glare"));
	post.effects.w = std::max(post.effects.w, scriptEffect("stun"));
	post.effects2.x = std::max(post.effects2.x, scriptEffect("suffocate"));
	m_renderer.PostProcess(ctx.Commands(), ctx.SceneColor().Get(), ctx.PostColor().Get(), post);
}

} // namespace voxelia
