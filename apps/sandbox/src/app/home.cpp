// Écran d'accueil du sandbox (comme l'ancienne version) : rouvrir un projet récent, ouvrir un
// exemple ou un projet existant, ou en créer un ; l'éditeur s'affiche ensuite.
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#include <sdl3/io/dialog.hpp>
#include <sdl3/io/filesystem.hpp>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

namespace fs = std::filesystem;
constexpr size_t kMaxRecent = 12;

fs::path RecentFile() {
	return fs::path(sdl3::filesystem::AppDataDir("sandbox").c_str()) / "recent.txt";
}

/// Nom affiché d'un projet ou d'une scène : son index (`nom.project`) ou son fichier.
std::string DisplayName(const fs::path &path) {
	if (path.extension() == ".project")
		if (auto p = sandbox::Project::Load(path))
			return std::string(p.Value().name);
	return path.stem().string();
}

} // namespace

void SandboxApp::LoadRecent() {
	m_recent.clear();
	std::ifstream in(RecentFile());
	std::error_code ec;
	for (std::string line; std::getline(in, line);)
		if (!line.empty() && fs::exists(line, ec) && std::ranges::find(m_recent, line) == m_recent.end())
			m_recent.push_back(line);
}

void SandboxApp::AddRecent(const std::string &path) {
	std::error_code ec;
	const std::string full = fs::absolute(path, ec).lexically_normal().string();
	std::erase(m_recent, full);
	m_recent.insert(m_recent.begin(), full);
	if (m_recent.size() > kMaxRecent)
		m_recent.resize(kMaxRecent);
	const fs::path file = RecentFile();
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::trunc);
	for (const std::string &r : m_recent)
		out << r << '\n';
}

void SandboxApp::CreateProject(const std::string &folder, const std::string &name) {
	if (m_loading.exchange(true))
		return; // un chargement est déjà en cours
	m_loadingLabel = "Création du projet…";
	// La construction et l'enregistrement se font en arrière-plan : l'interface reste fluide.
	jobs::ThreadPool::Global().Submit([this, folder, name] {
		LoadOutcome outcome = CreateProjectOutcome(folder, name);
		std::lock_guard lock(m_loadMutex);
		m_loadResult = std::move(outcome);
		m_loading = false;
	});
}

void SandboxApp::DrawHome(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	c.FillRect(area, th.background);
	const float w = std::min(980.f, area.w - 48.f), h = std::min(620.f, area.h - 48.f);
	const sdl3::FRect panel{area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h};
	c.FillRoundRect(panel, 10.f, th.panel);
	c.StrokeRoundRect(panel, 10.f, th.panelBorder);
	c.Icon({panel.x + 24.f, panel.y + 20.f, 40.f, 40.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR), th.accent, 34.f);
	c.Text(panel.x + 74.f, panel.y + 18.f, "Sandbox", th.text, 26.f, sdl3::CanvasFont::Bold);
	c.Text(panel.x + 74.f, panel.y + 50.f, "Modeleur et éditeur de jeu — ouvrez un projet pour commencer", th.textDim, 13.f);
	const float split = panel.x + panel.w * 0.56f;
	// ── Gauche : projets récents et exemples ──
	u.BeginScroll("home-list", {panel.x + 20.f, panel.y + 86.f, split - panel.x - 32.f, panel.h - 106.f}, {.axes = ui::ScrollAxes::Vertical});
	const auto entry = [&](const std::string &path, size_t key) {
		const sdl3::FRect r = u.Next(46.f);
		u.PushId(uint64_t(key) + 9000);
		const auto it = u.Interact(u.MakeId("home-entry"), r);
		u.PopId();
		c.FillRoundRect(r, 6.f, it.hovered ? th.widgetHover : th.widget);
		const fs::path p(path);
		c.Icon({r.x + 8.f, r.y + 11.f, 24.f, 24.f}, ui::icons::Codepoint(p.extension() == ".project" ? ui::icons::MaterialIcons::FOLDER : ui::icons::MaterialIcons::DASHBOARD),
			   th.accent, 20.f);
		c.Text(r.x + 40.f, r.y + 5.f, c.Ellipsize(DisplayName(p), r.w - 50.f, 14.f), th.text, 14.f);
		c.Text(r.x + 40.f, r.y + 25.f, c.Ellipsize(path, r.w - 50.f, 11.f), th.textDim, 11.f);
		return it.leftClicked;
	};
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::HISTORY), "Projets récents");
	std::string open;
	for (size_t i = 0; i < m_recent.size(); ++i)
		if (entry(m_recent[i], i))
			open = m_recent[i];
	if (m_recent.empty())
		u.Wrapped("Aucun projet récent.", th.textDim, 12.f);
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::SCHOOL), "Exemples");
	std::error_code ec;
	size_t key = 100;
	for (const auto &dir : fs::directory_iterator("apps/sandbox/projects", ec))
		if (dir.is_directory(ec))
			for (const auto &f : fs::directory_iterator(dir.path(), ec))
				if (f.path().extension() == ".project" && entry(f.path().string(), key++))
					open = f.path().string();
	u.EndScroll();
	if (!open.empty()) {
		Load(open);
		return;
	}
	// ── Droite : ouvrir, créer ──
	c.Line(split, panel.y + 86.f, split, panel.Bottom() - 20.f, th.panelBorder);
	u.BeginColumn({split + 20.f, panel.y + 86.f, panel.Right() - split - 40.f, panel.h - 106.f});
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER_OPEN), "Ouvrir");
	if (u.PrimaryButton(u.Next(34.f), "Ouvrir un projet…"))
		OpenDocument();
	if (u.Button("Scène vide (sans projet)"))
		NewDocument();
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::HELP_OUTLINE), "Aide");
	if (u.Button("Catalogue des outils et fonctionnalités…"))
		m_showCatalog = true;
	if (u.Button("Raccourcis clavier…"))
		m_showShortcuts = true;
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::CREATE_NEW_FOLDER), "Nouveau projet");
	u.Label("Nom", th.textDim, 12.f);
	u.TextInput("home-name", m_newName, nullptr, "nom du projet");
	u.Label("Dossier", th.textDim, 12.f);
	u.Row({1.f, 0.42f}, 30.f);
	u.TextInput("home-folder", m_newFolder, nullptr, "dossier parent");
	if (u.Button("Parcourir…"))
		sdl3::dialog::ShowOpenFolder(
			[this](const sdl3::DialogResult &r, int) {
				if (r.Ok()) {
					std::lock_guard lock(m_dialogMutex);
					m_dialogResult = {3, std::string(r.First())};
				}
			},
			Window(), String(m_newFolder));
	u.Wrapped(std::format("Créera {}/{}/{}.project, scenes/main.scene et scripts/main.script.", m_newFolder, m_newName, m_newName), th.textDim, 11.f);
	if (u.PrimaryButton(u.Next(34.f), "Créer le projet"))
		CreateProject(m_newFolder, m_newName);
	u.EndColumn();
}

} // namespace sandbox
