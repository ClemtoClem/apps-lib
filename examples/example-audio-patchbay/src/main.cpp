// example-audio-patchbay — routage audio par graphe de nœuds.
//
// Sources (oscillateur, lecteur OGG / MP3 / WAV décodé en continu par SDL3_mixer, micro),
// effets (filtre biquad, gain, mélangeur, retard, écrêtage doux) et visualisations
// (oscilloscope, spectre) se relient dans un ui::NodeGraph ; les nœuds Sortie envoient le
// résultat au périphérique par un sdl3::AudioStream. Le graphe est évalué par blocs de 256
// échantillons dans l'ordre topologique ; les paramètres vivent dans les propriétés des nœuds
// (sauvegardées en JSON), l'état du traitement (phases, filtres, lignes de retard, décodeurs)
// à part. La file du flux est remplie depuis la boucle principale (≈ 80 ms d'avance) : ni
// fil d'exécution ni verrou.
//
//   Clic droit : ajouter un nœud · glisser une broche : relier · Espace : couper le son
//   Ctrl+S / Ctrl+O : sauver / charger · Suppr, Ctrl+C / V, F : comme tout NodeGraph
//
//   cpp-maker run example-audio-patchbay
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <audio/dsp.hpp>
#include <sdl3/input/audio.hpp>
#include <sdl3/input/mixer.hpp>
#include <ui/node_graph.hpp>

namespace {

constexpr int kRate = 48000;
constexpr size_t kBlock = 256;
constexpr float kLead = 0.08f; ///< avance de la file de lecture (secondes)
const std::filesystem::path kSavePath = "data/patchbay/patch.json";
const std::filesystem::path kSounds = "assets/sounds";

struct Kind {
	std::string_view id, title;
	sdl3::Color header;
	int inputs, outputs; ///< broches « audio »
	float body;			 ///< hauteur du corps
};
constexpr std::array kKinds = {
	Kind{"osc", "Oscillateur", sdl3::Rgba(52, 96, 160), 0, 1, 92},
	Kind{"player", "Lecteur", sdl3::Rgba(52, 120, 120), 0, 1, 92},
	Kind{"mic", "Micro", sdl3::Rgba(60, 110, 150), 0, 1, 28},
	Kind{"filter", "Filtre", sdl3::Rgba(120, 90, 40), 1, 1, 92},
	Kind{"gain", "Gain", sdl3::Rgba(110, 80, 50), 1, 1, 30},
	Kind{"mixer", "Mélangeur", sdl3::Rgba(100, 70, 130), 3, 1, 30},
	Kind{"delay", "Retard", sdl3::Rgba(130, 70, 90), 1, 1, 92},
	Kind{"clip", "Écrêtage", sdl3::Rgba(140, 60, 60), 1, 1, 30},
	Kind{"scope", "Oscilloscope", sdl3::Rgba(48, 110, 80), 1, 1, 110},
	Kind{"spectrum", "Spectre", sdl3::Rgba(48, 110, 80), 1, 1, 130},
	Kind{"output", "Sortie", sdl3::Rgba(70, 76, 92), 1, 0, 56},
};
const Kind *FindKind(std::string_view id) {
	for (const Kind &k : kKinds)
		if (k.id == id)
			return &k;
	return nullptr;
}

constexpr std::array<std::string_view, 5> kShapes = {"Sinus", "Carré", "Triangle", "Scie", "Bruit"};
constexpr std::array<std::string_view, 3> kFilters = {"Passe-bas", "Passe-haut", "Passe-bande"};

/// État de traitement d'un nœud (jamais sérialisé : reconstruit à partir des propriétés).
struct NodeState {
	audio::Oscillator osc;
	audio::BiQuad filter;
	audio::DelayLine delay{size_t(kRate)}; // jusqu'à 1 s
	audio::RingBuffer history{4096};
	std::optional<sdl3::AudioDecoder> decoder;
	std::string decoderFile;
	float rms = 0.f, peak = 0.f;
};

float Prop(const ui::GraphNode &n, std::string_view key, float fallback) {
	const auto it = n.props.find(std::string(key));
	return it == n.props.end() ? fallback : std::strtof(it->second.c_str(), nullptr);
}
void SetProp(ui::GraphNode &n, std::string_view key, float value) { n.props[std::string(key)] = std::format("{}", value); }

class Patchbay final : public app::Application {
protected:
	bool OnInit() override {
		if (auto mixer = sdl3::MixerContext::Create(); mixer.IsOk())
			m_mixer.emplace(std::move(mixer.Value()));
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(kSounds, ec))
			if (const std::string ext = entry.path().extension().string(); ext == ".ogg" || ext == ".mp3" || ext == ".wav" || ext == ".flac")
				m_files.push_back(entry.path().filename().string());
		std::ranges::sort(m_files);
		auto stream = sdl3::AudioStream::OpenPlayback(sdl3::AudioSpec{sdl3::AudioFormat::F32, 1, kRate});
		if (stream.IsOk()) {
			m_output.emplace(std::move(stream.Value()));
			m_output->Resume();
		}
		BuildDefaultPatch();
		SetupEditor();
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		ui::Context &u = Ui();
		if (!u.WantsKeyboard() && Events().KeyScan(SDL_SCANCODE_SPACE).IsJustPressed() && !Events().KeyScan(SDL_SCANCODE_LCTRL).IsPressed())
			m_mute = !m_mute;
		if (u.Ctrl() && u.KeycodePressed(SDLK_S))
			Save();
		if (u.Ctrl() && u.KeycodePressed(SDLK_O))
			Load();
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
		std::string left = m_output ? std::format("Sortie : {} Hz mono · {} ms d'avance", kRate, int(kLead * 1000.f)) : std::string("Aucune sortie audio (affichage seul)");
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
		if (id == "save")
			Save();
		else if (id == "load")
			Load();
		else if (id == "frame")
			m_editor.FrameAll(m_graph);
		else if (id == "mute")
			m_mute = !m_mute;
	}

	void OnShutdown() override {
		m_states.clear(); // décodeurs avant le contexte SDL3_mixer
		m_output.reset();
		m_input.reset();
	}

private:
	// ── Graphe ──────────────────────────────────────────────────────────────
	uint32_t Add(std::string_view kindId, ui::GraphPoint at) {
		const Kind *kind = FindKind(kindId);
		ui::GraphNode &node = m_graph.AddNode(std::string(kind->id), std::string(kind->title), at, kind->header);
		const uint32_t id = node.id;
		node.width = kind->id == "spectrum" || kind->id == "scope" ? 240.f : 200.f;
		node.bodyHeight = kind->body;
		static constexpr std::array<std::string_view, 3> kInputs = {"a", "b", "c"};
		for (int i = 0; i < kind->inputs; ++i)
			m_graph.AddInput(id, kind->inputs > 1 ? std::string(kInputs[size_t(i)]) : "entrée", "audio", ui::PinShape::Circle, sdl3::Rgba(110, 200, 160));
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
		const uint32_t spectrum = Add("spectrum", {560, 320}), mixer = Add("mixer", {840, 300}), output = Add("output", {1100, 300});
		ui::GraphNode *n = m_graph.FindNode(osc);
		SetProp(*n, "shape", 3.f); // dent de scie : riche en harmoniques pour le filtre
		SetProp(*n, "freq", 110.f);
		SetProp(*m_graph.FindNode(filter), "cutoff", 900.f);
		SetProp(*m_graph.FindNode(player), "playing", 0.f);
		Link(osc, filter);
		Link(filter, delay);
		Link(delay, scope);
		Link(player, gain);
		Link(gain, spectrum);
		Link(scope, mixer, 0);
		Link(spectrum, mixer, 1);
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

	// ── Traitement ──────────────────────────────────────────────────────────
	/// Produit les blocs nécessaires : selon la file de lecture, sinon selon l'horloge.
	void Process(double frameSeconds) {
		const std::vector<uint32_t> order = m_graph.TopologicalOrder();
		m_cycle = order.empty() && !m_graph.Nodes().empty();
		if (m_cycle)
			return;
		std::erase_if(m_states, [this](const auto &entry) { return m_graph.FindNode(entry.first) == nullptr; });
		OpenInputIfNeeded();
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

	void OpenInputIfNeeded() {
		const bool wanted = std::ranges::any_of(m_graph.Nodes(), [](const ui::GraphNode &n) { return n.kind == "mic"; });
		if (wanted && !m_input && !m_inputFailed) {
			auto stream = sdl3::AudioStream::OpenRecording(sdl3::AudioSpec{sdl3::AudioFormat::F32, 1, kRate});
			if (stream.IsOk()) {
				m_input.emplace(std::move(stream.Value()));
				m_input->Resume();
			} else {
				m_inputFailed = true;
			}
		} else if (!wanted && m_input) {
			m_input.reset();
		}
	}

	void ProcessBlock(const std::vector<uint32_t> &order) {
		std::array<float, kBlock> out{};
		std::array<float, kBlock> mic{};
		if (m_input) { // un bloc du micro, partagé par tous les nœuds Micro
			const int got = m_input->GetData(std::span<float>(mic));
			if (got > 0 && size_t(got) / sizeof(float) < kBlock)
				std::fill(mic.begin() + std::ptrdiff_t(size_t(got) / sizeof(float)), mic.end(), 0.f);
		}
		for (const uint32_t id : order) {
			ui::GraphNode *node = m_graph.FindNode(id);
			NodeState &st = m_states[id];
			std::array<float, kBlock> buffer{};
			// Entrées : somme des broches reliées (un lien par entrée).
			for (const uint32_t pin : node->inputs)
				if (const ui::GraphLink *link = m_graph.LinkInto(pin))
					if (const auto it = m_buffers.find(link->from); it != m_buffers.end())
						for (size_t i = 0; i < kBlock; ++i)
							buffer[i] += it->second[i];
			RunNode(*node, st, buffer, mic);
			st.history.Push(buffer);
			st.rms = audio::Rms(buffer);
			st.peak = std::max(audio::Peak(buffer), st.peak * 0.95f);
			if (node->kind == "output")
				for (size_t i = 0; i < kBlock; ++i)
					out[i] += buffer[i] * Prop(*node, "volume", 0.7f);
			for (const uint32_t pin : node->outputs)
				m_buffers[pin] = buffer;
		}
		for (float &s : out)
			s = m_mute ? 0.f : audio::SoftClip(s, 0.9f);
		if (m_output)
			m_output->PutData(std::span<const float>(out));
	}

	void RunNode(ui::GraphNode &node, NodeState &st, std::array<float, kBlock> &buf, const std::array<float, kBlock> &mic) {
		const std::string &k = node.kind;
		if (k == "osc") {
			st.osc.shape = audio::Waveform(std::clamp(int(Prop(node, "shape", 0.f)), 0, 4));
			st.osc.frequency = Prop(node, "freq", 220.f);
			st.osc.amplitude = Prop(node, "amp", 0.4f);
			st.osc.Render(buf, float(kRate));
		} else if (k == "player") {
			RunPlayer(node, st, buf);
		} else if (k == "mic") {
			buf = mic;
		} else if (k == "filter") {
			st.filter.Design(audio::BiQuadKind(std::clamp(int(Prop(node, "type", 0.f)), 0, 2)), float(kRate), Prop(node, "cutoff", 1200.f),
							 Prop(node, "q", 0.7071f));
			st.filter.Process(buf);
		} else if (k == "gain" || k == "mixer") {
			const float g = Prop(node, "gain", 1.f);
			for (float &s : buf)
				s *= g;
		} else if (k == "delay") {
			st.delay.SetDelay(size_t(Prop(node, "ms", 250.f) * float(kRate) / 1000.f));
			st.delay.feedback = Prop(node, "feedback", 0.35f);
			st.delay.mix = Prop(node, "mix", 0.4f);
			st.delay.Process(buf);
		} else if (k == "clip") {
			const float drive = Prop(node, "drive", 2.f);
			for (float &s : buf)
				s = audio::SoftClip(s * drive, 0.6f) / std::max(1.f, drive * 0.6f);
		}
		// scope, spectrum, output : laissent passer le signal.
	}

	/// Lecteur : décode le fichier choisi en continu, en boucle, au format du graphe.
	void RunPlayer(ui::GraphNode &node, NodeState &st, std::array<float, kBlock> &buf) {
		if (m_files.empty() || Prop(node, "playing", 1.f) < 0.5f || !m_mixer)
			return;
		const std::string &file = m_files[size_t(std::clamp(int(Prop(node, "file", 0.f)), 0, int(m_files.size()) - 1))];
		if (!st.decoder || st.decoderFile != file) {
			st.decoder.reset();
			auto decoder = sdl3::AudioDecoder::Create(String((kSounds / file).string().c_str()));
			if (decoder.IsError())
				return;
			st.decoder.emplace(std::move(decoder.Value()));
			st.decoderFile = file;
		}
		const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
		const int bytes = int(kBlock * sizeof(float));
		int got = st.decoder->Decode(buf.data(), bytes, &spec);
		if (got <= 0) { // fin du fichier : on repart du début
			st.decoder.reset();
			return;
		}
		const float gain = Prop(node, "gain", 0.8f);
		for (size_t i = 0; i < kBlock; ++i)
			buf[i] = i < size_t(got) / sizeof(float) ? buf[i] * gain : 0.f;
	}

	// ── Corps des nœuds ─────────────────────────────────────────────────────
	/// Ligne « libellé · curseur · valeur » liée à une propriété ; `log` : échelle logarithmique.
	void Slider(ui::Context &u, ui::GraphNode &node, std::string_view label, std::string_view key, float lo, float hi, float def,
				std::string_view unit, bool log = false) {
		sdl3::Canvas &c = u.Canvas();
		const ui::Theme &th = u.Style();
		const sdl3::FRect row = u.Next(24);
		float v = Prop(node, key, def);
		c.Text(row.x, row.y + 5, label, th.textDim, 12);
		const std::string text = v >= 1000.f && unit == "Hz" ? std::format("{:.1f} k", v / 1000.f) : std::format("{:.{}f} {}", v, v < 10.f ? 2 : 0, unit);
		c.Text(row.Right() - c.MeasureText(text, 12), row.y + 5, text, th.text, 12);
		float t = log ? std::log10(v) : v;
		if (u.SliderAt({row.x + 62.f, row.y, row.w - 62.f - 58.f, row.h}, key, t, log ? std::log10(lo) : lo, log ? std::log10(hi) : hi))
			SetProp(node, key, log ? std::pow(10.f, t) : t);
	}

	void ChoiceRow(ui::Context &u, ui::GraphNode &node, std::string_view key, std::span<const std::string_view> options) {
		int index = std::clamp(int(Prop(node, key, 0.f)), 0, int(options.size()) - 1);
		if (u.Choice(u.Next(26), key, index, options))
			SetProp(node, key, float(index));
	}

	void DrawBody(ui::Context &u, ui::GraphNode &node, const sdl3::FRect &body) {
		const ui::Theme &th = u.Style();
		NodeState &st = m_states[node.id];
		u.PushId(uint64_t(node.id));
		const std::string &k = node.kind;
		if (k == "osc") {
			ChoiceRow(u, node, "shape", kShapes);
			Slider(u, node, "Fréquence", "freq", 20.f, 8000.f, 220.f, "Hz", true);
			Slider(u, node, "Niveau", "amp", 0.f, 1.f, 0.4f, "");
		} else if (k == "player") {
			std::vector<std::string_view> names(m_files.begin(), m_files.end());
			if (names.empty())
				names.push_back("(aucun fichier dans assets/sounds)");
			ChoiceRow(u, node, "file", names);
			const sdl3::FRect row = u.Next(26);
			bool playing = Prop(node, "playing", 1.f) >= 0.5f;
			if (u.ButtonAt({row.x, row.y, 90.f, row.h}, playing ? "Pause" : "Lecture", true, playing)) {
				SetProp(node, "playing", playing ? 0.f : 1.f);
				st.decoder.reset();
			}
			Slider(u, node, "Niveau", "gain", 0.f, 1.5f, 0.8f, "");
		} else if (k == "mic") {
			u.Label(m_input ? "Périphérique d'enregistrement par défaut" : (m_inputFailed ? "Micro indisponible" : "Ouverture…"), th.textDim, 12);
		} else if (k == "filter") {
			ChoiceRow(u, node, "type", kFilters);
			Slider(u, node, "Coupure", "cutoff", 40.f, 16000.f, 1200.f, "Hz", true);
			Slider(u, node, "Résonance", "q", 0.3f, 12.f, 0.7071f, "");
		} else if (k == "gain" || k == "mixer") {
			Slider(u, node, "Gain", "gain", 0.f, 2.f, 1.f, "");
		} else if (k == "delay") {
			Slider(u, node, "Temps", "ms", 5.f, 1000.f, 250.f, "ms");
			Slider(u, node, "Réinject.", "feedback", 0.f, 0.95f, 0.35f, "");
			Slider(u, node, "Mélange", "mix", 0.f, 1.f, 0.4f, "");
		} else if (k == "clip") {
			Slider(u, node, "Saturation", "drive", 1.f, 10.f, 2.f, "");
		} else if (k == "scope") {
			std::vector<float> samples(1024);
			st.history.Latest(samples);
			u.Scope(body, samples, sdl3::Rgba(110, 200, 160));
		} else if (k == "spectrum") {
			std::vector<float> samples(4096);
			st.history.Latest(samples);
			audio::ApplyWindow(samples, audio::WindowFunction::Hann);
			std::vector<float> db = audio::MagnitudeDb(audio::Fft(samples), -100.f);
			for (float &d : db)
				d += 6.02f;
			u.SpectrumView(body, m_freqs, db, 30.f, 16000.f, -90.f, 0.f, sdl3::Rgba(110, 200, 160));
		} else if (k == "output") {
			Slider(u, node, "Volume", "volume", 0.f, 1.f, 0.7f, "");
			u.LevelMeter(u.Next(18), st.rms, st.peak, "Niveau");
		}
		u.PopId();
	}

	// ── Fichiers ────────────────────────────────────────────────────────────
	void Save() {
		std::error_code ec;
		std::filesystem::create_directories(kSavePath.parent_path(), ec);
		std::ofstream(kSavePath) << dataio::WriteJson(m_graph.Serialize());
		Ui().Notify("Patch sauvé : " + kSavePath.string(), Theme().success);
	}

	void Load() {
		std::ifstream in(kSavePath);
		std::stringstream text;
		text << in.rdbuf();
		auto json = dataio::ParseJson(text.str());
		if (!in || json.IsError() || !m_graph.Deserialize(json.Value())) {
			Ui().Notify("Aucun patch à charger : " + kSavePath.string(), Theme().danger);
			return;
		}
		m_states.clear();
		m_buffers.clear();
		m_editor.FrameAll(m_graph);
		Ui().Notify("Patch chargé", Theme().success);
	}

	std::optional<sdl3::MixerContext> m_mixer;
	std::optional<sdl3::AudioStream> m_output, m_input;
	bool m_inputFailed = false, m_mute = false, m_cycle = false, m_firstFrame = true;
	std::vector<std::string> m_files;
	ui::NodeGraph m_graph;
	ui::NodeGraphEditor m_editor;
	std::map<uint32_t, NodeState> m_states;
	std::map<uint32_t, std::array<float, kBlock>> m_buffers; ///< dernier bloc de chaque broche de sortie
	std::vector<float> m_freqs = audio::BinFrequencies(4096, float(kRate));
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
