#include "sdl3/render/graphics_context.hpp"

#include <SDL3/SDL.h>

#include <core/log.hpp>
#include <diagnostics/trace.hpp>
#include <sdl3/render/image.hpp>

namespace sdl3 {

std::unique_ptr<GraphicsContext> GraphicsContext::Create(const GraphicsContextOptions &o) {
	std::unique_ptr<GraphicsContext> c(new GraphicsContext());
	auto sdl = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO | sdl3::initFlags::EVENTS | sdl3::initFlags::GAMEPAD);
	if (!sdl) {
		core::log::Error("SDL_Init : {}", sdl.Error());
		return nullptr;
	}
	c->m_sdl = std::move(sdl.Value());
	SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (o.resizable)
		flags |= SDL_WINDOW_RESIZABLE;
	if (o.hidden)
		flags |= SDL_WINDOW_HIDDEN;
	auto window = sdl3::Window::Create(String(o.title), o.width, o.height, flags);
	if (!window) {
		core::log::Error("fenêtre : {}", window.Error());
		return nullptr;
	}
	c->m_window = std::move(window.Value());
	auto device = sdl3::GpuDevice::Create(SDL_GPU_SHADERFORMAT_SPIRV, o.debug);
	if (!device) {
		core::log::Error("GPU (SPIR-V/Vulkan requis) : {}", device.Error());
		return nullptr;
	}
	c->m_device = std::move(device.Value());
	// Le renderer 2D réclame la fenêtre et sa chaîne d'échange sur NOTRE device.
	auto renderer = sdl3::Renderer::CreateOnGpuDevice(c->m_device.Get(), c->m_window);
	if (!renderer) {
		core::log::Error("renderer GPU : {}", renderer.Error());
		return nullptr;
	}
	c->m_renderer = std::move(renderer.Value());
	c->SetVSync(o.vsync);
	if (c->m_device.TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
		c->m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	else
		c->m_depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	core::log::Info("GPU : pilote {}", c->m_device.Driver() ? c->m_device.Driver() : "?");
	return c;
}

GraphicsContext::~GraphicsContext() {
	if (m_device)
		(void)m_device.WaitIdle();
	m_sceneView.reset();
	m_postView.reset();
	m_sceneColor = {};
	m_sceneDepth = {};
	m_postColor = {};
	m_renderer = {};
}

void GraphicsContext::SetVSync(bool on) { m_renderer.SetVSync(on ? sdl3::Renderer::VSync(1) : sdl3::Renderer::VSync(0)); }

bool GraphicsContext::EnsureTargets() {
	const int w = m_renderer.GetOutputWidth(), h = m_renderer.GetOutputHeight();
	if (w <= 0 || h <= 0)
		return false;
	if (w == m_width && h == m_height && m_sceneColor)
		return true;
	(void)m_device.WaitIdle();
	m_sceneView.reset();
	m_postView.reset();
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = kColorFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ci.width = static_cast<uint32_t>(w);
	ci.height = static_cast<uint32_t>(h);
	ci.layer_count_or_depth = 1;
	ci.num_levels = 1;
	auto color = m_device.CreateTexture(ci);
	auto post = m_device.CreateTexture(ci); // cible du post-traitement
	ci.format = m_depthFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	auto depth = m_device.CreateTexture(ci);
	if (!color || !depth || !post) {
		core::log::Error("cibles de rendu {}x{} : {}", w, h, color ? depth ? post.Error() : depth.Error() : color.Error());
		return false;
	}
	m_sceneColor = std::move(color.Value());
	m_sceneDepth = std::move(depth.Value());
	m_postColor = std::move(post.Value());
	auto view = sdl3::Texture::FromGpuTexture(m_renderer, m_sceneColor.Get(), w, h, sdl3::PixelFormat::RGBA32);
	auto postView = sdl3::Texture::FromGpuTexture(m_renderer, m_postColor.Get(), w, h, sdl3::PixelFormat::RGBA32);
	if (!view || !postView) {
		core::log::Error("vue de scène : {}", view ? postView.Error() : view.Error());
		return false;
	}
	m_sceneView.emplace(std::move(view.Value()));
	m_postView.emplace(std::move(postView.Value()));
	m_width = w;
	m_height = h;
	return true;
}

bool GraphicsContext::BeginFrame() {
	DIAG_ZONE("Gfx::BeginFrame");
	if (m_window.IsMinimized() || !EnsureTargets())
		return false;
	m_cmd = m_device.AcquireCommandBuffer();
	m_frameOpen = static_cast<bool>(m_cmd);
	return m_frameOpen;
}

void GraphicsContext::EndScene() {
	DIAG_ZONE("Gfx::EndScene");
	if (!m_frameOpen)
		return;
	(void)m_cmd.Submit();
	m_cmd = {};
	m_frameOpen = false;
	m_renderer.SetDrawColor(sdl3::Color{20, 22, 28, 255});
	m_renderer.Clear();
	if (m_sceneVisible && m_postView)
		m_renderer.Render(*m_postView, sdl3::FRect{0.f, 0.f, float(m_width), float(m_height)});
}

void GraphicsContext::Present() {
	DIAG_ZONE("Gfx::Present");
	m_renderer.Present();
}

bool GraphicsContext::SaveScreenshot(const std::string &path) {
	auto pixels = m_renderer.ReadPixels();
	if (!pixels) {
		core::log::Error("capture : {}", pixels.Error());
		return false;
	}
	auto saved = sdl3::ImgSavePng(pixels.Value(), String(path));
	if (!saved) {
		core::log::Error("capture : {}", saved.Error());
		return false;
	}
	core::log::Info("capture enregistrée : {}", path);
	return true;
}

} // namespace sdl3
