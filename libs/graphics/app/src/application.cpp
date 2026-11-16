// Définitions de app/application.hpp
#include "app/application.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <core/log.hpp>

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

std::vector<std::string> AppConfig::ParseArgs(int argc, const char *const *argv) {
	std::vector<std::string> rest;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		std::string value;
		if (OptionValue(arg, "--frames", i, argc, argv, value))
			maxFrames = std::strtoull(value.c_str(), nullptr, 10);
		else if (OptionValue(arg, "--screenshot", i, argc, argv, value))
			screenshot = value;
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
	while (m_running) {
		const auto now = std::chrono::steady_clock::now();
		const double frameSeconds = std::chrono::duration<double>(now - last).count();
		last = now;
		Frame(frameSeconds);
		++m_frames;
		if (m_config.maxFrames && m_frames >= m_config.maxFrames) {
			if (!m_config.screenshot.empty() && !SaveScreenshot(m_config.screenshot)) {
				core::log::Error("{} : capture non écrite : {}", m_config.title, m_config.screenshot.string());
				code = 1;
			}
			m_running = false;
		}
		m_renderer->Present();
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

	m_events.Poll();
	if (m_events.IsQuitRequested() && OnCloseRequested())
		RequestQuit();
	const int steps = m_clock.Advance(frameSeconds);
	for (int i = 0; i < steps; ++i)
		OnFixedUpdate(m_clock.StepSeconds());
	OnUpdate(frameSeconds);
	if (m_config.scene && !m_window->IsMinimized() && EnsureSceneTargets())
		m_cmd = m_gpu->AcquireCommandBuffer();
	OnOffscreen();
	if (m_cmd) { // soumet les passes 3D enregistrées dans `Commands()`
		(void)m_cmd.Submit();
		m_cmd = {};
	}

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

bool Application::SaveScreenshot(const std::filesystem::path &path) {
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);
	auto surface = m_renderer->ReadPixels();
	return surface.IsOk() && surface.Value().SavePng(String(path.string()));
}

} // namespace app
