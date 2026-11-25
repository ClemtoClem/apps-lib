#pragma once
/**
 * @file terrain.hpp
 * @brief Génération procédurale déterministe et **sans dépendance entre chunks**
 *        (exécutable en parallèle sur le pool) : relief (continents + collines +
 *        crêtes montagneuses), biomes (température/humidité), océans, grottes 3D,
 *        minerais, arbres (placés par hachage de colonne, débordant proprement sur
 *        les chunks voisins), végétation et créatures.
 */
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <dataio/json.hpp>

#include <math/noise.hpp>

#include "blocks.hpp"
#include "entities.hpp"
#include "types.hpp"

namespace voxel {

/// Profondeurs géologiques (Y en blocs = mètres ; la surface/océans sont à Y = 0).
/// Géologie « Terre réaliste » : croûte (35 km) → manteau (2 270 km) → noyau
/// externe (2 370 km) → mer de lave (100 km) → inversion de gravité → monde miroir.
inline constexpr int kSeaLevel = 0;				 // océans et niveau de la mer
inline constexpr int kCrustDepth = 35'000;		 // croûte : 0 → -35 km
inline constexpr int kMantleDepth = 2'270'000;	 // manteau : -35 → -2 270 km
inline constexpr int kCoreDepth = 2'370'000;	 // noyau externe : -2 270 → -2 370 km
inline constexpr int kInversionY = -kCoreDepth; // point d'inversion de la gravité
/// Surface inversée (monde miroir) : reflet de la surface ordinaire à travers le noyau.
inline constexpr int kMirrorSurfaceY = 2 * kInversionY; // -4 740 000
/// Épaisseur de la mer de lave du noyau externe (100 km).
inline constexpr int kLavaSeaThickness = 100'000;

struct TerrainPalette {
	// Surface.
	BlockId stone, dirt, grass, sand, water, snow, snowyGrass, gravel, bedrock, coal, iron, log, leaves, spruceLog, spruceLeaves,
		cactus, ice, flower1, flower2, tallGrass;
	// Monde miroir (surface inversée).
	BlockId mirrorGrass, mirrorStone, mirrorSoil;
	// Sédimentaires (croûte).
	BlockId limestone, sandstone, shale, chalk, chert, conglomerate, dolomite, siltstone, gypsum;
	// Métamorphiques.
	BlockId gneiss, marble, slate, schist, phyllite, hornfels, quartzite, novaculite, amphibolite;
	// Magmatiques.
	BlockId granite, diorite, andesite, basalt, gabbro, peridotite, diabase, dolerite, rhyolite, scoria, tuff, pumice, pegmatite,
		basalticGlass;
	// Noyau.
	BlockId magma, obsidian;
	// Liquides (lacs de lave).
	BlockId lava;
	// Minerais et cristaux (surface → profondeur).
	BlockId copper, tin, gold, diamond, redstone, salt, amethyst, quartz;
	// Métaux additionnels et minéraux industriels.
	BlockId lead, silver, nickel, zinc, platinum, mercury, aluminum, titanium, tungsten, chromium, uranium, manganese, magnesium,
		zirconium, tantalum, osmium, cadmium, iridium, rutile, sulfur, nitrate, phosphorous;
	static TerrainPalette Resolve(BlockRegistry &reg);
};

enum class GeneratorType : uint8_t { Normal = 0, Flat = 1, Amplified = 2, Archipelago = 3 };

enum class TerrainBiome : uint8_t { Plains, Forest, Desert, Snow, Taiga, Beach, Ocean, Mountains, Count };
[[nodiscard]] const char *TerrainBiomeName(TerrainBiome b) noexcept; // « Plaines »…

/// Forme du relief et du climat (bruits de Perlin / simplex). Les valeurs par défaut
/// reproduisent le générateur d'origine : un monde existant ne change pas.
struct TerrainShape {
	float continentScale = 900.f, continentAmplitude = 36.f; // continents et océans
	float hillsScale = 160.f, hillsAmplitude = 10.f;		  // collines
	float mountainScale = 320.f, mountainAmplitude = 140.f;	  // crêtes montagneuses
	float mountainMaskScale = 1100.f;						  // étendue des massifs
	float detailAmplitude = 1.5f;							  // grain du sol
	int octaves = 4;										  // octaves des continents et collines
	float climateScale = 1200.f;							  // taille des zones climatiques
	float temperatureBias = 0.f, humidityBias = 0.f;		  // décalage du climat (-1..1)
	bool caves = true;
	bool trenches = true;	 // fosses abyssales océaniques
	float treeDensity = 1.f; // multiplicateur de la densité des arbres
};

/// Structure préfabriquée placée automatiquement par le générateur.
struct StructureRule {
	std::string structure;	 // identifiant qualifié (« core:house »)
	float chance = 0.25f;	 // probabilité par cellule de la grille
	int spacing = 96;		 // côté de la cellule (blocs) : au plus une structure par cellule
	uint32_t biomes = 0xFF; // masque de `TerrainBiome` (bit i : biome i autorisé)
};

/// Réglages de génération d'un monde (figés à sa création, conservés dans sa base).
struct TerrainSettings {
	GeneratorType type = GeneratorType::Normal;
	float relief = 1.f;	   // multiplicateur des collines et montagnes
	bool creatures = true; // créatures initiales dans les chunks générés
	bool trees = true;
	TerrainShape shape;
	std::vector<StructureRule> structures;
};

[[nodiscard]] dataio::NodePtr TerrainToJson(const TerrainSettings &t);
[[nodiscard]] TerrainSettings TerrainFromJson(const dataio::NodePtr &j);

/// Résolution d'une structure par identifiant (fournie par les mods).
struct Structure;
using StructureLookup = std::function<const Structure *(std::string_view id)>;

/// Échantillon d'une colonne (aperçus) : altitude, biome, climat.
struct ColumnSample {
	int height = 0;
	TerrainBiome biome = TerrainBiome::Plains;
	float temperature = 0.f, humidity = 0.f;
};

/// Structure posée par le générateur (origine = bloc d'ancrage, au sol).
struct StructurePlacement {
	size_t rule = 0;
	IVec3 origin;
};

class TerrainGenerator {
public:
	TerrainGenerator(uint64_t seed, const TerrainPalette &palette, TerrainSettings settings = {}, const BlockRegistry *blocks = nullptr,
					 const StructureLookup &structures = {});
	[[nodiscard]] const TerrainSettings &Settings() const noexcept { return m_settings; }

	/// Remplit `out` (kChunkVolume voxels, index `VoxelIndex`) pour le chunk `coord`.
	/// Ajoute les créatures initiales dans `spawns`. Thread-safe.
	void Generate(const IVec3 &coord, std::span<BlockId> out, std::vector<EntityRecord> *spawns) const;
	/// Altitude du sol (premier bloc solide sous le ciel) en (x, z).
	[[nodiscard]] int SurfaceHeight(int x, int z) const;
	[[nodiscard]] uint64_t Seed() const noexcept { return m_seed; }
	[[nodiscard]] int SeaLevel() const noexcept { return kSeaLevel; }
	/// Colonne (x, z) : altitude, biome, température, humidité. Thread-safe.
	[[nodiscard]] ColumnSample Sample(int x, int z) const;
	/// Structures placées dont l'origine tombe dans [x0, x1) × [z0, z1).
	void StructuresIn(int x0, int z0, int x1, int z1, std::vector<StructurePlacement> &out) const;

private:
	using Biome = TerrainBiome;
	struct Column {
		int height;
		Biome biome;
		float temperature = 0, humidity = 0;
	};
	/// Structure résolue : blocs relatifs à l'ancre, emprise.
	struct ResolvedStructure {
		std::vector<std::pair<IVec3, BlockId>> blocks;
		IVec3 min{}, max{}; // bornes relatives (incluses)
		bool replaceSolid = false;
	};
	/// Origine de la structure de la cellule (cx, cz) d'une règle, si elle existe.
	[[nodiscard]] bool CellPlacement(size_t rule, int cx, int cz, IVec3 &origin) const;
	void PlaceStructures(const IVec3 &coord, std::span<BlockId> out) const;
	[[nodiscard]] Column SampleColumn(int x, int z) const;
	[[nodiscard]] bool IsCave(int x, int y, int z, int surface) const;
	/// Profondeur géologique (0 à la surface, 2×noyau au fond du monde miroir).
	[[nodiscard]] int GeologicalDepth(int y) const;
	/// Roche dominante d'une profondeur (Y-mapping géologique) ; `strata` varie la
	/// roche au sein d'une couche (alternance de roches apparentées).
	[[nodiscard]] BlockId RockAt(int x, int y, int z, int depth) const;
	/// Minerai éventuel à (x, y, z) pour une profondeur donnée (filons, cheminées, géodes).
	[[nodiscard]] BlockId OreAt(int x, int y, int z, int depth) const;
	/// Faille / crevasse verticale.
	[[nodiscard]] bool IsFault(int x, int z, int y) const;
	/// Lac de lave souterrain (poche de `core:lava` liquide) à (x, y, z) pour une
	/// profondeur donnée ; `kAir` sinon. Scellé : les failles ne le percent pas.
	[[nodiscard]] BlockId LavaLakeAt(int x, int y, int z, int depth) const;

	uint64_t m_seed;
	TerrainSettings m_settings;
	TerrainPalette m_p;
	math::SimplexNoise m_continent, m_hills, m_mountain, m_temperature, m_humidity, m_cave, m_detail;
	math::SimplexNoise m_vein, m_fault, m_geode, m_ore, m_strata;
	std::vector<ResolvedStructure> m_structures; // une par règle (vide si non résolue)
};

} // namespace voxel
