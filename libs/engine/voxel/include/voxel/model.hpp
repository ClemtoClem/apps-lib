#pragma once
/**
 * @file model.hpp
 * @brief Modèles 3D des mods : représentation commune éditable, lecture et écriture de
 *        tous les formats rencontrés dans les fichiers JSON des mods, animation à images
 *        clés et géométrie (quadrilatères texturés) pour l'affichage.
 *
 * Formats lus (cf. mods `core`, `backrooms`, `elevators`) :
 *
 * | Format    | Source                                                                  | Unités             |
 * |-----------|-------------------------------------------------------------------------|--------------------|
 * | `Cube`    | bloc plein (`shape: FULL_CUBE` ou absent)                               | bloc, centré       |
 * | `Cross`   | végétation (`shape: cross`) : deux plans en croix                        | bloc, centré       |
 * | `Nodebox` | `nodebox: [[x0,y0,z0,x1,y1,z1]…]`, `{fixed: […]}`, `{type|shape: FIXED}` | bloc, centré       |
 * | `Boxes`   | modèle `format: json`, `data.boxes` (même liste que nodebox)            | bloc, centré       |
 * | `Rig`     | modèle `format: rig` : parties (pivot, rotation), boîtes `from/size/uv`  | pixel (1/16), pieds |
 * | `Obj`     | Wavefront `.obj` (`v`, `vt`, `f`, groupes `o`/`g` → parties)             | fichier            |
 * | `B3d`     | Blitz3D binaire (`BB3D` : `NODE`, `MESH`, `VRTS`, `TRIS`)               | fichier            |
 *
 * Les boîtes d'un rig utilisent le dépliage « box UV » (texture `textureSize`) ; celles
 * d'un nodebox prennent la région de la tuile 16×16 correspondant à leur position dans
 * le bloc (comme Minetest). Les clés inconnues sont conservées (`extra`, JSON compact)
 * pour que l'écriture ne perde rien.
 *
 * Animation : pistes de rotation (degrés, ajoutées à la rotation de repos) et de
 * position (unités du modèle) par partie, images clés interpolées linéairement.
 */
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/result.hpp>
#include <dataio/json.hpp>
#include <math/math.hpp>

namespace voxel {

enum class ModelFormat : uint8_t { Cube, Cross, Nodebox, Boxes, Rig, Obj, B3d };
[[nodiscard]] const char *ModelFormatLabel(ModelFormat format) noexcept; ///< « nodebox », « rig »…

/// Faces d'une boîte : +X, -X, +Y, -Y, +Z (avant), -Z (arrière).
enum class BoxFace : uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

struct ModelBox {
	math::FVector3 from{};
	math::FVector3 size{1, 1, 1};
	math::FVector2 uv{};	// origine du dépliage « box UV » (texels), rig seulement
	float inflate = 0.f;	// agrandissement uniforme (couche de vêtement)
	bool mirror = false;	// dépliage miroir (membres gauches)
	bool visible = true;	// affichage dans l'éditeur (non enregistré)
	std::string extra;		// autres clés (JSON compact) : only64, uv64…
	[[nodiscard]] math::FVector3 To() const noexcept { return from + size; }
};

struct MeshVertex {
	math::FVector3 position;
	math::FVector2 uv; // 0..1, origine en haut à gauche
};

/// Partie (groupe) : pivot et rotation de repos, boîtes et/ou maillage.
struct ModelPart {
	std::string name;
	math::FVector3 pivot{};
	math::FVector3 rotation{}; // degrés : X, puis Y, puis Z, autour du pivot
	std::vector<ModelBox> boxes;
	std::vector<MeshVertex> vertices;
	std::vector<uint32_t> indices; // triangles
	bool visible = true;
	std::string extra; // walk, idle, look… (JSON compact)
};

struct Keyframe {
	float time = 0.f;
	math::FVector3 value{};
};

struct AnimationTrack {
	std::string part;
	std::vector<Keyframe> rotation; // degrés, ajoutés à la rotation de repos
	std::vector<Keyframe> position; // unités du modèle, ajoutées à la position
};

struct Animation {
	std::string name;
	float length = 1.f;
	bool loop = true;
	std::vector<AnimationTrack> tracks;
	[[nodiscard]] AnimationTrack *Track(std::string_view part, bool create);
	[[nodiscard]] const AnimationTrack *Track(std::string_view part) const;
};

struct ModelBounds {
	math::FVector3 min{}, max{};
	[[nodiscard]] math::FVector3 Size() const noexcept { return max - min; }
	[[nodiscard]] math::FVector3 Center() const noexcept { return (min + max) * 0.5f; }
};

/// Mécanique générique d'un modèle : comportement réutilisable déclaré sur un rig
/// (`mechanic: {type, animation, toggle}`) et partagé par plusieurs objets.
/// Exemple : une porte générique a `type: "door"`, `animation: "ouvrir"` (rotation du
/// panneau autour de sa charnière) et `toggle: true` (aller-retour).
struct ModelMechanic {
	std::string type;	   // « door », « drawer », « switch »… (vide : aucune)
	std::string animation; // nom de l'animation jouée à l'activation
	bool toggle = true;	// bascule (aller-retour) ou simple déclenchement
	[[nodiscard]] bool Empty() const noexcept { return type.empty(); }
};

struct Model {
	std::string id;
	ModelFormat format = ModelFormat::Nodebox;
	float unitsPerBlock = 1.f; // rig : 16 (pixels)
	int textureWidth = 16, textureHeight = 16;
	bool boxUv = false; // dépliage « box UV » (rig) ; sinon projection par face (nodebox)
	std::vector<ModelPart> parts;
	std::vector<ModelBox> collision; // boîtes de collision d'un bloc (unités du modèle)
	std::vector<Animation> animations;
	std::optional<ModelMechanic> mechanic; // comportement réutilisable (rig)

	[[nodiscard]] int FindPart(std::string_view name) const noexcept;
	/// Boîte englobante au repos (unités du modèle), parties visibles ou non.
	[[nodiscard]] ModelBounds Bounds() const noexcept;
	[[nodiscard]] size_t BoxCount() const noexcept;
	[[nodiscard]] size_t TriangleCount() const noexcept;
	/// Les boîtes s'éditent (les maillages .obj/.b3d se déplacent et tournent en bloc).
	[[nodiscard]] bool HasMeshes() const noexcept;
};

// ── Lecture ──────────────────────────────────────────────────────────────────

/// Liste `[[x0,y0,z0,x1,y1,z1]…]` (bornes dans n'importe quel ordre).
[[nodiscard]] std::vector<ModelBox> ParseBoxList(const dataio::Json &list);
/// Nodebox d'un bloc : liste, `{fixed: […]}`, `{type|shape: "FIXED", fixed: […]}`.
[[nodiscard]] Result<Model, std::string> ParseNodebox(const dataio::Json &nodebox);
[[nodiscard]] Model MakeCubeModel();
[[nodiscard]] Model MakeCrossModel();
/// Définition `{type: model, format: rig, textureSize, parts: […], animations?}`.
[[nodiscard]] Result<Model, std::string> ParseRig(const dataio::Json &def);
/// Définition `{type: model, format: json, data: {boxes: […]}}`.
[[nodiscard]] Result<Model, std::string> ParseBoxesModel(const dataio::Json &def);
[[nodiscard]] Result<Model, std::string> ParseObj(std::string_view text);
[[nodiscard]] Result<Model, std::string> ParseB3d(std::span<const uint8_t> bytes);
/// Collision d'un bloc (`physics.collision` : `FIXED` + `fixed`, ou `FROM_NODEBOX`).
[[nodiscard]] std::vector<ModelBox> ParseCollision(const dataio::Json &blockDef, const Model &shape);
/// Animations `[{name, length, loop, tracks: [{part, rotation: [[t,x,y,z]…], position}]}]`.
[[nodiscard]] std::vector<Animation> ParseAnimations(const dataio::Json &list);

// ── Écriture ─────────────────────────────────────────────────────────────────

/// Boîtes visibles de toutes les parties (pivots/rotations ignorés : nodebox).
[[nodiscard]] dataio::Json BoxListToJson(const Model &model);
[[nodiscard]] dataio::Json BoxListToJson(std::span<const ModelBox> boxes);
/// Définition rig complète ; les clés de `original` (et de ses parties, par nom) non
/// gérées par l'éditeur sont conservées.
[[nodiscard]] dataio::Json RigToJson(const Model &model, const dataio::Json &original);
[[nodiscard]] dataio::Json AnimationsToJson(std::span<const Animation> animations);
/// Wavefront : un groupe `o` par partie (boîtes triangulées, maillages tels quels).
[[nodiscard]] std::string ObjToText(const Model &model);
/// Convertit en rig (unités pixel, box UV) — pour animer un nodebox ou un maillage.
[[nodiscard]] Model ToRig(const Model &model);

// ── Animation ────────────────────────────────────────────────────────────────

/// Valeur d'une piste à `time` (interpolation linéaire ; avant la première clé : la
/// première ; boucle : `time` ramené dans [0, length)).
[[nodiscard]] math::FVector3 SampleKeys(std::span<const Keyframe> keys, float time) noexcept;
/// Insère ou remplace la clé à `time` (à 1e-3 près), triée par temps.
void SetKey(std::vector<Keyframe> &keys, float time, const math::FVector3 &value);
/// Supprime la clé à `time` ; vrai si elle existait.
bool RemoveKey(std::vector<Keyframe> &keys, float time) noexcept;

/// Décalages de pose d'une partie (ajoutés au repos).
struct PartOffset {
	math::FVector3 rotation{}; // degrés
	math::FVector3 position{}; // unités du modèle
};

/// Pose à `time` : animation (facultative) + mouvements procéduraux des rigs
/// (`walk: [axe, phase°, amplitude°]`, `idle`) si `motionTime >= 0`.
[[nodiscard]] std::vector<PartOffset> SamplePose(const Model &model, const Animation *animation, float time, float motionTime = -1.f);

// ── Géométrie ────────────────────────────────────────────────────────────────

/// Rectangle de texture (texels) d'une face ; u1 < u0 : retourné horizontalement.
struct UvRect {
	float u0, v0, u1, v1;
};
/// Dépliage « box UV » d'une boîte de rig (ordre `BoxFace`).
[[nodiscard]] std::array<UvRect, 6> BoxUvLayout(const ModelBox &box) noexcept;
/// Cadrillage UV automatique : range le dépliage « box UV » de **toutes** les boîtes
/// (toutes parties) dans une grille sans chevauchement (emballage par étagères), en
/// agrandissant la texture si nécessaire. `margin` : espace (texels) entre deux boîtes.
/// Ne fait rien si le modèle n'utilise pas le dépliage « box UV ».
void AutoBoxUv(Model &model, int margin = 2);

/// Quadrilatère texturé en unités BLOC (repère du monde du modèle : y vers le haut).
struct ModelQuad {
	math::FVector3 p[4];
	math::FVector2 uv[4]; // 0..1
	math::FVector3 normal;
	int part = -1, box = -1; // box = -1 : triangle de maillage (p[3] == p[2])
	uint8_t face = 0;
};

/// Matrice de rotation (degrés, X puis Y puis Z) appliquée à `v`.
[[nodiscard]] math::FVector3 RotateEuler(const math::FVector3 &v, const math::FVector3 &degrees) noexcept;
/// Quadrilatères de toutes les parties et boîtes visibles, posées par `pose` (vide : repos).
void BuildQuads(const Model &model, std::span<const PartOffset> pose, std::vector<ModelQuad> &out);

/// Sommet texturé prêt pour le rendu (position en blocs, normale, UV 0..1) —
/// même disposition que `voxel::ModelVertex`.
struct ModelVertex {
	float x, y, z, nx, ny, nz, u, v;
};
/// Triangles du modèle posé (repos si `pose` vide), en unités blocs, face avant en
/// sens antihoraire (compatible avec l'élimination des faces arrière du pipeline).
void BuildModelMesh(const Model &model, std::span<const PartOffset> pose, std::vector<ModelVertex> &out);

} // namespace voxel
