#pragma once
/**
 * @file textures.hpp
 * @brief Textures procédurales (RGBA8) d'Abyssale : aucune image à livrer.
 *
 * Toutes sont tuilables (bruit périodique obtenu en mélangeant les bords) et générées au
 * démarrage en quelques millisecondes.
 */
#include <cstdint>
#include <vector>

namespace abyssale {

struct Image {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> rgba; ///< width × height × 4
};

/// Atlas du terrain : moitié gauche sable (rides, grains), moitié droite roche (fissures,
/// mousse). Utilisé en triplanaire « terrain » (`sdl3::Material::terrainAtlas`).
[[nodiscard]] Image TerrainAtlas(uint32_t tileSize = 512);
/// Roche seule (blocs, arches, falaises isolées).
[[nodiscard]] Image RockTexture(uint32_t size = 512);
/// Feuille d'algue : u en travers, v le long ; nervure claire, bords dorés.
[[nodiscard]] Image LeafTexture(uint32_t width = 64, uint32_t height = 256);
/// Éponge : alvéoles en relief (pores sombres sur fond clair).
[[nodiscard]] Image SpongeTexture(uint32_t size = 256);
/// Bois d'épave délavé, veiné et piqué.
[[nodiscard]] Image WoodTexture(uint32_t size = 256);
/// Raie manta : moitié haute dos sombre tacheté, moitié basse ventre clair.
[[nodiscard]] Image MantaTexture(uint32_t width = 128, uint32_t height = 256);
/// Poisson (`meshes::Fish`, u le long du corps depuis le nez, v autour : 0,25 dos, 0,75 ventre) :
/// dos bleu sombre, flancs argentés rayés, ligne latérale, ventre blanc.
[[nodiscard]] Image FishTexture(uint32_t width = 128, uint32_t height = 128);
/// Carapace de crustacé : marbrures et piqûres en niveaux clairs (teinte de chaque animal).
[[nodiscard]] Image ShellTexture(uint32_t size = 128);
/// Faisceau de lumière : intensité maximale au centre de la largeur, nulle sur les bords,
/// décroissante le long (v).
[[nodiscard]] Image ShaftTexture(uint32_t width = 64, uint32_t height = 256);

} // namespace abyssale
