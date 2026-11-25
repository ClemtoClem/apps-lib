#pragma once
/**
 * @file application.hpp
 * @brief Cycle de vie d'une application graphique : SDL, fenêtre, rendu 2D, polices,
 *        canvas, interface immédiate, boucle (simulation à pas fixe + rendu), captures.
 *
 * Toute application d'apps-lib répétait la même mise en place (contexte SDL, fenêtre,
 * renderer, échelle d'affichage, polices du canvas, `sdl3::EventsManager`, thème, barre
 * de titre personnalisée, captures pour les essais automatiques). `app::Application` la
 * factorise : il ne reste à écrire que le comportement.
 *
 * @code{.cpp}
 * class Demo final : public app::Application {
 *     bool OnInit() override { ...; return true; }
 *     void OnFixedUpdate(double step) override { world.Step(step); }
 *     void OnRender(const sdl3::FRect &area, double alpha) override {
 *         Canvas().FillRect(area, sdl3::Rgba(20, 24, 32));
 *     }
 * };
 *
 * int main(int argc, char **argv) {
 *     app::AppConfig config;
 *     config.title = "Démo";
 *     config.ParseArgs(argc, argv);          // --frames=N, --screenshot=F.png
 *     return Demo().Run(config);
 * }
 * @endcode
 *
 * Le canvas est démarré avant `OnRender` et clos après ; l'interface immédiate
 * (`Ui()`) est ouverte pour l'image. Une interface retenue (`ui::Ui`) se construit sur
 * `Canvas()` et `Events()` et s'affiche dans `OnRender` (`Present(dt, area)`).
 * Avec `customFrame`, la fenêtre est sans décoration : la barre de titre (icône, titre,
 * boutons personnalisés, réduire / agrandir / fermer) est dessinée par `ui::WindowFrame`
 * et `area` est la zone située dessous.
 */
#include <cstdint>
#include <filesystem>
#include <span>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdl3/render/canvas.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/render/gpu.hpp>
#include <sdl3/render/render.hpp>
#include <sdl3/core/sdl_context.hpp>
#include <ui/ui.hpp>
#include <ui/window_frame.hpp>

#include "clock.hpp"

namespace app {

/// Thème sombre bleuté commun aux applications (fond nuit, accent bleu).
[[nodiscard]] ui::Theme MidnightTheme();
/// Thème clair (fond gris très pâle, panneaux blancs, accent bleu).
[[nodiscard]] ui::Theme LightTheme();
/// Thème à contraste élevé (noir, blanc, accent jaune).
[[nodiscard]] ui::Theme ContrastTheme();

struct AppConfig {
	std::string title = "apps-lib";
	int width = 1280;
	int height = 720;
	int minWidth = 480;
	int minHeight = 320;
	int winX = sdl3::windowPos::CENTERED;
	int winY = sdl3::windowPos::CENTERED;
	
	bool resizable = true;
	bool vsync = true;
	/// Fenêtre sans décoration : barre de titre dessinée par l'application.
	bool customFrame = false;
	/// Icône de la barre de titre (police d'icônes, `ui::icons`).
	char32_t icon = 0;
	/// Initialise aussi l'audio de SDL (lecture, capture).
	bool audio = false;
	/// Crée un device SDL_GPU (SPIR-V) et le renderer 2D sur ce device : les textures
	/// rendues par `Gpu()` (scènes 3D) s'affichent alors sans copie (`Texture::FromGpuTexture`)
	/// et le renderer accepte les shaders 2D personnalisés (`sdl3::GpuRenderState`).
	bool gpu = false;
	/// Couches de validation GPU (diagnostic) — requiert `gpu`.
	bool debug = false;
	/// Scène 3D hors écran gérée par `Application` : textures couleur + profondeur + post,
	/// tampon de commandes `Commands()` acquis chaque image, scène affichée plein écran
	/// avant le dessin 2D (`SceneColor`/`SceneDepth`/`PostColor`, `SetSceneVisible`).
	/// Implique `gpu`.
	bool scene = false;
	ui::Theme theme = MidnightTheme();
	FixedStepClock::Config clock{};
	/// 0 : illimité ; sinon arrêt après ce nombre d'images (`--frames=N`). Avec
	/// `recordDirectory`, ce sont les images enregistrées (après l'attente `minSeconds`).
	uint64_t maxFrames = 0;
	/// Capture PNG de la dernière image avant l'arrêt (`--screenshot=F.png`).
	std::filesystem::path screenshot;
	/// Enregistre chaque image en PNG (`frame_0000.png`…) dans ce dossier (`--record=DIR`),
	/// à partir de la fin de l'attente `minSeconds`.
	std::filesystem::path recordDirectory;
	/// > 0 : durée fixe de chaque image en secondes (`--dt=S`), pour des captures
	/// reproductibles indépendantes de la vitesse de la machine.
	double fixedFrameSeconds = 0.0;
	/// Durée réelle minimale (s) avant l'arrêt sur `maxFrames` et la capture (`--wait=S`) :
	/// laisse le temps aux chargements et aux animations de s'installer.
	double minSeconds = 0.0;
	/// Test de redimensionnement (`--resize-test=DIR`) : après `minSeconds`, la fenêtre prend
	/// successivement plusieurs tailles (dont plein écran) ; chacune est capturée
	/// (`resize_N_LxH.png`) après stabilisation, puis l'application s'arrête.
	std::filesystem::path resizeTestDirectory;

	/// Lit les options communes (`--frames=N`, `--screenshot=F`, `--record=DIR`, `--dt=S`,
	/// `--wait=S`, `--resize-test=DIR`, aussi sous la forme `--frames N`) ; rend les autres
	/// arguments, dans l'ordre.
	std::vector<std::string> ParseArgs(int argc, const char *const *argv);
};

/// Écart moyen (0–255) entre pixels voisins d'une image RGBA8 : 2 à 5 pour une image
/// rendue, plus de 25 pour une texture corrompue ou non initialisée (bruit coloré).
/// Le test de redimensionnement échoue au-delà de `kCorruptionThreshold`.
[[nodiscard]] float ImageNoise(std::span<const uint8_t> rgba, int width, int height) noexcept;
inline constexpr float kCorruptionThreshold = 15.f;

class Application {
public:
	Application();
	virtual ~Application();
	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

	/// Initialise SDL, la fenêtre et le canvas, exécute la boucle, puis arrête tout.
	/// Rend 0 si tout s'est bien passé, 1 si l'initialisation (ou la capture) a échoué.
	int Run(AppConfig config);

	/// Change le titre (barre de titre et fenêtre) : changement de langue, document ouvert…
	void SetTitle(std::string title);

	/// Demande l'arrêt à la fin de l'image courante.
	void RequestQuit() noexcept { m_running = false; }
	[[nodiscard]] bool IsRunning() const noexcept { return m_running; }

	[[nodiscard]] sdl3::EventsManager &Events() noexcept { return m_events; }
	[[nodiscard]] sdl3::Window &Window() noexcept { return *m_window; }
	[[nodiscard]] sdl3::Renderer &Renderer() noexcept { return *m_renderer; }
	/// Device GPU partagé avec le renderer 2D (`AppConfig::gpu`) ; nul sinon.
	[[nodiscard]] sdl3::GpuDevice *Gpu() noexcept { return m_gpu ? &*m_gpu : nullptr; }
	/// Device GPU (requiert `AppConfig::gpu`/`scene`) — valide pendant toute la boucle.
	[[nodiscard]] sdl3::GpuDevice &Device() noexcept { return *m_gpu; }
	[[nodiscard]] sdl3::Canvas &Canvas() noexcept { return m_canvas; }
	/// Interface immédiate de l'image (ouverte pendant `OnRender`).
	[[nodiscard]] ui::Context &Ui() noexcept { return *m_ui; }
	[[nodiscard]] const ui::Theme &Theme() const noexcept { return m_ui->Style(); }
	[[nodiscard]] const AppConfig &Config() const noexcept { return m_config; }
	[[nodiscard]] const FixedStepClock &Clock() const noexcept { return m_clock; }
	/// Change la durée du pas de simulation (l'horloge repart de zéro).
	void SetFixedStep(double stepSeconds) noexcept;
	[[nodiscard]] uint64_t FrameCount() const noexcept { return m_frames; }
	/// Images par seconde (moyenne glissante sur une demi-seconde).
	[[nodiscard]] float Fps() const noexcept { return m_fps; }
	/// Durée réelle de la dernière image (secondes).
	[[nodiscard]] float FrameSeconds() const noexcept { return m_frameSeconds; }

	/// Capture l'image en cours de dessin (à appeler avant la présentation) en PNG.
	bool SaveScreenshot(const std::filesystem::path &path);
	/// Capture puis vérifie l'image (ImageNoise) ; faux si l'écriture échoue ou si l'image
	/// paraît corrompue.
	bool SaveCheckedScreenshot(const std::filesystem::path &path);

	// ── Scène 3D hors écran (`AppConfig::scene`) ─────────────────────────────
	/// Tampon de commandes GPU de l'image (acquis avant `OnOffscreen`, soumis après) ;
	/// vide si la scène n'est pas gérée.
	[[nodiscard]] sdl3::GpuCommandBuffer &Commands() noexcept { return m_cmd; }
	/// Cible couleur de la scène (le jeu y dessine la 3D dans `OnOffscreen`).
	[[nodiscard]] sdl3::GpuTexture &SceneColor() noexcept { return m_sceneColor; }
	[[nodiscard]] sdl3::GpuTexture &SceneDepth() noexcept { return m_sceneDepth; }
	/// Cible du post-traitement (affichée plein écran avant le 2D).
	[[nodiscard]] sdl3::GpuTexture &PostColor() noexcept { return m_postColor; }
	[[nodiscard]] SDL_GPUTextureFormat SceneColorFormat() const noexcept { return kColorFormat; }
	[[nodiscard]] SDL_GPUTextureFormat SceneDepthFormat() const noexcept { return m_depthFormat; }
	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] float Aspect() const noexcept { return m_height > 0 ? float(m_width) / float(m_height) : 1.f; }
	/// Indique si la scène 3D doit être affichée (sinon, fond uni : menus).
	void SetSceneVisible(bool v) noexcept { m_sceneVisible = v; }
	void SetVSync(bool on);
	static constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

protected:
	/// Après création de la fenêtre et du canvas ; faux = abandon (`Run` rend 1).
	virtual bool OnInit() { return true; }
	/// Simulation, appelée 0..n fois par image avec un pas constant.
	virtual void OnFixedUpdate(double /*stepSeconds*/) {}
	/// Une fois par image, avec la durée réelle de l'image (entrées déjà lues).
	virtual void OnUpdate(double /*frameSeconds*/) {}
	/// Rendu hors écran (textures cibles, passes GPU) avant le dessin de la fenêtre :
	/// le renderer peut changer de cible ici, il doit la rendre (`ResetTarget`).
	virtual void OnOffscreen() {}
	/// Dessin de l'image dans `area` (sous la barre de titre) ; `alpha` : fraction du
	/// pas fixe suivant (interpolation).
	virtual void OnRender(const sdl3::FRect & /*area*/, double /*alpha*/) {}
	/// Fermeture demandée.
	virtual bool OnCloseRequested() { return true; }
	/// Avant la destruction de la fenêtre.
	virtual void OnShutdown() {}
	/// Boutons personnalisés de la barre de titre (`customFrame`), relus à chaque image.
	virtual void TitleButtons(std::vector<ui::FrameButton> & /*left*/, std::vector<ui::FrameButton> & /*right*/) {}
	/// Un bouton personnalisé de la barre de titre a été cliqué.
	virtual void OnTitleButton(std::string_view /*id*/) {}

private:
	bool Init();
	void Frame(double frameSeconds);
	bool EnsureSceneTargets();
	sdl3::FRect DrawTitleBar();

	AppConfig m_config;
	std::optional<sdl3::SdlContext> m_sdl;
	std::optional<sdl3::Window> m_window;
	std::optional<sdl3::GpuDevice> m_gpu; // détruit après le renderer qui l'utilise
	std::optional<sdl3::Renderer> m_renderer;
	sdl3::Canvas m_canvas;
	sdl3::EventsManager m_events;
	std::unique_ptr<ui::Context> m_ui;
	std::unique_ptr<ui::WindowChrome> m_chrome;
	std::vector<ui::FrameButton> m_left, m_right;
	FixedStepClock m_clock;
	uint64_t m_frames = 0;
	uint64_t m_recorded = 0; ///< images enregistrées (`--record`)
	float m_fps = 0.f, m_fpsTime = 0.f, m_frameSeconds = 0.f;
	int m_fpsFrames = 0;
	bool m_running = false;
	// Scène 3D hors écran (`AppConfig::scene`).
	sdl3::GpuTexture m_sceneColor, m_sceneDepth, m_postColor;
	std::optional<sdl3::Texture> m_postView; // vue 2D de la cible de post-traitement
	sdl3::GpuCommandBuffer m_cmd;
	SDL_GPUTextureFormat m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	int m_width = 0, m_height = 0;
	bool m_sceneVisible = true;
};

} // namespace app
