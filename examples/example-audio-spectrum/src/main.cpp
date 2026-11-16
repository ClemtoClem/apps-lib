// example-audio-spectrum — analyseur de spectre temps réel.
//
// Source : un périphérique d'enregistrement (`sdl3::AudioStream::OpenRecording`, lu à
// chaque image) ou un signal de test (balayage logarithmique et harmonique), utile sans
// micro. Le signal, multiplié par le gain, alimente un tampon circulaire ; la FFT (taille
// et fenêtre réglables) donne le spectre en dB, avec une courbe de maintien des crêtes.
// La fréquence dominante est affinée par interpolation parabolique et convertie en note.
//
//   F  geler / reprendre l'affichage          Échap  quitter
//
//   cpp-maker run example-audio-spectrum
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

constexpr std::array<int, 2> kRates = {44100, 48000};
constexpr std::array<size_t, 4> kSizes = {1024, 2048, 4096, 8192};
constexpr std::array<std::string_view, 4> kWindows = {"Rectangulaire", "Hann", "Hamming", "Blackman"};
constexpr size_t kCapacity = 16384;

/// Nom de note (la4 = 440 Hz) et écart en cents.
std::string NoteOf(float hz) {
	static constexpr std::array<std::string_view, 12> kNames = {"do", "do#", "ré", "ré#", "mi", "fa", "fa#", "sol", "sol#", "la", "la#", "si"};
	if (hz < 16.f)
		return "—";
	const float semis = 12.f * std::log2(hz / 440.f) + 69.f; // numéro MIDI
	const int midi = int(std::lround(semis));
	const int cents = int(std::lround((semis - float(midi)) * 100.f));
	return std::format("{}{}  {:+d} cents", kNames[size_t((midi % 12 + 12) % 12)], midi / 12 - 1, cents);
}

class Analyzer final : public app::Application {
protected:
	bool OnInit() override {
		m_devices = sdl3::EnumerateRecordingDevices();
		m_sourceNames.push_back("Signal de test (balayage)");
		m_sourceNames.push_back("Enregistrement par défaut");
		for (const sdl3::AudioDeviceInfo &d : m_devices)
			m_sourceNames.push_back(d.name.c_str());
		m_sweep[0].shape = audio::Waveform::Sawtooth;
		m_sweep[1].shape = audio::Waveform::Sine;
		m_sweep[1].amplitude = 0.15f;
		OpenSource();
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		if (!Ui().WantsKeyboard()) {
			if (Events().KeyScan(SDL_SCANCODE_F).IsJustPressed())
				m_frozen = !m_frozen;
			if (Events().KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
				RequestQuit();
		}
		Capture(float(frameSeconds));
		if (!m_frozen)
			Analyse();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		const sdl3::FRect side{area.x, area.y, 320.f, area.h};
		DrawSettings(side);
		DrawDisplays({side.Right(), area.y, area.w - side.w, area.h});
	}

private:
	[[nodiscard]] int Rate() const { return kRates[size_t(m_rate)]; }
	[[nodiscard]] size_t FftSize() const { return kSizes[size_t(m_size)]; }

	void OpenSource() {
		m_stream.reset();
		m_history.Clear();
		m_hold.clear();
		if (m_source == 0) {
			m_status = "Signal de test : dent de scie en balayage 80 Hz → 5 kHz, plus un sinus fixe à 1 kHz.";
			return;
		}
		const sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, Rate()};
		auto stream = m_source == 1 ? sdl3::AudioStream::OpenRecording(spec)
									: sdl3::AudioStream::OpenRecording(spec, m_devices[size_t(m_source - 2)].id);
		if (stream.IsError()) {
			m_status = std::format("Capture indisponible : {}", std::string_view(stream.Error().GetData(), stream.Error().GetSize()));
			return;
		}
		m_stream.emplace(std::move(stream.Value()));
		m_stream->Resume();
		m_status = std::format("Capture : {} · {} Hz mono", m_sourceNames[size_t(m_source)], Rate());
	}

	/// Lit tout ce qui est disponible (ou synthétise la durée de l'image), applique le gain.
	void Capture(float frameSeconds) {
		const float gain = std::pow(10.f, m_gainDb / 20.f);
		if (m_stream) {
			std::array<float, 1024> block{};
			int got = 0;
			while ((got = m_stream->GetData(std::span<float>(block))) > 0) {
				const size_t n = size_t(got) / sizeof(float);
				for (size_t i = 0; i < n; ++i)
					block[i] *= gain;
				m_history.Push(std::span(block).first(n));
			}
			return;
		}
		if (m_source != 0)
			return;
		// Balayage logarithmique sur 8 s : la fréquence change par blocs de 64 échantillons.
		const size_t total = size_t(std::min(frameSeconds, 0.1f) * float(Rate()));
		std::vector<float> block(total);
		for (size_t done = 0; done < total; done += 64) {
			m_sweepTime += 64.f / float(Rate());
			const float t = std::fmod(m_sweepTime, 8.f) / 8.f;
			m_sweep[0].frequency = 80.f * std::pow(5000.f / 80.f, t);
			m_sweep[1].frequency = 1000.f;
			const std::span<float> chunk = std::span(block).subspan(done, std::min<size_t>(64, total - done));
			m_sweep[0].Render(chunk, float(Rate()));
			m_sweep[1].Render(chunk, float(Rate()), true);
		}
		for (float &s : block)
			s *= gain;
		m_history.Push(block);
	}

	void Analyse() {
		const size_t n = FftSize();
		m_samples.resize(n);
		m_history.Latest(m_samples);
		m_rms = audio::Rms(m_samples);
		m_peak = audio::Peak(m_samples);
		std::vector<float> windowed = m_samples;
		audio::ApplyWindow(windowed, audio::WindowFunction(m_window));
		m_db = audio::MagnitudeDb(audio::Fft(windowed), -120.f);
		static constexpr std::array<float, 4> kWindowGain = {0.f, 6.02f, 5.35f, 7.54f}; // gain cohérent de chaque fenêtre
		for (float &d : m_db)
			d += kWindowGain[size_t(m_window)];
		m_freqs = audio::BinFrequencies(n, float(Rate()));
		// Maintien des crêtes : redescend de 12 dB/s.
		if (m_hold.size() != m_db.size())
			m_hold = m_db;
		const float decay = 12.f * FrameSeconds();
		for (size_t i = 0; i < m_db.size(); ++i)
			m_hold[i] = std::max(m_db[i], m_hold[i] - decay);
		// Fréquence dominante dans la plage affichée, affinée par interpolation parabolique.
		size_t best = 0;
		for (size_t i = 1; i + 1 < m_db.size(); ++i)
			if (m_freqs[i] >= m_minHz && m_freqs[i] <= m_maxHz && (best == 0 || m_db[i] > m_db[best]))
				best = i;
		m_dominantDb = best ? m_db[best] : -120.f;
		if (best && best + 1 < m_db.size()) {
			const float a = m_db[best - 1], b = m_db[best], c = m_db[best + 1];
			const float offset = (a - 2.f * b + c) != 0.f ? 0.5f * (a - c) / (a - 2.f * b + c) : 0.f;
			m_dominant = (float(best) + std::clamp(offset, -0.5f, 0.5f)) * float(Rate()) / float(n);
		}
	}

	void DrawSettings(const sdl3::FRect &side) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		c.FillRect(side, th.title);
		c.Line(side.Right(), side.y, side.Right(), side.Bottom(), th.panelBorder);
		u.BeginColumn(side.Inset(14));
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::MOUSE), "Source");
		std::vector<std::string_view> sources(m_sourceNames.begin(), m_sourceNames.end());
		if (u.Choice(u.Next(30), "source", m_source, sources))
			OpenSource();
		u.Wrapped(m_status, th.textDim, 12);
		u.SliderFloat("Gain", m_gainDb, -20.f, 40.f, "{:+.0f} dB");

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Analyse");
		std::array<std::string, 4> sizeText;
		std::array<std::string_view, 4> sizeNames;
		for (size_t i = 0; i < kSizes.size(); ++i) {
			sizeText[i] = std::format("{} points · {:.1f} Hz", kSizes[i], float(Rate()) / float(kSizes[i]));
			sizeNames[i] = sizeText[i];
		}
		u.Label("Taille de la FFT", th.textDim, 13);
		if (u.Choice(u.Next(30), "size", m_size, sizeNames))
			m_hold.clear();
		u.Label("Fenêtre", th.textDim, 13);
		u.Choice(u.Next(30), "window", m_window, kWindows);
		const std::array<std::string_view, 2> rateNames = {"44 100 Hz", "48 000 Hz"};
		u.Label("Fréquence d'échantillonnage", th.textDim, 13);
		if (u.Choice(u.Next(30), "rate", m_rate, rateNames))
			OpenSource();

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::ASPECT_RATIO), "Affichage");
		// Curseurs logarithmiques : la valeur affichée est la fréquence, pas son logarithme.
		const auto hzSlider = [&](std::string_view label, std::string_view id, float &hz, float lo, float hi) {
			const sdl3::FRect row = u.Next(26);
			c.Text(row.x, row.y + 5, label, th.text, 14);
			const std::string value = hz >= 1000.f ? std::format("{:.1f} kHz", hz / 1000.f) : std::format("{:.0f} Hz", hz);
			c.Text(row.x + 92.f, row.y + 6, value, th.textDim, 13);
			float t = std::log10(hz);
			if (u.SliderAt({row.x + 172.f, row.y, row.w - 172.f, row.h}, id, t, std::log10(lo), std::log10(hi)))
				hz = std::pow(10.f, t);
		};
		hzSlider("Min.", "fmin", m_minHz, 10.f, 1000.f);
		hzSlider("Max.", "fmax", m_maxHz, 1000.f, float(Rate()) / 2.f);
		m_minHz = std::min(m_minHz, m_maxHz * 0.5f);
		u.SliderFloat("Plancher", m_floorDb, -120.f, -40.f, "{:.0f} dB");
		u.Checkbox("Maintien des crêtes", m_showHold);
		u.Checkbox("Geler l'affichage (F)", m_frozen);
		u.EndColumn();
	}

	void DrawDisplays(const sdl3::FRect &main) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		const sdl3::FRect body = main.Inset(16);
		// Bandeau : fréquence dominante, note, niveaux.
		const sdl3::FRect head{body.x, body.y, body.w, 64.f};
		c.FillRoundRect(head, 10.f, th.widget);
		const std::string freq = m_dominantDb > m_floorDb + 6.f
									 ? (m_dominant >= 1000.f ? std::format("{:.3f} kHz", m_dominant / 1000.f) : std::format("{:.1f} Hz", m_dominant))
									 : std::string("—");
		c.Text(head.x + 16, head.y + 8, "FRÉQUENCE DOMINANTE", th.textDim, 11, sdl3::CanvasFont::Bold);
		c.Text(head.x + 16, head.y + 26, freq, th.text, 26, sdl3::CanvasFont::Bold);
		c.Text(head.x + 230, head.y + 8, "NOTE", th.textDim, 11, sdl3::CanvasFont::Bold);
		c.Text(head.x + 230, head.y + 30, m_dominantDb > m_floorDb + 6.f ? NoteOf(m_dominant) : "—", th.accent, 18, sdl3::CanvasFont::Bold);
		const float metersX = head.x + 470.f;
		u.LevelMeter({metersX, head.y + 12, head.Right() - metersX - 16, 14}, m_rms, m_rms, "RMS");
		u.LevelMeter({metersX, head.y + 36, head.Right() - metersX - 16, 14}, m_rms, m_peak, "Crête");
		if (m_frozen)
			u.Badge({head.Right() - 90.f, head.y - 10.f, 80.f, 20.f}, "GELÉ", th.warning);

		float y = head.Bottom() + 16.f;
		const float scopeH = (body.Bottom() - y) * 0.28f;
		c.Text(body.x, y, std::format("FORME D'ONDE — {} échantillons", m_samples.size()), th.textDim, 12, sdl3::CanvasFont::Bold);
		y += 20.f;
		u.Scope({body.x, y, body.w, scopeH}, m_samples, th.accent);
		y += scopeH + 14.f;
		c.Text(body.x, y, std::format("SPECTRE — {}", kWindows[size_t(m_window)]), th.textDim, 12, sdl3::CanvasFont::Bold);
		y += 20.f;
		const sdl3::FRect spectrum{body.x, y, body.w, body.Bottom() - y};
		u.SpectrumView(spectrum, m_freqs, m_db, m_minHz, m_maxHz, m_floorDb, 0.f, sdl3::Rgba(96, 200, 160));
		if (m_showHold && !m_hold.empty()) { // maintien des crêtes en trait, par-dessus
			c.PushClip(spectrum);
			DrawCurve(spectrum, m_hold, sdl3::WithAlpha(th.warning, 200));
			c.PopClip();
		}
	}

	/// Courbe en trait superposée à la vue (mêmes axes que SpectrumView).
	void DrawCurve(const sdl3::FRect &r, const std::vector<float> &db, sdl3::Color color) {
		const sdl3::FRect plot{r.x + 34.f, r.y + 6.f, std::max(1.f, r.w - 40.f), std::max(1.f, r.h - 24.f)};
		const float logMin = std::log10(m_minHz), logMax = std::log10(m_maxHz);
		std::vector<float> xy;
		for (size_t i = 1; i < db.size() && i < m_freqs.size(); ++i) {
			if (m_freqs[i] < m_minHz || m_freqs[i] > m_maxHz)
				continue;
			xy.push_back(plot.x + plot.w * (std::log10(m_freqs[i]) - logMin) / (logMax - logMin));
			xy.push_back(plot.y + plot.h * (1.f - std::clamp((db[i] - m_floorDb) / -m_floorDb, 0.f, 1.f)));
		}
		if (xy.size() >= 4)
			Canvas().Polyline(xy.data(), xy.size() / 2, color, 1.2f);
	}

	std::vector<sdl3::AudioDeviceInfo> m_devices;
	std::vector<std::string> m_sourceNames;
	int m_source = 0, m_rate = 1, m_size = 2, m_window = 1;
	std::optional<sdl3::AudioStream> m_stream;
	std::string m_status;
	std::array<audio::Oscillator, 2> m_sweep{};
	float m_sweepTime = 0.f;
	float m_gainDb = 0.f, m_minHz = 30.f, m_maxHz = 16000.f, m_floorDb = -100.f;
	bool m_showHold = true, m_frozen = false;
	audio::RingBuffer m_history{kCapacity};
	std::vector<float> m_samples, m_db, m_hold, m_freqs;
	float m_rms = 0.f, m_peak = 0.f, m_dominant = 0.f, m_dominantDb = -120.f;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Analyseur de spectre";
	config.width = 1240;
	config.height = 800;
	config.minWidth = 1000;
	config.minHeight = 640;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::TIMELINE);
	config.audio = true;
	config.ParseArgs(argc, argv);
	return Analyzer().Run(config);
}
