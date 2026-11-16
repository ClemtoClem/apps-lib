// example-audio-generator — générateur de signaux.
//
// Quatre oscillateurs (sinus, carré, triangle, dent de scie, bruit ; carré et dent de scie
// à bande limitée PolyBLEP) sont additionnés, passés au volume maître et à un écrêtage doux,
// puis envoyés au périphérique choisi par un `sdl3::AudioStream`. Le flux est alimenté depuis
// la boucle principale : chaque image complète la file jusqu'à ~60 ms d'avance, ce qui suffit
// à éviter les coupures sans fil d'exécution ni verrou. Oscilloscope, spectre (FFT Hann sur
// 4096 points) et vumètres montrent le signal réellement envoyé.
//
//   Espace  couper / rétablir le son        1 à 4  activer un oscillateur
//
//   cpp-maker run example-audio-generator
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <audio/dsp.hpp>
#include <sdl3/input/audio.hpp>

namespace {

constexpr int kRate = 48000;
constexpr size_t kBlock = 256;		  ///< échantillons produits par appel
constexpr float kLeadSeconds = 0.06f; ///< avance maintenue dans la file du flux
constexpr size_t kFft = 4096;
constexpr float kMinHz = 20.f, kMaxHz = 12000.f;

constexpr std::array<std::string_view, 5> kShapes = {"Sin", "Carré", "Tri", "Scie", "Bruit"};
constexpr std::array<std::string_view, 5> kShapeNames = {"Sinus", "Carré (PolyBLEP)", "Triangle", "Dent de scie (PolyBLEP)", "Bruit blanc"};
constexpr std::array kColors = {sdl3::Rgba(74, 144, 255), sdl3::Rgba(96, 200, 140), sdl3::Rgba(236, 180, 72), sdl3::Rgba(222, 96, 140)};

/// Curseur logarithmique : t ∈ [0, 1] ↔ fréquence ∈ [kMinHz, kMaxHz].
float ToHz(float t) { return kMinHz * std::pow(kMaxHz / kMinHz, t); }
float FromHz(float hz) { return std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz); }

class Generator final : public app::Application {
protected:
	bool OnInit() override {
		const std::array<float, 4> freqs = {220.f, 440.f, 660.f, 110.f};
		for (size_t i = 0; i < m_osc.size(); ++i) {
			m_osc[i].frequency = freqs[i];
			m_osc[i].amplitude = 0.35f;
			m_osc[i].enabled = i == 0;
			m_osc[i].shape = audio::Waveform(i % 4);
		}
		m_devices = sdl3::EnumeratePlaybackDevices();
		m_deviceNames.push_back("Périphérique par défaut");
		for (const sdl3::AudioDeviceInfo &d : m_devices)
			m_deviceNames.push_back(d.name.c_str());
		OpenDevice();
		return true;
	}

	void OnUpdate(double) override {
		ui::Context &u = Ui();
		if (!u.WantsKeyboard()) {
			sdl3::EventsManager &in = Events();
			if (in.KeyScan(SDL_SCANCODE_SPACE).IsJustPressed())
				m_mute = !m_mute;
			for (int i = 0; i < 4; ++i)
				if (in.KeyScan(SDL_Scancode(SDL_SCANCODE_1 + i)).IsJustPressed())
					m_osc[size_t(i)].enabled = !m_osc[size_t(i)].enabled;
		}
		FeedStream();
		// Niveaux et spectre du signal envoyé (4096 derniers échantillons).
		m_history.Latest(m_window);
		const std::span<const float> recent = std::span(m_window).last(2048);
		m_rms = audio::Rms(recent);
		m_peakHold = std::max(audio::Peak(recent), m_peakHold * 0.97f);
		std::vector<float> analysed = m_window;
		audio::ApplyWindow(analysed, audio::WindowFunction::Hann);
		m_spectrum = audio::MagnitudeDb(audio::Fft(analysed), -100.f);
		for (float &d : m_spectrum)
			d += 6.02f; // compense le gain moyen de la fenêtre de Hann (½)
	}

	void OnRender(const sdl3::FRect &area, double) override {
		const sdl3::FRect side{area.x, area.y, 340.f, area.h};
		const sdl3::FRect main{side.Right(), area.y, area.w - side.w, area.h};
		DrawOscillators(side);
		DrawAnalysis(main);
	}

	void OnShutdown() override { m_stream.reset(); }

private:
	void OpenDevice() {
		m_stream.reset();
		const sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, kRate};
		auto stream = m_device == 0 ? sdl3::AudioStream::OpenPlayback(spec)
									: sdl3::AudioStream::OpenPlayback(spec, m_devices[size_t(m_device - 1)].id);
		if (stream.IsError()) {
			m_status = std::format("Sortie indisponible : {}", std::string_view(stream.Error().GetData(), stream.Error().GetSize()));
			return;
		}
		m_stream.emplace(std::move(stream.Value()));
		m_stream->Resume();
		m_status = std::format("Sortie : {} · {} Hz mono", m_deviceNames[size_t(m_device)], kRate);
	}

	/// Complète la file du flux jusqu'à kLeadSeconds d'avance (aussi sans périphérique :
	/// l'analyse affiche alors le signal qui aurait été joué).
	void FeedStream() {
		const int lead = int(kLeadSeconds * float(kRate) * sizeof(float));
		int guard = 64; // au plus ~0,34 s produits par image
		std::array<float, kBlock> block{};
		while (guard-- > 0 && (m_stream ? m_stream->Queued() < lead : m_produced < size_t(float(kRate) * kLeadSeconds))) {
			block.fill(0.f);
			for (audio::Oscillator &osc : m_osc)
				osc.Render(block, float(kRate), true);
			for (float &s : block)
				s *= m_mute ? 0.f : m_master;
			audio::SoftClip(block, 0.85f);
			m_history.Push(block);
			if (m_stream)
				m_stream->PutData(std::span<const float>(block));
			else
				m_produced += kBlock;
		}
		if (!m_stream)
			m_produced = 0;
	}

	void DrawOscillators(const sdl3::FRect &side) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		c.FillRect(side, th.title);
		c.Line(side.Right(), side.y, side.Right(), side.Bottom(), th.panelBorder);
		u.BeginColumn(side.Inset(14));
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::MUSIC_NOTE), "Oscillateurs");
		for (size_t i = 0; i < m_osc.size(); ++i) {
			audio::Oscillator &osc = m_osc[i];
			u.PushId(uint64_t(i));
			const sdl3::FRect card = u.Next(132);
			c.FillRoundRect(card, 8.f, osc.enabled ? th.widget : sdl3::WithAlpha(th.widget, 120));
			c.FillRoundRect({card.x, card.y, 4.f, card.h}, 2.f, osc.enabled ? kColors[i] : th.panelBorder);
			u.BeginColumn(card.Inset(10.f, 8.f).Inset(4.f, 0.f));
			const sdl3::FRect head = u.Next(24);
			c.Text(head.x, head.y + 4, std::format("Oscillateur {}", i + 1), th.text, 14, sdl3::CanvasFont::Bold);
			u.Toggle({head.Right() - 44.f, head.y + 2.f, 44.f, 20.f}, "on", osc.enabled);
			int shape = int(osc.shape);
			const sdl3::FRect shapes = u.Next(26);
			const float w = shapes.w / float(kShapes.size());
			for (size_t s = 0; s < kShapes.size(); ++s) {
				const sdl3::FRect button{shapes.x + float(s) * w, shapes.y, w - 3.f, shapes.h};
				if (u.ButtonAt(button, kShapes[s], true, shape == int(s)))
					shape = int(s);
				if (u.MouseIn(button))
					u.Tooltip(kShapeNames[s]);
			}
			osc.shape = audio::Waveform(shape);
			float t = FromHz(osc.frequency);
			const sdl3::FRect freqRow = u.Next(24);
			c.Text(freqRow.x, freqRow.y + 4, "Fréquence", th.textDim, 13);
			const std::string hz = osc.frequency >= 1000.f ? std::format("{:.2f} kHz", osc.frequency / 1000.f) : std::format("{:.0f} Hz", osc.frequency);
			c.Text(freqRow.Right() - c.MeasureText(hz, 13), freqRow.y + 4, hz, th.text, 13);
			if (u.SliderAt({freqRow.x + 78.f, freqRow.y, freqRow.w - 78.f - 74.f, freqRow.h}, "freq", t, 0.f, 1.f))
				osc.frequency = ToHz(t);
			const sdl3::FRect ampRow = u.Next(24);
			c.Text(ampRow.x, ampRow.y + 4, "Amplitude", th.textDim, 13);
			const std::string amp = std::format("{:.0f} %", osc.amplitude * 100.f);
			c.Text(ampRow.Right() - c.MeasureText(amp, 13), ampRow.y + 4, amp, th.text, 13);
			u.SliderAt({ampRow.x + 78.f, ampRow.y, ampRow.w - 78.f - 74.f, ampRow.h}, "amp", osc.amplitude, 0.f, 1.f);
			u.EndColumn();
			u.PopId();
		}
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::VOLUME_UP), "Sortie");
		u.SliderFloat("Volume maître", m_master, 0.f, 1.f, "{:.2f}");
		u.Checkbox("Muet (Espace)", m_mute);
		const sdl3::FRect deviceRow = u.Next(30);
		const std::vector<std::string_view> names(m_deviceNames.begin(), m_deviceNames.end());
		int device = m_device;
		if (u.Choice(deviceRow, "device", device, names) && device != m_device) {
			m_device = device;
			OpenDevice();
		}
		u.Wrapped(m_status, th.textDim, 12);
		u.EndColumn();
	}

	void DrawAnalysis(const sdl3::FRect &main) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		const sdl3::FRect body = main.Inset(16);
		const float meterH = 70.f, scopeH = (body.h - meterH - 3 * 34.f) * 0.42f;
		float y = body.y;
		const auto title = [&](std::string_view text) {
			c.Text(body.x, y, text, th.textDim, 12, sdl3::CanvasFont::Bold);
			y += 22.f;
		};
		title("OSCILLOSCOPE — 20 ms");
		u.Scope({body.x, y, body.w, scopeH}, std::span(m_window).last(size_t(kRate / 50)), th.accent);
		y += scopeH + 12.f;
		title("SPECTRE — FFT 4096 points, fenêtre de Hann");
		const float spectrumH = body.Bottom() - meterH - 34.f - y;
		u.SpectrumView({body.x, y, body.w, spectrumH}, m_freqs, m_spectrum, kMinHz, 20000.f, -100.f, 0.f, sdl3::Rgba(96, 200, 160));
		y += spectrumH + 12.f;
		title("NIVEAUX");
		u.LevelMeter({body.x, y, body.w, 16.f}, m_rms, m_rms, "RMS");
		u.LevelMeter({body.x, y + 26.f, body.w, 16.f}, m_rms, m_peakHold, "Crête");
	}

	std::array<audio::Oscillator, 4> m_osc{};
	float m_master = 0.6f;
	bool m_mute = false;
	std::vector<sdl3::AudioDeviceInfo> m_devices;
	std::vector<std::string> m_deviceNames;
	int m_device = 0;
	std::optional<sdl3::AudioStream> m_stream;
	std::string m_status;
	size_t m_produced = 0;
	audio::RingBuffer m_history{kFft};
	std::vector<float> m_window = std::vector<float>(kFft);
	std::vector<float> m_spectrum;
	std::vector<float> m_freqs = audio::BinFrequencies(kFft, float(kRate));
	float m_rms = 0.f, m_peakHold = 0.f;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Générateur de signaux";
	config.width = 1240;
	config.height = 820;
	config.minWidth = 960;
	config.minHeight = 700;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::MUSIC_NOTE);
	config.audio = true;
	config.ParseArgs(argc, argv);
	return Generator().Run(config);
}
