#pragma once
/**
 * @file avatar.hpp
 * @brief Avatar bipède animé (skin 64×64) : postures, nages, gestes et maillage posé.
 *
 * Le corps est formé de six parties articulées (tête, torse, bras, jambes) ; chacune tourne
 * autour de son pivot (cou, épaules, hanches) par des angles d'Euler (X puis Y puis Z, en
 * radians). La tête et les bras suivent le torse ; l'ensemble suit la racine (centre du
 * corps), qui peut se coucher (lit, nage) ou se pencher (chute libre).
 *
 * Repère du corps : origine au centre (hauteur totale 1,8 bloc, pieds à y = −0,9), avant = −Z,
 * droite du personnage = +X. Conventions d'angles (vérifiées par les tests) :
 *  - bras/jambes, X > 0 : le membre part vers l'avant (π/2 : horizontal devant, π : en haut) ;
 *  - bras droit, Z > 0 (bras gauche, Z < 0) : le membre s'écarte du corps ;
 *  - torse, X < 0 : penché en avant ; tête, X > 0 : regarde vers le haut ;
 *  - racine, X = +π/2 : couché sur le dos, tête vers +Z ; X = −π/2 : à plat ventre, tête vers −Z.
 *
 * `ComputeAvatarPose` produit la posture d'un état (debout, marche, accroupi, assis, couché,
 * nage, chute libre, vol) à un instant donné, puis superpose un geste ponctuel (coucou,
 * enfilage d'armure) et le balancement du bras (frappe). `BlendPose` lisse les transitions.
 * `BuildAvatarMesh` produit les triangles texturés (format de `voxel::ModelVertex`) : skin,
 * objets tenus dans les mains et pièces d'armure (régions de la texture fournies par l'appelant).
 */
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <math/math.hpp>

#include "model.hpp"

namespace voxel {

enum class AvatarPart : uint8_t { Head, Body, ArmRight, ArmLeft, LegRight, LegLeft, Count };
inline constexpr size_t kAvatarParts = size_t(AvatarPart::Count);

enum class AvatarState : uint8_t { Stand, Walk, Crouch, Sit, Sleep, Swim, FreeFall, Fly, Count };
enum class SwimStyle : uint8_t { Crawl, Breaststroke, Backstroke, Count };
enum class AvatarGesture : uint8_t { None, Wave, WaveBoth, WaveSmall, EquipArmor, Count };

[[nodiscard]] const char *AvatarStateId(AvatarState s) noexcept;		// "stand", "walk"…
[[nodiscard]] const char *AvatarStateLabel(AvatarState s) noexcept;	// « Debout »…
[[nodiscard]] const char *SwimStyleId(SwimStyle s) noexcept;			// "crawl", "breaststroke", "backstroke"
[[nodiscard]] const char *SwimStyleLabel(SwimStyle s) noexcept;		// « Crawl », « Brasse », « Dos crawlé »
[[nodiscard]] std::optional<SwimStyle> ParseSwimStyle(std::string_view id) noexcept;
[[nodiscard]] const char *AvatarGestureId(AvatarGesture g) noexcept;	// "wave", "wave_both", "wave_small", "equip_armor"
[[nodiscard]] const char *AvatarGestureLabel(AvatarGesture g) noexcept; // « Coucou »…
[[nodiscard]] std::optional<AvatarGesture> ParseAvatarGesture(std::string_view id) noexcept;
/// Durée d'un geste (s) ; les coucous se jouent ~2,4 s.
[[nodiscard]] float AvatarGestureDuration(AvatarGesture g) noexcept;

struct AvatarPose {
	std::array<math::FVector3, kAvatarParts> rotation{}; // Euler (X, Y, Z) autour du pivot de la partie
	math::FVector3 rootRotation{};						 // rotation de tout le corps autour de son centre
	math::FVector3 rootOffset{};						 // translation de tout le corps (blocs)
};

struct AvatarInput {
	AvatarState state = AvatarState::Stand;
	SwimStyle swim = SwimStyle::Crawl;
	float time = 0;			// horloge d'animation (s)
	float walkPhase = 0;	// phase de marche (radians, avance avec la distance parcourue)
	float speed = 0;		// vitesse horizontale (blocs/s)
	float lookPitch = 0;	// tangage du regard (radians, > 0 : vers le haut)
	bool firstPerson = false; // bras levés vers l'avant pour rester dans le champ de vision
	AvatarGesture gesture = AvatarGesture::None;
	float gestureTime = 0;	// temps écoulé depuis le début du geste (s)
	float swing = 0;		// frappe du bras droit : 0 → 1 (fin)
	bool holdingRight = false, holdingLeft = false;
};

[[nodiscard]] AvatarPose ComputeAvatarPose(const AvatarInput &in);
/// Interpolation linéaire composante par composante (t = 0 : a, t = 1 : b).
[[nodiscard]] AvatarPose BlendPose(const AvatarPose &a, const AvatarPose &b, float t);

// ── Géométrie ─────────────────────────────────────────────────────────────

/// Rectangle UV en texels {u0, v0, u1, v1} pour les faces +X, −X, +Y, −Y, +Z, −Z.
using AvatarFaceUv = std::array<std::array<float, 4>, 6>;

/// Boîte rattachée à une partie, en coordonnées locales au pivot de la partie.
struct AvatarBox {
	AvatarPart part = AvatarPart::Body;
	math::FVector3 min{}, max{};
	AvatarFaceUv uv{};
};

/// Pivot de la partie (repère du corps, posture de référence).
[[nodiscard]] math::FVector3 AvatarPivot(AvatarPart p) noexcept;
/// Boîte de la partie (skin 64×64) relative à son pivot ; `inflate` > 0 : couche d'armure.
[[nodiscard]] AvatarBox AvatarPartBox(AvatarPart p, float inflate = 0.f);
/// Point d'une partie (local au pivot) transformé dans le repère du corps par la posture.
[[nodiscard]] math::FVector3 AvatarTransform(const AvatarPose &pose, AvatarPart part, const math::FVector3 &local) noexcept;
/// Bout de la main (local au pivot du bras) : point d'attache des objets tenus.
[[nodiscard]] math::FVector3 AvatarHandAnchor() noexcept;
/// Position de l'œil (repère du corps) pour la posture donnée.
[[nodiscard]] math::FVector3 AvatarEye(const AvatarPose &pose) noexcept;

/// Sommet compatible avec `voxel::ModelVertex` : position, normale, UV normalisés.
struct AvatarVertex {
	float x, y, z, nx, ny, nz, u, v;
};

struct AvatarMeshOptions {
	bool head = true;					// faux en vue à la première personne
	float textureWidth = 64.f, textureHeight = 64.f; // texture de la skin (atlas éventuel)
	std::vector<AvatarBox> extras;		// objets tenus, armure (UV dans la même texture)
	float armThickness = 1.f;			// épaisseur des bras (< 1 : bras affinés à la première personne)
};

/// Triangles du corps posé (6 sommets par face) ajoutés à `out`.
void BuildAvatarMesh(const AvatarPose &pose, const AvatarMeshOptions &options, std::vector<AvatarVertex> &out);

/// Boîtes d'un objet tenu en main (cube de bloc ou plaque d'objet) attachées au bras.
/// `faces` : régions UV (texels) des faces latérales ; `top` : face du dessus.
[[nodiscard]] AvatarBox AvatarHeldBox(bool rightHand, bool flatItem, const std::array<float, 4> &side, const std::array<float, 4> &top);

/// Rig éditable de l'avatar du joueur (six parties : tête, torse, bras, jambes) —
/// même géométrie que l'avatar animé, exposée au Studio (forme et animations).
[[nodiscard]] Model BuildAvatarRig();

/// Noms des six parties standard d'un rig d'avatar, dans l'ordre `AvatarPart`.
inline constexpr std::array<std::string_view, kAvatarParts> kAvatarPartRigNames = {"head", "body", "arm_right", "arm_left", "leg_right", "leg_left"};

/// Triangles des boîtes attachées à l'avatar (objets tenus, armure), posées par la
/// posture d'un rig (`pose` : décalages par partie ; `rig` : pivots/rotations de repos).
/// Les rectangles UV (`AvatarBox::uv`, en texels) sont normalisés par `textureWidth`×`textureHeight`.
void BuildAvatarExtrasMesh(const Model &rig, std::span<const PartOffset> pose, std::span<const AvatarBox> boxes,
						   float textureWidth, float textureHeight, std::vector<ModelVertex> &out);

} // namespace voxel
