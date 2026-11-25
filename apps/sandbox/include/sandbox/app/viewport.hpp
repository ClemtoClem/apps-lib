#pragma once
/**
 * @file viewport.hpp
 * @brief Vue 3D du sandbox : document → maillages sdl3 (un par matériau et par modèle,
 *        reconstruits quand la géométrie change), arêtes, axes, sélection, aperçus d'outil.
 *
 * Style « SketchUp » : faces mates des deux côtés, arêtes sombres, axes rouge / bleu (vertical)
 * / vert pleins du côté positif et pointillés du côté négatif, ciel clair ; ombres et rayons X
 * facultatifs. Pendant l'édition d'un groupe, le reste du modèle est estompé.
 */
#include <memory>
#include <unordered_map>
#include <vector>

#include <sdl3/render/render3d.hpp>
#include <sdl3/render/gpu.hpp>
#include <sdl3/render/render.hpp>

#include "sandbox/editor/editor.hpp"
#include "sandbox/game/session.hpp"

namespace sandbox {

/// Niveau de qualité du rendu : post-traitement (bloom, rayons, saturation, vignettage).
enum class RenderQuality : uint8_t { Low, Medium, High, Ultra };

/// Réglages de post-traitement d'un niveau de qualité (les valeurs par défaut = Élevé).
[[nodiscard]] inline sdl3::Post PostFor(RenderQuality quality) noexcept {
	sdl3::Post p;
	switch (quality) {
		case RenderQuality::Low:
			p.bloomStrength = 0.f;
			p.raysStrength = 0.f;
			p.saturation = 1.0f;
			p.vignette = 0.f;
			break;
		case RenderQuality::Medium:
			p.bloomStrength = 0.2f;
			p.raysStrength = 0.f;
			p.vignette = 0.2f;
			break;
		case RenderQuality::High:
			break; // valeurs par défaut
		case RenderQuality::Ultra:
			p.bloomStrength = 0.6f;
			p.raysStrength = 0.85f;
			p.saturation = 1.2f;
			p.vignette = 0.45f;
			break;
	}
	return p;
}

struct DisplayOptions {
	bool edges = true;
	bool axes = true;
	bool shadows = false;
	bool sky = true;
	bool xray = false; ///< faces translucides
	bool lamps = true; ///< lampes des groupes visibles aussi en édition
	RenderQuality quality = RenderQuality::High; ///< post-traitement (bloom, rayons…)
};

class Viewport {
public:
	bool Init(sdl3::GpuDevice &device);
	/// Rend le document dans une cible `width`×`height` (pixels) ; à appeler dans OnOffscreen.
	/// `game` : partie en cours (caméra du joueur, groupes déplacés, lumières), sans aides d'édition.
	void Render(Editor &editor, sdl3::Renderer &view2d, int width, int height, const DisplayOptions &options, const game::GameSession *game = nullptr);
	[[nodiscard]] sdl3::Texture *View() noexcept { return m_target.View(); }
	[[nodiscard]] float Aspect() const noexcept { return m_target.Aspect(); }
	[[nodiscard]] const sdl3::RenderStats &Stats() const noexcept { return m_r3d->Stats(); }

private:
	struct Part {
		sdl3::MeshId mesh = 0;
		uint32_t material = 0;
	};
	struct Cache {
		uint64_t revision = ~uint64_t(0);
		std::vector<Part> parts;
		std::vector<std::pair<modeling::EdgeId, std::pair<modeling::Vec3, modeling::Vec3>>> edges;
	};
	void Sync(const modeling::Model &model, Cache &cache);
	void DrawModel(const Editor &editor, const modeling::Model &model, Cache &cache, const math::FMatrix4 &transform, bool active, float opacity,
				   const DisplayOptions &options);
	void DrawSelection(Editor &editor);
	void DrawAxes();
	void DrawPreview(const Editor &editor);
	[[nodiscard]] sdl3::Material MaterialFor(const Document &doc, uint32_t index, float opacity) const;

	std::unique_ptr<sdl3::Renderer3D> m_r3d;
	sdl3::RenderTarget3D m_target;
	std::unordered_map<EntityId, Cache> m_cache;
	uint64_t m_docRevision = 0;
	sdl3::MeshId m_selectionMesh = 0;
	uint64_t m_selectionKey = 0;
};

} // namespace sandbox
