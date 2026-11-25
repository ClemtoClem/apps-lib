#pragma once
/**
 * @file world.hpp
 * @brief Physique de jeu : corps mobiles (boîtes alignées sur les axes, sphères) contre un décor
 *        de triangles statique et des maillages cinématiques qui suivent une transformation.
 *
 * Volontairement simple et robuste :
 *  - décor statique : soupe de triangles rangée dans une grille de hachage ;
 *  - maillages cinématiques (`AddMesh`) : triangles dans leur repère local, placés par une
 *    matrice que l'application change à chaque image (porte qui tourne, plateau qui monte) ;
 *    les corps les heurtent avec leur forme exacte et sont repoussés quand ils bougent ;
 *  - corps sans rotation : boîte (test des axes séparateurs contre un triangle, 13 axes) ou
 *    sphère (point le plus proche) ; sortie le long de l'axe de moindre pénétration ;
 *  - corps contre corps : séparation selon les masses et impulsion avec rebond ; seules les
 *    paires dont les boîtes se recouvrent sont testées (balayage trié sur X) ;
 *  - gravité modulée par corps (`gravityScale` : flottabilité) et frein du milieu (`drag`) ;
 *  - frottement au sol, marches franchies sans sauter (`stepHeight`) ;
 *  - pas fixe (`Step`) avec sous-pas : pas d'effet tunnel pour les vitesses de jeu ;
 *  - contacts : liste par corps (`touching`) et évènements début / fin du dernier pas.
 *
 * Les propriétaires (`UserId`) relient corps et triangles aux objets de l'application
 * (groupe d'un document, entité ECS : `Entity::Bits()`) ; `kWorld` : décor sans propriétaire.
 */
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <math/math.hpp>

namespace physics {

using UserId = uint64_t;
inline constexpr UserId kWorld = 0;
using BodyId = int;		///< rang d'un corps (-1 : aucun)
using ColliderId = int; ///< rang d'un maillage cinématique (-1 : aucun)

enum class Shape : uint8_t { Box, Sphere };

struct Body {
	math::FVector3 center;
	math::FVector3 half{0.5f, 0.5f, 0.5f}; ///< demi-dimensions (boîte)
	math::FVector3 velocity{};
	Shape shape = Shape::Box;
	float radius = 0.5f; ///< sphère
	float mass = 1.f;
	float bounce = 0.2f;
	float friction = 0.6f;
	float stepHeight = 0.f; ///< > 0 : franchit les marches de cette hauteur
	float gravityScale = 1.f; ///< part de la gravité subie (0 : flottabilité neutre, poisson)
	float drag = 0.f;		  ///< frein linéaire du milieu (1/s, eau ≈ 1–3) : v ← v·e^(−drag·dt)
	bool dynamic = true;	///< faux : immobile (placé par l'application), repousse les autres
	bool ghost = false;		///< traversable : signale seulement ses contacts
	bool enabled = true;	///< faux : ignoré (ni simulé, ni touché)
	UserId user = kWorld;
	// Résultats du dernier pas.
	bool grounded = false;
	std::vector<UserId> touching; ///< propriétaires touchés (kWorld : décor sans propriétaire)

	/// Sphère de rayon `r` centrée en `c`.
	[[nodiscard]] static Body MakeSphere(const math::FVector3 &c, float r) {
		Body b;
		b.center = c;
		b.shape = Shape::Sphere;
		b.radius = r;
		b.half = math::FVector3(r);
		return b;
	}
	/// Demi-dimensions de la boîte englobante (forme comprise).
	[[nodiscard]] math::FVector3 Extent() const noexcept { return shape == Shape::Sphere ? math::FVector3(radius) : half; }
	[[nodiscard]] math::FAABB Bounds() const noexcept { return {center - Extent(), center + Extent()}; }
};

struct Hit {
	float distance = 0.f;
	math::FVector3 point, normal;
	UserId user = kWorld;
	BodyId body = -1;		  ///< corps touché (-1 : décor ou maillage)
	ColliderId collider = -1; ///< maillage cinématique touché
};

/// Ce qu'un rayon peut toucher (`World::Raycast`).
struct RayFilter {
	bool bodies = true;			 ///< faux : décor et maillages seulement (sonde du sol)
	bool dynamicBodies = true;	 ///< faux : corps immobiles seulement
	BodyId ignoreBody = -1;		 ///< corps ignoré
	UserId ignoreUser = kWorld;	 ///< propriétaire ignoré (tous ses corps et triangles), kWorld : aucun
};

/// Début ou fin d'un contact entre le corps de `self` et `other` (corps, maillage ou décor).
struct ContactEvent {
	UserId self = kWorld, other = kWorld;
	BodyId body = -1;
	bool began = true;
};

class World {
public:
	explicit World(float cellSize = 2.f) : m_cell(cellSize) {}

	// ── Décor statique ───────────────────────────────────────────────────
	/// Triangles (trois sommets par triangle, repère du monde).
	void AddTriangles(std::span<const math::FVector3> vertices, UserId user = kWorld);
	/// Retire tous les triangles appartenant à `user` (compaction différée, grille reconstruite).
	void RemoveTriangles(UserId user);
	[[nodiscard]] size_t TriangleCount() const noexcept { return m_triangles.size() - m_freeTriangles.size(); }

	// ── Maillages cinématiques ───────────────────────────────────────────
	/// Triangles dans le repère local du maillage, placés par `transform`.
	ColliderId AddMesh(std::span<const math::FVector3> localVertices, const math::FMatrix4 &transform, UserId user);
	/// Retire un maillage cinématique (le slot est réutilisé par le prochain `AddMesh`).
	void RemoveMesh(ColliderId id);
	void SetTransform(ColliderId id, const math::FMatrix4 &transform);
	[[nodiscard]] const math::FMatrix4 &TransformOf(ColliderId id) const { return m_meshes[size_t(id)].transform; }
	/// Boîte englobante du maillage placé (repère du monde).
	[[nodiscard]] const math::FAABB &MeshBounds(ColliderId id) const { return m_meshes[size_t(id)].bounds; }
	/// Désactivé : traversable et invisible aux rayons (groupe caché).
	void SetMeshEnabled(ColliderId id, bool enabled) { m_meshes[size_t(id)].enabled = enabled; }
	[[nodiscard]] size_t MeshCount() const noexcept { return m_meshes.size(); }

	// ── Corps ────────────────────────────────────────────────────────────
	BodyId AddBody(const Body &body);
	/// Retire un corps (désactivé ; le slot est réutilisé par le prochain `AddBody`).
	void RemoveBody(BodyId id);
	[[nodiscard]] Body &GetBody(BodyId id) { return m_bodies[size_t(id)]; }
	[[nodiscard]] const Body &GetBody(BodyId id) const { return m_bodies[size_t(id)]; }
	[[nodiscard]] size_t BodyCount() const noexcept { return m_bodies.size(); }

	/// Avance d'un pas : gravité, déplacement, contacts, rebond, frottement, évènements.
	void Step(float dt, const math::FVector3 &gravity);
	/// Contacts commencés ou terminés pendant le dernier pas.
	[[nodiscard]] std::span<const ContactEvent> Events() const noexcept { return m_events; }
	/// Le corps, placé tel quel, recouvre-t-il le décor, un maillage actif ou un corps actif non
	/// fantôme (sauf `ignoreBody` ; corps dynamiques seulement si `dynamicBodies`), ou son centre
	/// est-il enfermé dans un solide (`InsideSolid`) ? Sert à choisir une place libre.
	[[nodiscard]] bool Blocked(const Body &body, BodyId ignoreBody = -1, bool dynamicBodies = true) const;
	/// Parité des triangles traversés par un rayon montant depuis `point` : impair → le point
	/// est dans un volume fermé (bloc, coque) ou sous une surface ouverte (relief).
	[[nodiscard]] bool InsideSolid(const math::FVector3 &point) const;
	/// Premier obstacle le long du rayon (`ray.dir` normé) : décor, maillages et corps non
	/// fantômes, sauf `ignoreBody`.
	[[nodiscard]] std::optional<Hit> Raycast(const math::FRay &ray, float maxDistance, BodyId ignoreBody = -1) const;
	/// Variante filtrée (ex. sonder le relief sous les pattes d'un animal sans toucher les autres).
	[[nodiscard]] std::optional<Hit> Raycast(const math::FRay &ray, float maxDistance, const RayFilter &filter) const;

private:
	struct Triangle {
		math::FVector3 a, b, c;
		math::FAABB bounds;
		UserId user;
		bool dead = false; ///< retiré (slot réutilisable)
	};
	struct Mesh {
		std::vector<math::FVector3> local;
		std::vector<Triangle> placed; ///< triangles dans le repère du monde
		math::FMatrix4 transform;
		math::FAABB bounds{};
		UserId user = kWorld;
		bool enabled = true;
	};
	struct Contact {
		math::FVector3 push; ///< sortie du corps
		UserId user;
	};
	[[nodiscard]] int64_t CellCoord(float v) const noexcept;
	[[nodiscard]] static uint64_t CellKey(int64_t x, int64_t y, int64_t z) noexcept;
	/// Triangles du décor dont la boîte recouvre `box` (sans doublon).
	void Query(const math::FAABB &box, std::vector<uint32_t> &out) const;
	/// Contact le plus profond de `body` avec le décor et les maillages.
	[[nodiscard]] std::optional<Contact> Deepest(const Body &body) const;
	/// Sort `body` du décor ; `normals` : normales de contact.
	bool ResolveWorld(Body &body, std::vector<math::FVector3> &normals);
	void ResolveBodies();
	/// Déplacement par sous-pas (`maxStep` > 0 : longueur maximale d'un sous-pas).
	void Move(Body &body, const math::FVector3 &delta, std::vector<math::FVector3> &normals, float maxStep = 0.f);
	void StepUp(Body &body, const math::FVector3 &start, const math::FVector3 &delta, std::vector<math::FVector3> &normals);
	void CollectEvents();

	float m_cell;
	std::vector<Triangle> m_triangles;
	std::unordered_map<uint64_t, std::vector<uint32_t>> m_grid;
	std::vector<Mesh> m_meshes;
	std::vector<Body> m_bodies;
	std::vector<BodyId> m_freeBodies;		 ///< slots de corps réutilisables
	std::vector<uint32_t> m_freeTriangles; ///< slots de triangles réutilisables
	std::vector<ColliderId> m_freeMeshes;	 ///< slots de maillages réutilisables
	std::vector<std::pair<BodyId, UserId>> m_contacts; ///< contacts du pas précédent (triés)
	std::vector<ContactEvent> m_events;
	mutable std::vector<std::pair<BodyId, UserId>> m_nowScratch, m_began, m_ended; ///< scratch de CollectEvents
	mutable std::vector<uint32_t> m_scratch, m_stamp;
	std::vector<uint32_t> m_sweep; ///< corps actifs triés par abscisse minimale (ResolveBodies)
	float m_topY = -1e30f;		   ///< sommet du décor statique (rayon de InsideSolid borné)
	mutable uint32_t m_query = 0;
};

// ── Tests de contact (utilisables seuls) ─────────────────────────────────────

/// Boîte / triangle : vecteur qui sort la boîte du triangle (axe de moindre pénétration).
[[nodiscard]] std::optional<math::FVector3> BoxTriangle(const math::FVector3 &center, const math::FVector3 &half, const math::FVector3 &a,
														const math::FVector3 &b, const math::FVector3 &c);
/// Sphère / triangle : vecteur qui sort la sphère du triangle.
[[nodiscard]] std::optional<math::FVector3> SphereTriangle(const math::FVector3 &center, float radius, const math::FVector3 &a, const math::FVector3 &b,
														   const math::FVector3 &c);
/// Point du triangle le plus proche de `p`.
[[nodiscard]] math::FVector3 ClosestOnTriangle(const math::FVector3 &p, const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c);
/// Corps / corps : normale (de `a` vers `b`) et profondeur du recouvrement.
[[nodiscard]] std::optional<std::pair<math::FVector3, float>> BodyOverlap(const Body &a, const Body &b);
/// Distance du rayon à un triangle (deux faces).
[[nodiscard]] std::optional<float> RayTriangle(const math::FRay &ray, const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c);

} // namespace physics
