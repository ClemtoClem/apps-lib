// Voxelia Studio — outils de génération du monde (phase 6). Cf. studio_worldgen.hpp.
#include "studio_worldgen.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <sdl3/sdl3.hpp>

#include <core/hash.hpp>
#include <core/string.hpp>

#include "studio_panels.hpp"

namespace voxelia::studio {

namespace icons = ui::icons;
using Rect = sdl3::FRect;
using sdl3::Rgba;

namespace {

constexpr int kMapSize = 256; // pixels de la carte (calculée en arrière-plan)

/// Résultat de la carte : image RGBA, altitudes et biomes par pixel, structures.
struct MapImage {
	int size = kMapSize;
	float centerX = 0, centerZ = 0, scale = 4;
	std::vector<uint8_t> rgba;
	std::vector<int> heights;
	std::vector<uint8_t> biomes;
	std::vector<voxel::StructurePlacement> structures;
	double ms = 0;
};

sdl3::Color BiomeColor(voxel::TerrainBiome b) {
	switch (b) {
	case voxel::TerrainBiome::Plains: return Rgba(118, 176, 72);
	case voxel::TerrainBiome::Forest: return Rgba(58, 128, 52);
	case voxel::TerrainBiome::Desert: return Rgba(222, 200, 130);
	case voxel::TerrainBiome::Snow: return Rgba(236, 242, 248);
	case voxel::TerrainBiome::Taiga: return Rgba(86, 128, 100);
	case voxel::TerrainBiome::Beach: return Rgba(232, 214, 160);
	case voxel::TerrainBiome::Ocean: return Rgba(44, 92, 170);
	case voxel::TerrainBiome::Mountains: return Rgba(132, 132, 138);
	default: return Rgba(200, 0, 200);
	}
}

/// Rendu de la carte (thread de chargement + pool global pour les lignes).
MapImage RenderMap(std::shared_ptr<const voxel::TerrainGenerator> gen, float cx, float cz, float scale, std::stop_token stop) {
	MapImage m;
	m.centerX = cx;
	m.centerZ = cz;
	m.scale = scale;
	const int n = m.size;
	m.rgba.assign(size_t(n * n * 4), 255);
	m.heights.assign(size_t(n * n), 0);
	m.biomes.assign(size_t(n * n), 0);
	const uint64_t t0 = sdl3::GetTicksMS();
	const float x0 = cx - float(n) * 0.5f * scale, z0 = cz - float(n) * 0.5f * scale;
	jobs::ParallelFor(0, size_t(n), 8, [&](size_t row) {
		if (stop.stop_requested())
			return;
		for (int col = 0; col < n; ++col) {
			const voxel::ColumnSample s = gen->Sample(int(std::floor(x0 + float(col) * scale)), int(std::floor(z0 + float(row) * scale)));
			m.heights[row * size_t(n) + size_t(col)] = s.height;
			m.biomes[row * size_t(n) + size_t(col)] = uint8_t(s.biome);
		}
	});
	// Couleurs : biome, profondeur de l'eau, neige des sommets, ombrage du relief.
	for (int row = 0; row < n; ++row)
		for (int col = 0; col < n; ++col) {
			const size_t i = size_t(row * n + col);
			const int h = m.heights[i];
			const auto biome = voxel::TerrainBiome(m.biomes[i]);
			sdl3::Color c = BiomeColor(biome);
			if (h < voxel::kSeaLevel) {
				const float depth = std::clamp(float(voxel::kSeaLevel - h) / 60.f, 0.f, 1.f);
				c = sdl3::MixColor(Rgba(70, 140, 210), Rgba(16, 36, 90), depth);
			} else if (h > 150) {
				c = sdl3::MixColor(c, Rgba(245, 248, 252), std::clamp(float(h - 150) / 40.f, 0.f, 1.f));
			}
			if (h >= voxel::kSeaLevel) { // ombrage (lumière du nord-ouest)
				const int hl = m.heights[size_t(row * n + std::max(0, col - 1))], hu = m.heights[size_t(std::max(0, row - 1) * n + col)];
				const float slope = (float(h - hl) + float(h - hu)) / scale;
				const float shade = std::clamp(1.f + slope * 0.06f, 0.6f, 1.35f);
				c = {uint8_t(std::min(255.f, float(c.r) * shade)), uint8_t(std::min(255.f, float(c.g) * shade)), uint8_t(std::min(255.f, float(c.b) * shade)),
					 255};
			}
			m.rgba[i * 4] = c.r;
			m.rgba[i * 4 + 1] = c.g;
			m.rgba[i * 4 + 2] = c.b;
		}
	const float half = float(n) * 0.5f * scale;
	gen->StructuresIn(int(cx - half), int(cz - half), int(cx + half), int(cz + half), m.structures);
	m.ms = double(sdl3::GetTicksMS() - t0);
	return m;
}

voxel::TerrainSettings Preset(int which) {
	voxel::TerrainSettings t;
	switch (which) {
	case 1: t.type = voxel::GeneratorType::Flat; break;
	case 2:
		t.type = voxel::GeneratorType::Amplified;
		t.shape.mountainAmplitude = 220.f;
		t.shape.hillsAmplitude = 18.f;
		break;
	case 3:
		t.type = voxel::GeneratorType::Archipelago;
		t.shape.continentScale = 600.f;
		break;
	case 4: // collines douces : peu de montagnes, beaucoup de forêts
		t.shape.mountainAmplitude = 30.f;
		t.shape.hillsAmplitude = 18.f;
		t.shape.hillsScale = 220.f;
		t.shape.humidityBias = 0.25f;
		t.shape.treeDensity = 1.6f;
		break;
	default: break;
	}
	return t;
}

} // namespace

struct WorldgenWorkshop::Impl {
	App &app;
	voxel::TerrainSettings settings;
	uint64_t seed = 587651579;
	std::string seedText = "587651579";
	std::string worldName = "Monde généré";
	uint64_t version = 1, mapVersion = 0;
	uint64_t lastChange = 0;
	jobs::AsyncValue<MapImage> map;
	std::optional<sdl3::Texture> texture;
	bool textureStale = false;
	float viewX = 0, viewZ = 0, scale = 4.f;
	bool dragging = false;
	float lastX = 0, lastY = 0;
	// Banc d'essai.
	int testKind = 0, structure = 0, environment = 0;
	std::string blockFilter, command = "/elevator build";
	int block = 0;
	bool pending = false;
	int pendingKind = 0;
	std::string pendingId;
	uint64_t launchTicks = 0;
	size_t consoleMark = 0;
	math::FVector3 testOrigin{};
	std::vector<ecs::Entity> before; // entités présentes avant l'action (pour le bilan)
	std::optional<math::FVector3> focus;
	std::string status;
	struct Report {
		bool valid = false;
		size_t placed = 0, entities = 0, joints = 0, active = 0, overlapping = 0, errors = 0;
		std::string what;
	} report;

	explicit Impl(App &a) : app(a) {}

	void Changed() {
		++version;
		lastChange = sdl3::GetTicksMS();
	}
};

WorldgenWorkshop::WorldgenWorkshop(App &app) : m_impl(std::make_unique<Impl>(app)) {}
WorldgenWorkshop::~WorldgenWorkshop() = default;

std::optional<math::FVector3> WorldgenWorkshop::TakeFocus() { return std::exchange(m_impl->focus, std::nullopt); }
std::string WorldgenWorkshop::TakeStatus() { return std::exchange(m_impl->status, {}); }

// ══════════════════════════════════════════════════════════════════════════
// Générateur
// ══════════════════════════════════════════════════════════════════════════

void WorldgenWorkshop::DrawSettings(ui::Context &u, const Rect &r) {
	Impl &s = *m_impl;
	const ui::Theme &th = u.Style();
	voxel::TerrainSettings &t = s.settings;
	voxel::TerrainShape &sh = t.shape;
	u.PushId("worldgen");
	u.BeginScroll("worldgen", r.Inset(10, 8));
	u.Section(icons::Codepoint(icons::MaterialIcons::TERRAIN), "Préréglages");
	static constexpr const char *kPresets[] = {"Normal", "Plat", "Amplifié", "Archipel", "Collines"};
	u.Row({1, 1, 1}, 26);
	for (int p = 0; p < 5; ++p) {
		if (p == 3)
			u.Row({1, 1, 1}, 26);
		if (u.Button(kPresets[p])) {
			auto rules = t.structures;
			t = Preset(p);
			t.structures = std::move(rules);
			s.Changed();
		}
	}
	if (u.Button("Partie", s.app.game != nullptr)) { // reprendre les réglages de la partie
		t = s.app.game->World().Config().terrain;
		s.seed = s.app.game->World().Config().seed;
		s.seedText = std::to_string(s.seed);
		s.Changed();
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::PUBLIC), "Graine et type");
	u.Row({3, 1}, 28);
	if (u.TextInput("##seed", s.seedText, nullptr, "graine")) {
		const uint64_t v = std::strtoull(s.seedText.c_str(), nullptr, 10);
		if (v != s.seed) {
			s.seed = v;
			s.Changed();
		}
	}
	if (u.Button("Aléatoire")) {
		s.seed = core::Mix64(sdl3::GetTicksMS() + s.seed) % 1'000'000'000;
		s.seedText = std::to_string(s.seed);
		s.Changed();
	}
	static constexpr std::string_view kTypes[] = {"Normal", "Plat", "Amplifié", "Archipel"};
	int type = int(t.type);
	if (u.Choice(u.Next(26), "##type", type, kTypes)) {
		t.type = voxel::GeneratorType(type);
		s.Changed();
	}
	auto slider = [&](std::string_view label, float &v, float lo, float hi, std::string_view fmt = "{:.0f}") {
		if (u.SliderFloat(label, v, lo, hi, fmt))
			s.Changed();
	};
	slider("Relief ×", t.relief, 0.f, 3.f, "{:.2f}");
	u.Section(icons::Codepoint(icons::MaterialIcons::TERRAIN), "Relief (bruits)");
	slider("Continents", sh.continentScale, 150.f, 4000.f);
	slider("  amplitude", sh.continentAmplitude, 0.f, 120.f);
	slider("Collines", sh.hillsScale, 30.f, 800.f);
	slider("  amplitude##c", sh.hillsAmplitude, 0.f, 80.f);
	slider("Montagnes", sh.mountainScale, 60.f, 1500.f);
	slider("  amplitude##m", sh.mountainAmplitude, 0.f, 400.f);
	slider("Massifs", sh.mountainMaskScale, 200.f, 5000.f);
	slider("Détail", sh.detailAmplitude, 0.f, 8.f, "{:.1f}");
	float oct = float(sh.octaves);
	if (u.SliderFloat("Octaves", oct, 1.f, 8.f, "{:.0f}") && int(std::round(oct)) != sh.octaves) {
		sh.octaves = int(std::round(oct));
		s.Changed();
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::WB_SUNNY), "Climat et biomes");
	slider("Climat (échelle)", sh.climateScale, 200.f, 5000.f);
	slider("Température", sh.temperatureBias, -1.f, 1.f, "{:.2f}");
	slider("Humidité", sh.humidityBias, -1.f, 1.f, "{:.2f}");
	slider("Arbres ×", sh.treeDensity, 0.f, 4.f, "{:.2f}");
	if (u.Checkbox("Grottes", sh.caves) || u.Checkbox("Fosses abyssales", sh.trenches) || u.Checkbox("Arbres", t.trees) ||
		u.Checkbox("Créatures", t.creatures))
		s.Changed();

	// ── Structures placées par le générateur ──
	u.Section(icons::Codepoint(icons::MaterialIcons::CATEGORY), std::format("Structures ({})", t.structures.size()));
	const auto &structs = s.app.content.mods.Structures();
	std::vector<std::string_view> names;
	for (const voxel::Structure &st : structs)
		names.push_back(st.id);
	for (size_t i = 0; i < t.structures.size(); ++i) {
		voxel::StructureRule &rule = t.structures[i];
		u.PushId(uint64_t(i) + 400);
		int pick = 0;
		for (size_t k = 0; k < names.size(); ++k)
			if (names[k] == rule.structure)
				pick = int(k);
		u.Row({4, 1}, 26);
		if (!names.empty() && u.Choice(u.Next(), "##st", pick, names)) {
			rule.structure = std::string(names[size_t(pick)]);
			s.Changed();
		}
		const bool remove = u.Button("Suppr.");
		float chance = rule.chance * 100.f;
		if (u.SliderFloat("Chance (%)", chance, 0.f, 100.f, "{:.0f}")) {
			rule.chance = chance / 100.f;
			s.Changed();
		}
		float spacing = float(rule.spacing);
		if (u.SliderFloat("Espacement", spacing, 16.f, 512.f, "{:.0f}")) {
			rule.spacing = int(spacing);
			s.Changed();
		}
		// Biomes autorisés (cases compactes).
		const Rect line = u.Next(48);
		for (int b = 0; b < int(voxel::TerrainBiome::Count); ++b) {
			const float cw = line.w / 4.f;
			const Rect cell{line.x + float(b % 4) * cw, line.y + float(b / 4) * 24, cw - 4, 22};
			const bool on = (rule.biomes >> b) & 1u;
			if (u.ButtonAt(cell, std::format("{}##b{}", voxel::TerrainBiomeName(voxel::TerrainBiome(b)), b), true, on)) {
				rule.biomes ^= 1u << uint32_t(b);
				s.Changed();
			}
		}
		u.Space(4);
		u.PopId();
		if (remove) {
			t.structures.erase(t.structures.begin() + std::ptrdiff_t(i));
			s.Changed();
			break;
		}
	}
	if (u.Button("Ajouter une règle de structure", !structs.empty())) {
		t.structures.push_back({structs.front().id, 0.25f, 96, 0xFFu & ~(1u << uint32_t(voxel::TerrainBiome::Ocean))});
		s.Changed();
	}

	// ── Créer un monde ──
	u.Section(icons::Codepoint(icons::MaterialIcons::ADD), "Nouveau monde");
	u.TextInput("##name", s.worldName, nullptr, "nom du monde");
	if (u.Button("Créer un monde avec ces réglages")) {
		WorldSettings ws;
		ws.name = s.worldName;
		ws.seed = s.seed;
		ws.generator = int(t.type);
		ws.relief = t.relief;
		ws.creatures = t.creatures;
		ws.trees = t.trees;
		ws.terrain = dataio::WriteJson(voxel::TerrainToJson(t), -1);
		std::string folder = WorldFolderName(ws.name);
		for (int k = 2; fs::exists(s.app.WorldsDir() / folder); ++k)
			folder = std::format("{}_{}", WorldFolderName(ws.name), k);
		s.status = s.app.StartGame(folder, ws) ? std::format("monde « {} » créé ({})", ws.name, folder) : "échec de création du monde";
	}
	u.Label("Les réglages sont figés à la création du monde (enregistrés dans sa base).", th.textDim, 12);
	u.EndScroll();
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Carte
// ══════════════════════════════════════════════════════════════════════════

void WorldgenWorkshop::DrawMap(ui::Context &u, const Rect &r, sdl3::Renderer &ren, jobs::ThreadPool &worker) {
	Impl &s = *m_impl;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	u.PushId("worldmap");
	// Demande de rendu : après 0,3 s sans changement (curseurs), ou au déplacement.
	if (s.mapVersion != s.version && sdl3::GetTicksMS() - s.lastChange > 300) {
		s.mapVersion = s.version;
		voxel::BlockRegistry &reg = s.app.content.blocks;
		const auto *mods = &s.app.content.mods;
		auto gen = std::make_shared<const voxel::TerrainGenerator>(s.seed, voxel::TerrainPalette::Resolve(reg), s.settings, &reg,
																   [mods](std::string_view id) { return mods->FindStructure(id); });
		s.map.Request(worker, [gen, cx = s.viewX, cz = s.viewZ, sc = s.scale](std::stop_token stop) { return RenderMap(gen, cx, cz, sc, stop); });
	}
	if (s.map.Poll())
		s.textureStale = true;
	const MapImage *m = s.map.Get();
	c.FillRect(r, Rgba(12, 14, 18));
	const float side = std::min(r.w, r.h - 120);
	const Rect pic{r.x + (r.w - side) * 0.5f, r.y + 8, side, side};
	if (!m) {
		u.BeginColumn({r.x, r.y + r.h * 0.3f, r.w, 120});
		u.Loader("Calcul de la carte…");
		u.EndColumn();
		u.PopId();
		return;
	}
	if (s.textureStale || !s.texture) {
		if (!s.texture) {
			if (auto t = sdl3::Texture::Create(ren, m->size, m->size, sdl3::PixelFormat::RGBA32)) {
				s.texture.emplace(std::move(t.Value()));
				s.texture->SetScaleMode(sdl3::ScaleMode::NEAREST);
			}
		}
		if (s.texture)
			s.texture->Update(m->rgba.data(), m->size * 4);
		s.textureStale = false;
	}
	if (s.texture)
		c.Image(*s.texture, pic);
	if (s.map.Loading())
		u.Spinner({pic.Right() - 40, pic.y + 8, 32, 32}, 22);
	const float pxPerBlock = pic.w / (float(m->size) * m->scale);
	auto toScreen = [&](float wx, float wz) {
		return math::FVector2{pic.x + (wx - (m->centerX - float(m->size) * 0.5f * m->scale)) * pxPerBlock,
							  pic.y + (wz - (m->centerZ - float(m->size) * 0.5f * m->scale)) * pxPerBlock};
	};
	c.PushClip(pic);
	// Structures placées.
	for (const voxel::StructurePlacement &p : m->structures) {
		const math::FVector2 sp = toScreen(float(p.origin.x), float(p.origin.z));
		c.FillCircle(sp.x, sp.y, 4.5f, Rgba(255, 90, 60));
		c.StrokeRect({sp.x - 6, sp.y - 6, 12, 12}, Rgba(255, 255, 255, 200));
	}
	// Joueur de la partie ouverte (même graine).
	if (Game *g = s.app.game.get(); g && g->World().Config().seed == s.seed)
		if (const auto *t = g->Ecs().Get<voxel::Transform>(g->Player())) {
			const math::FVector2 sp = toScreen(t->position.x, t->position.z);
			c.FillCircle(sp.x, sp.y, 5, th.info);
		}
	// Origine du monde.
	const math::FVector2 o = toScreen(0, 0);
	c.Line(o.x - 8, o.y, o.x + 8, o.y, Rgba(255, 255, 255, 160));
	c.Line(o.x, o.y - 8, o.x, o.y + 8, Rgba(255, 255, 255, 160));
	c.PopClip();
	c.StrokeRect(pic, th.panelBorder);

	// Interaction : glisser = déplacer, molette = zoom (recalcul de la carte).
	const bool hover = pic.Contains(u.MouseX(), u.MouseY()) && u.MouseIn(pic);
	if (hover && u.MousePressed(sdl3::MouseButton::Left)) {
		s.dragging = true;
		s.lastX = u.MouseX();
		s.lastY = u.MouseY();
	}
	if (s.dragging) {
		if (u.MouseDown(sdl3::MouseButton::Left)) {
			s.viewX -= (u.MouseX() - s.lastX) / pxPerBlock;
			s.viewZ -= (u.MouseY() - s.lastY) / pxPerBlock;
			s.lastX = u.MouseX();
			s.lastY = u.MouseY();
		} else {
			s.dragging = false;
			++s.version; // recalcul au relâchement
		}
	}
	if (hover && u.WheelY() != 0) {
		s.scale = std::clamp(s.scale * std::pow(0.8f, u.WheelY()), 0.25f, 64.f);
		s.Changed();
	}
	// Lecture du point survolé.
	std::string info = std::format("centre {:.0f}, {:.0f} · {:.2g} bloc/pixel · {} km² · calcul {:.0f} ms · {} structure(s)", m->centerX, m->centerZ,
								   m->scale, double(float(m->size) * m->scale) * double(float(m->size) * m->scale) / 1e6, m->ms, m->structures.size());
	if (hover) {
		const int col = std::clamp(int((u.MouseX() - pic.x) / pic.w * float(m->size)), 0, m->size - 1);
		const int row = std::clamp(int((u.MouseY() - pic.y) / pic.h * float(m->size)), 0, m->size - 1);
		const size_t i = size_t(row * m->size + col);
		const float wx = m->centerX + (float(col) - float(m->size) * 0.5f) * m->scale, wz = m->centerZ + (float(row) - float(m->size) * 0.5f) * m->scale;
		info = std::format("x {:.0f}, z {:.0f} · altitude {} · {}", wx, wz, m->heights[i], voxel::TerrainBiomeName(voxel::TerrainBiome(m->biomes[i])));
		u.Tooltip(info);
	}
	c.Text(r.x + 10, pic.Bottom() + 6, c.Ellipsize(info, r.w - 20, 12), th.textDim, 12);
	// Profil d'altitude de la ligne centrale.
	std::vector<float> profile(size_t(m->size));
	for (int col = 0; col < m->size; ++col)
		profile[size_t(col)] = float(m->heights[size_t((m->size / 2) * m->size + col)]);
	u.BeginColumn({r.x + 10, pic.Bottom() + 24, r.w - 20, r.Bottom() - pic.Bottom() - 26});
	u.Plot("profil d'altitude (ligne centrale, ouest → est)", profile, -60.f, 220.f, std::max(40.f, r.Bottom() - pic.Bottom() - 50), th.accent);
	u.EndColumn();
	// Légende des biomes.
	float lx = pic.Right() + 10;
	if (lx + 110 < r.Right())
		for (int b = 0; b < int(voxel::TerrainBiome::Count); ++b) {
			const float ly = pic.y + float(b) * 22;
			c.FillRect({lx, ly + 4, 14, 14}, BiomeColor(voxel::TerrainBiome(b)));
			c.Text(lx + 20, ly + 3, voxel::TerrainBiomeName(voxel::TerrainBiome(b)), th.textDim, 12);
		}
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Banc d'essai
// ══════════════════════════════════════════════════════════════════════════

void WorldgenWorkshop::DrawSandbox(ui::Context &u, const Rect &r) {
	Impl &s = *m_impl;
	const ui::Theme &th = u.Style();
	u.PushId("sandbox");
	u.BeginScroll("sandbox", r.Inset(10, 8));
	u.Section(icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), "Banc d'essai");
	u.Wrapped("Un monde neutre, recréé à chaque lancement, pour vérifier un bloc, une structure ou un mécanisme isolé.", th.textDim, 12);
	static constexpr std::string_view kKinds[] = {"Structure", "Bloc", "Mécanisme : ascenseur", "Mécanisme : porte", "Commande"};
	u.Choice(u.Next(26), "##kind", s.testKind, kKinds);
	std::string id;
	if (s.testKind == 0) {
		const auto &structs = s.app.content.mods.Structures();
		std::vector<std::string_view> names;
		for (const voxel::Structure &st : structs)
			names.push_back(st.id);
		if (!names.empty()) {
			s.structure = std::clamp(s.structure, 0, int(names.size()) - 1);
			u.Choice(u.Next(26), "##structure", s.structure, names);
			id = std::string(names[size_t(s.structure)]);
		}
	} else if (s.testKind == 1) {
		u.TextInput("##blockfilter", s.blockFilter, nullptr, "filtrer les blocs…");
		std::vector<std::string_view> names;
		const std::string f = String(s.blockFilter).ToLower();
		for (const voxel::BlockDef &d : s.app.content.blocks.All())
			if (d.material != voxel::Material::Air && (f.empty() || String(d.name).ToLower().find(f) != std::string::npos) && names.size() < 400)
				names.push_back(d.name);
		if (!names.empty()) {
			s.block = std::clamp(s.block, 0, int(names.size()) - 1);
			u.Choice(u.Next(26), "##block", s.block, names);
			id = std::string(names[size_t(s.block)]);
		}
	} else if (s.testKind == 4) {
		u.TextInput("##command", s.command, nullptr, "/elevator build 4");
		id = s.command;
	}
	static constexpr std::string_view kEnv[] = {"Plat et neutre", "Réglages du générateur"};
	u.Choice(u.Next(26), "##env", s.environment, kEnv);
	bool launch = u.Button(s.pending ? "Chargement du monde d'essai…##launch" : "Lancer la simulation##launch", !s.pending);
	if (launch) {
		WorldSettings ws;
		ws.name = "Banc d'essai";
		ws.seed = s.environment == 0 ? 1 : s.seed;
		ws.gameMode = 1;
		ws.cheats = true;
		ws.dayCycle = false;
		ws.startTime = 0.45f;
		ws.creatures = false;
		ws.trees = s.environment == 1 && s.settings.trees;
		voxel::TerrainSettings t = s.environment == 0 ? voxel::TerrainSettings{} : s.settings;
		if (s.environment == 0) {
			t.type = voxel::GeneratorType::Flat;
			t.structures.clear();
		}
		t.creatures = false;
		ws.generator = int(t.type);
		ws.terrain = dataio::WriteJson(voxel::TerrainToJson(t), -1);
		if (s.app.StartGame("banc_d_essai", ws)) { // monde recréé (dossier effacé)
			s.pending = true;
			s.pendingKind = s.testKind;
			s.pendingId = id;
			s.launchTicks = sdl3::GetTicksMS();
			s.report = {};
			s.status = "banc d'essai : monde neutre en cours de chargement";
		} else {
			s.status = "banc d'essai : échec d'ouverture du monde";
		}
	}
	// Bilan de la dernière simulation.
	if (s.report.valid) {
		u.Section(icons::Codepoint(icons::MaterialIcons::CHECK), "Vérifications");
		u.Label(s.report.what, th.text, 13);
		auto line = [&](bool ok, std::string text) {
			const Rect row = u.Next(22);
			u.Icon({row.x, row.y, 20, row.h}, ok ? icons::Codepoint(icons::MaterialIcons::CHECK) : icons::Codepoint(icons::MaterialIcons::CLOSE), ok ? th.success : th.danger, 16);
			u.Canvas().Text(row.x + 24, row.y + 3, text, ok ? th.text : th.danger, 13);
		};
		if (s.report.placed)
			line(true, std::format("{} bloc(s) posé(s)", s.report.placed));
		line(true, std::format("{} entité(s) créée(s)", s.report.entities));
		line(s.report.active == s.report.joints, std::format("joints actifs : {} / {}", s.report.active, s.report.joints));
		line(s.report.overlapping == 0, std::format("entités dans un bloc : {}", s.report.overlapping));
		line(s.report.errors == 0, std::format("erreurs dans la console : {}", s.report.errors));
		if (Game *g = s.app.game.get())
			u.Label(std::format("simulation : tick {}{}", g->Tick(), g->Paused() ? " (en pause)" : ""), th.textDim, 12);
		if (u.Button("Relancer la simulation##again"))
			s.report.valid = false, s.pending = false;
	}
	u.EndScroll();
	u.PopId();
}

void WorldgenWorkshop::Update(Game *game) {
	Impl &s = *m_impl;
	if (!s.pending || !game || !game->WorldLoaded())
		return;
	s.pending = false;
	ecs::World &w = game->Ecs();
	const auto *pt = w.Get<voxel::Transform>(game->Player());
	if (!pt)
		return;
	// Devant le joueur, au sol.
	const math::FVector3 fwd{std::sin(pt->yaw), 0, -std::cos(pt->yaw)};
	const math::FVector3 front = pt->position + fwd * 7.f;
	const int fx = int(std::floor(front.x)), fz = int(std::floor(front.z));
	const int ground = game->World().SurfaceHeight(fx, fz);
	s.testOrigin = {float(fx) + 0.5f, float(ground + 1), float(fz) + 0.5f};
	const size_t entitiesBefore = w.EntityCount();
	const size_t consoleBefore = game->ConsoleLines().size();
	size_t placed = 0;
	std::string what;
	switch (s.pendingKind) {
	case 0:
		if (const voxel::Structure *st = s.app.content.mods.FindStructure(s.pendingId)) {
			placed = game->World().PlaceStructure(*st, fx, ground + 1, fz);
			what = "structure " + s.pendingId;
		}
		break;
	case 1:
		if (const voxel::BlockId id = s.app.content.blocks.Find(s.pendingId); id != voxel::kAir) {
			placed = game->World().SetBlock(fx, ground + 1, fz, id) ? 1 : 0;
			what = "bloc " + s.pendingId;
		}
		break;
	case 2:
		BuildDemoElevator(*game, front, {});
		what = "ascenseur (cabine, contrepoids, porte)";
		break;
	case 3:
		BuildDemoDoor(*game, front);
		what = "porte sur charnière";
		break;
	default:
		what = "commande " + s.pendingId;
		game->ExecuteConsole(s.pendingId);
		break;
	}
	// Bilan : entités créées, joints, chevauchements avec les blocs, erreurs de script.
	game->Update(PlayerInput{}, 0.05f); // un pas : les joints s'initialisent
	Impl::Report rep;
	rep.valid = true;
	rep.what = what.empty() ? "rien à poser (identifiant introuvable)" : what;
	rep.placed = placed;
	rep.entities = w.EntityCount() > entitiesBefore ? w.EntityCount() - entitiesBefore : 0;
	rep.joints = game->Mechanics().joints;
	rep.active = game->Mechanics().active;
	w.MakeQuery<const voxel::Transform, const voxel::Body>().Each([&](ecs::Entity e, const voxel::Transform &t, const voxel::Body &b) {
		if (e != game->Player() && b.collide && (t.position - s.testOrigin).Length() < 24.f &&
			game->World().BoxIntersectsSolid({t.position - b.halfExtents * 0.9f, t.position + b.halfExtents * 0.9f}))
			++rep.overlapping;
	});
	const auto &lines = game->ConsoleLines();
	for (size_t i = consoleBefore; i < lines.size(); ++i) {
		const std::string low = String(lines[i]).ToLower();
		if (low.find("erreur") != std::string::npos || low.find("échec") != std::string::npos || low.find(".script:") != std::string::npos)
			++rep.errors;
	}
	s.report = rep;
	s.focus = s.testOrigin + math::FVector3{0, 2, 0};
	s.status = std::format("banc d'essai : {} — {} bloc(s), {} entité(s), joints {}/{}", rep.what, rep.placed, rep.entities, rep.active, rep.joints);
}

} // namespace voxelia::studio
