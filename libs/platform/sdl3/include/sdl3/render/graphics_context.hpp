#pragma once
/**
 * @file context.hpp
 * @brief Contexte graphique bâti uniquement sur le wrapper SDL3 :
 *        `sdl3::Window` + `sdl3::GpuDevice` (SDL_GPU, SPIR-V embarqué) +
 *        `sdl3::Renderer` créé **sur le même device GPU** pour la 2D.
 *
 * Image type :
 * ```
 * if (ctx.BeginFrame()) {
 *     voxels.Render(ctx, camera, params);  // passes 3D → texture de scène
 *     ctx.EndScene();                       // soumet le 3D, affiche la scène
 *     canvas.Begin(); …; canvas.End();      // UI 2D par le renderer SDL
 *     ctx.Present();
 * }
 * ```
 * Le rendu 3D s'effectue dans une texture couleur + profondeur hors écran ;
 * le renderer 2D l'affiche (sans copie, `Texture::FromGpuTexture`) puis dessine
 * l'interface par-dessus. Les deux flux de commandes sont soumis dans l'ordre
 * sur la même file GPU.
 */
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <math/math.hpp>
#include <sdl3/render/gpu.hpp>
#include <sdl3/render/render.hpp>
#include <sdl3/core/sdl_context.hpp>

namespace sdl3 {

struct GraphicsContextOptions {
	std::string title = "apps-lib";
	int width = 1280;
	int height = 720;
	bool vsync = true;
	bool resizable = true;
	bool debug = false; // couches de validation GPU
	bool hidden = false;
};

class GraphicsContext {
public:
	/// Crée la fenêtre, le device GPU et le renderer 2D. Journalise et retourne nullopt en cas d'échec.
	[[nodiscard]] static std::unique_ptr<GraphicsContext> Create(const GraphicsContextOptions &options);
	~GraphicsContext();
	GraphicsContext(const GraphicsContext &) = delete;
	GraphicsContext &operator=(const GraphicsContext &) = delete;

	/// Démarre une image : adapte les cibles à la taille de la fenêtre, acquiert
	/// un tampon de commandes. Faux si la fenêtre est réduite.
	bool BeginFrame();
	/// Soumet les commandes 3D et dessine la scène plein écran avec le renderer 2D.
	void EndScene();
	/// Présente l'image (renderer 2D).
	void Present();
	/// Capture l'image courante (avant Present) en PNG.
	bool SaveScreenshot(const std::string &path);

	[[nodiscard]] sdl3::Window &Window() noexcept { return m_window; }
	[[nodiscard]] sdl3::GpuDevice &Device() noexcept { return m_device; }
	[[nodiscard]] sdl3::Renderer &Renderer2D() noexcept { return m_renderer; }
	[[nodiscard]] sdl3::GpuCommandBuffer &Commands() noexcept { return m_cmd; }
	[[nodiscard]] sdl3::GpuTexture &SceneColor() noexcept { return m_sceneColor; }
	[[nodiscard]] sdl3::GpuTexture &SceneDepth() noexcept { return m_sceneDepth; }
	/// Cible du post-traitement (affichée par le renderer 2D après le 3D).
	[[nodiscard]] sdl3::GpuTexture &PostColor() noexcept { return m_postColor; }
	[[nodiscard]] SDL_GPUTextureFormat SceneColorFormat() const noexcept { return kColorFormat; }
	[[nodiscard]] SDL_GPUTextureFormat SceneDepthFormat() const noexcept { return m_depthFormat; }
	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] float Aspect() const noexcept { return m_height > 0 ? float(m_width) / float(m_height) : 1.f; }
	[[nodiscard]] const char *DriverName() const noexcept { return m_device.Driver(); }
	void SetVSync(bool on);
	/// Indique si la scène 3D doit être affichée (sinon, fond uni : menus).
	void SetSceneVisible(bool v) noexcept { m_sceneVisible = v; }

	static constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

private:
	GraphicsContext() = default;
	bool EnsureTargets();

	sdl3::SdlContext m_sdl;
	sdl3::Window m_window;
	sdl3::GpuDevice m_device;
	sdl3::Renderer m_renderer;
	sdl3::GpuTexture m_sceneColor;
	sdl3::GpuTexture m_sceneDepth;
	sdl3::GpuTexture m_postColor;			   // résultat du post-traitement
	std::optional<sdl3::Texture> m_sceneView; // vue 2D de la texture de scène
	std::optional<sdl3::Texture> m_postView;  // vue 2D du post-traitement
	sdl3::GpuCommandBuffer m_cmd;
	SDL_GPUTextureFormat m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	int m_width = 0, m_height = 0;
	bool m_frameOpen = false;
	bool m_sceneVisible = true;
};

} // namespace sdl3
