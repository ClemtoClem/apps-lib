#pragma once
/**
 * @file viewport3d.hpp
 * @brief Viewport 3D ancrable : rend la scène voxel (chunks + boîtes d'entités)
 *        vers une texture et l'affiche dans son rectangle, avec une caméra
 *        orbitale, un gizmo de translation/rotation et le picking d'entités.
 *        Widget du module `ui`.
 *
 * Picking : `Draw` arme `PickRequested()` lors d'un clic hors gizmo ; l'appelant
 * calcule le rayon (`RayOrigin`/`RayDirection`), choisit l'entité et appelle
 * `SetSelection`/`ClearSelection`. Pendant `Dragging()`, `Selection()` (et
 * `SelectionYaw()`) donne la position/orientation à appliquer à l'entité.
 */
#include <optional>
#include <span>
#include <vector>

#include <sdl3/render/canvas.hpp>
#include <voxel/renderer.hpp>
#include <sdl3/render/gpu.hpp>
#include <sdl3/render/render.hpp>

namespace ui {

class Context;

class Viewport3D {
public:
	enum class Gizmo : uint8_t { Translate, Rotate };

	Viewport3D();

	/// (Re)crée les cibles couleur/profondeur et la vue 2D pour `ren2d`.
	void Resize(sdl3::GpuDevice &dev, sdl3::Renderer &ren2d, int w, int h, SDL_GPUTextureFormat depthFormat);

	/// Rend la scène (chunks + `entityBoxes` + gizmo) dans la texture.
	void Render(sdl3::GpuDevice &dev, voxel::Renderer &renderer, const voxel::SceneParams &params,
				std::span<const voxel::BoxInstance> entityBoxes, const voxel::BoxInstance *outline);

	/// Affiche la texture dans `area`, gère la caméra, le gizmo et le picking.
	void Draw(Context &ctx, const sdl3::FRect &area);

	// ── Caméra ────────────────────────────────────────────────────────────
	[[nodiscard]] voxel::Camera &Camera() noexcept { return m_camera; }
	[[nodiscard]] const voxel::Camera &Camera() const noexcept { return m_camera; }
	void SetTarget(const math::FVector3 &t) noexcept { m_target = t; }
	[[nodiscard]] const math::FVector3 &Target() const noexcept { return m_target; }
	[[nodiscard]] bool Ready() const noexcept { return m_color && m_view.has_value(); }

	// ── Sélection & gizmo ─────────────────────────────────────────────────
	void SetSelection(const math::FVector3 &pos) noexcept {
		m_selPos = pos;
		m_hasSelection = true;
	}
	void ClearSelection() noexcept {
		m_hasSelection = false;
		m_dragAxis = -1;
	}
	[[nodiscard]] bool HasSelection() const noexcept { return m_hasSelection; }
	[[nodiscard]] const math::FVector3 &Selection() const noexcept { return m_selPos; }
	void SetSelectionYaw(float yaw) noexcept { m_selYaw = yaw; }
	[[nodiscard]] float SelectionYaw() const noexcept { return m_selYaw; }
	void SetGizmo(Gizmo g) noexcept { m_gizmo = g; }
	[[nodiscard]] Gizmo GetGizmo() const noexcept { return m_gizmo; }
	/// Le gizmo est en train d'être glissé (l'appelant applique Selection() à l'entité).
	[[nodiscard]] bool Dragging() const noexcept { return m_dragAxis >= 0; }

	// ── Picking ───────────────────────────────────────────────────────────
	/// Vrai si un clic « de sélection » a eu lieu cette image (hors gizmo).
	[[nodiscard]] bool PickRequested() const noexcept { return m_pickRequested; }
	void ClearPickRequest() noexcept { m_pickRequested = false; }
	[[nodiscard]] math::FVector3 RayOrigin() const noexcept { return m_camera.position; }
	/// Rayon sous le curseur (coordonnées monde), pour le raycast de l'appelant.
	[[nodiscard]] math::FVector3 RayDirection(float mouseX, float mouseY, const sdl3::FRect &area) const noexcept;

private:
	void SyncCamera() noexcept;
	[[nodiscard]] float GizmoScale() const noexcept { return m_distance * 0.16f; }
	void BuildGizmo(std::vector<voxel::BoxInstance> &out) const noexcept;
	[[nodiscard]] int PickGizmoAxis(const math::FVector3 &o, const math::FVector3 &d) const noexcept;
	[[nodiscard]] float AxisParam(const math::FVector3 &o, const math::FVector3 &d, int axis) const noexcept;

	voxel::Camera m_camera;
	math::FVector3 m_target{0, 40, 0};
	float m_distance = 24.f;
	sdl3::GpuTexture m_color, m_depth;
	std::optional<sdl3::Texture> m_view;
	int m_width = 0, m_height = 0;
	float m_lastX = 0, m_lastY = 0;
	bool m_captured = false;

	// Sélection + gizmo.
	math::FVector3 m_selPos{};
	float m_selYaw = 0.f;
	bool m_hasSelection = false;
	Gizmo m_gizmo = Gizmo::Translate;
	int m_dragAxis = -1; // -1 aucun ; 0/1/2 = X/Y/Z ; 3 = rotation (axe Y)
	math::FVector3 m_dragStartPos{};
	float m_dragStartParam = 0.f;
	float m_dragStartYaw = 0.f;
	float m_dragStartMouseX = 0.f;
	bool m_pickRequested = false;
	std::vector<voxel::BoxInstance> m_boxes; // entités + gizmo (réutilisé)
};

} // namespace ui
