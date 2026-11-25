#pragma once
/**
 * Entrées/sorties d'un Model.
 *
 *  - Format natif `.mesh` : JSON lisible (dataio), qui conserve TOUT ce qui
 *    fait un modèle éditable (faces planes avec trous, arêtes adoucies/
 *    lissées, courbes, matériaux, calques) — c'est le fichier d'un maillage
 *    modélisé dans un projet du sandbox.
 *  - Export OBJ (+ .mtl) / STL / PLY : maillage triangulé via
 *    dataio::SaveMesh (formats d'échange, comme les exports d'openskp).
 *  - Import de triangles (FromTriangles) : n'importe quel maillage —
 *    primitives de sdl3::MeshData, glTF importé… — redevient un modèle
 *    éditable : sommets soudés, triangles coplanaires fusionnés en faces,
 *    arêtes des surfaces courbes adoucies.
 */
#include <span>

#include <dataio/node.hpp>

#include "model.hpp"
#include "triangulate.hpp"

namespace modeling {

inline constexpr const char *MODEL_FORMAT = "sandbox.model"; ///< fichiers `.model` (« modeling.model » encore lu)
inline constexpr int MODEL_VERSION = 1;

[[nodiscard]] dataio::NodePtr ToJson(const Model &model);
[[nodiscard]] Result<Model, String> FromJson(const dataio::NodePtr &node);

[[nodiscard]] String Serialize(const Model &model);
[[nodiscard]] Result<Model, String> Deserialize(const String &json);

/// Fichiers `.mesh` (JSON). NONE = succès.
[[nodiscard]] Option<String> SaveModel(const String &path, const Model &model);
[[nodiscard]] Result<Model, String> LoadModel(const String &path);

/// Export triangulé : .obj (+ .mtl des matériaux), .stl, .ply.
[[nodiscard]] Option<String> ExportModel(const String &path, const Model &model);

struct ImportOptions {
	/// Arêtes entre faces dont l'angle est inférieur : adoucies et lissées.
	float softenAngle = 25.f * math::PI / 180.f;
	/// Fusionne les triangles coplanaires voisins en faces polygonales.
	bool mergeCoplanar = true;
	uint32_t material = INVALID_ID;
};

/// Modèle éditable d'après un maillage indexé (triangles).
[[nodiscard]] Model FromTriangles(std::span<const Vec3> positions, std::span<const uint32_t> indices,
								  const ImportOptions &options = {});

/// Import d'un fichier glTF 2.0 (.gltf + tampons, .glb) en modèle éditable : maillages de la
/// scène par défaut placés par la hiérarchie des nœuds (Y vers le haut, mètres, comme le
/// sandbox). Les matériaux glTF rejoignent `palette` (rapprochés par nom, ajoutés à la fin
/// sinon) ; le modèle rendu utilise cette palette.
[[nodiscard]] Result<Model, String> ImportGltf(const String &path, std::vector<Material> &palette, const ImportOptions &options = {});

} // namespace modeling
