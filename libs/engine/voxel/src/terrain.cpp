#include "voxel/terrain.hpp"

#include "voxel/mods.hpp"

#include <algorithm>
#include <cmath>

#include <diagnostics/trace.hpp>

namespace voxel {

TerrainPalette TerrainPalette::Resolve(BlockRegistry &r) {
	auto pick = [&](std::initializer_list<std::string_view> names) {
		for (auto n : names)
			if (BlockId id = r.Find(n); id != kAir)
				return id;
		return r.FindOrCreate(*names.begin());
	};
	TerrainPalette p{};
	p.stone = pick({"core:stone"});
	p.dirt = pick({"core:dirt"});
	p.grass = pick({"core:grass", "core:grass_block"});
	p.sand = pick({"core:sand"});
	p.water = pick({"core:water"});
	p.snow = pick({"core:snow"});
	p.snowyGrass = pick({"core:snowy_grass", "core:snow"});
	p.gravel = pick({"core:gravel"});
	p.bedrock = pick({"core:bedrock"});
	p.coal = pick({"core:coal_ore"});
	p.iron = pick({"core:iron_ore"});
	p.log = pick({"core:oak_log"});
	p.leaves = pick({"core:oak_leaves"});
	p.spruceLog = pick({"core:spruce_log", "core:oak_log"});
	p.spruceLeaves = pick({"core:spruce_leaves", "core:oak_leaves"});
	p.cactus = pick({"core:cactus", "core:oak_leaves"});
	p.ice = pick({"core:ice"});
	p.flower1 = pick({"core:poppy", "core:dandelion"});
	p.flower2 = pick({"core:dandelion", "core:poppy"});
	p.tallGrass = pick({"core:tall_grass", "core:short_grass", "core:fern", "core:dandelion"});
	// Monde miroir (surface inversée).
	p.mirrorGrass = pick({"core:inverted_grass", "core:grass"});
	p.mirrorStone = pick({"core:inverted_stone", "core:stone"});
	p.mirrorSoil = pick({"core:inverted_soil", "core:dirt"});
	// Sédimentaires (croûte).
	p.limestone = pick({"core:limestone"});
	p.sandstone = pick({"core:sandstone"});
	p.shale = pick({"core:shale"});
	p.chalk = pick({"core:chalk"});
	p.chert = pick({"core:chert"});
	p.conglomerate = pick({"core:conglomerate"});
	p.dolomite = pick({"core:dolomite"});
	p.siltstone = pick({"core:siltstone"});
	p.gypsum = pick({"core:gypsum"});
	// Métamorphiques.
	p.gneiss = pick({"core:gneiss"});
	p.marble = pick({"core:marble"});
	p.slate = pick({"core:slate"});
	p.schist = pick({"core:schist"});
	p.phyllite = pick({"core:phyllite"});
	p.hornfels = pick({"core:hornfels"});
	p.quartzite = pick({"core:quartzite"});
	p.novaculite = pick({"core:novaculite"});
	p.amphibolite = pick({"core:amphibolite"});
	// Magmatiques.
	p.granite = pick({"core:granite"});
	p.diorite = pick({"core:diorite"});
	p.andesite = pick({"core:andesite"});
	p.basalt = pick({"core:basalt"});
	p.gabbro = pick({"core:gabbro"});
	p.peridotite = pick({"core:peridotite"});
	p.diabase = pick({"core:diabase"});
	p.dolerite = pick({"core:dolerite"});
	p.rhyolite = pick({"core:rhyolite"});
	p.scoria = pick({"core:scoria"});
	p.tuff = pick({"core:tuff"});
	p.pumice = pick({"core:pumice"});
	p.pegmatite = pick({"core:pegmatite"});
	p.basalticGlass = pick({"core:basaltic_glass"});
	// Noyau.
	p.magma = pick({"core:magma", "core:lava"});
	p.lava = pick({"core:lava", "core:magma"});
	p.obsidian = pick({"core:obsidian"});
	// Minerais et cristaux (surface → profondeur).
	p.copper = pick({"core:copper_ore"});
	p.tin = pick({"core:tin_ore"});
	p.gold = pick({"core:gold_ore"});
	p.diamond = pick({"core:diamond_ore"});
	p.redstone = pick({"core:redstone_ore"});
	p.salt = pick({"core:salt", "core:sand"});
	p.amethyst = pick({"core:amethyst"});
	p.quartz = pick({"core:quartz"});
	// Métaux additionnels et minéraux industriels.
	p.lead = pick({"core:lead_ore"});
	p.silver = pick({"core:silver_ore"});
	p.nickel = pick({"core:nickel_ore"});
	p.zinc = pick({"core:zinc_ore"});
	p.platinum = pick({"core:platinum_ore"});
	p.mercury = pick({"core:mercury_ore"});
	p.aluminum = pick({"core:aluminum_ore"});
	p.titanium = pick({"core:titanium_ore"});
	p.tungsten = pick({"core:tungsten_ore"});
	p.chromium = pick({"core:chromium_ore"});
	p.uranium = pick({"core:uranium_ore"});
	p.manganese = pick({"core:manganese_ore"});
	p.magnesium = pick({"core:magnesium_ore"});
	p.zirconium = pick({"core:zirconium_ore"});
	p.tantalum = pick({"core:tantalum_ore"});
	p.osmium = pick({"core:osmium_ore"});
	p.cadmium = pick({"core:cadmium_ore"});
	p.iridium = pick({"core:iridium_ore"});
	p.rutile = pick({"core:rutile_ore"});
	p.sulfur = pick({"core:sulfur_ore"});
	p.nitrate = pick({"core:nitrate_ore"});
	p.phosphorous = pick({"core:phosphorous_ore"});
	return p;
}

TerrainGenerator::TerrainGenerator(uint64_t seed, const TerrainPalette &palette, TerrainSettings settings, const BlockRegistry *blocks,
								   const StructureLookup &structures)
	: m_seed(seed), m_settings(std::move(settings)), m_p(palette), m_continent(seed ^ 0x1111), m_hills(seed ^ 0x2222), m_mountain(seed ^ 0x3333),
	  m_temperature(seed ^ 0x4444), m_humidity(seed ^ 0x5555), m_cave(seed ^ 0x6666), m_detail(seed ^ 0x7777), m_vein(seed ^ 0x8888),
	  m_fault(seed ^ 0x9999), m_geode(seed ^ 0xaaaa), m_ore(seed ^ 0xbbbb), m_strata(seed ^ 0xcccc) {
	// Structures des règles : blocs résolus une fois (le générateur reste sans état partagé).
	m_structures.resize(m_settings.structures.size());
	for (size_t i = 0; i < m_settings.structures.size(); ++i) {
		const Structure *st = structures && blocks ? structures(m_settings.structures[i].structure) : nullptr;
		if (!st)
			continue;
		ResolvedStructure &rs = m_structures[i];
		rs.replaceSolid = st->replaceSolid;
		const int layerCount = int(st->layers.size());
		bool first = true;
		for (int ly = 0; ly < layerCount; ++ly) {
			const int cy = layerCount - 1 - ly;
			const auto &rows = st->layers[size_t(ly)];
			for (int rz = 0; rz < int(rows.size()); ++rz)
				for (int cx = 0; cx < int(rows[size_t(rz)].size()); ++cx) {
					const char ch = rows[size_t(rz)][size_t(cx)];
					const auto it = st->palette.find(ch);
					if (ch == ' ' || it == st->palette.end())
						continue;
					const BlockId id = blocks->Find(it->second);
					if (id == kAir)
						continue;
					const IVec3 rel{cx - st->anchor[0], cy - st->anchor[1], rz - st->anchor[2]};
					rs.blocks.push_back({rel, id});
					rs.min = first ? rel : IVec3{std::min(rs.min.x, rel.x), std::min(rs.min.y, rel.y), std::min(rs.min.z, rel.z)};
					rs.max = first ? rel : IVec3{std::max(rs.max.x, rel.x), std::max(rs.max.y, rel.y), std::max(rs.max.z, rel.z)};
					first = false;
				}
		}
	}
}

const char *TerrainBiomeName(TerrainBiome b) noexcept {
	static constexpr const char *kNames[] = {"Plaines", "Forêt", "Désert", "Neige", "Taïga", "Plage", "Océan", "Montagnes"};
	return b < TerrainBiome::Count ? kNames[size_t(b)] : "?";
}

dataio::Json TerrainToJson(const TerrainSettings &t) {
	dataio::Json o = dataio::Json::MakeObject();
	o.Set("type", dataio::Json(int(t.type)));
	o.Set("relief", dataio::Json(double(t.relief)));
	o.Set("creatures", dataio::Json(t.creatures));
	o.Set("trees", dataio::Json(t.trees));
	const TerrainShape &sh = t.shape;
	dataio::Json shape = dataio::Json::MakeObject();
	for (const auto &[key, value] : std::initializer_list<std::pair<const char *, float>>{
			 {"continentScale", sh.continentScale}, {"continentAmplitude", sh.continentAmplitude}, {"hillsScale", sh.hillsScale},
			 {"hillsAmplitude", sh.hillsAmplitude}, {"mountainScale", sh.mountainScale}, {"mountainAmplitude", sh.mountainAmplitude},
			 {"mountainMaskScale", sh.mountainMaskScale}, {"detailAmplitude", sh.detailAmplitude}, {"climateScale", sh.climateScale},
			 {"temperatureBias", sh.temperatureBias}, {"humidityBias", sh.humidityBias}, {"treeDensity", sh.treeDensity}})
		shape.Set(key, dataio::Json(double(value)));
	shape.Set("octaves", dataio::Json(sh.octaves));
	shape.Set("caves", dataio::Json(sh.caves));
	shape.Set("trenches", dataio::Json(sh.trenches));
	o.Set("shape", std::move(shape));
	dataio::Json rules = dataio::Json::MakeArray();
	for (const StructureRule &r : t.structures) {
		dataio::Json rj = dataio::Json::MakeObject();
		rj.Set("structure", dataio::Json(r.structure));
		rj.Set("chance", dataio::Json(double(r.chance)));
		rj.Set("spacing", dataio::Json(r.spacing));
		rj.Set("biomes", dataio::Json(int64_t(r.biomes)));
		rules.Push(std::move(rj));
	}
	o.Set("structures", std::move(rules));
	return o;
}

TerrainSettings TerrainFromJson(const dataio::Json &j) {
	TerrainSettings t;
	t.type = static_cast<GeneratorType>(std::clamp<int64_t>(j["type"].AsInt(0), 0, 3));
	t.relief = j["relief"].AsFloat(1.f);
	t.creatures = j["creatures"].AsBool(true);
	t.trees = j["trees"].AsBool(true);
	const dataio::Json &sh = j["shape"];
	TerrainShape &s = t.shape;
	auto f = [&](const char *key, float &v) { v = sh[key].AsFloat(v); };
	f("continentScale", s.continentScale);
	f("continentAmplitude", s.continentAmplitude);
	f("hillsScale", s.hillsScale);
	f("hillsAmplitude", s.hillsAmplitude);
	f("mountainScale", s.mountainScale);
	f("mountainAmplitude", s.mountainAmplitude);
	f("mountainMaskScale", s.mountainMaskScale);
	f("detailAmplitude", s.detailAmplitude);
	f("climateScale", s.climateScale);
	f("temperatureBias", s.temperatureBias);
	f("humidityBias", s.humidityBias);
	f("treeDensity", s.treeDensity);
	s.octaves = int(std::clamp<int64_t>(sh["octaves"].AsInt(s.octaves), 1, 8));
	s.caves = sh["caves"].AsBool(s.caves);
	s.trenches = sh["trenches"].AsBool(s.trenches);
	for (const dataio::Json &rj : j["structures"].AsArray()) {
		StructureRule r;
		r.structure = std::string(rj["structure"].AsString());
		r.chance = std::clamp(rj["chance"].AsFloat(r.chance), 0.f, 1.f);
		r.spacing = int(std::clamp<int64_t>(rj["spacing"].AsInt(r.spacing), 8, 4096));
		r.biomes = uint32_t(rj["biomes"].AsInt(0xFF));
		if (!r.structure.empty())
			t.structures.push_back(std::move(r));
	}
	return t;
}

namespace {
/// Bruit cellulaire 3D (Worley) : distance normalisée [0, ~1.7] au point de cellule
/// le plus proche (0 = centre). Idéal pour les grottes, filons et géodes.
float Worley3(uint64_t seed, float x, float y, float z, float scale) {
	const int cx = static_cast<int>(std::floor(x / scale));
	const int cy = static_cast<int>(std::floor(y / scale));
	const int cz = static_cast<int>(std::floor(z / scale));
	float best = 1e30f;
	for (int dz = -1; dz <= 1; ++dz)
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx) {
				const int ix = cx + dx, iy = cy + dy, iz = cz + dz;
				const float px = (static_cast<float>(ix) + math::Hash01(ix, iy, iz, seed)) * scale;
				const float py = (static_cast<float>(iy) + math::Hash01(ix, iy, iz, seed ^ 0x9e37)) * scale;
				const float pz = (static_cast<float>(iz) + math::Hash01(ix, iy, iz, seed ^ 0x5a5a)) * scale;
				const float d = (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz);
				if (d < best)
					best = d;
			}
	return std::sqrt(best) / scale;
}
} // namespace

TerrainGenerator::Column TerrainGenerator::SampleColumn(int x, int z) const {
	const float fx = static_cast<float>(x), fz = static_cast<float>(z);
	if (m_settings.type == GeneratorType::Flat)
		return {0, Biome::Plains};
	const TerrainShape &sh = m_settings.shape;
	float continent = m_continent.Fbm2(fx / sh.continentScale, fz / sh.continentScale, sh.octaves);
	if (m_settings.type == GeneratorType::Archipelago)
		continent = continent * 0.8f - 0.42f + 0.35f * m_continent.Noise2(fx / 140.f, fz / 140.f); // océan semé d'îles
	const float hills = m_hills.Fbm2(fx / sh.hillsScale, fz / sh.hillsScale, sh.octaves);
	const float mountainMask = math::SmoothStep(0.05f, 0.45f, m_mountain.Fbm2(fx / sh.mountainMaskScale, fz / sh.mountainMaskScale, 2));
	const float ridges = m_mountain.Ridged2(fx / sh.mountainScale, fz / sh.mountainScale, 5);
	const float detail = m_detail.Noise2(fx / 24.f, fz / 24.f);
	const float relief = m_settings.relief * (m_settings.type == GeneratorType::Amplified ? 2.3f : 1.f);
	// Hauteur relative au niveau de la mer (Y = 0) : océans (−) et continents (+).
	float h = continent * sh.continentAmplitude +
			  relief * (hills * sh.hillsAmplitude * (0.5f + 0.5f * (continent + 1.f)) + ridges * ridges * sh.mountainAmplitude * mountainMask) +
			  detail * sh.detailAmplitude;
	// Fosses abyssales : failles tectoniques océaniques jusqu'à −11 000 m.
	const float trench = math::SmoothStep(0.84f, 0.985f, m_mountain.Ridged2(fx / 1800.f, fz / 1800.f, 3));
	if (h < -6.f && sh.trenches)
		h -= trench * 11'000.f;
	const int height = std::clamp(static_cast<int>(h), kWorldMinY + 8, kWorldMaxY - 40);

	const float temp = m_temperature.Fbm2(fx / sh.climateScale, fz / sh.climateScale, 3) - static_cast<float>(std::max(0, height - 40)) / 120.f +
					   sh.temperatureBias;
	const float hum = m_humidity.Fbm2(fx / (sh.climateScale * 0.8333f), fz / (sh.climateScale * 0.8333f), 3) + sh.humidityBias;
	Biome b;
	if (height < kSeaLevel - 3)
		b = Biome::Ocean;
	else if (height <= kSeaLevel + 1)
		b = temp < -0.35f ? Biome::Snow : Biome::Beach;
	else if (mountainMask > 0.5f && height > 80)
		b = Biome::Mountains;
	else if (temp < -0.35f)
		b = hum > 0.0f ? Biome::Taiga : Biome::Snow;
	else if (temp > 0.35f && hum < -0.05f)
		b = Biome::Desert;
	else
		b = hum > 0.1f ? Biome::Forest : Biome::Plains;
	return {height, b, temp, hum};
}

bool TerrainGenerator::IsCave(int x, int y, int z, int surface) const {
	if (m_settings.type == GeneratorType::Flat || !m_settings.shape.caves || y > surface - 3)
		return false;
	const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
	// Réseaux karstiques uniquement dans la croûte sédimentaire (jusqu'à -10 000).
	const int depth = GeologicalDepth(y);
	if (depth > 10'000)
		return false;
	// Grottes « spaghetti » : intersection de deux nappes de bruit proches de 0.
	const float a = m_cave.Noise3(fx / 48.f, fy / 32.f, fz / 48.f);
	const float b = m_cave.Noise3(fx / 48.f + 100.f, fy / 32.f, fz / 48.f + 100.f);
	if (a * a + b * b < 0.012f)
		return true;
	// Immenses salles naturelles (bruit cellulaire) : cavités sphériques.
	return Worley3(m_seed ^ 0x6666, fx, fy, fz, 96.f) < 0.05f;
}

int TerrainGenerator::GeologicalDepth(int y) const {
	if (y >= kInversionY)
		return -y;				   // monde normal : la profondeur croît vers le bas
	return 2 * kCoreDepth + y; // monde miroir : la profondeur redescend vers la surface inversée
}

BlockId TerrainGenerator::RockAt(int x, int y, int z, int depth) const {
	if (depth < 4)
		return m_p.dirt; // sol meuble
	if (depth < 12)
		return m_p.sand; // sable / sédiment superficiel
	if (depth < 300)
		return m_p.stone; // roche superficielle

	// Alternance de roches apparentées au sein d'une couche (strates).
	const float strata = m_strata.Noise3(static_cast<float>(x) / 96.f, static_cast<float>(y) / 160.f, static_cast<float>(z) / 96.f);

	// Croûte sédimentaire (0 → −35 km).
	if (depth < 2'000) {
		// calcaire dominant, poches de craie et de dolomie.
		if (strata > 0.35f)
			return m_p.chalk;
		if (strata < -0.55f)
			return m_p.dolomite;
		return m_p.limestone;
	}
	if (depth < 6'000) {
		// grès, conglomérat, siltite.
		if (strata > 0.45f)
			return m_p.conglomerate;
		if (strata < -0.5f)
			return m_p.siltstone;
		return m_p.sandstone;
	}
	if (depth < 15'000) {
		// schiste argileux, siltite, gypse.
		if (strata > 0.55f)
			return m_p.siltstone;
		if (strata < -0.6f)
			return m_p.gypsum;
		return m_p.shale;
	}
	if (depth < kCrustDepth) {
		// socle sédimentaire : dolomie, chert, puis ardoise (transition métamorphique).
		if (strata > 0.4f)
			return m_p.chert;
		if (strata < -0.5f)
			return m_p.slate;
		return m_p.dolomite;
	}

	// Manteau (−35 → −2 270 km) : métamorphique → magmatique → ultramafique.
	if (depth < kMantleDepth) {
		const float t = static_cast<float>(depth - kCrustDepth) / static_cast<float>(kMantleDepth - kCrustDepth);
		if (t < 0.06f) {
			// croûte inférieure métamorphique : gneiss, schiste, marbre.
			if (strata > 0.35f)
				return m_p.marble;
			if (strata < -0.35f)
				return m_p.schist;
			return m_p.gneiss;
		}
		if (t < 0.16f) {
			// quartzite, phyllite, hornfels, novaculite, amphibolite.
			if (strata > 0.55f)
				return m_p.quartzite;
			if (strata > 0.25f)
				return m_p.phyllite;
			if (strata < -0.55f)
				return m_p.novaculite;
			if (strata < -0.25f)
				return m_p.hornfels;
			return m_p.amphibolite;
		}
		if (t < 0.4f) {
			// granite, diorite, pegmatite.
			if (strata > 0.4f)
				return m_p.pegmatite;
			if (strata < -0.4f)
				return m_p.diorite;
			return m_p.granite;
		}
		if (t < 0.7f) {
			// basalte, gabbro, diabase, dolérite, andésite.
			if (strata > 0.55f)
				return m_p.diabase;
			if (strata > 0.2f)
				return m_p.gabbro;
			if (strata < -0.55f)
				return m_p.dolerite;
			if (strata < -0.2f)
				return m_p.andesite;
			return m_p.basalt;
		}
		if (t < 0.9f) {
			// rhyolite, scories, tuf, ponce, verre basaltique.
			if (strata > 0.6f)
				return m_p.basalticGlass;
			if (strata > 0.3f)
				return m_p.rhyolite;
			if (strata < -0.6f)
				return m_p.tuff;
			if (strata < -0.3f)
				return m_p.scoria;
			return m_p.pumice;
		}
		// zone ultramafique (approche du noyau) : péridotite.
		return m_p.peridotite;
	}

	// Noyau externe (−2 270 → −2 370 km) : mer de lave continue.
	if (depth < kCoreDepth)
		return m_p.magma;
	return m_p.bedrock; // ne survient pas (le miroir s'arrête à 2×noyau)
}

BlockId TerrainGenerator::OreAt(int x, int y, int z, int depth) const {
	const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
	// Géodes creuses (améthyste / quartz) au cœur des roches dures.
	if (depth > 300 && depth < kMantleDepth) {
		const int gx = x & ~63, gy = y & ~63, gz = z & ~63;
		if (math::Hash01(gx, gy, gz, m_seed ^ 0xaaaa) < 0.02f) {
			const float dx = fx - (static_cast<float>(gx) + 32.f);
			const float dy = fy - (static_cast<float>(gy) + 32.f);
			const float dz = fz - (static_cast<float>(gz) + 32.f);
			const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (d >= 5.f && d < 7.f)
				return math::Hash01(x, y, z, m_seed ^ 0xdddd) < 0.5f ? m_p.amethyst : m_p.quartz;
			if (d < 5.f)
				return kAir; // creuse
		}
	}
	// Filons de minerais (bruit cellulaire), selon la profondeur.
	if (depth < 2'000) {
		// Surface & sédimentaire : charbon, fer, cuivre, étain + minéraux industriels.
		if (Worley3(m_seed ^ 0xbbbb, fx, fy, fz, 22.f) < 0.12f) {
			const float r = math::Hash01(x, y, z, m_seed ^ 0x1111);
			if (r < 0.22f)
				return m_p.coal;
			if (r < 0.42f)
				return m_p.iron;
			if (r < 0.58f)
				return m_p.copper;
			if (r < 0.72f)
				return m_p.tin;
			if (r < 0.80f)
				return m_p.sulfur;
			if (r < 0.88f)
				return m_p.nitrate;
			if (r < 0.95f)
				return m_p.phosphorous;
			return m_p.mercury;
		}
	} else if (depth < kCrustDepth) {
		// Transition (−2 → −35 km) : or, fer, sel + métaux de base.
		if (Worley3(m_seed ^ 0xbbbb, fx, fy, fz, 32.f) < 0.10f) {
			const float r = math::Hash01(x, y, z, m_seed ^ 0x2222);
			if (r < 0.18f)
				return m_p.gold;
			if (r < 0.34f)
				return m_p.iron;
			if (r < 0.46f)
				return m_p.salt;
			if (r < 0.56f)
				return m_p.lead;
			if (r < 0.66f)
				return m_p.silver;
			if (r < 0.76f)
				return m_p.zinc;
			if (r < 0.86f)
				return m_p.nickel;
			if (r < 0.94f)
				return m_p.aluminum;
			return m_p.uranium;
		}
	} else if (depth < kMantleDepth) {
		// Profondeur métamorphique (−35 → −2 270 km) : diamants, redstone, or + métaux rares.
		if (Worley3(m_seed ^ 0xbbbb, fx, fy, fz, 48.f) < 0.08f) {
			const float r = math::Hash01(x, y, z, m_seed ^ 0x3333);
			if (r < 0.12f)
				return m_p.diamond;
			if (r < 0.24f)
				return m_p.redstone;
			if (r < 0.32f)
				return m_p.gold;
			if (r < 0.38f)
				return m_p.titanium;
			if (r < 0.44f)
				return m_p.rutile;
			if (r < 0.50f)
				return m_p.chromium;
			if (r < 0.56f)
				return m_p.manganese;
			if (r < 0.62f)
				return m_p.magnesium;
			if (r < 0.68f)
				return m_p.zirconium;
			if (r < 0.74f)
				return m_p.tungsten;
			if (r < 0.80f)
				return m_p.platinum;
			if (r < 0.86f)
				return m_p.osmium;
			if (r < 0.92f)
				return m_p.iridium;
			if (r < 0.96f)
				return m_p.tantalum;
			return m_p.cadmium;
		}
	} else if (depth < kCoreDepth) {
		// Foyers hydrothermaux / sulfures à la frontière de la mer de lave.
		if (Worley3(m_seed ^ 0xbbbb, fx, fy, fz, 16.f) < 0.20f)
			return m_p.obsidian;
	}
	return kAir; // aucun minerai
}

bool TerrainGenerator::IsFault(int x, int z, int y) const {
	// Failles / crevasses : crêtes ridged 2D qui percent verticalement, avec
	// un léger serpentement 3D.
	const float r = m_fault.Ridged2(static_cast<float>(x) / 500.f, static_cast<float>(z) / 500.f, 4);
	if (r < 0.72f)
		return false;
	const float wobble = m_fault.Noise3(static_cast<float>(x) / 140.f, static_cast<float>(y) / 70.f, static_cast<float>(z) / 140.f);
	return r > 0.78f + 0.10f * wobble;
}

BlockId TerrainGenerator::LavaLakeAt(int x, int y, int z, int depth) const {
	// Lacs de lave souterrains : jamais dans la couche de surface (sol sec).
	if (depth < 12)
		return kAir;
	// Un lac sphérique déterministe par cellule de 16 blocs, plus fréquent en profondeur.
	constexpr int kCell = 16;
	const int cx = math::FloorDiv(x, kCell), cy = math::FloorDiv(y, kCell), cz = math::FloorDiv(z, kCell);
	const float chance = 0.02f + 0.45f * math::SmoothStep(0.f, 1.f, static_cast<float>(depth - 256) / 55'000.f);
	if (math::Hash01(cx, cy, cz, m_seed ^ 0x7a7a) >= chance)
		return kAir;
	// Centre du lac dans la cellule (déterministe, indépendant de la présence).
	const float px = (static_cast<float>(cx) + 0.2f + 0.6f * math::Hash01(cx, cy, cz, m_seed ^ 0x1111)) * static_cast<float>(kCell);
	const float py = (static_cast<float>(cy) + 0.2f + 0.6f * math::Hash01(cx, cy, cz, m_seed ^ 0x2222)) * static_cast<float>(kCell);
	const float pz = (static_cast<float>(cz) + 0.2f + 0.6f * math::Hash01(cx, cy, cz, m_seed ^ 0x3333)) * static_cast<float>(kCell);
	const float dx = static_cast<float>(x) + 0.5f - px;
	const float dy = static_cast<float>(y) + 0.5f - py;
	const float dz = static_cast<float>(z) + 0.5f - pz;
	const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (r < 4.5f)
		return m_p.lava; // intérieur du lac (liquide)
	if (r < 6.5f)
		return m_p.obsidian; // croûte d'obsidienne qui scelle le lac (les failles ne le percent pas)
	return kAir;
}

int TerrainGenerator::SurfaceHeight(int x, int z) const { return std::max(SampleColumn(x, z).height, kSeaLevel); }

ColumnSample TerrainGenerator::Sample(int x, int z) const {
	const Column c = SampleColumn(x, z);
	return {c.height, c.biome, c.temperature, c.humidity};
}

bool TerrainGenerator::CellPlacement(size_t rule, int cx, int cz, IVec3 &origin) const {
	const StructureRule &r = m_settings.structures[rule];
	const ResolvedStructure &rs = m_structures[rule];
	if (rs.blocks.empty())
		return false;
	const uint64_t salt = m_seed ^ (0x5157ULL + rule * 0x9E3779B97F4A7C15ULL);
	if (math::Hash01(cx, int(rule), cz, salt) >= r.chance)
		return false;
	// Position dans la cellule (marge : l'emprise ne déborde pas sur la cellule voisine).
	const int w = rs.max.x - rs.min.x + 1, d = rs.max.z - rs.min.z + 1;
	const int freeX = std::max(1, r.spacing - w), freeZ = std::max(1, r.spacing - d);
	const int x = cx * r.spacing - rs.min.x + int(math::Hash01(cx, 17, cz, salt) * float(freeX));
	const int z = cz * r.spacing - rs.min.z + int(math::Hash01(cx, 29, cz, salt) * float(freeZ));
	const Column c = SampleColumn(x, z);
	if (c.height < kSeaLevel || !(r.biomes & (1u << uint32_t(c.biome))))
		return false;
	origin = {x, c.height + 1, z};
	return true;
}

void TerrainGenerator::StructuresIn(int x0, int z0, int x1, int z1, std::vector<StructurePlacement> &out) const {
	for (size_t i = 0; i < m_settings.structures.size(); ++i) {
		const int sp = std::max(8, m_settings.structures[i].spacing);
		for (int cz = math::FloorDiv(z0, sp) - 1; cz <= math::FloorDiv(z1, sp) + 1; ++cz)
			for (int cx = math::FloorDiv(x0, sp) - 1; cx <= math::FloorDiv(x1, sp) + 1; ++cx) {
				IVec3 o;
				if (CellPlacement(i, cx, cz, o) && o.x >= x0 && o.x < x1 && o.z >= z0 && o.z < z1)
					out.push_back({i, o});
			}
	}
}

void TerrainGenerator::PlaceStructures(const IVec3 &coord, std::span<BlockId> out) const {
	if (m_settings.type == GeneratorType::Flat && m_settings.structures.empty())
		return;
	const int ox = coord.x * kChunkSize, oy = coord.y * kChunkSize, oz = coord.z * kChunkSize;
	for (size_t i = 0; i < m_settings.structures.size(); ++i) {
		const ResolvedStructure &rs = m_structures[i];
		if (rs.blocks.empty())
			continue;
		const int sp = std::max(8, m_settings.structures[i].spacing);
		// Cellules dont l'emprise peut toucher ce chunk.
		for (int cz = math::FloorDiv(oz - sp, sp); cz <= math::FloorDiv(oz + kChunkSize + sp, sp); ++cz)
			for (int cx = math::FloorDiv(ox - sp, sp); cx <= math::FloorDiv(ox + kChunkSize + sp, sp); ++cx) {
				IVec3 o;
				if (!CellPlacement(i, cx, cz, o))
					continue;
				if (o.x + rs.max.x < ox || o.x + rs.min.x >= ox + kChunkSize || o.z + rs.max.z < oz || o.z + rs.min.z >= oz + kChunkSize ||
					o.y + rs.max.y < oy || o.y + rs.min.y >= oy + kChunkSize)
					continue;
				for (const auto &[rel, id] : rs.blocks) {
					const int lx = o.x + rel.x - ox, ly = o.y + rel.y - oy, lz = o.z + rel.z - oz;
					if (lx < 0 || ly < 0 || lz < 0 || lx >= kChunkSize || ly >= kChunkSize || lz >= kChunkSize)
						continue;
					BlockId &dst = out[static_cast<size_t>(VoxelIndex(lx, ly, lz))];
					if (rs.replaceSolid || dst == kAir || rel.y < 0)
						dst = id;
				}
			}
	}
}

void TerrainGenerator::Generate(const IVec3 &coord, std::span<BlockId> out, std::vector<EntityRecord> *spawns) const {
	DIAG_ZONE("Terrain::Generate");
	const int ox = coord.x * kChunkSize, oy = coord.y * kChunkSize, oz = coord.z * kChunkSize;
	std::fill(out.begin(), out.end(), kAir);
	// Hors de la plage verticale supérieure : rien (ciel).
	if (oy > kWorldMaxY)
		return;

	// Bornes du relief (marge d'un chunk). Le relief ordinaire est borné ; le relief
	// miroir en est le reflet à travers le noyau (msh = kMirrorSurfaceY − sh).
	constexpr int kSurfaceTop = 215;					  // plus haut sommet (kWorldMaxY − 40)
	constexpr int kSurfaceBottom = -12'000;				  // fosse abyssale maximale (marge)
	constexpr int kMirrorTop = kMirrorSurfaceY - kSurfaceBottom;	  // −4 728 000
	constexpr int kMirrorBottom = kMirrorSurfaceY - kSurfaceTop; // −4 740 215
	const bool nearNormal = oy + kChunkSize - 1 >= kSurfaceBottom && oy <= kSurfaceTop;
	const bool nearMirror = oy + kChunkSize - 1 >= kMirrorBottom && oy <= kMirrorTop;

	if (!nearNormal && !nearMirror) {
		if (oy > kSurfaceTop || oy + kChunkSize - 1 < kMirrorBottom)
			return; // ciel (ordinaire ou miroir) : déjà air
		// ── Profondeur : roche géologique, minerais, failles, lacs et mer de lave ─
		// Aucun échantillonnage de relief : GeologicalDepth suffit (les grottes, limitées
		// à 10 km d'une surface, ne s'appliquent plus ici).
		for (int z = 0; z < kChunkSize; ++z) {
			for (int x = 0; x < kChunkSize; ++x) {
				const int wx = ox + x, wz = oz + z;
				for (int y = 0; y < kChunkSize; ++y) {
					const int wy = oy + y;
					const int depth = GeologicalDepth(wy);
					BlockId b;
					if (depth >= kMantleDepth && depth < kCoreDepth)
						b = m_p.magma; // mer de lave continue du noyau externe (aucune faille ne la perce)
					else if (const BlockId lake = LavaLakeAt(wx, wy, wz, depth); lake != kAir)
						b = lake; // lac de lave souterrain (lave + croûte d'obsidienne, jamais percé par une faille)
					else if (depth < kCoreDepth && IsFault(wx, wz, wy))
						b = kAir; // faille / crevasse
					else if (const BlockId ore = OreAt(wx, wy, wz, depth); ore != kAir)
						b = ore;
					else
						b = RockAt(wx, wy, wz, depth);
					out[static_cast<size_t>(VoxelIndex(x, y, z))] = b;
				}
			}
		}
		return;
	}

	// ── Zone de surface (ordinaire et/ou miroir) : relief, sol, eau ───────────
	Column cols[kChunkArea];
	for (int z = 0; z < kChunkSize; ++z)
		for (int x = 0; x < kChunkSize; ++x)
			cols[x + z * kChunkSize] = SampleColumn(ox + x, oz + z);

	for (int z = 0; z < kChunkSize; ++z) {
		for (int x = 0; x < kChunkSize; ++x) {
			const Column c = cols[x + z * kChunkSize];
			const int wx = ox + x, wz = oz + z;
			const int msh = kMirrorSurfaceY - c.height; // surface miroir (reflet du relief)
			for (int y = 0; y < kChunkSize; ++y) {
				const int wy = oy + y;
				BlockId b = kAir;
				if (wy > c.height) {
					// Au-dessus de la surface ordinaire : océan (eau/glace) ou air.
					if (wy <= kSeaLevel)
						b = (wy == kSeaLevel && (c.biome == Biome::Snow || c.biome == Biome::Taiga)) ? m_p.ice : m_p.water;
				} else if (wy < msh) {
					// Au-dessous de la surface miroir : ciel du monde miroir (air).
					b = kAir;
				} else if (wy == c.height) {
					// Bloc de surface ordinaire.
					switch (c.biome) {
					case Biome::Desert:
					case Biome::Beach: b = m_p.sand; break;
					case Biome::Ocean: b = c.height < kSeaLevel - 12 ? m_p.gravel : m_p.sand; break;
					case Biome::Snow:
					case Biome::Taiga: b = m_p.snowyGrass; break;
					case Biome::Mountains: b = c.height > 150 ? m_p.snow : m_p.stone; break;
					default: b = m_p.grass; break;
					}
				} else if (wy == msh) {
					// Bloc de surface miroir (l'océan ordinaire → fond sableux inversé).
					b = c.height < kSeaLevel - 3 ? m_p.sand : m_p.mirrorGrass;
				} else {
					const int localDepth = c.height - wy; // profondeur sous la surface ordinaire
					const int mirrorDepth = wy - msh;	 // profondeur sous la surface miroir
					if (localDepth < 4) {
						b = (c.biome == Biome::Desert || c.biome == Biome::Beach || c.biome == Biome::Ocean) ? m_p.sand
							: c.biome == Biome::Mountains ? m_p.stone
														  : m_p.dirt;
					} else if (mirrorDepth < 4) {
						b = m_p.mirrorSoil;
					} else if (wy > kSeaLevel) {
						b = m_p.stone; // corps des montagnes (au-dessus du niveau de la mer)
					} else if (const BlockId lake = LavaLakeAt(wx, wy, wz, GeologicalDepth(wy)); lake != kAir) {
						b = lake; // lac de lave souterrain (croûte d'obsidienne + lave)
					} else if (IsCave(wx, wy, wz, c.height)) {
						b = kAir;
					} else if (const BlockId ore = OreAt(wx, wy, wz, GeologicalDepth(wy)); ore != kAir) {
						b = ore;
					} else {
						b = RockAt(wx, wy, wz, GeologicalDepth(wy));
					}
				}
				out[static_cast<size_t>(VoxelIndex(x, y, z))] = b;
			}
			// Végétation basse sur le sol (monde ordinaire uniquement).
			const int ly = c.height + 1 - oy;
			if (ly >= 0 && ly < kChunkSize && c.height >= kSeaLevel && (c.biome == Biome::Plains || c.biome == Biome::Forest)) {
				const size_t below = static_cast<size_t>(VoxelIndex(x, ly - 1 < 0 ? 0 : ly - 1, z));
				const float r = math::Hash01(wx, c.height, wz, m_seed ^ 0xf10);
				if (ly > 0 && out[below] == m_p.grass) {
					if (r < 0.010f)
						out[static_cast<size_t>(VoxelIndex(x, ly, z))] = m_p.flower1;
					else if (r < 0.020f)
						out[static_cast<size_t>(VoxelIndex(x, ly, z))] = m_p.flower2;
					else if (r < 0.10f)
						out[static_cast<size_t>(VoxelIndex(x, ly, z))] = m_p.tallGrass;
				}
			}
		}
	}

	// Arbres : colonnes candidates avec une marge de 3 blocs autour du chunk ; seuls
	// les voxels tombant dans ce chunk sont écrits → aucune dépendance entre chunks.
	auto put = [&](int wx, int wy, int wz, BlockId b, bool replaceSolid) {
		const int lx = wx - ox, ly = wy - oy, lz = wz - oz;
		if (lx < 0 || ly < 0 || lz < 0 || lx >= kChunkSize || ly >= kChunkSize || lz >= kChunkSize)
			return;
		BlockId &slot = out[static_cast<size_t>(VoxelIndex(lx, ly, lz))];
		if (replaceSolid || slot == kAir || slot == m_p.tallGrass || slot == m_p.flower1 || slot == m_p.flower2)
			slot = b;
	};
	constexpr int kMargin = 3;
	for (int wz = oz - kMargin; wz < oz + kChunkSize + kMargin; ++wz) {
		for (int wx = ox - kMargin; wx < ox + kChunkSize + kMargin; ++wx) {
			if (!m_settings.trees)
				break;
			const float r = math::Hash01(wx, 0, wz, m_seed ^ 0x7ee);
			if (r > 0.03f * std::max(1.f, m_settings.shape.treeDensity))
				continue;
			const Column c = (wx >= ox && wz >= oz && wx < ox + kChunkSize && wz < oz + kChunkSize) ? cols[(wx - ox) + (wz - oz) * kChunkSize]
																								  : SampleColumn(wx, wz);
			if (c.height < kSeaLevel + 1 || c.height + 10 < oy || c.height > oy + kChunkSize + 1)
				continue;
			const float density = c.biome == Biome::Forest ? 0.03f : c.biome == Biome::Taiga ? 0.02f : c.biome == Biome::Plains ? 0.004f
																: c.biome == Biome::Desert ? 0.002f : 0.f;
			if (r > density * m_settings.shape.treeDensity)
				continue;
			const int base = c.height + 1;
			const int h = 4 + static_cast<int>(math::Hash01(wx, 1, wz, m_seed) * 3.f);
			if (c.biome == Biome::Desert) {
				for (int y = 0; y < 3; ++y)
					put(wx, base + y, wz, m_p.cactus, false);
				continue;
			}
			const bool spruce = c.biome == Biome::Taiga;
			const BlockId log = spruce ? m_p.spruceLog : m_p.log, leaves = spruce ? m_p.spruceLeaves : m_p.leaves;
			for (int y = 0; y < h; ++y)
				put(wx, base + y, wz, log, true);
			const int top = base + h;
			for (int dy = -3; dy <= 1; ++dy) {
				const int radius = spruce ? std::max(0, 2 - (dy + 3) / 2) : (dy >= 0 ? 1 : 2);
				for (int dz = -radius; dz <= radius; ++dz)
					for (int dx = -radius; dx <= radius; ++dx) {
						if (std::abs(dx) == radius && std::abs(dz) == radius && radius > 1 && math::Hash01(wx + dx, top + dy, wz + dz, m_seed) < 0.6f)
							continue;
						put(wx + dx, top + dy, wz + dz, leaves, false);
					}
			}
			put(wx, top + 1, wz, leaves, false);
		}
	}

	PlaceStructures(coord, out);
	// Créatures initiales : une petite chance par chunk de surface.
	if (spawns && m_settings.creatures && math::Hash01(coord.x, coord.y, coord.z, m_seed ^ 0x5a5) < 0.18f) {
		const int x = static_cast<int>(math::Hash01(coord.x, 7, coord.z, m_seed) * kChunkSize);
		const int z = static_cast<int>(math::Hash01(coord.x, 9, coord.z, m_seed) * kChunkSize);
		const Column c = cols[x + z * kChunkSize];
		if (c.height >= kSeaLevel && c.height + 1 >= oy && c.height + 1 < oy + kChunkSize && c.biome != Biome::Ocean) {
			const int count = 1 + static_cast<int>(math::Hash01(coord.x, 3, coord.z, m_seed) * 3.f);
			for (int i = 0; i < count; ++i) {
				EntityRecord e;
				e.kind = c.biome == Biome::Snow || c.biome == Biome::Taiga ? "core:fox" : c.biome == Biome::Desert ? "core:camel" : "core:sheep";
				e.position = {static_cast<float>(ox + x) + 0.5f + static_cast<float>(i), static_cast<float>(c.height) + 1.1f,
							  static_cast<float>(oz + z) + 0.5f};
				e.halfExtents = {0.35f, 0.45f, 0.35f};
				e.health = e.maxHealth = 8.f;
				e.wander = true;
				spawns->push_back(std::move(e));
			}
		}
	}
}

} // namespace voxel
