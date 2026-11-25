// ============================================================================
// Abyssale — jardin abyssal sous-marin, monde océanique en streaming.
//
// Canyons rocheux, forêts de varech, grotte violette, épave, nageoire de baleine sculptée ;
// bancs de poissons, raies manta, bulles, particules ; rayons de lumière, caustiques, surface
// ondulante vue d'en dessous ; ambiance sonore synthétisée en temps réel.
//
// Le monde est une grille de chunks générés procéduralement autour du plongeur (streaming
// multi-threadé), disposés en labyrinthe depuis la graine. La carte d'origine est la carte
// d'entrée (chunk 0,0) ; les autres templates (barrière de coraux, épave de paquebot, …)
// sont placés aléatoirement. Les parties sont créées depuis une graine (modifiable), puis
// sauvegardées (métadonnées JSON + faune par chunk sur foundation.sql) et rechargées.
//
// Rendu : sdl3::Renderer3D en milieu sous-marin (`Lighting::water`), ondulations au GPU
// (`Material::sway`), anticrénelage temporel (TAA, FXAA en repli) et tramage.
//
//   abyssale [--quality=0|1|2] [--mute] [--stats] [--no-taa] [--diagnostics]
// ============================================================================
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>
#include <diagnostics/assert.hpp>
#include <diagnostics/session.hpp>
#include <diagnostics/trace.hpp>
#include <sdl3/input/input.hpp>
#include <sdl3/io/filesystem.hpp>
#include <sdl3/render/render3d.hpp>
#include <ui/builder.hpp>
#include <ui/retained.hpp>
#include <ui/ui.hpp>

#include "abyssale/ambience.hpp"
#include "abyssale/chunk.hpp"
#include "abyssale/config.hpp"
#include "abyssale/ecosystem.hpp"
#include "abyssale/meshes.hpp"
#include "abyssale/ocean_sound.hpp"
#include "abyssale/save.hpp"
#include "abyssale/stream.hpp"
#include "abyssale/terrain.hpp"
#include "abyssale/textures.hpp"
#include "abyssale/world.hpp"

namespace {

using namespace abyssale;
using math::FMatrix4;
using math::FVector3;

/// Style du menu principal (embarqué : aucun fichier à livrer).
constexpr const char *kMenuCss = R"css(
:root { color: #dcecf5; font-size: 14px; accent-color: #3fb6e0; widget-color: #1b2f40; widget-hover-color: #243d52; widget-active-color: #2c4a60; }
.overlay { background-color: #030a1296; }
.card { background-color: #0a1a28e6; border: 1px solid #50aad278; border-radius: 14px; padding: 22px 26px; gap: 8px; }
.title { font-size: 26px; font-weight: 700; color: #78d6f0; }
.subtitle { font-size: 13px; color: #8ea7b6; margin-bottom: 10px; }
.caption { font-size: 12px; color: #8ea7b6; margin-top: 4px; }
button { padding: 8px 16px; border-radius: 8px; background-color: #1b2f40; border: 1px solid #2c4a60; color: #e6f3fa; }
button:hover { background-color: #243d52; border-color: #3f6a86; }
button.primary { background-color: #1d6f93; border: 1px solid #5cc4e8; color: #ffffff; font-weight: 700; }
button.primary:hover { background-color: #2584ad; }
button.quit:hover { background-color: #4a2230; border-color: #a04a62; }
button.danger { background-color: #6b2434; border-color: #a04a62; }
button.danger:hover { background-color: #8a2f44; }
combobox { padding: 6px 10px; border-radius: 8px; background-color: #1b2f40; border: 1px solid #2c4a60; }
combobox:hover { background-color: #243d52; }
lineedit { padding: 6px 10px; border-radius: 8px; background-color: #12202d; border: 1px solid #2c4a60; }
scroll { background-color: transparent; }
)css";

struct Options {
	int quality = 0;
	bool mute = false;
	bool stats = false;
	bool taa = true;
	bool diagnostics = false;
};

/// Graine aléatoire (générée à la création d'une partie, modifiable dans le menu).
uint64_t RandomSeed() {
	std::random_device rd;
	return (uint64_t(rd()) << 32) ^ uint64_t(rd());
}

/// Dossier d'une partie dérivé de sa graine.
std::string FolderFor(uint64_t seed) { return std::format("plongee-{}", seed); }

enum class Screen { Main, Worlds };

class AbyssaleApp final : public app::Application {
public:
	explicit AbyssaleApp(Options options) : m_options(options), m_quality(std::clamp(options.quality, 0, 2)), m_taa(options.taa), m_diagnostics(options.diagnostics) {}

protected:
	bool OnInit() override;
	void OnUpdate(double frameSeconds) override;
	void OnOffscreen() override;
	void OnRender(const sdl3::FRect &area, double alpha) override;
	void OnShutdown() override {
		SaveCurrentWorld();
		if (m_options.stats && m_statFrames > 0)
			core::log::Info("abyssale : {} images, {:.1f} ms en moyenne ({:.0f} i/s), pire {:.1f} ms", m_statFrames, m_statSeconds * 1000.0 / double(m_statFrames),
							double(m_statFrames) / m_statSeconds, m_statWorst * 1000.0);
	}

private:
	void BuildPopulation();
	void SetDiving(bool diving);
	void BuildMenu();
	void RecreateWorld(uint64_t seed, const FVector3 &position, const std::string &folder);
	void StartDive();
	void SaveCurrentWorld();
	void ApplyDiagnostics();
	sdl3::TextureId Upload(const Image &img) { return m_r3d->CreateTexture(img.rgba, img.width, img.height); }
	[[nodiscard]] FVector3 Forward() const noexcept {
		const float cp = std::cos(m_pitch);
		return {std::sin(m_yaw) * cp, std::sin(m_pitch), -std::cos(m_yaw) * cp};
	}
	[[nodiscard]] sdl3::Material MaterialFor(Model model) const;
	[[nodiscard]] bool DiverSimulated() const noexcept { return !m_menu; }

	Options m_options;
	std::unique_ptr<sdl3::Renderer3D> m_r3d;
	sdl3::RenderTarget3D m_target;
	std::unique_ptr<Ocean> m_ocean;
	std::unique_ptr<Ambience> m_ambience;
	Shapes m_shapes;
	OceanSound m_sound;

	std::array<sdl3::MeshId, kModelCount> m_models{};
	sdl3::MeshId m_surface = 0, m_mote = 0, m_shaft = 0, m_fishFar = 0;
	core::RobinMap<ChunkKey, sdl3::MeshId> m_chunkMeshes;

	struct Textures {
		sdl3::TextureId terrain = 0, rock = 0, leaf = 0, sponge = 0, wood = 0, shaft = 0, manta = 0, fish = 0, shell = 0;
	} m_tex;

	sdl3::FRect m_view{};
	int m_targetW = 0, m_targetH = 0;
	FVector3 m_pos{0.f, -18.f, 0.f};
	FVector3 m_lastCamera{};
	float m_yaw = 0.f, m_pitch = 0.25f;
	bool m_menu = true;
	int m_quality = 0;
	bool m_taa = true;
	bool m_diagnostics = false;

	Screen m_screen = Screen::Main;
	std::string m_seedText;
	uint64_t m_seed = 0;
	std::string m_activeWorld; ///< dossier de la partie en cours (vide : non sauvegardée)
	std::string m_metaCreated;
	double m_playSeconds = 0;
	std::vector<std::pair<std::string, WorldMeta>> m_worlds;

	std::unique_ptr<ui::Ui> m_menuUi;
	ui::Widget m_playButton, m_volumeText;
	float m_volumePercent = 50.f;
	bool m_menuResume = false;
	float m_time = 0.f;
	float m_exhaleTimer = 3.f;
	bool m_exhale = false;
	std::vector<std::vector<sdl3::MeshInstance>> m_scratch;
	std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
	uint64_t m_statFrames = 0;
	double m_statSeconds = 0.0, m_statWorst = 0.0;
};

bool AbyssaleApp::OnInit() {
	m_r3d = sdl3::Renderer3D::Create(*Gpu());
	if (!m_r3d)
		return false;
	m_tex.terrain = Upload(TerrainAtlas());
	m_tex.rock = Upload(RockTexture());
	m_tex.leaf = Upload(LeafTexture());
	m_tex.sponge = Upload(SpongeTexture());
	m_tex.wood = Upload(WoodTexture());
	m_tex.shaft = Upload(ShaftTexture());
	m_tex.manta = Upload(MantaTexture());
	m_tex.fish = Upload(FishTexture());
	m_tex.shell = Upload(ShellTexture());

	// ── Maillages du catalogue (le terrain est maillé par chunk) ────────────
	const Terrain catalogTerrain;
	std::vector<sdl3::MeshData> models(kModelCount);
	for (size_t m = 0; m < kModelCount; ++m) {
		if (Model(m) == Model::Terrain)
			continue;
		models[m] = meshes::ModelMesh(Model(m), catalogTerrain);
		m_models[m] = m_r3d->Upload(models[m]);
	}
	m_shapes = MakeShapes(models);
	m_surface = m_r3d->Upload(meshes::Surface(2.f * kHalfExtent + 200.f));
	m_mote = m_r3d->Upload(sdl3::MeshData::Sphere(1.f, 8, 6));
	m_shaft = m_r3d->Upload(meshes::Shaft());
	m_fishFar = m_r3d->Upload(meshes::FishFar());

	m_seed = RandomSeed() % 1000000000;
	m_seedText = std::to_string(m_seed);
	RecreateWorld(m_seed, {0.f, -18.f, 0.f}, FolderFor(m_seed));
	m_sound.SetVolume(m_options.mute ? 0.f : 0.5f);
	if (!m_options.mute && !m_sound.Open())
		core::log::Warn("abyssale : aucune sortie audio, partie silencieuse");
	SetDiving(false);
	BuildMenu();
	const sdl3::Point size = Window().GetSize();
	m_view = {0.f, 0.f, float(size.x), float(size.y)};
	return true;
}

void AbyssaleApp::ApplyDiagnostics() {
	const bool active = !diagnostics::session::BasePath().empty();
	if (m_diagnostics == active)
		return;
	if (m_diagnostics) {
		diagnostics::session::Options diag;
		diag.app = "abyssale";
		const std::string base = diagnostics::session::Start(diag);
		if (!base.empty())
			core::log::Info("abyssale : diagnostics activés ({}.vcap)", base);
	} else {
		core::log::Info("abyssale : diagnostics désactivés");
		diagnostics::session::Stop();
	}
}

void AbyssaleApp::BuildPopulation() {
	if (!m_ocean)
		return;
	const Quality &q = kQualities[size_t(m_quality)];
	m_ambience = std::make_unique<Ambience>(m_ocean->GetTerrain(), q, m_seed ^ 0xA3B1);
}

void AbyssaleApp::RecreateWorld(uint64_t seed, const FVector3 &position, const std::string &folder) {
	// Libère les maillages de terrain des chunks précédents.
	for (const auto &[key, mesh] : m_chunkMeshes)
		m_r3d->Release(mesh);
	m_chunkMeshes.Clear();
	m_ambience.reset();
	const Quality &q = kQualities[size_t(m_quality)];
	std::filesystem::path dir = sdl3::filesystem::AppDataDir("abyssale").c_str();
	m_ocean = std::make_unique<Ocean>(q, m_shapes, seed, dir / "worlds" / folder, jobs::ThreadPool::Global(), position, m_time);
	m_ocean->Open();
	m_seed = seed;
	m_activeWorld = folder;
	m_metaCreated.clear();
	m_pos = position;
	m_playSeconds = 0;
	m_lastCamera = {};
	m_ambience = std::make_unique<Ambience>(m_ocean->GetTerrain(), q, seed ^ 0xA3B1);
	m_ocean->Eco().PlaceDiver(position);
	core::log::Info("abyssale : monde créé (graine {})", seed);
}

void AbyssaleApp::StartDive() {
	uint64_t seed = 0;
	const char *s = m_seedText.c_str();
	const char *end = s + m_seedText.size();
	const auto res = std::from_chars(s, end, seed);
	if (res.ec != std::errc{} || seed == 0)
		seed = core::HashString(m_seedText) % 1000000000 + 1;
	if (seed != m_seed || !m_ocean)
		RecreateWorld(seed, {0.f, -18.f, 0.f}, FolderFor(seed));
	SaveCurrentWorld();
	m_menu = false;
	SetDiving(true);
}

void AbyssaleApp::SaveCurrentWorld() {
	if (!m_ocean || m_activeWorld.empty())
		return;
	WorldMeta meta;
	meta.name = std::format("Plongée {}", m_seed);
	meta.seed = m_seed;
	meta.quality = m_quality;
	meta.position = m_pos;
	meta.yaw = m_yaw;
	meta.pitch = m_pitch;
	meta.created = m_metaCreated.empty() ? NowText() : m_metaCreated;
	meta.lastPlayed = NowText();
	meta.playSeconds = m_playSeconds;
	meta.Save(sdl3::filesystem::AppDataDir("abyssale").c_str() / std::filesystem::path("worlds") / m_activeWorld);
	m_ocean->SaveAll();
	if (m_metaCreated.empty())
		m_metaCreated = meta.created;
}

void AbyssaleApp::BuildMenu() {
	m_volumePercent = m_sound.Volume() * 100.f;
	m_menuUi = std::make_unique<ui::Ui>(Canvas(), Events());
	m_menuUi->SetTheme(Ui().Style());
	m_menuUi->Sheet(ui::StyleSheet::Parse(kMenuCss, "abyssale-menu"));

	if (m_screen == Screen::Worlds) {
		m_worlds = ListWorlds(sdl3::filesystem::AppDataDir("abyssale").c_str() / std::filesystem::path("worlds"));
		auto col = ui::Column().Class("card").Position(ui::Position::Absolute).Anchor(ui::Anchor::Center).Width(520.f).Height(460.f);
		col.Child(ui::Text("P A R T I E S").Class("title").TextAlign(ui::TextAlign::Center));
		col.Child(ui::Text(std::format("{} partie(s) sauvegardée(s)", m_worlds.size())).Class("subtitle").TextAlign(ui::TextAlign::Center));
		auto scroll = ui::Scroll().Grow();
		if (m_worlds.empty())
			scroll.Child(ui::Text("Aucune partie : créez-en une depuis le menu.").Class("caption"));
		for (const auto &[folder, meta] : m_worlds) {
			auto row = ui::Row().Gap(8.f).AlignItems(ui::Align::Center);
			row.Child(ui::Text(meta.name).Grow());
			row.Child(ui::Button("Ouvrir").OnClick([this, folder] {
				if (const auto saved = WorldMeta::Load(sdl3::filesystem::AppDataDir("abyssale").c_str() / std::filesystem::path("worlds") / folder)) {
					m_quality = std::clamp(saved->quality, 0, 2);
					RecreateWorld(saved->seed, saved->position, folder);
					m_yaw = saved->yaw;
					m_pitch = saved->pitch;
					m_metaCreated = saved->created;
					m_menu = false;
					SetDiving(true);
				}
			}));
			row.Child(ui::Button("Supprimer").Class("danger").OnClick([this, folder, name = meta.name] {
				DeleteWorld(sdl3::filesystem::AppDataDir("abyssale").c_str() / std::filesystem::path("worlds"), folder);
				m_menuUi->Notify(std::format("Partie « {} » supprimée", name), Ui().Style().warning);
				BuildMenu();
			}));
			scroll.Child(std::move(row));
		}
		col.Child(std::move(scroll));
		col.Child(ui::Button("Retour").OnClick([this] {
			m_screen = Screen::Main;
			BuildMenu();
		}));
		m_menuUi->Root(ui::Box().Class("overlay").Children({std::move(col)}));
		return;
	}

	m_menuUi->Root(ui::Box().Class("overlay").Children({
		ui::Column()
			.Class("card")
			.Position(ui::Position::Absolute)
			.Anchor(ui::Anchor::Center)
			.Width(380.f)
			.Children({
				ui::Text("A B Y S S A L E").Class("title").TextAlign(ui::TextAlign::Center),
				ui::Text("Jardin abyssal sous-marin").Class("subtitle").TextAlign(ui::TextAlign::Center),
				ui::Text("Graine du monde").Class("caption"),
				ui::Row().Gap(8.f).Children({
					ui::LineEdit().Bind(m_seedText).Grow().Tooltip("Un même nombre redonne le même monde"),
					ui::Button("↻").OnClick([this] { m_seedText = std::to_string(RandomSeed() % 1000000000); }),
				}),
				ui::PrimaryButton("Créer la plongée").Ref(m_playButton).OnClick([this] { StartDive(); }),
				ui::Button("Parties sauvegardées").OnClick([this] {
					m_screen = Screen::Worlds;
					BuildMenu();
				}),
				ui::Separator(),
				ui::Text("Qualité").Class("caption"),
				ui::ComboBox()
					.Options({kQualityNames[0], kQualityNames[1], kQualityNames[2]})
					.Bind(m_quality)
					.OnIndexChanged([this](int index) {
						m_quality = index;
						BuildPopulation();
					}),
				ui::Checkbox("Anticrénelage temporel").Bind(m_taa).Tooltip("Lisse le scintillement des feuilles et des détails lointains"),
				ui::Text("Volume des effets sonores").Class("caption"),
				ui::Row().Gap(12.f).AlignItems(ui::Align::Center).Children({
					ui::Slider(0.f, 100.f).Bind(m_volumePercent).Grow().OnChange([this](float v) { m_sound.SetVolume(v / 100.f); }),
					ui::Text("").Ref(m_volumeText).Width(44.f).TextAlign(ui::TextAlign::Right),
				}),
				ui::Separator(),
				ui::Checkbox("Activation du diagnostic").Bind(m_diagnostics).Tooltip("Trace, journal et rapport de plantage"),
				ui::Separator(),
				ui::Button("Quitter").Class("quit").OnClick([this] { RequestQuit(); }),
			}),
	}));
}

void AbyssaleApp::SetDiving(bool diving) {
	if (m_ocean)
		m_ocean->Eco().SetDiverPhysics(DiverSimulated());
	sdl3::mouse::SetRelative(Window().Get(), diving);
	if (diving)
		sdl3::mouse::Hide();
	else
		sdl3::mouse::Show();
}

void AbyssaleApp::OnUpdate(double frameSeconds) {
	const float dt = std::clamp(float(frameSeconds), 0.f, 0.1f);
	m_time += dt;
	if (m_time > 8.f) {
		++m_statFrames;
		m_statSeconds += frameSeconds;
		m_statWorst = std::max(m_statWorst, frameSeconds);
	}
	if (!m_menu)
		m_playSeconds += double(dt);
	sdl3::EventsManager &in = Events();

	if (in.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed() && !Ui().WantsKeyboard()) {
		m_menu = !m_menu;
		if (m_menu)
			SaveCurrentWorld();
		SetDiving(!m_menu);
	}

	float speed = 0.f;
	if (!m_menu) {
		m_yaw += in.MouseRelX() * 0.0022f;
		m_pitch = std::clamp(m_pitch - in.MouseRelY() * 0.0022f, -1.5f, 1.5f);
		const bool fwd = in.Key(SDLK_Z).IsPressed() || in.Key(SDLK_W).IsPressed() || in.Key(SDLK_UP).IsPressed();
		const bool back = in.Key(SDLK_S).IsPressed() || in.Key(SDLK_DOWN).IsPressed();
		const bool right = in.Key(SDLK_D).IsPressed() || in.Key(SDLK_RIGHT).IsPressed();
		const bool left = in.Key(SDLK_Q).IsPressed() || in.Key(SDLK_LEFT).IsPressed();
		const bool up = in.Key(SDLK_E).IsPressed() || in.Key(SDLK_SPACE).IsPressed();
		const bool down = in.Key(SDLK_A).IsPressed() || in.Key(SDLK_C).IsPressed();
		const FVector3 forward = Forward(), rightV{std::cos(m_yaw), 0.f, std::sin(m_yaw)};
		FVector3 wish{};
		if (fwd) wish += forward;
		if (back) wish -= forward;
		if (right) wish += rightV;
		if (left) wish -= rightV;
		if (up) wish.y += 1.f;
		if (down) wish.y -= 1.f;
		if (wish.LengthSq() > 1e-6f)
			wish = wish.Normalized();
		const float target = in.Shift() ? 9.f : 4.f;
		m_ocean->Eco().SteerDiver(wish * target, dt);
	}

	m_exhale = false;
	if (!m_menu) {
		m_exhaleTimer -= dt;
		if (m_exhaleTimer <= 0.f) {
			m_exhale = true;
			m_exhaleTimer = 4.f + 1.f * std::sin(m_time);
		}
	}
	const FVector3 sunDir = FVector3{0.22f, -1.f, 0.12f}.Normalized();
	{
		DIAG_ZONE("Abyssale::Streaming");
		m_ocean->Update(m_pos, dt);
	}
	{
		DIAG_ZONE("Abyssale::Ecosystem");
		m_ocean->Eco().Update(dt, m_time, Forward(), m_exhale);
	}
	m_pos = m_ocean->Eco().DiverPosition();
	speed = m_ocean->Eco().DiverVelocity().Length();
	if (!DIAG_VERIFY(std::isfinite(m_pos.x) && std::isfinite(m_pos.y) && std::isfinite(m_pos.z), "position du plongeur invalide ({}, {}, {})", m_pos.x, m_pos.y,
					 m_pos.z)) {
		m_pos = {0.f, -18.f, 0.f};
		m_ocean->Eco().PlaceDiver(m_pos);
	}
	{
		DIAG_ZONE("Abyssale::Ambience");
		m_ambience->Update(dt, m_time, m_pos, sunDir);
	}
	{
		DIAG_ZONE("Abyssale::Sound");
		m_sound.Update({speed, std::max(0.f, kSurfaceY - m_pos.y), m_ocean->Eco().NearbyBubbles(), m_exhale});
	}
	DIAG_COUNTER("abyssale.chunks", m_ocean->LoadedChunkCount());
	DIAG_COUNTER("abyssale.entites", m_ocean->Eco().Entities().EntityCount());
	DIAG_COUNTER("abyssale.corps", m_ocean->Eco().Physics().BodyCount());
	DIAG_COUNTER("abyssale.profondeur_m", kSurfaceY - m_pos.y);
}

/// Portée d'affichage d'une catégorie.
struct DrawRange {
	float distance = 0.f, radius = 0.f;
};

DrawRange RangeOf(Model model) {
	const size_t m = size_t(model);
	if (m >= size_t(Model::Kelp0) && m <= size_t(Model::Kelp2))
		return {110.f, kKelpHeights[m - size_t(Model::Kelp0)] * 1.3f};
	switch (model) {
	case Model::Frond0:
	case Model::Frond1: return {90.f, 10.f};
	case Model::Tuft: return {60.f, 1.f};
	case Model::Seagrass0:
	case Model::Seagrass1:
	case Model::Seagrass2: return {55.f, 1.f};
	case Model::Barrel0:
	case Model::Barrel1: return {90.f, 3.f};
	case Model::Tubes: return {80.f, 2.f};
	case Model::Coral:
	case Model::Fan: return {80.f, 3.f};
	case Model::SeaWhip: return {50.f, 1.5f};
	case Model::Mushroom: return {50.f, 1.f};
	case Model::SeaPen: return {45.f, 1.f};
	case Model::TubeWorm: return {35.f, 0.6f};
	case Model::Barnacles: return {30.f, 0.6f};
	case Model::Anemone0:
	case Model::Anemone1:
	case Model::FeatherDuster: return {60.f, 1.f};
	case Model::Fish: return {90.f, 1.f};
	case Model::Manta: return {140.f, 3.5f};
	case Model::Crab: return {50.f, 1.f};
	case Model::Lobster: return {60.f, 2.f};
	case Model::Shrimp: return {40.f, 0.5f};
	case Model::VentChimney: return {80.f, 2.f};
	case Model::Statue: return {70.f, 2.f};
	default: return {};
	}
}

sdl3::Material AbyssaleApp::MaterialFor(Model model) const {
	sdl3::Material mat = sdl3::Material::Pbr({1.f, 1.f, 1.f}, 0.f, 0.6f);
	if (const SwayParams sway = PlantSway(model); sway.Active()) {
		mat.sway = sdl3::Material::Sway::Plant;
		mat.swayAmplitude = sway.amplitude;
		mat.swayFrequency = sway.frequency;
		mat.swayHeight = sway.height;
	}
	switch (model) {
	case Model::Terrain:
		mat = sdl3::Material::Pbr({0.48f, 0.56f, 0.6f}, 0.f, 0.95f);
		mat.texture = m_tex.terrain;
		mat.triplanar = true;
		mat.terrainAtlas = true;
		mat.uvScale = {0.9f, 0.9f};
		mat.groundCover = sdl3::Srgb(0xc23a9e);
		break;
	case Model::Rock0:
	case Model::Rock1:
	case Model::Rock2:
	case Model::Arch:
		mat = sdl3::Material::Pbr({0.6f, 0.64f, 0.66f}, 0.f, 0.9f);
		mat.texture = m_tex.rock;
		mat.triplanar = true;
		mat.uvScale = {1.1f, 1.1f};
		break;
	case Model::Wreck:
		mat = sdl3::Material::Pbr({0.85f, 0.82f, 0.76f}, 0.f, 0.85f);
		mat.texture = m_tex.wood;
		mat.doubleSided = true;
		break;
	case Model::Fluke:
		mat = sdl3::Material::Pbr({0.78f, 0.84f, 0.86f}, 0.f, 0.9f);
		mat.texture = m_tex.rock;
		mat.triplanar = true;
		mat.uvScale = {2.f, 2.f};
		break;
	case Model::VentChimney:
		mat = sdl3::Material::Pbr({0.42f, 0.4f, 0.38f}, 0.f, 0.9f);
		mat.texture = m_tex.rock;
		mat.triplanar = true;
		mat.uvScale = {1.2f, 1.2f};
		break;
	case Model::Statue:
		mat = sdl3::Material::Pbr({0.74f, 0.7f, 0.62f}, 0.f, 0.85f);
		mat.texture = m_tex.rock;
		mat.triplanar = true;
		mat.uvScale = {1.4f, 1.4f};
		break;
	case Model::Kelp0:
	case Model::Kelp1:
	case Model::Kelp2:
		mat.color = {0.4f, 0.5f, 0.36f};
		mat.roughness = 0.55f;
		mat.texture = m_tex.leaf;
		mat.doubleSided = true;
		mat.translucency = 0.55f;
		mat.nearFade = 0.6f;
		break;
	case Model::Frond0:
	case Model::Frond1:
		mat.roughness = 0.5f;
		mat.texture = m_tex.leaf;
		mat.doubleSided = true;
		mat.translucency = 0.5f;
		mat.nearFade = 0.6f;
		break;
	case Model::Tuft:
		mat.doubleSided = true;
		mat.castShadow = false;
		mat.translucency = 0.6f;
		mat.emissive = {0.12f, 0.03f, 0.1f};
		mat.nearFade = 0.8f;
		break;
	case Model::Seagrass0:
	case Model::Seagrass1:
	case Model::Seagrass2:
		mat.roughness = 0.7f;
		mat.doubleSided = true;
		mat.castShadow = false;
		mat.translucency = 0.5f;
		mat.nearFade = 0.8f;
		break;
	case Model::Barrel0:
	case Model::Barrel1:
	case Model::Tubes:
		mat.roughness = 0.92f;
		mat.texture = m_tex.sponge;
		mat.doubleSided = true;
		mat.nearFade = 1.f;
		break;
	case Model::Coral:
	case Model::Fan:
		mat.roughness = 0.65f;
		mat.nearFade = 1.f;
		break;
	case Model::SeaWhip:
	case Model::SeaPen:
		mat.castShadow = false;
		mat.nearFade = 0.8f;
		break;
	case Model::Mushroom:
	case Model::TubeWorm:
	case Model::Barnacles:
		mat.roughness = model == Model::Barnacles ? 0.85f : 0.6f;
		mat.castShadow = false;
		mat.nearFade = 0.6f;
		break;
	case Model::Anemone0:
	case Model::Anemone1:
		mat.color = {1.f, 0.7f, 0.8f};
		mat.roughness = 0.5f;
		mat.doubleSided = true;
		mat.nearFade = 0.6f;
		break;
	case Model::FeatherDuster:
		mat.color = {1.f, 0.9f, 0.7f};
		mat.doubleSided = true;
		mat.nearFade = 0.6f;
		break;
	case Model::Fish:
		mat = sdl3::Material::Pbr({0.9f, 0.94f, 0.98f}, 0.55f, 0.32f);
		mat.texture = m_tex.fish;
		mat.sway = sdl3::Material::Sway::Swim;
		mat.swayAmplitude = 0.06f;
		mat.swayFrequency = 10.f;
		mat.castShadow = false;
		break;
	case Model::Manta:
		mat = sdl3::Material::Pbr({1.f, 1.f, 1.f}, 0.05f, 0.55f);
		mat.texture = m_tex.manta;
		mat.sway = sdl3::Material::Sway::Flap;
		mat.swayAmplitude = 0.22f;
		mat.swayFrequency = 1.5f;
		break;
	case Model::Crab:
	case Model::Lobster:
		mat = sdl3::Material::Pbr({1.f, 1.f, 1.f}, 0.15f, 0.62f);
		mat.texture = m_tex.shell;
		mat.sway = sdl3::Material::Sway::Walk;
		mat.swayAmplitude = model == Model::Crab ? 0.035f : 0.018f;
		mat.swayFrequency = model == Model::Crab ? 9.f : 6.f;
		mat.swayHeight = model == Model::Crab ? 0.22f : 0.05f;
		break;
	case Model::Shrimp:
		mat = sdl3::Material::Pbr({1.f, 1.f, 1.f}, 0.1f, 0.35f);
		mat.texture = m_tex.shell;
		mat.translucency = 0.5f;
		mat.sway = sdl3::Material::Sway::Paddle;
		mat.swayAmplitude = 0.012f;
		mat.swayFrequency = 7.f;
		mat.swayHeight = 0.11f;
		break;
	case Model::Bubble:
		mat = sdl3::Material::Glass(sdl3::Srgb(0x86c4d4), 0.35f);
		mat.additive = true;
		mat.rim = true;
		mat.emissive = sdl3::Srgb(0x9adcf0) * 0.06f;
		mat.castShadow = false;
		mat.caustics = false;
		break;
	default: break;
	}
	return mat;
}

void AbyssaleApp::OnOffscreen() {
	DIAG_ZONE("Abyssale::OnOffscreen");
	if (!m_r3d || m_view.w <= 0.f || !m_ocean)
		return;
	const float scale = Canvas().Scale();
	const int w = int(m_view.w * scale), h = int(m_view.h * scale);
	if (w <= 0 || h <= 0)
		return;
	if (w != m_targetW || h != m_targetH) {
		if (!m_target.Resize(*m_r3d, &Renderer(), w, h))
			return;
		m_targetW = w;
		m_targetH = h;
	}
	const Quality &q = kQualities[size_t(m_quality)];

	// Intègre les chunks générés (maillages de terrain à téléverser).
	for (auto &[key, mesh] : m_ocean->DrainMeshes())
		m_chunkMeshes[key] = m_r3d->Upload(mesh);
	for (const ChunkKey key : m_ocean->DrainUnloaded())
		if (const sdl3::MeshId *mesh = m_chunkMeshes.Find(key)) {
			m_r3d->Release(*mesh);
			m_chunkMeshes.Erase(key);
		}

	const FVector3 fwd = Forward();
	const auto visible = [&](const std::vector<sdl3::MeshInstance> &in, float maxDistance, float radius) {
		std::vector<sdl3::MeshInstance> &out = m_scratch.emplace_back();
		out.reserve(in.size());
		for (const sdl3::MeshInstance &inst : in) {
			const FVector3 d = FVector3{inst.model.m[3][0], inst.model.m[3][1], inst.model.m[3][2]} - m_pos;
			const float dist = d.Length();
			if (dist > maxDistance + radius)
				continue;
			if (dist > radius && math::Dot(d, fwd) < -radius)
				continue;
			out.push_back(inst);
		}
		return std::span<const sdl3::MeshInstance>(out);
	};
	m_scratch.clear();
	m_scratch.reserve(kModelCount + 2);

	sdl3::Camera3D camera;
	camera.position = m_pos;
	camera.target = m_pos + Forward();
	camera.fovY = 62.f * math::DEG2RAD;
	camera.zNear = 0.15f;
	camera.zFar = 300.f;

	sdl3::Lighting lighting;
	lighting.time = m_time;
	lighting.sun.direction = {0.22f, -1.f, 0.12f};
	lighting.sun.color = sdl3::Srgb(0xfff2d6);
	lighting.sun.intensity = 5.6f;
	lighting.sun.castShadow = q.shadows;
	lighting.sun.shadowCenter = m_pos + Forward() * 18.f;
	lighting.sun.shadowExtent = 48.f;
	sdl3::Environment &env = lighting.environment;
	env.zenith = sdl3::Srgb(0x8fdcea);
	env.horizon = sdl3::Srgb(0x1f7c95);
	env.ground = sdl3::Srgb(0x175a70);
	env.intensity = 0.52f;
	env.drawSky = true;
	env.showSun = false;
	env.fogDensity = 0.03f;
	lighting.water.enabled = true;
	lighting.water.surfaceY = kSurfaceY;
	lighting.water.absorption = 0.012f;
	lighting.water.caustics = 0.42f;
	lighting.water.causticsScale = 0.32f;
	lighting.water.depthFog = 0.9f;
	lighting.exposure = 1.2f;
	lighting.post.bloomStrength = 0.22f;
	lighting.post.bloomThreshold = 3.f;
	lighting.post.raysStrength = 0.5f;
	lighting.post.saturation = 1.1f;
	lighting.post.vignette = 0.55f;
	lighting.post.taa.enabled = m_taa;
	lighting.post.taa.feedback = 0.9f;
	lighting.post.taa.sharpness = 0.25f;
	lighting.post.fxaa = !m_taa;

	// ── Terrain : un maillage par chunk chargé ──
	const sdl3::Material terrainMat = MaterialFor(Model::Terrain);
	for (const auto &[key, mesh] : m_chunkMeshes)
		if (mesh)
			m_r3d->Draw(mesh, FMatrix4::Identity(), terrainMat);

	// ── Entités : une passe par catégorie ──
	for (size_t m = 0; m < kModelCount; ++m) {
		const Model model = Model(m);
		if (model == Model::Terrain)
			continue;
		const std::vector<sdl3::MeshInstance> &instances = m_ocean->Eco().Instances(model);
		if (instances.empty())
			continue;
		const DrawRange range = RangeOf(model);
		const std::span<const sdl3::MeshInstance> shown = range.distance > 0.f ? visible(instances, range.distance, range.radius) : std::span(instances);
		if (model == Model::Fish) {
			std::vector<sdl3::MeshInstance> &near = m_scratch.emplace_back(), &far = m_scratch.emplace_back();
			for (const sdl3::MeshInstance &inst : shown)
				((FVector3{inst.model.m[3][0], inst.model.m[3][1], inst.model.m[3][2]} - m_pos).LengthSq() < 25.f * 25.f ? near : far).push_back(inst);
			const sdl3::Material mat = MaterialFor(model);
			m_r3d->DrawInstances(m_models[m], mat, near);
			m_r3d->DrawInstances(m_fishFar, mat, far);
			continue;
		}
		m_r3d->DrawInstances(m_models[m], MaterialFor(model), shown);
	}

	// ── Surface de l'eau ──
	sdl3::Material surface;
	surface.waterSurface = true;
	surface.doubleSided = true;
	surface.castShadow = false;
	surface.receiveShadow = false;
	surface.caustics = false;
	m_r3d->Draw(m_surface, FMatrix4::Translation({m_pos.x, kSurfaceY, m_pos.z}), surface);

	// ── Additifs : faisceaux, bulles, particules, reflets ──
	sdl3::Material shaft = sdl3::Material::Pbr({0.f, 0.f, 0.f}, 0.f, 1.f);
	shaft.emissive = sdl3::Srgb(0xd8fbff) * 0.5f;
	shaft.edgeFade = true;
	shaft.texture = m_tex.shaft;
	shaft.opacity = 0.99f;
	shaft.additive = true;
	shaft.doubleSided = true;
	shaft.castShadow = false;
	shaft.receiveShadow = false;
	shaft.caustics = false;
	m_r3d->DrawInstances(m_shaft, shaft, m_ambience->shafts);
	sdl3::Material mote = sdl3::Material::Pbr({0.f, 0.f, 0.f}, 0.f, 1.f);
	mote.emissive = {0.7f, 0.85f, 0.9f};
	mote.opacity = 0.99f;
	mote.additive = true;
	mote.castShadow = false;
	mote.receiveShadow = false;
	mote.caustics = false;
	m_r3d->DrawInstances(m_mote, mote, m_ambience->motes);
	sdl3::Material glint = mote;
	glint.emissive = {2.6f, 2.4f, 1.9f};
	m_r3d->DrawInstances(m_mote, glint, m_ambience->glints);

	if ((m_pos - m_lastCamera).LengthSq() > 25.f)
		m_target.ResetHistory();
	m_lastCamera = m_pos;
	DIAG_COUNTER("abyssale.instances_preparees", [&] {
		size_t n = 0;
		for (const auto &v : m_scratch)
			n += v.size();
		return n;
	}());
	DIAG_ZONE("Abyssale::Render3D");
	auto cmd = Gpu()->AcquireCommandBuffer();
	m_r3d->Render(cmd, m_target, camera, lighting);
	{
		DIAG_ZONE("Abyssale::Submit");
		(void)cmd.Submit();
	}
	DIAG_COUNTER("abyssale.appels_dessin", m_r3d->Stats().drawCalls);
	DIAG_COUNTER("abyssale.triangles", m_r3d->Stats().triangles);
}

void AbyssaleApp::OnRender(const sdl3::FRect &area, double) {
	DIAG_ZONE("Abyssale::OnRender");
	m_view = area;
	sdl3::Canvas &c = Canvas();
	if (sdl3::Texture *view = m_target.View())
		c.Image(*view, area);
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	if (u.WantsKeyboard())
		return;
	if (m_menu) {
		if (const bool resume = m_time > 1.f; resume != m_menuResume) {
			m_playButton.SetText(resume ? "Reprendre la plongée" : "Créer la plongée");
			m_menuResume = resume;
		}
		m_volumeText.SetText(std::format("{:.0f} %", m_volumePercent));
		m_menuUi->Present(FrameSeconds(), area);
		ApplyDiagnostics();
		return;
	}
	const std::string help = std::format("ZQSD nager · E/Espace monter · A descendre · Maj palmer vite · Échap menu · {} i/s · {:.0f} m",
										Fps() > 0.f ? int(Fps()) : 0, kSurfaceY - m_pos.y);
	const float tw = c.MeasureText(help, 12.f);
	c.FillRoundRect({area.x + (area.w - tw) * 0.5f - 12.f, area.Bottom() - 34.f, tw + 24.f, 24.f}, 6.f, sdl3::Rgba(4, 16, 26, 130));
	c.Text(area.x + (area.w - tw) * 0.5f, area.Bottom() - 26.f, help, th.textDim, 12.f);
	if (m_options.stats)
		c.Text(area.x + 12.f, area.y + 12.f, std::format("{} i/s · {} appels · {} k triangles · {} chunks", int(Fps()), m_r3d->Stats().drawCalls,
														  m_r3d->Stats().triangles / 1000, m_ocean->LoadedChunkCount()),
			   th.text, 12.f);
}

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Abyssale";
	config.width = 1280;
	config.height = 800;
	config.customFrame = true;
	config.gpu = true;
	config.audio = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::TERRAIN);
	Options options;
	for (const std::string &arg : config.ParseArgs(argc, argv)) {
		const auto value = [&](std::string_view key) -> std::optional<std::string> {
			if (arg.starts_with(key) && arg.size() > key.size() && arg[key.size()] == '=')
				return arg.substr(key.size() + 1);
			return std::nullopt;
		};
		if (const auto q = value("--quality"))
			options.quality = std::clamp(std::atoi(q->c_str()), 0, 2);
		else if (arg == "--mute")
			options.mute = true;
		else if (arg == "--stats")
			options.stats = true;
		else if (arg == "--no-taa")
			options.taa = false;
		else if (arg == "--diagnostics")
			options.diagnostics = true;
	}
	if (options.diagnostics) {
		DIAG_THREAD_NAME("main");
		diagnostics::session::Options diag;
		diag.app = "abyssale";
		const std::string base = diagnostics::session::Start(diag);
		if (!base.empty())
			core::log::Info("abyssale : diagnostics dans {}.(vcap|log|crash.txt)", base);
	}
	const int code = AbyssaleApp(options).Run(config);
	diagnostics::session::Stop();
	return code;
}
