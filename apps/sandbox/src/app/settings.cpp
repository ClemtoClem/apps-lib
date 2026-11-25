// Réglages généraux persistés du sandbox, au format TOML (dataio::TomlDocument) :
// fractionnement des fichiers volumineux, disposition de l'interface, raccourcis clavier.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <dataio/documents/toml.hpp>
#include <sdl3/io/filesystem.hpp>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

namespace fs = std::filesystem;

fs::path SettingsFile() {
	return fs::path(sdl3::filesystem::AppDataDir("sandbox").c_str()) / "settings.toml";
}

/// Borne un ratio de séparateur à une plage raisonnable.
float ClampRatio(float v) { return std::clamp(v, 0.05f, 0.95f); }

} // namespace

void SandboxApp::LoadSettings() {
	dataio::TomlDocument doc;
	const String path(SettingsFile().string());
	if (doc.DecodeFile(path).IsSome())
		return; // absent ou illisible : réglages par défaut
	const dataio::NodePtr root = doc.GetRoot();
	if (!root || !root->IsObject())
		return;
	// Fractionnement automatique des fichiers volumineux.
	m_splitFiles = (*root)["split_files"]->AsBool(false);
	m_splitMaxMb = uint64_t(std::max<int64_t>(1, (*root)["split_max_mb"]->AsInt(100)));
	// Disposition de l'interface (ratios et régions visibles).
	if (const dataio::NodePtr layout = (*root)["layout"]; layout->IsObject()) {
		m_splitMain = ClampRatio((*layout)["main"]->AsFloat(0.72f));
		m_splitTop = ClampRatio((*layout)["top"]->AsFloat(0.22f));
		m_splitCenter = ClampRatio((*layout)["center"]->AsFloat(0.72f));
		m_leftShown = (*layout)["left"]->AsBool(true);
		m_rightShown = (*layout)["right"]->AsBool(true);
		m_bottomShown = (*layout)["bottom"]->AsBool(true);
	}
	// Raccourcis clavier : table [keys], « action » = « combinaison ».
	if (const dataio::NodePtr keys = (*root)["keys"]; keys->IsObject())
		for (const auto &[action, chord] : keys->AsObject())
			if (const auto parsed = ui::KeyChord::FromString(chord->AsString()))
				(void)m_keys.Set(action.c_str(), *parsed);
}

void SandboxApp::SaveSettings() {
	CaptureLayout(); // lit les ratios actuels des séparateurs avant d'écrire
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("split_files", dataio::Node::MakeBool(m_splitFiles));
	root->Set("split_max_mb", dataio::Node::MakeInt(int64_t(m_splitMaxMb)));
	dataio::NodePtr layout = dataio::Node::MakeObject();
	// Ratios arrondis à 2 décimales : un fichier TOML lisible et un aller-retour exact.
	const auto round2 = [](float v) { return std::round(double(v) * 100.0) / 100.0; };
	layout->Set("main", dataio::Node::MakeFloat(round2(m_splitMain)));
	layout->Set("top", dataio::Node::MakeFloat(round2(m_splitTop)));
	layout->Set("center", dataio::Node::MakeFloat(round2(m_splitCenter)));
	layout->Set("left", dataio::Node::MakeBool(m_leftShown));
	layout->Set("right", dataio::Node::MakeBool(m_rightShown));
	layout->Set("bottom", dataio::Node::MakeBool(m_bottomShown));
	root->Set("layout", std::move(layout));
	dataio::NodePtr keys = dataio::Node::MakeObject();
	for (const ui::KeyMap::Binding &b : m_keys.Bindings())
		if (b.chord != b.defaultChord) // seules les combinaisons modifiées sont écrites
			keys->Set(String(b.action), dataio::Node::MakeString(String(b.chord.ToString())));
	root->Set("keys", std::move(keys));
	dataio::TomlDocument doc;
	doc.SetRoot(std::move(root));
	(void)doc.EncodeFile(String(SettingsFile().string()));
}

dataio::FileOptions SandboxApp::SplitOptions() const noexcept {
	dataio::FileOptions o;
	o.split = m_splitFiles;
	o.maxSize = std::max<uint64_t>(1, m_splitMaxMb) * 1024 * 1024;
	return o;
}

} // namespace sandbox
