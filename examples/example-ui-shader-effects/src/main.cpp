// example-ui-shader-effects — effets GPU sur des panneaux d'interface ordinaires.
//
// Les cartes (texte, bouton, curseur, case à cocher) sont dessinées dans une couche : une
// texture cible de la taille de la fenêtre, aux mêmes coordonnées, si bien que les widgets
// restent interactifs. Chaque carte est ensuite recopiée à l'écran à travers son propre
// shader de fragments (`sdl3::GpuRenderState`, renderer 2D « gpu » de SDL), avec une marge
// pour les effets qui débordent (halo).
//
//   cpp-maker run example-ui-shader-effects
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>

#include <app/app.hpp>

#include "shaders_spirv.inc"

namespace {

struct EffectDef {
	std::string_view name;
	std::string_view description;
	std::span<const unsigned char> code; // vide : sans effet
	sdl3::Color top, bottom;				  // dégradé de la carte
};

const std::array<EffectDef, 7> kEffects = {{
	{"Sans effet", "Référence : la couche recopiée telle quelle.", {}, sdl3::Rgba(64, 108, 190), sdl3::Rgba(40, 62, 120)},
	{"Teinte animée", "Rotation de teinte (YIQ) selon le temps et la position.", shaders::k_hue_frag, sdl3::Rgba(70, 140, 220), sdl3::Rgba(120, 70, 200)},
	{"Halo", "La couverture voisine éclaire le pourtour de la carte.", shaders::k_glow_frag, sdl3::Rgba(36, 44, 70), sdl3::Rgba(24, 28, 44)},
	{"Écran cathodique", "Courbure, aberration chromatique, lignes de balayage.", shaders::k_crt_frag, sdl3::Rgba(30, 90, 70), sdl3::Rgba(20, 50, 40)},
	{"Mosaïque", "Blocs de 1 à 8 pixels selon l'intensité.", shaders::k_pixelate_frag, sdl3::Rgba(200, 120, 60), sdl3::Rgba(150, 60, 70)},
	{"Ondulation", "Déplacement sinusoïdal animé des coordonnées.", shaders::k_wave_frag, sdl3::Rgba(40, 150, 180), sdl3::Rgba(30, 80, 140)},
	{"Verre dépoli", "Flou à 25 échantillons, teinte froide et grain.", shaders::k_frost_frag, sdl3::Rgba(225, 230, 240), sdl3::Rgba(180, 190, 210)},
}};

/// Uniformes des shaders d'effet (bloc `Effect` de effect.glsl).
struct EffectUniforms {
	float time, strength, texelU, texelV;
	float u0, v0, u1, v1;
};

struct Effect {
	sdl3::GpuShader shader;
	sdl3::GpuRenderState state;
};

class ShaderEffectsDemo final : public app::Application {
protected:
	bool OnInit() override {
		for (size_t i = 0; i < kEffects.size(); ++i) {
			if (kEffects[i].code.empty())
				continue;
			SDL_GPUShaderCreateInfo info{};
			info.code = kEffects[i].code.data();
			info.code_size = kEffects[i].code.size();
			info.entrypoint = "main";
			info.format = SDL_GPU_SHADERFORMAT_SPIRV;
			info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
			info.num_samplers = 1;
			info.num_uniform_buffers = 1;
			auto shader = Gpu()->CreateShader(info);
			if (!shader) {
				core::log::Error("shader « {} » : {}", kEffects[i].name, shader.Error());
				return false;
			}
			auto state = sdl3::GpuRenderState::Create(Renderer().Get(), {.fragmentShader = shader.Value().Get()});
			if (!state) {
				core::log::Error("état de rendu « {} » : {}", kEffects[i].name, state.Error());
				return false;
			}
			m_effects[i].emplace(Effect{std::move(shader.Value()), std::move(state.Value())});
		}
		return true;
	}

	void OnFixedUpdate(double step) override {
		if (m_animate)
			m_time += float(step);
	}

	void OnOffscreen() override {
		// Couche à la taille de la fenêtre en pixels : mêmes coordonnées que l'écran.
		const int w = Renderer().GetOutputWidth(), h = Renderer().GetOutputHeight();
		if (m_layer && w == m_layerW && h == m_layerH)
			return;
		m_layer.reset();
		auto layer = Renderer().CreateTexture(sdl3::PixelFormat::RGBA32, sdl3::TextureAccess::TARGET, w, h);
		if (!layer)
			return;
		m_layer.emplace(std::move(layer.Value()));
		m_layer->SetBlendMode(sdl3::BlendMode::BLEND_PREMULTIPLIED); // dessinée sur du transparent : prémultipliée
		m_layerW = w;
		m_layerH = h;
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		sdl3::Canvas &c = Canvas();
		const sdl3::FRect body = area.Inset(20);

		// Barre de réglages.
		const sdl3::FRect bar{body.x, body.y, body.w, 34.f};
		c.Text(bar.x, bar.y + 6.f, "Effets GPU sur des widgets interactifs", th.text, 18.f, sdl3::CanvasFont::Bold);
		u.BeginColumn({bar.Right() - 620.f, bar.y, 620.f, bar.h});
		u.Row({1.6f, 1.f, 1.2f}, 30.f);
		const sdl3::FRect slider = u.Next();
		c.Text(slider.x, slider.y + 6.f, "Intensité", th.textDim, 13.f);
		u.SliderAt({slider.x + 70.f, slider.y, slider.w - 70.f, slider.h}, "strength", m_strength, 0.f, 1.f);
		u.Checkbox("Animer", m_animate);
		u.Checkbox("Comparer sans effet", m_compare);
		u.EndColumn();

		// Cartes : 4 × 2 (la dernière case explique le principe).
		const sdl3::FRect grid{body.x, bar.Bottom() + 16.f, body.w, body.Bottom() - bar.Bottom() - 16.f};
		constexpr float kGap = 24.f, kCaption = 44.f;
		const float cw = (grid.w - 3.f * kGap) / 4.f, ch = (grid.h - kGap) / 2.f - kCaption;
		const auto cardRect = [&](size_t i) {
			return sdl3::FRect{grid.x + float(i % 4) * (cw + kGap), grid.y + float(i / 4) * (ch + kCaption + kGap), cw, ch};
		};

		if (m_layer) {
			c.SetTarget(&*m_layer);
			Renderer().SetDrawColor(sdl3::Color{0, 0, 0, 0});
			Renderer().Clear();
			for (size_t i = 0; i < kEffects.size(); ++i)
				DrawCard(i, cardRect(i));
			c.SetTarget(nullptr);
			const float scale = c.Scale();
			for (size_t i = 0; i < kEffects.size(); ++i) {
				const sdl3::FRect card = cardRect(i), dst = card.Inset(-18.f); // marge : halo (les shaders ne lisent que la carte)
				const sdl3::FRect src{dst.x * scale, dst.y * scale, dst.w * scale, dst.h * scale};
				Effect *effect = m_effects[i] && !m_compare ? &*m_effects[i] : nullptr;
				if (effect) {
					const EffectUniforms uniforms{m_time,
												  m_strength,
												  1.f / float(m_layerW),
												  1.f / float(m_layerH),
												  card.x * scale / float(m_layerW),
												  card.y * scale / float(m_layerH),
												  card.Right() * scale / float(m_layerW),
												  card.Bottom() * scale / float(m_layerH)};
					effect->state.SetFragmentUniforms(0, &uniforms, sizeof uniforms);
					c.SetRenderState(effect->state.Get());
				}
				c.ImageRegion(*m_layer, float(m_layerW), float(m_layerH), src, dst);
				if (effect)
					c.SetRenderState(nullptr);
			}
		}
		for (size_t i = 0; i < kEffects.size(); ++i) {
			const sdl3::FRect card = cardRect(i);
			c.Text(card.x, card.Bottom() + 8.f, kEffects[i].name, th.text, 14.f, sdl3::CanvasFont::Bold);
			c.Text(card.x, card.Bottom() + 26.f, c.Ellipsize(kEffects[i].description, card.w, 12.f), th.textDim, 12.f);
		}
		DrawExplanation(cardRect(7));
	}

private:
	void DrawCard(size_t i, const sdl3::FRect &r) {
		ui::Context &u = Ui();
		sdl3::Canvas &c = Canvas();
		const EffectDef &e = kEffects[i];
		const bool light = i == 6;
		c.FillRoundRect(r, 12.f, e.bottom);
		c.FillGradientV({r.x, r.y + 12.f, r.w, r.h - 24.f}, e.top, e.bottom);
		c.FillRoundRect({r.x, r.y, r.w, 24.f}, 12.f, e.top);
		const sdl3::Color ink = light ? sdl3::Rgba(30, 34, 44) : sdl3::Rgba(240, 244, 250);
		u.BeginColumn(r.Inset(16));
		u.Label("Contenu réel", ink, 17.f);
		u.Label(std::format("Bouton cliqué {} fois", m_clicks[i]), light ? sdl3::Rgba(70, 76, 90) : sdl3::Rgba(210, 218, 232), 12.f);
		u.Space(4.f);
		u.PushId(uint64_t(i));
		if (u.Button("Bouton"))
			++m_clicks[i];
		u.SliderFloat("Valeur", m_values[i], 0.f, 1.f);
		u.Checkbox("Option", m_checks[i]);
		u.ProgressBar(m_values[i]);
		u.PopId();
		u.EndColumn();
	}

	void DrawExplanation(const sdl3::FRect &r) {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		Canvas().StrokeRoundRect(r, 12.f, th.panelBorder);
		u.BeginColumn(r.Inset(16));
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::INFO), "Principe");
		u.Wrapped("Les cartes sont dessinées dans une texture cible aux coordonnées de l'écran, puis recopiées à travers un "
				  "shader SPIR-V par carte (SDL_GPURenderState). Les widgets restent cliquables : seule l'image change.",
				  th.textDim, 12.f);
		u.EndColumn();
	}

	std::array<std::optional<Effect>, kEffects.size()> m_effects;
	std::optional<sdl3::Texture> m_layer;
	int m_layerW = 0, m_layerH = 0;
	std::array<float, kEffects.size()> m_values{0.3f, 0.5f, 0.7f, 0.4f, 0.6f, 0.2f, 0.8f};
	std::array<bool, kEffects.size()> m_checks{true, false, true, false, true, false, true};
	std::array<int, kEffects.size()> m_clicks{};
	float m_time = 0.f, m_strength = 0.8f;
	bool m_animate = true, m_compare = false;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Effets de shaders";
	config.width = 1320;
	config.height = 780;
	config.customFrame = true;
	config.gpu = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::BRUSH);
	config.ParseArgs(argc, argv);
	return ShaderEffectsDemo().Run(config);
}
