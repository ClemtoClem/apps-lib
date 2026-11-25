#pragma once
// Voxelia — phase 5 : éditeur du monde en direct (« Live World Editor »).
//
//  - Caméra libre : le mode éditeur (F4 dans la fenêtre du jeu) détache la caméra du joueur
//    (vol X/Y/Z : ZQSD/WASD, Espace / E monter, Q / Maj descendre, Ctrl rapide ; regard au clic
//    droit maintenu ; molette : vitesse). Le joueur reste immobile, le streaming suit la caméra.
//  - Gizmo de transformation (dessin 2D projeté) : flèches X/Y/Z (translation, un axe à la
//    fois), cercle de cap (rotation autour de Y) ; accroche 1/16 bloc et 15°, Ctrl : libre.
//  - Superpositions de débogage partagées par le jeu et le Studio : boîtes AABB des entités,
//    vecteurs vitesse, bordures des chunks, portées et rayons des lumières, joints.
#include <optional>
#include <span>
#include <vector>

#include <sdl3/render/canvas.hpp>
#include <ui/ui.hpp>

#include "app.hpp"

namespace voxelia {

struct DebugView {
	bool boxes = true;	  // boîtes de collision des entités (AABB)
	bool velocity = true; // vecteurs vitesse
	bool chunks = false;  // bordures des chunks (couleur : modifié / généré)
	bool lights = true;	  // portée et rayons des lumières dynamiques
	bool joints = true;	  // joints (ancres, liens)
	float range = 40.f;	  // distance d'affichage (blocs)
};

struct DebugLine {
	math::FVector3 a, b;
	sdl3::Color color;
	float width = 1.5f;
};

/// Lignes de débogage autour de `eye` (entité `selected` en évidence).
void CollectDebugLines(Game &game, const DebugView &view, const math::FVector3 &eye, ecs::Entity selected, std::vector<DebugLine> &out);

/// Projection monde → écran d'une caméra dans le rectangle `rect`.
struct Projector {
	sdl3::FRect rect;
	voxel::Camera camera;
	math::FVector3 forward, right, up;
	float tanF = 1, aspect = 1;
	[[nodiscard]] static Projector From(const voxel::Camera &camera, const sdl3::FRect &rect);
	bool Point(const math::FVector3 &p, float &x, float &y) const;
	/// Segment découpé au plan proche ; faux s'il est entièrement derrière la caméra.
	bool Segment(math::FVector3 a, math::FVector3 b, float &x0, float &y0, float &x1, float &y1) const;
	/// Direction du rayon sous le point d'écran (x, y).
	[[nodiscard]] math::FVector3 Ray(float x, float y) const;
};

void DrawDebugLines(sdl3::Canvas &c, const Projector &proj, std::span<const DebugLine> lines);

/// Entité dont la boîte est traversée en premier par le rayon (`skip` ignorée).
[[nodiscard]] ecs::Entity PickEntity(Game &game, const math::FVector3 &origin, const math::FVector3 &dir, ecs::Entity skip = {});

/// Gizmo de transformation dessiné en 2D par-dessus une vue 3D.
class TransformGizmo {
public:
	enum class Mode : uint8_t { Translate, Rotate };
	Mode mode = Mode::Translate;
	bool snap = true;

	/// Survol / glissé : modifie `position` et `yaw` ; vrai si l'entrée a été prise par le gizmo.
	bool Interact(ui::Context &u, const Projector &proj, math::FVector3 &position, float &yaw);
	void Draw(sdl3::Canvas &c, const Projector &proj, const math::FVector3 &position, float yaw) const;
	[[nodiscard]] bool Dragging() const noexcept { return m_axis >= 0; }
	[[nodiscard]] bool Hovered() const noexcept { return m_hover >= 0; }

private:
	[[nodiscard]] float Length(const Projector &proj, const math::FVector3 &p) const;
	int m_axis = -1, m_hover = -1; // 0..2 : X/Y/Z ; 3 : cap
	float m_startX = 0, m_startY = 0, m_startAngle = 0, m_startYaw = 0;
	math::FVector3 m_startPos{};
	math::FVector2 m_axisScreen{};
};

/// Mode éditeur de la fenêtre du jeu.
class InGameEditor {
public:
	bool active = false;
	DebugView view;
	TransformGizmo gizmo;
	ecs::Entity selected;
	std::optional<voxel::RayHit> block; // bloc visé au dernier clic
	float speed = 12.f;					 // m/s
	voxel::Camera camera;

	void Toggle(Game &game);
	/// Déplacement de la caméra libre (entrées du joueur détournées).
	void Update(Game &game, const PlayerInput &in, float dt, bool fast, float wheel);
	/// Superpositions, gizmo, sélection au clic, panneau des options.
	void Draw(ui::Context &u, Game &game);
};

} // namespace voxelia
