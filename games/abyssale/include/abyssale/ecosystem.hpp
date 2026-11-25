#pragma once
/**
 * @file ecosystem.hpp
 * @brief Monde vivant d'Abyssale : chaque objet est une entité (foundation.ecs) dotée d'un
 *        volume de collision (engine.physics), et chaque être vivant agit seul.
 *
 * L'écosystème couvre tous les chunks chargés : chaque chunk est peuplé par `SpawnChunk`
 * (décor statique + faune), ses entités portent un `ChunkTag`, et il est déchargé par
 * `UnloadChunk` (la faune est sérialisée en `EntityRecord` avant d'être retirée).
 */
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <ecs/ecs.hpp>
#include <math/math.hpp>
#include <physics/ecs.hpp>
#include <sdl3/render/renderer3d.hpp>

#include "abyssale/config.hpp"
#include "abyssale/model.hpp"
#include "abyssale/terrain.hpp"
#include "abyssale/world.hpp"

namespace abyssale {

namespace detail {
struct EcoState; ///< état partagé des systèmes (ressource du monde ECS)
}

// ── Composants ───────────────────────────────────────────────────────────────

/// Apparence : catégorie de rendu et instance (matrice, teinte).
struct Render {
	Model model = Model::Terrain;
	sdl3::MeshInstance instance{};
};

/// Entité immobile : son instance est mise en cache une fois pour toutes.
struct Static {};

/// Appartenance à un chunk (cx, cz encodés en clé de Morton).
struct ChunkTag {
	uint64_t key = 0;
};

/// Corps immobiles qui épousent la forme de l'entité (points du repère local) : sphères le
/// long d'une tige (suivant l'ondulation si la catégorie ondule), bouts d'ailes d'une raie.
struct Parts {
	static constexpr size_t kMax = 4;
	std::array<physics::BodyId, kMax> body{-1, -1, -1, -1};
	std::array<math::FVector3, kMax> local{};
	uint8_t count = 0;
};

/// Trajectoire circulaire : autour d'un lieu (altitude relative au fond, rayon ondulant) ou
/// passant par un point précis (altitude absolue, rayon constant).
struct Orbit {
	math::FVector3 center;
	float radius = 10.f, height = 8.f, speed = 0.05f, phase = 0.f;
	bool terrainRelative = true;
	[[nodiscard]] math::FVector3 At(const Terrain &terrain, float angle) const noexcept;
	/// Orbite qui passe par `through` à l'instant `time` (centre au-dessus de `pivot`).
	[[nodiscard]] static Orbit Through(const math::FVector3 &pivot, const math::FVector3 &through, float speed, float time);
};

/// Plante qui plie sous ce qui la traverse puis se redresse (ressort amorti) : flexion au
/// niveau `reach` (m au-dessus du pied), appliquée comme une inclinaison autour du pied.
struct Flexible {
	math::FMatrix4 rest = math::FMatrix4::Identity(); ///< pose au repos
	math::FVector3 base;							   ///< pied de la plante
	float reach = 10.f;								   ///< hauteur de référence de la flexion (m)
	math::FVector2 bend{}, velocity{};				   ///< déplacement à `reach` (x, z) et sa vitesse
};

/// Poisson d'un banc : place dans la formation, panique (0 calme, 1 fuite), cap affiché,
/// temps passé sans pouvoir avancer.
struct SchoolMember {
	uint32_t school = 0;
	math::FVector3 slot;
	float phase = 0.f, scale = 1.f, panic = 0.f;
	math::FVector3 heading{1.f, 0.f, 0.f};
	float stuck = 0.f;
};

/// Raie : grande boucle, ajustée au relief (hauteur, tangage, roulis lissés).
struct Cruiser {
	Orbit orbit;
	float scale = 3.2f;
	float span = 3.f;		  ///< demi-envergure (m)
	float lift = 0.f;		  ///< hauteur ajoutée à la trajectoire pour passer au-dessus du relief (m)
	float pitch = 0.f, roll = 0.f; ///< inclinaisons lissées (rad ; roulis > 0 : aile droite basse)
	bool posed = false;		  ///< première pose faite (sinon : valeurs prises d'emblée)
};

/// Marcheur du fond (crabe : de côté ; homard : droit devant), rappelé vers son terrier.
struct Walker {
	math::FVector3 home;
	float heading = 0.f, speed = 0.2f, phase = 0.f, range = 8.f;
	bool sideways = true;
	float foot = 0.f; ///< du centre du corps à l'origine du maillage (m)
	math::FVector3 last{}; ///< position avant le pas précédent (progrès réel)
	float stuck = 0.f;	   ///< temps passé sans avancer (s)
	std::array<math::FVector3, 8> feet{}; ///< pointes des pattes (repère local du maillage)
	uint8_t footCount = 0;
	math::FVector3 up{0.f, 1.f, 0.f}; ///< normale lissée du plan d'appui
	float height = 0.f;				  ///< hauteur lissée du repère du maillage
	bool posed = false;
};

/// Crevette : vol stationnaire autour d'un point, dards (et fuite devant le plongeur).
struct Hoverer {
	math::FVector3 home;
	float phase = 0.f, dartTimer = 1.f;
	math::FVector3 dart{};
};

/// Anémone : disque oral qui respire.
struct Pulsing {
	math::FVector3 base;
	float phase = 0.f, scale = 1.f;
};

/// Ver tubicole : couronne rétractée quand le plongeur s'approche.
struct Retracting {
	math::FVector3 base;
	float phase = 0.f, deploy = 1.f; ///< 0 rétractée, 1 déployée
};

/// Bulle : monte, ondule, renaît à sa source (souffle du plongeur : disparaît en surface).
struct BubbleState {
	math::FVector3 pos;
	float speed = 1.f, size = 0.04f, phase = 0.f;
	int vent = -1; ///< source du fond ; −1 : souffle du plongeur ou flotteur d'une plante
	bool alive = true;
	ecs::Entity source = ecs::kNull; ///< plante émettrice (flotteur au point `local`)
	math::FVector3 local{};
	uint64_t chunk = 0; ///< chunk de la source du fond
};

/// Le plongeur.
struct Player {};

// ── Formes de collision ──────────────────────────────────────────────────────

struct Shapes {
	std::array<math::FAABB, kModelCount> bounds{};		///< boîte locale du maillage de chaque catégorie
	std::vector<math::FVector3> arch, wreck, fluke;		///< triangles locaux
	std::array<std::vector<math::FVector3>, 3> rocks;	///< triangles locaux (maillages simplifiés)
};

/// Triangles (trois sommets chacun) d'un maillage.
[[nodiscard]] std::vector<math::FVector3> TriangleSoup(const sdl3::MeshData &mesh);
/// Formes tirées des maillages du catalogue (`models[m]` : maillage de la catégorie m).
[[nodiscard]] Shapes MakeShapes(std::span<const sdl3::MeshData> models);

// ── Persistance des entités mobiles ──────────────────────────────────────────

/// Type d'entité mobile sérialisée (poissons, raies, crustacés, crevettes).
enum class EntityKind : uint8_t { Fish, Manta, Crab, Lobster, Shrimp };

/// État complet d'une entité mobile, dans un format binaire compact et stable.
struct EntityRecord {
	EntityKind kind = EntityKind::Fish;
	math::FVector3 position{}, velocity{}, anchor{}, headingVec{};
	float heading = 0.f, scale = 1.f, phase = 0.f;
	float radius = 0.f, height = 0.f, speed = 0.f;
	float lift = 0.f, pitch = 0.f, roll = 0.f;
	float range = 0.f;
	uint32_t school = 0;
	uint32_t flags = 0; ///< bit0 : terrainRelative (raie) ; bit1 : sideways (crabe) ; bit2 : posed
};

[[nodiscard]] std::vector<uint8_t> SerializeRecords(std::span<const EntityRecord> records);
[[nodiscard]] std::vector<EntityRecord> DeserializeRecords(std::span<const uint8_t> bytes);

// ── Écosystème ───────────────────────────────────────────────────────────────

class Ecosystem {
public:
	/// Prépare le monde ECS, la physique, l'ordonnanceur et le plongeur.
	Ecosystem(const Terrain &terrain, const Shapes &shapes, const math::FVector3 &diverStart, float startTime, uint64_t seed = 0x11FE);
	~Ecosystem();
	Ecosystem(const Ecosystem &) = delete;
	Ecosystem &operator=(const Ecosystem &) = delete;

	/// Peuple le chunk `key` (décor + faune). `saved` non vide : la faune est restaurée
	/// depuis la sauvegarde au lieu d'être régénérée.
	void SpawnChunk(uint64_t key, const World &world, const Quality &quality, uint64_t chunkSeed, std::span<const EntityRecord> saved = {});
	/// Sérialise la faune mobile du chunk `key` (pour la sauvegarde).
	[[nodiscard]] std::vector<EntityRecord> CaptureChunk(uint64_t key);
	/// Décharge le chunk `key` : retire décor, corps et faune.
	void UnloadChunk(uint64_t key);

	// ── Plongeur ─────────────────────────────────────────────────────────
	void SteerDiver(const math::FVector3 &wish, float dt);
	void PlaceDiver(const math::FVector3 &position);
	void SetDiverPhysics(bool enabled);
	[[nodiscard]] math::FVector3 DiverPosition() const;
	[[nodiscard]] math::FVector3 DiverVelocity() const;

	/// Avance d'un pas ; `forward` : regard du plongeur ; `exhale` : bouffée de bulles.
	void Update(float dt, float time, const math::FVector3 &forward, bool exhale);

	/// Instances à dessiner de la catégorie.
	[[nodiscard]] const std::vector<sdl3::MeshInstance> &Instances(Model model) const noexcept { return m_buckets[size_t(model)]; }
	/// Bulles à quelques mètres du plongeur (pour le son).
	[[nodiscard]] int NearbyBubbles() const noexcept;

	[[nodiscard]] ecs::World &Entities() noexcept { return m_ecs; }
	[[nodiscard]] physics::World &Physics() noexcept { return *m_physics; }
	[[nodiscard]] const physics::World &Physics() const noexcept { return *m_physics; }
	[[nodiscard]] ecs::Entity PlayerEntity() const noexcept { return m_player; }
	[[nodiscard]] std::span<const ecs::Schedule::SystemStats> Timings() const noexcept { return m_schedule.Stats(); }

private:
	void SpawnScenery(uint64_t key, const World &world);
	void SpawnLife(uint64_t key, const World &world, const Quality &quality, uint64_t chunkSeed, std::span<const EntityRecord> saved);
	void CollectInstances(bool statics);

	const Terrain &m_terrain;
	const Shapes &m_shapes;
	ecs::World m_ecs;
	ecs::Schedule m_schedule;
	detail::EcoState *m_state = nullptr;   ///< ressource de m_ecs
	physics::World *m_physics = nullptr; ///< ressource de m_ecs
	ecs::Entity m_player = ecs::kNull;
	physics::BodyId m_playerBody = -1;
	std::map<uint64_t, ecs::Entity> m_chunkGround; ///< entité portant les triangles du décor
	std::array<std::vector<sdl3::MeshInstance>, kModelCount> m_buckets;
	std::array<bool, kModelCount> m_dynamic{}; ///< catégorie recalculée à chaque image
};

} // namespace abyssale
