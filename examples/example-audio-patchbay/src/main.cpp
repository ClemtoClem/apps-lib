#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <audio/dsp.hpp>
#include <audio/reverb.hpp>
#include <audio/synth.hpp>
#include <sdl3/io/filesystem.hpp>
#include <sdl3/input/audio.hpp>
#include <sdl3/input/mixer.hpp>
#include <ui/node_graph.hpp>

namespace {

constexpr int kRate = 48000;
constexpr size_t kBlock = 256;
constexpr float kLead = 0.08f; ///< avance de la file de lecture (secondes)
constexpr size_t kFft = 4096;
const std::filesystem::path kSounds = "assets/sounds";

std::filesystem::path SavePath() {
	return std::filesystem::path(sdl3::filesystem::AppDataDir("example-audio-patchbay").c_str()) / "patch.json";
}

// ── Métadonnées d'affichage ─────────────────────────────────────────────────
struct Kind {
	std::string_view id, title;
	sdl3::Color header;
	int inputs, outputs; ///< broches « audio »
	float body;			 ///< hauteur du corps
	float width;		 ///< largeur du nœud
};
constexpr std::array kKinds = {
	Kind{"osc", "Oscillateur", sdl3::Rgba(52, 96, 160), 0, 1, 92, 200.f},
	Kind{"noise", "Bruit coloré", sdl3::Rgba(70, 96, 140), 0, 1, 64, 200.f},
	Kind{"synth", "Clavier", sdl3::Rgba(150, 90, 140), 0, 1, 216, 260.f},
	Kind{"player", "Lecteur", sdl3::Rgba(52, 120, 120), 0, 1, 92, 200.f},
	Kind{"mic", "Entrée", sdl3::Rgba(60, 110, 150), 0, 1, 60, 200.f},
	Kind{"filter", "Filtre", sdl3::Rgba(120, 90, 40), 1, 1, 92, 200.f},
	Kind{"gain", "Gain", sdl3::Rgba(110, 80, 50), 1, 1, 30, 200.f},
	Kind{"mixer", "Mélangeur", sdl3::Rgba(100, 70, 130), 4, 1, 30, 200.f},
	Kind{"delay", "Retard", sdl3::Rgba(130, 70, 90), 1, 1, 92, 200.f},
	Kind{"clip", "Écrêtage", sdl3::Rgba(140, 60, 60), 1, 1, 54, 200.f},
	Kind{"reverb", "Réverbération", sdl3::Rgba(90, 70, 130), 1, 1, 92, 200.f},
	Kind{"limiter", "Limiteur", sdl3::Rgba(120, 100, 50), 1, 1, 54, 200.f},
	Kind{"scope", "Oscilloscope", sdl3::Rgba(48, 110, 80), 1, 1, 134, 240.f},
	Kind{"spectrum", "Spectre", sdl3::Rgba(48, 110, 80), 1, 1, 154, 240.f},
	Kind{"output", "Sortie", sdl3::Rgba(70, 76, 92), 1, 0, 84, 200.f},
};
const Kind *FindKind(std::string_view id) {
	for (const Kind &k : kKinds)
		if (k.id == id)
			return &k;
	return nullptr;
}

constexpr std::array<std::string_view, 5> kShapes = {"Sinus", "Carré", "Triangle", "Scie", "Bruit"};
constexpr std::array<std::string_view, 3> kFilters = {"Passe-bas", "Passe-haut", "Passe-bande"};
constexpr std::array<std::string_view, 3> kNoises = {"Blanc", "Rose", "Brun"};
constexpr std::array<std::string_view, 4> kWindows = {"Rectangulaire", "Hann", "Hamming", "Blackman"};
constexpr std::array<std::string_view, 12> kNotes = {"do", "do#", "ré", "ré#", "mi", "fa", "fa#", "sol", "sol#", "la", "la#", "si"};

// ── Accesseurs de propriétés (JSON-friendly) ────────────────────────────────
float Prop(const ui::GraphNode &n, std::string_view key, float fallback) {
	const auto it = n.props.find(std::string(key));
	return it == n.props.end() ? fallback : std::strtof(it->second.c_str(), nullptr);
}
void SetProp(ui::GraphNode &n, std::string_view key, float value) {
	n.props[std::string(key)] = std::format("{}", value);
}

/// Index de périphérique valide pour une liste dont l'entrée 0 est « par défaut ».
int DeviceIndex(size_t count, float prop) {
	if (count == 0)
		return 0;
	return std::clamp(int(prop), 0, int(count) - 1);
}

/// Fréquence (Hz) d'un numéro de note MIDI.
float MidiHz(int midi) { return 440.f * std::exp2(float(midi - 69) / 12.f); }

// ── Contextes injectés dans les nœuds ───────────────────────────────────────
struct AudioContext {
	float rate;
	const std::map<int, std::array<float, kBlock>> &inputs; ///< bloc capté par périphérique d'entrée
	size_t inputCount;										///< nombre de périphériques d'entrée (0 : par défaut)
	const std::vector<std::string> &files;
	const std::filesystem::path &soundsDir;
	sdl3::MixerContext *mixer;
};
struct UiContext {
	const std::vector<float> &freqs;
	const std::vector<std::string> &files;
	const std::vector<std::string> &inputNames;  ///< périphériques d'entrée (0 : par défaut)
	const std::vector<std::string> &outputNames; ///< périphériques de sortie (0 : par défaut)
	const std::vector<bool> &inputFailed;		 ///< échec d'ouverture par périphérique d'entrée
};

// ── Interface d'un bloc audio ───────────────────────────────────────────────
class AudioNode {
public:
	virtual ~AudioNode() = default;

	/// Traite un bloc de kBlock échantillons (entrées déjà sommées dans `buf`).
	virtual void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) = 0;

	/// Dessine le corps du nœud (contrôles, visualisations).
	virtual void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body, const UiContext &ctx) = 0;

	/// Indique si la sortie doit être mixée vers le périphérique.
	virtual bool IsOutput() const { return false; }

	/// Met à jour l'historique et les niveaux (appelé par l'orchestrateur).
	void UpdateMetrics(const std::array<float, kBlock> &buf) {
		history.Push(buf);
		rms = audio::Rms(buf);
		peak = std::max(audio::Peak(buf), peak * 0.95f);
	}

	audio::RingBuffer history{kFft}; ///< historique pour oscillo / spectre
	float rms = 0.f, peak = 0.f;

protected:
	// Helpers UI communs aux nœuds.
	static void Slider(ui::Context &u, ui::GraphNode &node, std::string_view label, std::string_view key, float lo, float hi,
					   float def, std::string_view unit, bool log = false) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Theme &th = u.Style();
		const sdl3::FRect row = u.Next(24);
		float v = Prop(node, key, def);
		c.Text(row.x, row.y + 5, label, th.textDim, 12);
		const std::string text = v >= 1000.f && unit == "Hz" ? std::format("{:.1f} k", v / 1000.f)
															 : std::format("{:.{}f} {}", v, v < 10.f ? 2 : 0, unit);
		c.Text(row.Right() - c.MeasureText(text, 12), row.y + 5, text, th.text, 12);
		float t = log ? std::log10(v) : v;
		if (u.SliderAt({row.x + 62.f, row.y, row.w - 62.f - 58.f, row.h}, key, t, log ? std::log10(lo) : lo,
					   log ? std::log10(hi) : hi))
			SetProp(node, key, log ? std::pow(10.f, t) : t);
	}

	static void ChoiceRow(ui::Context &u, ui::GraphNode &node, std::string_view key, std::span<const std::string_view> options) {
		if (options.empty())
			return;
		int index = std::clamp(int(Prop(node, key, 0.f)), 0, int(options.size()) - 1);
		if (u.Choice(u.Next(26), key, index, options))
			SetProp(node, key, float(index));
	}
};

// ── Oscillateur (glissando de fréquence et d'amplitude lissés) ──────────────
class OscillatorNode final : public AudioNode {
public:
	OscillatorNode() : m_freq(220.f, 0.02f), m_amp(0.4f, 0.02f) {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		m_osc.shape = audio::Waveform(std::clamp(int(Prop(node, "shape", 0.f)), 0, 4));
		m_freq.SetTarget(Prop(node, "freq", 220.f));
		m_amp.SetTarget(Prop(node, "amp", 0.4f));
		for (float &s : buf) {
			m_osc.frequency = m_freq.Next(ctx.rate);
			m_osc.amplitude = m_amp.Next(ctx.rate);
			s = m_osc.Next(ctx.rate);
		}
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		ChoiceRow(u, node, "shape", kShapes);
		Slider(u, node, "Fréquence", "freq", 20.f, 8000.f, 220.f, "Hz", true);
		Slider(u, node, "Niveau", "amp", 0.f, 1.f, 0.4f, "");
	}

private:
	audio::Oscillator m_osc;
	audio::Smoother m_freq, m_amp;
};

// ── Bruit coloré (blanc, rose, brun) ────────────────────────────────────────
class NoiseNode final : public AudioNode {
public:
	NoiseNode()
		: m_noise{audio::Noise(audio::NoiseColor::White), audio::Noise(audio::NoiseColor::Pink), audio::Noise(audio::NoiseColor::Brown)} {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &) override {
		const int color = std::clamp(int(Prop(node, "color", 0.f)), 0, 2);
		const float level = Prop(node, "level", 0.3f);
		for (float &s : buf)
			s = m_noise[size_t(color)].Next() * level;
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		ChoiceRow(u, node, "color", kNoises);
		Slider(u, node, "Niveau", "level", 0.f, 1.f, 0.3f, "");
	}

private:
	std::array<audio::Noise, 3> m_noise;
};

// ── Clavier polyphonique (voix + enveloppes, panoramique à puissance constante) ─
class SynthNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		const audio::Waveform shape = audio::Waveform(std::clamp(int(Prop(node, "shape", 0.f)), 0, 4));
		for (audio::Voice &v : m_voices)
			v.shape = shape;
		for (float &s : buf) {
			float left = 0.f, right = 0.f, send = 0.f;
			for (audio::Voice &v : m_voices)
				if (v.Active())
					v.Mix(ctx.rate, left, right, send);
			s = (left + right) * 0.7071f; // réduction mono à gain unitaire (pan centré)
		}
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		ChoiceRow(u, node, "shape", kShapes);
		Slider(u, node, "Octave", "octave", 2.f, 5.f, 4.f, "");
		Slider(u, node, "Niveau", "level", 0.f, 1.f, 0.35f, "");
		Slider(u, node, "Attaque", "attack", 0.001f, 0.2f, 0.01f, "s");
		Slider(u, node, "Durée", "duration", 0.05f, 2.f, 0.5f, "s");
		DrawKeys(u, node, u.Next(56));
	}

private:
	void DrawKeys(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &r) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Theme &th = u.Style();
		const int octave = std::clamp(int(Prop(node, "octave", 4.f)), 2, 5);
		const float level = Prop(node, "level", 0.35f);
		const float attack = Prop(node, "attack", 0.01f);
		const float duration = Prop(node, "duration", 0.5f);
		const float whiteW = r.w / 7.f;
		static constexpr std::array<int, 7> kWhite = {0, 2, 4, 5, 7, 9, 11};
		static constexpr std::array<int, 5> kBlack = {1, 3, 6, 8, 10};
		for (int i = 0; i < 7; ++i) {
			const int midi = (octave + 1) * 12 + kWhite[size_t(i)];
			const sdl3::FRect k{r.x + float(i) * whiteW, r.y, whiteW - 1.f, r.h};
			const bool hot = u.Interact(u.MakeId(std::format("w{}", midi)), k).hovered;
			c.FillRoundRect(k, 3.f, hot ? th.accent : th.widgetActive);
			c.Line(k.x, k.Bottom(), k.Right(), k.Bottom(), th.panelBorder);
			c.Text(k.x + (k.w - c.MeasureText(kNotes[size_t(kWhite[size_t(i)])], 12)) * 0.5f, k.Bottom() - 18.f,
				   kNotes[size_t(kWhite[size_t(i)])], hot ? th.accentText : th.textDim, 12);
			if (u.Interact(u.MakeId(std::format("w{}", midi)), k).leftClicked)
				Play(midi, level, attack, duration);
		}
		for (int i = 0; i < 5; ++i) {
			const int midi = (octave + 1) * 12 + kBlack[size_t(i)];
			const sdl3::FRect k{r.x + whiteW * float(i + 1) - whiteW * 0.25f, r.y, whiteW * 0.5f, r.h * 0.62f};
			const bool hot = u.Interact(u.MakeId(std::format("b{}", midi)), k).hovered;
			c.FillRoundRect(k, 3.f, hot ? th.accent : th.text);
			if (u.Interact(u.MakeId(std::format("b{}", midi)), k).leftClicked)
				Play(midi, level, attack, duration);
		}
	}

	void Play(int midi, float level, float attack, float duration) {
		audio::Note note;
		note.from = note.to = MidiHz(midi);
		note.duration = duration;
		note.level = level;
		note.attack = attack;
		m_voices[m_next].Play(note);
		m_next = (m_next + 1) % m_voices.size();
	}

	std::array<audio::Voice, 8> m_voices{};
	size_t m_next = 0;
};

// ── Lecteur (décodeur SDL3_mixer encapsulé) ─────────────────────────────────
class PlayerNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		if (ctx.files.empty() || Prop(node, "playing", 1.f) < 0.5f || !ctx.mixer)
			return;
		const int idx = std::clamp(int(Prop(node, "file", 0.f)), 0, int(ctx.files.size()) - 1);
		const std::string &file = ctx.files[size_t(idx)];
		if (!m_decoder || m_decoderFile != file) {
			m_decoder.reset();
			auto decoder = sdl3::AudioDecoder::Create(String((ctx.soundsDir / file).string().c_str()));
			if (decoder.IsError())
				return;
			m_decoder.emplace(std::move(decoder.Value()));
			m_decoderFile = file;
		}
		const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
		const int bytes = int(kBlock * sizeof(float));
		const int got = m_decoder->Decode(buf.data(), bytes, &spec);
		if (got <= 0) { // fin de fichier : on repart du début au prochain bloc
			m_decoder.reset();
			return;
		}
		const float gain = Prop(node, "gain", 0.8f);
		for (size_t i = 0; i < kBlock; ++i)
			buf[i] = i < size_t(got) / sizeof(float) ? buf[i] * gain : 0.f;
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &ctx) override {
		std::vector<std::string_view> names(ctx.files.begin(), ctx.files.end());
		if (names.empty())
			names.push_back("(aucun fichier dans assets/sounds)");
		ChoiceRow(u, node, "file", names);
		const sdl3::FRect row = u.Next(26);
		const bool playing = Prop(node, "playing", 1.f) >= 0.5f;
		if (u.ButtonAt({row.x, row.y, 90.f, row.h}, playing ? "Pause" : "Lecture", true, playing)) {
			SetProp(node, "playing", playing ? 0.f : 1.f);
			m_decoder.reset();
		}
		Slider(u, node, "Niveau", "gain", 0.f, 1.5f, 0.8f, "");
	}

private:
	std::optional<sdl3::AudioDecoder> m_decoder;
	std::string m_decoderFile;
};

// ── Entrée audio : copie le bloc capturé sur le périphérique choisi ─────────
class MicNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		const int dev = DeviceIndex(ctx.inputCount, Prop(node, "device", 0.f));
		const auto it = ctx.inputs.find(dev);
		buf = it != ctx.inputs.end() ? it->second : std::array<float, kBlock>{};
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &ctx) override {
		if (ctx.inputNames.empty()) {
			u.Label("Aucun périphérique d'entrée", u.Style().textDim, 12);
			return;
		}
		std::vector<std::string_view> names(ctx.inputNames.begin(), ctx.inputNames.end());
		ChoiceRow(u, node, "device", names);
		const int dev = DeviceIndex(ctx.inputNames.size(), Prop(node, "device", 0.f));
		const bool failed = size_t(dev) < ctx.inputFailed.size() && ctx.inputFailed[size_t(dev)];
		u.Label(failed ? "Entrée indisponible" : std::format("Entrée : {}", ctx.inputNames[size_t(dev)]),
				failed ? u.Style().danger : u.Style().textDim, 12);
	}
};

// ── Filtre biquad (coupure et résonance lissées) ────────────────────────────
class FilterNode final : public AudioNode {
public:
	FilterNode() : m_cut(1200.f, 0.02f), m_q(0.7071f, 0.02f) {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		m_cut.SetTarget(Prop(node, "cutoff", 1200.f));
		m_q.SetTarget(Prop(node, "q", 0.7071f));
		m_filter.Design(audio::BiQuadKind(std::clamp(int(Prop(node, "type", 0.f)), 0, 2)), ctx.rate,
						m_cut.Next(ctx.rate), m_q.Next(ctx.rate));
		m_filter.Process(buf);
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		ChoiceRow(u, node, "type", kFilters);
		Slider(u, node, "Coupure", "cutoff", 40.f, 16000.f, 1200.f, "Hz", true);
		Slider(u, node, "Résonance", "q", 0.3f, 12.f, 0.7071f, "");
	}

private:
	audio::BiQuad m_filter;
	audio::Smoother m_cut, m_q;
};

// ── Gain / Mélangeur (même DSP, nombre d'entrées différent) ─────────────────
class GainNode final : public AudioNode {
public:
	GainNode() : m_gain(1.f, 0.02f) {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		m_gain.SetTarget(Prop(node, "gain", 1.f));
		const float g = m_gain.Next(ctx.rate);
		for (float &s : buf)
			s *= g;
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Gain", "gain", 0.f, 2.f, 1.f, "");
	}

private:
	audio::Smoother m_gain;
};

// ── Retard (delay line encapsulée) ──────────────────────────────────────────
class DelayNode final : public AudioNode {
public:
	DelayNode() : m_delay(size_t(kRate)) {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		m_delay.SetDelay(size_t(Prop(node, "ms", 250.f) * ctx.rate / 1000.f));
		m_delay.feedback = Prop(node, "feedback", 0.35f);
		m_delay.mix = Prop(node, "mix", 0.4f);
		m_delay.Process(buf);
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Temps", "ms", 5.f, 1000.f, 250.f, "ms");
		Slider(u, node, "Réinject.", "feedback", 0.f, 0.95f, 0.35f, "");
		Slider(u, node, "Mélange", "mix", 0.f, 1.f, 0.4f, "");
	}

private:
	audio::DelayLine m_delay;
};

// ── Écrêtage doux ───────────────────────────────────────────────────────────
class ClipNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &) override {
		const float drive = Prop(node, "drive", 2.f);
		const float threshold = Prop(node, "threshold", 0.6f);
		for (float &s : buf)
			s = audio::SoftClip(s * drive, threshold) / std::max(1.f, drive * threshold);
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Saturation", "drive", 1.f, 10.f, 2.f, "");
		Slider(u, node, "Seuil", "threshold", 0.1f, 0.95f, 0.6f, "");
	}
};

// ── Réverbération de Schroeder ──────────────────────────────────────────────
class ReverbNode final : public AudioNode {
public:
	ReverbNode() : m_reverb(float(kRate)) {}
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &) override {
		m_reverb.SetMix(Prop(node, "mix", 0.3f));
		m_reverb.SetRoomSize(Prop(node, "size", 0.6f));
		m_reverb.SetDamping(Prop(node, "damping", 0.5f));
		m_reverb.Process(buf.data(), buf.size());
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Mélange", "mix", 0.f, 1.f, 0.3f, "");
		Slider(u, node, "Taille", "size", 0.f, 1.f, 0.6f, "");
		Slider(u, node, "Amorti", "damping", 0.f, 1.f, 0.5f, "");
	}

private:
	audio::Reverb m_reverb;
};

// ── Limiteur crête ──────────────────────────────────────────────────────────
class LimiterNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		m_limiter.SetThreshold(Prop(node, "threshold", 0.5f));
		m_limiter.SetRelease(Prop(node, "release", 0.3f));
		for (float &s : buf) {
			float right = 0.f;
			m_limiter.Process(s, right, ctx.rate);
		}
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Seuil", "threshold", 0.05f, 0.95f, 0.5f, "");
		Slider(u, node, "Relâche.", "release", 0.01f, 1.f, 0.3f, "s");
	}

private:
	audio::Limiter m_limiter;
};

// ── Oscilloscope (pass-through + visualisation) ─────────────────────────────
class ScopeNode final : public AudioNode {
public:
	void Process(ui::GraphNode &, std::array<float, kBlock> &, const AudioContext &) override {}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &) override {
		Slider(u, node, "Amplitude", "gain", 0.1f, 4.f, 1.f, "×");
		std::vector<float> samples(1024);
		history.Latest(samples);
		u.Scope(u.Remaining(), samples, sdl3::Rgba(110, 200, 160), Prop(node, "gain", 1.f));
	}
};

// ── Spectre (pass-through + FFT, fenêtre réglable) ──────────────────────────
class SpectrumNode final : public AudioNode {
public:
	void Process(ui::GraphNode &, std::array<float, kBlock> &, const AudioContext &) override {}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &ctx) override {
		ChoiceRow(u, node, "window", kWindows);
		Slider(u, node, "Plancher", "floor", -120.f, -40.f, -100.f, "dB");
		std::vector<float> samples(kFft);
		history.Latest(samples);
		audio::ApplyWindow(samples, audio::WindowFunction(std::clamp(int(Prop(node, "window", 1.f)), 0, 3)));
		static constexpr std::array<float, 4> kGain = {0.f, 6.02f, 5.35f, 7.54f}; // gain cohérent de chaque fenêtre
		std::vector<float> db = audio::MagnitudeDb(audio::Fft(samples), Prop(node, "floor", -100.f));
		for (float &d : db)
			d += kGain[size_t(std::clamp(int(Prop(node, "window", 1.f)), 0, 3))];
		u.SpectrumView(u.Remaining(), ctx.freqs, db, 30.f, 16000.f, Prop(node, "floor", -100.f), 0.f, sdl3::Rgba(110, 200, 160));
	}
};

// ── Sortie : volume, limiteur de protection et périphérique au choix ────────
class OutputNode final : public AudioNode {
public:
	void Process(ui::GraphNode &node, std::array<float, kBlock> &buf, const AudioContext &ctx) override {
		const float vol = Prop(node, "volume", 0.7f);
		for (float &s : buf) {
			s *= vol;
			float right = 0.f;
			m_limiter.Process(s, right, ctx.rate);
		}
	}
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &, const UiContext &ctx) override {
		if (!ctx.outputNames.empty()) {
			std::vector<std::string_view> names(ctx.outputNames.begin(), ctx.outputNames.end());
			ChoiceRow(u, node, "device", names);
		}
		Slider(u, node, "Volume", "volume", 0.f, 1.f, 0.7f, "");
		u.LevelMeter(u.Next(18), rms, peak, "Niveau");
	}
	bool IsOutput() const override { return true; }

private:
	audio::Limiter m_limiter{0.9f, 0.3f};
};

// ── Fabrique ────────────────────────────────────────────────────────────────
std::unique_ptr<AudioNode> MakeAudioNode(std::string_view id) {
	if (id == "osc")      return std::make_unique<OscillatorNode>();
	if (id == "noise")    return std::make_unique<NoiseNode>();
	if (id == "synth")    return std::make_unique<SynthNode>();
	if (id == "player")   return std::make_unique<PlayerNode>();
	if (id == "mic")      return std::make_unique<MicNode>();
	if (id == "filter")   return std::make_unique<FilterNode>();
	if (id == "gain" || id == "mixer") return std::make_unique<GainNode>();
	if (id == "delay")    return std::make_unique<DelayNode>();
	if (id == "clip")     return std::make_unique<ClipNode>();
	if (id == "reverb")   return std::make_unique<ReverbNode>();
	if (id == "limiter")  return std::make_unique<LimiterNode>();
	if (id == "scope")    return std::make_unique<ScopeNode>();
	if (id == "spectrum") return std::make_unique<SpectrumNode>();
	if (id == "output")   return std::make_unique<OutputNode>();
	return nullptr;
}

// ── Application : orchestration uniquement ──────────────────────────────────
class Patchbay final : public app::Application {
protected:
	bool OnInit() override {
		if (auto mixer = sdl3::MixerContext::Create(); mixer.IsOk())
			m_mixer.emplace(std::move(mixer.Value()));
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(kSounds, ec))
			if (const std::string ext = entry.path().extension().string();
				ext == ".ogg" || ext == ".mp3" || ext == ".wav" || ext == ".flac")
				m_files.push_back(entry.path().filename().string());
		std::ranges::sort(m_files);
		m_outputDevices = sdl3::EnumeratePlaybackDevices();
		m_outputNames.push_back("Périphérique par défaut");
		for (const sdl3::AudioDeviceInfo &d : m_outputDevices)
			m_outputNames.push_back(d.name.c_str());
		m_inputDevices = sdl3::EnumerateRecordingDevices();
		m_inputNames.push_back("Périphérique par défaut");
		for (const sdl3::AudioDeviceInfo &d : m_inputDevices)
			m_inputNames.push_back(d.name.c_str());
		m_inputFailed.assign(m_inputNames.size(), false);
		OpenOutput(0);
		BuildDefaultPatch();
		SetupEditor();
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		ui::Context &u = Ui();
		if (!u.WantsKeyboard() && Events().KeyScan(SDL_SCANCODE_SPACE).IsJustPressed() &&
			!Events().KeyScan(SDL_SCANCODE_LCTRL).IsPressed())
			m_mute = !m_mute;
		if (u.Ctrl() && u.KeycodePressed(SDLK_S)) Save();
		if (u.Ctrl() && u.KeycodePressed(SDLK_O)) Load();
		Process(frameSeconds);
	}

	void OnRender(const sdl3::FRect &area, double) override {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::FRect status{area.x, area.Bottom() - 28.f, area.w, 28.f};
		const sdl3::FRect canvas{area.x, area.y, area.w, area.h - status.h};
		if (m_firstFrame) {
			m_editor.Draw(Ui(), canvas, m_graph);
			m_editor.FrameAll(m_graph);
			m_firstFrame = false;
		}
		m_editor.Draw(Ui(), canvas, m_graph);
		c.FillRect(status, th.title);
		c.Line(status.x, status.y, status.Right(), status.y, th.panelBorder);
		std::string left = m_output ? std::format("Sortie : {} · {} Hz mono · {} ms d'avance",
												  m_outputNames[size_t(m_outputDevice)], kRate, int(kLead * 1000.f))
									: std::string("Aucune sortie audio (affichage seul)");
		if (m_cycle)
			left = "Cycle détecté : le graphe n'est pas évalué";
		else if (m_mute)
			left += " · MUET (Espace)";
		c.Text(status.x + 12, status.y + 7, left, m_cycle ? th.danger : (m_mute ? th.warning : th.textDim), 12);
		const std::string_view help = "Clic droit : ajouter · Espace : muet · Ctrl+S / Ctrl+O";
		c.Text(status.Right() - 12 - c.MeasureText(help, 12), status.y + 7, help, th.textDim, 12);
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "save", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SAVE), .tooltip = "Sauver (Ctrl+S)"});
		left.push_back({.id = "load", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), .tooltip = "Charger (Ctrl+O)"});
		left.push_back({.id = "frame", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CENTER_FOCUS_STRONG), .tooltip = "Tout cadrer (F)", .separator = true});
		left.push_back({.id = "mute", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::VOLUME_UP), .tooltip = "Couper le son (Espace)", .active = m_mute});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "save")       Save();
		else if (id == "load")  Load();
		else if (id == "frame") m_editor.FrameAll(m_graph);
		else if (id == "mute")  m_mute = !m_mute;
	}

	void OnShutdown() override {
		m_processors.clear(); // détruit les décodeurs avant le contexte SDL3_mixer
		m_output.reset();
		m_inputs.clear();
	}

private:
	// ── Graphe ──────────────────────────────────────────────────────────────
	uint32_t Add(std::string_view kindId, ui::GraphPoint at) {
		const Kind *kind = FindKind(kindId);
		ui::GraphNode &node = m_graph.AddNode(std::string(kind->id), std::string(kind->title), at, kind->header);
		const uint32_t id = node.id;
		node.width = kind->width;
		node.bodyHeight = kind->body;
		static constexpr std::array<std::string_view, 4> kInputs = {"a", "b", "c", "d"};
		for (int i = 0; i < kind->inputs; ++i)
			m_graph.AddInput(id, kind->inputs > 1 ? std::string(kInputs[size_t(i)]) : "entrée", "audio",
							 ui::PinShape::Circle, sdl3::Rgba(110, 200, 160));
		for (int i = 0; i < kind->outputs; ++i)
			m_graph.AddOutput(id, "sortie", "audio", ui::PinShape::Triangle, sdl3::Rgba(110, 200, 160));
		return id;
	}

	void Link(uint32_t from, uint32_t to, size_t input = 0) {
		m_graph.Connect(m_graph.FindNode(from)->outputs[0], m_graph.FindNode(to)->inputs[input]);
	}

	void BuildDefaultPatch() {
		const uint32_t osc = Add("osc", {40, 60}), filter = Add("filter", {300, 40}), delay = Add("delay", {560, 40});
		const uint32_t scope = Add("scope", {820, 30}), player = Add("player", {40, 330}), gain = Add("gain", {300, 350});
		const uint32_t spectrum = Add("spectrum", {560, 320}), synth = Add("synth", {40, 560});
		const uint32_t noise = Add("noise", {40, 850}), reverb = Add("reverb", {300, 850}), limiter = Add("limiter", {560, 850});
		const uint32_t mixer = Add("mixer", {900, 300}), output = Add("output", {1160, 300});
		ui::GraphNode *n = m_graph.FindNode(osc);
		SetProp(*n, "shape", 3.f); // dent de scie : riche en harmoniques pour le filtre
		SetProp(*n, "freq", 110.f);
		SetProp(*m_graph.FindNode(filter), "cutoff", 900.f);
		SetProp(*m_graph.FindNode(player), "playing", 0.f);
		SetProp(*m_graph.FindNode(synth), "level", 0.3f);
		SetProp(*m_graph.FindNode(reverb), "mix", 0.4f);
		SetProp(*m_graph.FindNode(noise), "level", 0.25f);
		Link(osc, filter);
		Link(filter, delay);
		Link(delay, scope);
		Link(player, gain);
		Link(gain, spectrum);
		Link(noise, reverb);
		Link(reverb, limiter);
		Link(scope, mixer, 0);
		Link(spectrum, mixer, 1);
		Link(limiter, mixer, 2);
		Link(synth, mixer, 3);
		Link(mixer, output);
	}

	void SetupEditor() {
		ui::NodeGraphCallbacks &cb = m_editor.callbacks;
		cb.drawBody = [this](ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body, float) { DrawBody(u, node, body); };
		cb.canvasMenu = [this](ui::GraphPoint at) {
			std::vector<ui::GraphMenuItem> items;
			for (const Kind &k : kKinds)
				items.push_back({std::format("Ajouter : {}", k.title), [this, &k, at] { Add(k.id, at); }, ""});
			return items;
		};
		cb.nodeMenu = [this](ui::GraphNode &node) {
			const uint32_t id = node.id;
			return std::vector<ui::GraphMenuItem>{{"Supprimer", [this, id] { m_graph.RemoveNode(id); }, "Suppr"}};
		};
		cb.linkMenu = [this](ui::GraphLink &link) {
			const uint32_t id = link.id;
			return std::vector<ui::GraphMenuItem>{{"Supprimer le lien", [this, id] { m_graph.Disconnect(id); }, "Suppr"}};
		};
	}

	// ── Périphériques ──────────────────────────────────────────────────────
	void OpenOutput(int idx) {
		m_output.reset();
		m_outputDevice = idx;
		const sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, kRate};
		auto stream = idx == 0 ? sdl3::AudioStream::OpenPlayback(spec)
							   : sdl3::AudioStream::OpenPlayback(spec, m_outputDevices[size_t(idx - 1)].id);
		if (stream.IsOk()) {
			m_output.emplace(std::move(stream.Value()));
			m_output->Resume();
		}
	}

	std::optional<sdl3::AudioStream> OpenInput(int idx) {
		const sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, kRate};
		auto stream = idx == 0 ? sdl3::AudioStream::OpenRecording(spec)
							   : sdl3::AudioStream::OpenRecording(spec, m_inputDevices[size_t(idx - 1)].id);
		if (stream.IsError())
			return std::nullopt;
		stream.Value().Resume();
		return std::move(stream.Value());
	}

	/// Ouvre / ferme les flux d'entrée selon les nœuds « mic » présents.
	void SyncInputDevices(const std::vector<uint32_t> &order) {
		std::set<int> needed;
		for (const uint32_t id : order)
			if (const ui::GraphNode *n = m_graph.FindNode(id); n && n->kind == "mic")
				needed.insert(DeviceIndex(m_inputNames.size(), Prop(*n, "device", 0.f)));
		for (size_t i = 0; i < m_inputFailed.size(); ++i)
			if (!needed.contains(int(i)))
				m_inputFailed[i] = false;
		for (auto it = m_inputs.begin(); it != m_inputs.end();) {
			if (!needed.contains(it->first)) {
				m_inputBlocks.erase(it->first);
				it = m_inputs.erase(it);
			} else {
				++it;
			}
		}
		for (const int idx : needed) {
			if (m_inputs.contains(idx) || (size_t(idx) < m_inputFailed.size() && m_inputFailed[size_t(idx)]))
				continue;
			auto stream = OpenInput(idx);
			if (stream)
				m_inputs.emplace(idx, std::move(*stream));
			else if (size_t(idx) < m_inputFailed.size())
				m_inputFailed[size_t(idx)] = true;
		}
	}

	/// Rouvre la sortie quand le nœud « output » demande un autre périphérique.
	void SyncOutputDevice(const std::vector<uint32_t> &order) {
		int wanted = 0;
		for (const uint32_t id : order)
			if (const ui::GraphNode *n = m_graph.FindNode(id); n && n->kind == "output") {
				wanted = DeviceIndex(m_outputNames.size(), Prop(*n, "device", 0.f));
				break;
			}
		if (wanted != m_outputDevice || !m_output)
			OpenOutput(wanted);
	}

	// ── Traitement ──────────────────────────────────────────────────────────
	void Process(double frameSeconds) {
		const std::vector<uint32_t> order = m_graph.TopologicalOrder();
		m_cycle = order.empty() && !m_graph.Nodes().empty();
		if (m_cycle)
			return;
		std::erase_if(m_processors, [this](const auto &entry) { return m_graph.FindNode(entry.first) == nullptr; });
		SyncInputDevices(order);
		SyncOutputDevice(order);
		if (m_output) {
			const int lead = int(kLead * float(kRate) * sizeof(float));
			for (int guard = 0; guard < 64 && m_output->Queued() < lead; ++guard)
				ProcessBlock(order);
			return;
		}
		m_due += frameSeconds * double(kRate);
		for (int guard = 0; guard < 64 && m_due >= double(kBlock); ++guard, m_due -= double(kBlock))
			ProcessBlock(order);
		m_due = std::min(m_due, double(kBlock) * 4.0);
	}

	void ProcessBlock(const std::vector<uint32_t> &order) {
		std::array<float, kBlock> out{};
		for (auto &[idx, stream] : m_inputs) { // un bloc capté par périphérique d'entrée ouvert
			std::array<float, kBlock> block{};
			if (stream) {
				const int got = stream->GetData(std::span<float>(block));
				if (got > 0 && size_t(got) / sizeof(float) < kBlock)
					std::fill(block.begin() + std::ptrdiff_t(size_t(got) / sizeof(float)), block.end(), 0.f);
			}
			m_inputBlocks[idx] = block;
		}

		const AudioContext actx{float(kRate), m_inputBlocks, m_inputNames.size(), m_files, kSounds, m_mixer ? &*m_mixer : nullptr};

		for (const uint32_t id : order) {
			ui::GraphNode *node = m_graph.FindNode(id);
			if (!node)
				continue;
			std::unique_ptr<AudioNode> &proc = m_processors[id];
			if (!proc)
				proc = MakeAudioNode(node->kind);
			if (!proc)
				continue;

			// Entrées : somme des broches reliées (un lien par entrée).
			std::array<float, kBlock> buffer{};
			for (const uint32_t pin : node->inputs)
				if (const ui::GraphLink *link = m_graph.LinkInto(pin))
					if (const auto it = m_buffers.find(link->from); it != m_buffers.end())
						for (size_t i = 0; i < kBlock; ++i)
							buffer[i] += it->second[i];

			proc->Process(*node, buffer, actx);
			proc->UpdateMetrics(buffer);

			if (proc->IsOutput())
				for (size_t i = 0; i < kBlock; ++i)
					out[i] += buffer[i];

			for (const uint32_t pin : node->outputs)
				m_buffers[pin] = buffer;
		}
		for (float &s : out)
			s = m_mute ? 0.f : audio::SoftClip(s, 0.9f);
		if (m_output)
			m_output->PutData(std::span<const float>(out));
	}

	// ── UI corps des nœuds (délègue au processeur) ─────────────────────────
	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body) {
		std::unique_ptr<AudioNode> &proc = m_processors[node.id];
		if (!proc)
			proc = MakeAudioNode(node.kind);
		if (!proc)
			return;
		const UiContext ctx{m_freqs, m_files, m_inputNames, m_outputNames, m_inputFailed};
		u.PushId(uint64_t(node.id));
		proc->DrawBody(u, node, body, ctx);
		u.PopId();
	}

	// ── Fichiers ────────────────────────────────────────────────────────────
	void Save() {
		std::error_code ec;
		std::filesystem::create_directories(SavePath().parent_path(), ec);
		std::ofstream(SavePath()) << dataio::WriteJson(m_graph.Serialize());
		Ui().Notify("Patch sauvé : " + SavePath().string(), Theme().success);
	}

	void Load() {
		std::ifstream in(SavePath());
		std::stringstream text;
		text << in.rdbuf();
		auto json = dataio::ParseJson(text.str());
		if (!in || json.IsError() || !m_graph.Deserialize(json.Value())) {
			Ui().Notify("Aucun patch à charger : " + SavePath().string(), Theme().danger);
			return;
		}
		m_processors.clear();
		m_buffers.clear();
		m_editor.FrameAll(m_graph);
		Ui().Notify("Patch chargé", Theme().success);
	}

	std::optional<sdl3::MixerContext> m_mixer;
	std::optional<sdl3::AudioStream> m_output;
	int m_outputDevice = 0;
	std::vector<std::string> m_outputNames;
	std::vector<sdl3::AudioDeviceInfo> m_outputDevices;
	std::map<int, std::optional<sdl3::AudioStream>> m_inputs;
	std::map<int, std::array<float, kBlock>> m_inputBlocks;
	std::vector<std::string> m_inputNames;
	std::vector<sdl3::AudioDeviceInfo> m_inputDevices;
	std::vector<bool> m_inputFailed;
	bool m_mute = false, m_cycle = false, m_firstFrame = true;
	std::vector<std::string> m_files;
	ui::NodeGraph m_graph;
	ui::NodeGraphEditor m_editor;
	std::map<uint32_t, std::unique_ptr<AudioNode>> m_processors;
	std::map<uint32_t, std::array<float, kBlock>> m_buffers; ///< dernier bloc de chaque broche de sortie
	std::vector<float> m_freqs = audio::BinFrequencies(kFft, float(kRate));
	double m_due = 0.0;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Patchbay audio";
	config.width = 1360;
	config.height = 800;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::WORKSPACES);
	config.audio = true;
	config.ParseArgs(argc, argv);
	return Patchbay().Run(config);
}
