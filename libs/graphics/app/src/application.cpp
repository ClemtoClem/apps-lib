// Définitions de app/application.hpp
#include "app/application.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>

#include <core/log.hpp>
#include <diagnostics/trace.hpp>

namespace app {

namespace {

sdl3::CanvasFontSet DefaultFonts() {
	return sdl3::CanvasFontSet{"assets/fonts/DejaVuSans.ttf", "assets/fonts/DejaVuSans-Bold.ttf", "assets/fonts/DejaVuSansMono.ttf",
						"assets/fonts/MaterialIcons-Regular.otf"};
}

/// Valeur d'une option `--nom=valeur` ou `--nom valeur` (avance `i` dans le second cas).
bool OptionValue(std::string_view arg, std::string_view name, int &i, int argc, const char *const *argv, std::string &out) {
	if (arg == name && i + 1 < argc) {
		out = argv[++i];
		return true;
	}
	if (arg.size() > name.size() + 1 && arg.starts_with(name) && arg[name.size()] == '=') {
		out = std::string(arg.substr(name.size() + 1));
		return true;
	}
	return false;
}

} // namespace

ui::Theme MidnightTheme() {
	ui::Theme theme;
	theme.background = sdl3::Rgba(15, 19, 30);
	theme.panel = sdl3::Rgba(24, 30, 46, 245);
	theme.panelBorder = sdl3::Rgba(48, 58, 84);
	theme.title = sdl3::Rgba(20, 25, 38);
	theme.accent = sdl3::Rgba(74, 144, 255);
	theme.accentHover = sdl3::Rgba(110, 168, 255);
	theme.accentText = sdl3::Rgba(255, 255, 255);
	theme.widget = sdl3::Rgba(36, 44, 66);
	theme.widgetHover = sdl3::Rgba(48, 58, 86);
	theme.widgetActive = sdl3::Rgba(62, 74, 108);
	theme.row = sdl3::Rgba(28, 35, 54, 170);
	return theme;
}

ui::Theme LightTheme() {
	ui::Theme t;
	t.background = sdl3::Rgba(240, 243, 248);
	t.panel = sdl3::Rgba(255, 255, 255, 250);
	t.panelBorder = sdl3::Rgba(207, 215, 227);
	t.title = sdl3::Rgba(226, 232, 241);
	t.text = sdl3::Rgba(31, 38, 51);
	t.textDim = sdl3::Rgba(98, 108, 126);
	t.accent = sdl3::Rgba(47, 111, 208);
	t.accentHover = sdl3::Rgba(61, 127, 224);
	t.accentText = sdl3::Rgba(255, 255, 255);
	t.widget = sdl3::Rgba(228, 233, 241);
	t.widgetHover = sdl3::Rgba(214, 223, 236);
	t.widgetActive = sdl3::Rgba(190, 208, 236);
	t.row = sdl3::Rgba(232, 237, 245, 200);
	t.info = sdl3::Rgba(47, 111, 208);
	return t;
}

ui::Theme ContrastTheme() {
	ui::Theme t;
	t.background = sdl3::Rgba(0, 0, 0);
	t.panel = sdl3::Rgba(0, 0, 0, 255);
	t.panelBorder = sdl3::Rgba(255, 255, 255);
	t.title = sdl3::Rgba(0, 0, 0);
	t.text = sdl3::Rgba(255, 255, 255);
	t.textDim = sdl3::Rgba(220, 220, 220);
	t.accent = sdl3::Rgba(255, 255, 0);
	t.accentHover = sdl3::Rgba(255, 255, 140);
	t.accentText = sdl3::Rgba(0, 0, 0);
	t.widget = sdl3::Rgba(40, 40, 40);
	t.widgetHover = sdl3::Rgba(70, 70, 0);
	t.widgetActive = sdl3::Rgba(110, 110, 0);
	t.row = sdl3::Rgba(25, 25, 25, 255);
	t.info = sdl3::Rgba(255, 255, 0);
	return t;
}

std::vector<std::string> AppConfig::ParseArgs(int argc, const char *const *argv) {
	std::vector<std::string> rest;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		std::string value;
		if (OptionValue(arg, "--frames", i, argc, argv, value))
			maxFrames = std::strtoull(value.c_str(), nullptr, 10);
		else if (OptionValue(arg, "--screenshot", i, argc, argv, value))
			screenshot = value;
		else if (OptionValue(arg, "--record", i, argc, argv, value))
			recordDirectory = value;
		else if (OptionValue(arg, "--dt", i, argc, argv, value))
			fixedFrameSeconds = std::max(0.0, std::strtod(value.c_str(), nullptr));
		else if (OptionValue(arg, "--wait", i, argc, argv, value))
			minSeconds = std::max(0.0, std::strtod(value.c_str(), nullptr));
		else if (OptionValue(arg, "--resize-test", i, argc, argv, value))
			resizeTestDirectory = value;
		else
			rest.emplace_back(arg);
	}
	return rest;
}

Application::Application() = default;
Application::~Application() = default;

bool Application::Init() {
	sdl3::InitFlags flags = sdl3::initFlags::VIDEO | sdl3::initFlags::EVENTS | sdl3::initFlags::GAMEPAD;
	if (m_config.audio)
		flags |= sdl3::initFlags::AUDIO;
	auto sdl = sdl3::SdlContext::Create(flags);
	if (sdl.IsError()) {
		core::log::Error("{} : initialisation de SDL impossible ({})", m_config.title, sdl3::GetError());
		return false;
	}
	m_sdl.emplace(std::move(sdl.Value()));

	sdl3::WindowFlags windowFlags = sdl3::windowFlags::HIGH_PIXEL_DENSITY;
	if (m_config.resizable)
		windowFlags |= sdl3::windowFlags::RESIZABLE;
	if (m_config.customFrame)
		windowFlags |= sdl3::windowFlags::BORDERLESS;
	auto window = sdl3::Window::Create(String(m_config.title.c_str()), m_config.width, m_config.height, windowFlags);
	if (window.IsError()) {
		core::log::Error("{} : création de la fenêtre impossible ({})", m_config.title, sdl3::GetError());
		return false;
	}
	m_window.emplace(std::move(window.Value()));
	m_window->SetMinimumSize(m_config.minWidth, m_config.minHeight);
	m_window->SetPosition(m_config.winX, m_config.winY);
	m_window->StartTextInput(); // évènements de saisie de texte (champs, chiffres, accents)
	if (m_config.gpu || m_config.scene) {
		auto device = sdl3::GpuDevice::Create(SDL_GPU_SHADERFORMAT_SPIRV, m_config.debug);
		if (device.IsError()) {
			core::log::Error("{} : device GPU (SPIR-V) indisponible ({})", m_config.title, sdl3::GetError());
			return false;
		}
		m_gpu.emplace(std::move(device.Value()));
		core::log::Info("{} : GPU, pilote {}", m_config.title, m_gpu->Driver() ? m_gpu->Driver() : "?");
		if (m_config.scene)
			m_depthFormat = m_gpu->TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
														 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
								? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
								: SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	}
	auto renderer = m_gpu ? sdl3::Renderer::CreateOnGpuDevice(m_gpu->Get(), *m_window) : sdl3::Renderer::Create(*m_window);
	if (renderer.IsError()) {
		core::log::Error("{} : création du rendu impossible ({})", m_config.title, sdl3::GetError());
		return false;
	}
	m_renderer.emplace(std::move(renderer.Value()));
	m_renderer->SetVSync(sdl3::Renderer::VSync(m_config.vsync ? 1 : 0));

	const float scale = m_window->DisplayScale();
	ui::icons::RegisterAll();
	if (!m_canvas.Init(*m_renderer, DefaultFonts(), scale > 0.f ? scale : 1.f)) {
		core::log::Error("{} : polices introuvables (lancer depuis la racine du projet)", m_config.title);
		return false;
	}
	m_ui = std::make_unique<ui::Context>(m_canvas, m_events);
	m_ui->Style() = m_config.theme;
	if (m_config.customFrame)
		m_chrome = std::make_unique<ui::WindowChrome>(m_window->Get());
	// Fenêtre déplacée vers un écran d'une autre densité : le canvas suit.
	(void)m_events.Bind(sdl3::EventType::WINDOW_DISPLAY_SCALE_CHANGED, [this](const sdl3::Event &) {
		m_canvas.SetScale(std::max(m_window->DisplayScale(), 0.5f));
	});
	return true;
}

int Application::Run(AppConfig config) {
	m_config = std::move(config);
	m_clock = FixedStepClock(m_config.clock);
	m_frames = 0;
	if (!Init())
		return 1;
	m_running = true;
	if (!OnInit()) {
		core::log::Error("{} : initialisation de l'application impossible", m_config.title);
		m_running = false;
		return 1;
	}

	int code = 0;
	auto last = std::chrono::steady_clock::now();
	const auto start = last;
	// Test de redimensionnement : tailles successives (0 × 0 : plein écran), chacune capturée
	// après stabilisation (une seconde et demie, plusieurs images).
	struct ResizeStep {
		int w, h;
	};
	static constexpr ResizeStep kResizeSteps[] = {{1600, 900}, {0, 0}, {960, 600}, {0, 0}, {1280, 720}, {1366, 821}};
	size_t resizeStep = 0;
	double resizeAt = -1.0;
	while (m_running) {
		const auto now = std::chrono::steady_clock::now();
		const double measured = std::chrono::duration<double>(now - last).count();
		last = now;
		Frame(m_config.fixedFrameSeconds > 0.0 ? m_config.fixedFrameSeconds : measured);
		const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (!m_config.resizeTestDirectory.empty() && elapsed >= std::max(m_config.minSeconds, 2.0)) {
			if (resizeAt < 0.0) { // applique la taille de l'étape en cours
				const ResizeStep &step = kResizeSteps[resizeStep];
				if (step.w == 0)
					m_window->Maximize();
				else {
					if (m_window->IsMaximized())
						m_window->Restore();
					m_window->SetSize(step.w, step.h);
				}
				resizeAt = elapsed;
			} else if (elapsed - resizeAt >= 1.5) { // stabilisée : capture, puis étape suivante
				const sdl3::Point size = m_window->GetSize();
				const auto file = m_config.resizeTestDirectory / std::format("resize_{}_{}x{}.png", resizeStep, size.x, size.y);
				if (!SaveCheckedScreenshot(file)) {
					core::log::Error("{} : test de redimensionnement échoué : {}", m_config.title, file.string());
					code = 1;
				}
				resizeAt = -1.0;
				if (++resizeStep == std::size(kResizeSteps))
					m_running = false;
			}
		}
		if (!m_config.recordDirectory.empty() && elapsed >= m_config.minSeconds) { // après l'attente
			const auto file = m_config.recordDirectory / std::format("frame_{:04d}.png", m_recorded++);
			if (!SaveScreenshot(file))
				core::log::Error("{} : image non enregistrée : {}", m_config.title, file.string());
		}
		++m_frames;
		// En enregistrement, `--frames` compte les images écrites (l'attente n'en consomme pas).
		const uint64_t counted = m_config.recordDirectory.empty() ? m_frames : m_recorded;
		if (m_config.maxFrames && counted >= m_config.maxFrames && elapsed >= m_config.minSeconds) {
			if (!m_config.screenshot.empty() && !SaveScreenshot(m_config.screenshot)) {
				core::log::Error("{} : capture non écrite : {}", m_config.title, m_config.screenshot.string());
				code = 1;
			}
			m_running = false;
		}
		{
			DIAG_ZONE("App::Present");
			m_renderer->Present();
		}
		DIAG_FRAME(); // fin d'image : profiler et fil de garde des sessions de diagnostic
	}
	OnShutdown();
	if (m_gpu)
		(void)m_gpu->WaitIdle();
	return code;
}

void Application::Frame(double frameSeconds) {
	m_frameSeconds = float(frameSeconds);
	m_fpsTime += m_frameSeconds;
	if (++m_fpsFrames, m_fpsTime >= 0.5f) {
		m_fps = float(m_fpsFrames) / m_fpsTime;
		m_fpsTime = 0.f;
		m_fpsFrames = 0;
	}

	DIAG_COUNTER("app.image_ms", frameSeconds * 1000.0);
	{
		DIAG_ZONE("App::Events");
		m_events.Poll();
	}
	if (m_events.IsQuitRequested() && OnCloseRequested())
		RequestQuit();
	const int steps = m_clock.Advance(frameSeconds);
	{
		DIAG_ZONE("App::OnFixedUpdate");
		for (int i = 0; i < steps; ++i)
			OnFixedUpdate(m_clock.StepSeconds());
	}
	{
		DIAG_ZONE("App::OnUpdate");
		OnUpdate(frameSeconds);
	}
	if (m_config.scene && !m_window->IsMinimized() && EnsureSceneTargets())
		m_cmd = m_gpu->AcquireCommandBuffer();
	{
		DIAG_ZONE("App::OnOffscreen");
		OnOffscreen();
	}
	if (m_cmd) { // soumet les passes 3D enregistrées dans `Commands()`
		DIAG_ZONE("App::Submit");
		(void)m_cmd.Submit();
		m_cmd = {};
	}
	DIAG_ZONE("App::Render");

	const ui::Theme &theme = m_ui->Style();
	m_renderer->SetDrawColor(sdl3::Color{theme.background.r, theme.background.g, theme.background.b, 255});
	m_renderer->Clear();
	if (m_config.scene && m_sceneVisible && m_postView)
		m_renderer->Render(*m_postView, sdl3::FRect{0.f, 0.f, float(m_width), float(m_height)});
	m_canvas.Begin();
	m_ui->Begin(m_frameSeconds);
	const sdl3::FRect area = DrawTitleBar();
	OnRender(area, m_clock.Alpha());
	m_ui->DrawNotifications(m_frameSeconds, area.y + 12.f);
	m_ui->End();
	m_canvas.End();
}

sdl3::FRect Application::DrawTitleBar() {
	const sdl3::FRect window{0, 0, m_canvas.Width(), m_canvas.Height()};
	if (!m_chrome)
		return window;
	m_left.clear();
	m_right.clear();
	TitleButtons(m_left, m_right);
	ui::WindowFrameOptions options;
	options.title = m_config.title;
	options.icon = m_config.icon;
	options.left = m_left;
	options.right = m_right;
	options.maximized = m_chrome->Maximized();
	options.focused = m_chrome->Focused();
	const ui::WindowFrameResult result = ui::WindowFrame(*m_ui, window, options);
	m_chrome->Update(result.layout, m_canvas.Scale());
	if (result.action == ui::FrameAction::Custom)
		OnTitleButton(result.custom);
	else if (m_chrome->Apply(result.action) && OnCloseRequested())
		RequestQuit();
	return result.layout.content;
}

void Application::SetTitle(std::string title) {
	m_config.title = std::move(title);
	if (m_window)
		(void)m_window->SetTitle(String(m_config.title.c_str()));
}

void Application::SetFixedStep(double stepSeconds) noexcept {
	m_config.clock.stepSeconds = stepSeconds;
	m_clock = FixedStepClock(m_config.clock);
}

void Application::SetVSync(bool on) {
	if (m_renderer)
		m_renderer->SetVSync(on ? sdl3::Renderer::VSync(1) : sdl3::Renderer::VSync(0));
}

bool Application::EnsureSceneTargets() {
	const int w = m_renderer->GetOutputWidth(), h = m_renderer->GetOutputHeight();
	if (w <= 0 || h <= 0)
		return false;
	if (w == m_width && h == m_height && m_sceneColor)
		return true;
	(void)m_gpu->WaitIdle();
	m_postView.reset();
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = kColorFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ci.width = static_cast<uint32_t>(w);
	ci.height = static_cast<uint32_t>(h);
	ci.layer_count_or_depth = 1;
	ci.num_levels = 1;
	auto color = m_gpu->CreateTexture(ci);
	auto post = m_gpu->CreateTexture(ci); // cible du post-traitement
	ci.format = m_depthFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	auto depth = m_gpu->CreateTexture(ci);
	if (!color || !depth || !post) {
		core::log::Error("cibles de rendu {}x{} : {}", w, h, color ? depth ? post.Error() : depth.Error() : color.Error());
		return false;
	}
	m_sceneColor = std::move(color.Value());
	m_sceneDepth = std::move(depth.Value());
	m_postColor = std::move(post.Value());
	auto postView = sdl3::Texture::FromGpuTexture(*m_renderer, m_postColor.Get(), w, h, sdl3::PixelFormat::RGBA32);
	if (!postView) {
		core::log::Error("vue de scène : {}", postView.Error());
		return false;
	}
	m_postView.emplace(std::move(postView.Value()));
	m_width = w;
	m_height = h;
	return true;
}

float ImageNoise(std::span<const uint8_t> rgba, int width, int height) noexcept {
	if (width < 2 || height < 1 || rgba.size() < size_t(width) * size_t(height) * 4)
		return 0.f;
	double total = 0.0;
	size_t count = 0;
	for (int y = 0; y < height; y += 4)
		for (int x = 0; x + 1 < width; x += 2) {
			const uint8_t *a = &rgba[(size_t(y) * size_t(width) + size_t(x)) * 4];
			total += std::abs(int(a[0]) - int(a[4])) + std::abs(int(a[1]) - int(a[5])) + std::abs(int(a[2]) - int(a[6]));
			++count;
		}
	return count ? float(total / double(count) / 3.0) : 0.f;
}

bool Application::SaveCheckedScreenshot(const std::filesystem::path &path) {
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);
	auto surface = m_renderer->ReadPixels();
	if (surface.IsError() || !surface.Value().SavePng(String(path.string())))
		return false;
	const std::vector<uint8_t> rgba = surface.Value().ToRgba8();
	const float noise = ImageNoise(rgba, surface.Value().GetWidth(), surface.Value().GetHeight());
	if (noise > kCorruptionThreshold) {
		core::log::Error("{} : image corrompue (bruit {:.1f}) : {}", m_config.title, noise, path.string());
		return false;
	}
	return true;
}

bool Application::SaveScreenshot(const std::filesystem::path &path) {
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);
	auto surface = m_renderer->ReadPixels();
	return surface.IsOk() && surface.Value().SavePng(String(path.string()));
}

} // namespace app
