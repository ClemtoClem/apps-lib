#pragma once
/**
 * @file blocks.hpp
 * @brief Registre des blocs : identifiants compacts (`BlockId` 16 bits), noms
 *        `mod:bloc`, matériau, textures par face, lumière émise. Rempli par les mods
 *        (fichiers JSON du dossier `blocks` et `blocks.json`).
 *
 * Formes (phase 3) : un bloc peut décrire une forme `nodebox` (boîtes en unités bloc,
 * centrées : -0,5..0,5), un maillage `.obj` / `.b3d` (`type: obj|b3d`, `path`), des
 * boîtes de collision (`physics.collision` : `FIXED` + `fixed`, ou `FROM_NODEBOX`) et
 * une connectivité automatique :
 *
 * @code
 *   "connects": { "to": ["core:fence", "#fence", "#solid"],
 *                 "north": [[x0,y0,z0,x1,y1,z1]…], "south": […], "east": […], "west": […],
 *                 "up": […], "down": […] }
 * @endcode
 *
 * Les boîtes d'une face s'ajoutent à la forme quand le voisin de ce côté est un bloc
 * cité (identifiant, `#étiquette`, ou `#solid` pour tout cube plein opaque).
 */
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>
#include <dataio/json.hpp>

#include "types.hpp"

namespace voxel {

/// Boîte d'une forme de bloc : x0, y0, z0, x1, y1, z1 (unités bloc, centrée sur le bloc).
using BlockBox = std::array<float, 6>;

/// Triangle d'un maillage de bloc (unités bloc centrées ; UV 0..1, origine en haut à gauche).
struct BlockTriangle {
	std::array<float, 9> position;
	std::array<float, 6> uv;
};

/// Connectivité automatique d'un bloc à ses voisins.
struct ConnectRule {
	std::vector<std::string> to;				 // identifiants qualifiés, « #étiquette », « #solid »
	std::array<std::vector<BlockBox>, 6> faces; // boîtes ajoutées par face connectée (ordre `Face`)
	[[nodiscard]] bool Empty() const noexcept { return to.empty(); }
};

/// Interaction au clic droit déclarée par un bloc (« interact » du JSON).
struct BlockInteraction {
	std::string toggle; // bloc de remplacement (porte ouverte ↔ fermée, lampe allumée ↔ éteinte)
	std::string action; // action du joueur : « sit » (s'asseoir), « sleep » (dormir)
	std::string call;	// fonction globale du script du mod appelée avec l'évènement
	std::string sound;	// son joué à l'interaction
	[[nodiscard]] bool Empty() const noexcept { return toggle.empty() && action.empty() && call.empty(); }
};

/// Cellule supplémentaire d'un bloc de plusieurs cases (haut d'une porte, tête de lit).
struct BlockPart {
	std::array<int, 3> offset{};
	std::string block; // nom qualifié
};

enum class Material : uint8_t {
	Air,		 // invisible, traversable
	Opaque,		 // plein, cache ses voisins
	Cutout,		 // texture à trous (feuilles, fleurs) : alpha testé, ne cache pas les voisins
	Translucent, // verre, glace : mélange alpha
	Liquid		 // eau, lave : mélange alpha, traversable
};

struct BlockDef {
	std::string name;		 // « core:stone »
	std::string displayName; // « Pierre »
	Material material = Material::Opaque;
	std::array<std::string, 6> textures; // nom de texture par face (Face)
	std::array<uint16_t, 6> layers{};	 // couche dans la texture-tableau (résolue par le rendu)
	uint8_t light = 0;					 // lumière émise 0..15
	std::string biomeTint;				 // teinte de biome des textures (« foliage », « grass »…), vide : aucune
	bool solid = true;					 // collision
	bool cross = false;					 // végétation rendue en croix
	float hardness = 1.0f;				 // dureté (temps de minage de base)
	std::string bestTool;				 // outil optimal (« pickaxe », « axe », « shovel »… ; vide : main nue)
	std::string soundGroup;				 // matériau sonore (« stone », « wood », « plant »… ; vide : aucun)
	std::vector<std::string> tags;
	std::string mod;
	std::vector<BlockBox> boxes;		  // forme nodebox (vide : cube plein ou croix)
	std::vector<BlockTriangle> mesh;	  // maillage .obj / .b3d (vide : aucun)
	std::vector<BlockBox> collision;	  // collision (vide : cube plein si solide)
	ConnectRule connect;				  // connectivité automatique
	BlockInteraction interact;			  // clic droit (bascule, action, script)
	std::vector<BlockPart> parts;		  // cellules posées avec le bloc (relatives au bloc)
	std::array<int, 3> partOf{};		  // partie d'un bloc multiple : décalage vers la cellule racine
	bool isPart = false;
	std::string icon;					  // image d'inventaire (« core:items/oak_door »), vide : rendu du bloc
	bool item = true;					  // présent dans l'inventaire créatif

	// ── Liquides ────────────────────────────────────────────────────────
	uint8_t liquidRange = 0; // 0 = pas un liquide ; sinon portée d'étalement (1..15)
	float liquidTick = 1.f;  // intervalle entre deux pas d'écoulement (viscosité : plus grand = plus lent)
	bool renewable = false;  // source infinie (règle 2×2) : eau oui, lave non
	std::string flowingTexture; // texture du liquide en mouvement
	struct LiquidMix {
		std::string with;	// liquide déclencheur (nom qualifié)
		std::string source; // résultat pour une source
		std::string flowing; // résultat pour un écoulement
	};
	std::vector<LiquidMix> liquidMix; // réactions (lave + eau → obsidienne…)

	// ── Blocs soumis à la gravité ────────────────────────────────────────
	bool falling = false; // tombe dans le vide s'il n'est pas soutenu (sable, gravier)

	// ── Explosions ────────────────────────────────────────────────────────
	float blastResistance = 1.0f; // résistance aux explosions (≈ dureté par défaut)
	float explosionRadius = 0.f;  // > 0 : bloc explosif (rayon en mètres)
	float explosionDelay = 0.f;   // mèche avant détonation (secondes)

	// ── Réflectance ──────────────────────────────────────────────────────
	float reflectivity = 0.f; // 0 mat, 1 miroir (métaux, verre, glace)

	[[nodiscard]] bool HasShape() const noexcept { return !boxes.empty() || !mesh.empty(); }

	[[nodiscard]] bool IsOpaque() const noexcept { return material == Material::Opaque && boxes.empty() && mesh.empty(); }
	[[nodiscard]] bool IsVisible() const noexcept { return material != Material::Air; }
	[[nodiscard]] bool IsLiquid() const noexcept { return liquidRange > 0 || material == Material::Liquid; }
	[[nodiscard]] bool HasTag(std::string_view t) const noexcept;

	// ── Orientation ────────────────────────────────────────────────────────
	bool orientAuto = true;				  // orienter face au joueur / à la verticale (accroupi)
	uint8_t orientDefault = 0;			  // orientation par défaut (auto désactivée ou introuvable)
	std::vector<uint8_t> orientAllowed;	  // orientations autorisées (vide : toutes)
	std::vector<uint8_t> orientForbidden; // orientations interdites
};

class BlockRegistry {
public:
	BlockRegistry();

	/// Enregistre (ou remplace) un bloc ; retourne son identifiant.
	BlockId Register(BlockDef def);
	/// Charge une liste JSON de définitions pour le mod `modId` (`baseDir` : dossier du mod,
	/// pour les maillages `.obj` / `.b3d`). Retourne le nombre de blocs.
	size_t LoadDefinitions(const dataio::Json &list, std::string_view modId, const std::filesystem::path &baseDir = {});
	/// Vrai si un bloc `rule` se connecte au bloc `other` (identifiants, étiquettes, #solid).
	[[nodiscard]] bool Connects(const ConnectRule &rule, BlockId other) const noexcept;

	[[nodiscard]] BlockId Find(std::string_view name) const noexcept;
	/// Identifiant d'un bloc, créé à la volée (couleur générée) s'il est inconnu.
	BlockId FindOrCreate(std::string_view name);
	[[nodiscard]] const BlockDef &Get(BlockId id) const noexcept { return m_defs[id < m_defs.size() ? id : 0]; }
	[[nodiscard]] BlockDef &Mutable(BlockId id) noexcept { return m_defs[id]; }
	[[nodiscard]] size_t Count() const noexcept { return m_defs.size(); }
	[[nodiscard]] const std::vector<BlockDef> &All() const noexcept { return m_defs; }

	// Tables plates consultées dans les boucles chaudes (mesher, physique).
	[[nodiscard]] bool IsOpaque(BlockId id) const noexcept { return m_opaque[id]; }
	[[nodiscard]] bool IsSolid(BlockId id) const noexcept { return m_solid[id]; }
	[[nodiscard]] Material MaterialOf(BlockId id) const noexcept { return m_material[id]; }

	/// Nom complet : « stone » dans le mod « core » → « core:stone ».
	[[nodiscard]] static std::string Qualify(std::string_view name, std::string_view modId);

private:
	std::vector<BlockDef> m_defs;
	core::RobinMap<std::string, BlockId, core::Hasher<std::string>> m_byName;
	std::vector<uint8_t> m_opaque, m_solid;
	std::vector<Material> m_material;
};

} // namespace voxel
