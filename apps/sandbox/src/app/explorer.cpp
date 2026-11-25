// Explorateur de fichiers du projet (région du bas, comme l'ancienne version) : arbre des
// dossiers, fichiers en vignettes, actions selon le type (ouvrir, éditer, insérer, importer,
// attribuer) ; double-clic : action principale.
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

namespace fs = std::filesystem;
using ui::icons::MaterialIcons;

enum class FileKind : uint8_t { Folder, Project, Scene, Script, Json, Object, Model, Sound, Image, Other };

FileKind KindOf(const fs::path &p) {
	std::string ext = p.extension().string();
	std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
	if (ext == ".project")
		return FileKind::Project;
	if (ext == ".scene" || ext == ".sandbox")
		return FileKind::Scene;
	if (ext == ".script")
		return FileKind::Script;
	if (ext == ".json")
		return FileKind::Json;
	if (ext == ".object")
		return FileKind::Object;
	if (ext == ".model" || ext == ".gltf" || ext == ".glb")
		return FileKind::Model;
	if (ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac")
		return FileKind::Sound;
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp")
		return FileKind::Image;
	return FileKind::Other;
}

char32_t IconOf(FileKind k) {
	switch (k) {
	case FileKind::Folder: return ui::icons::Codepoint(MaterialIcons::FOLDER);
	case FileKind::Project: return ui::icons::Codepoint(MaterialIcons::VIEW_IN_AR);
	case FileKind::Scene: return ui::icons::Codepoint(MaterialIcons::DASHBOARD);
	case FileKind::Script: return ui::icons::Codepoint(MaterialIcons::CODE);
	case FileKind::Json: return ui::icons::Codepoint(MaterialIcons::DESCRIPTION);
	case FileKind::Object: return ui::icons::Codepoint(MaterialIcons::CATEGORY);
	case FileKind::Model: return ui::icons::Codepoint(MaterialIcons::VIEW_IN_AR);
	case FileKind::Sound: return ui::icons::Codepoint(MaterialIcons::VOLUME_UP);
	case FileKind::Image: return ui::icons::Codepoint(MaterialIcons::IMAGE);
	default: return ui::icons::Codepoint(MaterialIcons::DESCRIPTION);
	}
}

/// Fichiers lisibles comme texte dans l'éditeur du centre.
bool IsText(FileKind k) { return k == FileKind::Script || k == FileKind::Json || k == FileKind::Project || k == FileKind::Scene || k == FileKind::Object; }

} // namespace

fs::path SandboxApp::ExplorerRoot() const {
	if (m_project)
		return m_project->Folder();
	if (!m_path.empty())
		return fs::path(m_path).parent_path();
	return "apps/sandbox/projects";
}

void SandboxApp::OpenFileInEditor(const std::string &path) {
	for (size_t i = 0; i < m_files.size(); ++i)
		if (m_files[i].path == path) {
			m_activeFile = int(i);
			ShowPanel("script");
			return;
		}
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		Ui().Notify("Impossible de lire " + path, Theme().danger, 4.f);
		return;
	}
	std::stringstream text;
	text << in.rdbuf();
	m_files.push_back({path, String(text.str()), String(text.str())});
	m_activeFile = int(m_files.size()) - 1;
	ShowPanel("script");
}

bool SandboxApp::SaveOpenFile() {
	if (!FileActive())
		return false;
	OpenFile &f = m_files[size_t(m_activeFile)];
	std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
	if (!out) {
		Ui().Notify("Impossible d'écrire " + f.path, Theme().danger, 4.f);
		return false;
	}
	out.write(f.text.data(), std::streamsize(f.text.size()));
	f.saved = f.text;
	// Les scripts de la scène qui viennent de ce fichier sont relus (même contenu partout).
	std::error_code ec;
	const fs::path scene = m_path.empty() ? fs::path() : fs::path(m_path).parent_path();
	const auto same = [&](const String &rel) { return !rel.IsEmpty() && fs::equivalent(scene / rel.c_str(), f.path, ec); };
	Document &doc = m_editor.Doc();
	if (same(doc.Game().scriptFile))
		doc.Game().script = f.text;
	for (Entity &e : doc.Entities())
		if (same(e.behavior.scriptFile))
			e.behavior.script = f.text;
	m_paramsKey.clear(); // paramètres relus
	Ui().Notify("Enregistré : " + fs::path(f.path).filename().string(), Theme().success);
	return true;
}

void SandboxApp::DrawExplorer(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	std::error_code ec;
	const fs::path root = ExplorerRoot();
	if (m_explorerDir.empty() || !fs::is_directory(m_explorerDir, ec) || fs::relative(m_explorerDir, root, ec).string().starts_with(".."))
		m_explorerDir = root.string();
	c.FillRect(area, th.panel);
	// ── Arbre des dossiers ──
	const float treeW = std::clamp(area.w * 0.22f, 160.f, 280.f);
	const sdl3::FRect tree{area.x, area.y, treeW, area.h};
	c.FillRect(tree, th.background);
	u.BeginScroll("explorer-tree", {tree.x + 4.f, tree.y + 4.f, tree.w - 8.f, tree.h - 8.f}, {.axes = ui::ScrollAxes::Vertical});
	const auto folder = [&](auto &&self, const fs::path &dir, int depth) -> void {
		const sdl3::FRect r = u.Next(24.f);
		u.PushId(dir.string());
		const auto it = u.Interact(u.MakeId("dir"), r);
		u.PopId();
		const bool on = dir.string() == m_explorerDir, open = m_explorerOpen.contains(dir.string()) || depth == 0;
		if (on || it.hovered)
			c.FillRoundRect(r, 4.f, on ? sdl3::WithAlpha(th.accent, 70) : th.widgetHover);
		const float x = r.x + 4.f + float(depth) * 14.f;
		c.Icon({x, r.y + 2.f, 20.f, 20.f}, ui::icons::Codepoint(open ? MaterialIcons::FOLDER_OPEN : MaterialIcons::FOLDER), th.accent, 16.f);
		c.Text(x + 22.f, r.y + 4.f, c.Ellipsize(depth == 0 ? (m_project ? std::string(m_project->name) : dir.filename().string()) : dir.filename().string(),
												 r.Right() - x - 26.f, 12.5f),
			   on ? th.text : th.textDim, 12.5f);
		if (it.leftClicked) {
			m_explorerDir = dir.string();
			if (depth > 0 && !m_explorerOpen.erase(dir.string()))
				m_explorerOpen.insert(dir.string());
		}
		if (!open)
			return;
		std::vector<fs::path> children;
		for (const auto &entry : fs::directory_iterator(dir, ec))
			if (entry.is_directory(ec) && !entry.path().filename().string().starts_with("."))
				children.push_back(entry.path());
		std::ranges::sort(children);
		for (const fs::path &child : children)
			self(self, child, depth + 1);
	};
	folder(folder, root, 0);
	u.EndScroll();
	c.Line(tree.Right() - 0.5f, tree.y, tree.Right() - 0.5f, tree.Bottom(), th.panelBorder);
	// ── Fichiers du dossier choisi, en vignettes ──
	const sdl3::FRect files{tree.Right() + 8.f, area.y + 38.f, area.Right() - tree.Right() - 16.f, area.h - 42.f};
	std::vector<fs::path> entries;
	for (const auto &entry : fs::directory_iterator(m_explorerDir, ec))
		if (!entry.path().filename().string().starts_with("."))
			entries.push_back(entry.path());
	std::ranges::sort(entries, [&](const fs::path &a, const fs::path &b) {
		const bool da = fs::is_directory(a, ec), db = fs::is_directory(b, ec);
		return da != db ? da : a.filename() < b.filename(); // dossiers d'abord
	});
	// Barre : chemin, actions du fichier sélectionné.
	const fs::path selected = m_explorerSel;
	const FileKind kind = m_explorerSel.empty() ? FileKind::Other : KindOf(selected);
	const std::string where = fs::relative(m_explorerDir, root, ec).generic_string();
	c.Text(files.x, area.y + 10.f, c.Ellipsize(where == "." ? std::string("/") : "/" + where, 220.f, 12.f), th.textDim, 12.f);
	float bx = files.Right();
	const auto action = [&](std::string_view labelText, bool enabled = true) {
		const float w = c.MeasureText(labelText, th.fontSize) + 22.f;
		bx -= w + 6.f;
		return u.ButtonAt({bx, area.y + 4.f, w, 28.f}, labelText, enabled);
	};
	Entity *target = ed.SelectedEntities().size() == 1 ? ed.Doc().Find(ed.SelectedEntities().front()) : nullptr;
	const auto open = [&](const fs::path &p, FileKind k) { // action principale (double-clic)
		switch (k) {
		case FileKind::Project:
		case FileKind::Scene: Load(p.string()); break;
		case FileKind::Script:
		case FileKind::Json: OpenFileInEditor(p.string()); break;
		case FileKind::Object: {
			const math::FVector3 t = ed.camera.Target();
			ed.Checkpoint("Insérer un objet");
			if (auto r = ed.Doc().InsertObject(p, {t.x, 0.f, t.z}); r.IsError())
				u.Notify(std::string(r.Error()), th.danger, 5.f);
			else {
				ed.ClearSelection();
				ed.SelectEntity(r.Value());
			}
			break;
		}
		case FileKind::Model: {
			const math::FVector3 t = ed.camera.Target();
			if (auto r = ed.ImportModel(String(p.string()), {t.x, 0.f, t.z}, m_importScale); r.IsError())
				u.Notify(std::string(r.Error()), th.danger, 5.f);
			break;
		}
		default: break;
		}
	};
	if (!m_explorerSel.empty()) {
		c.Text(files.x + 230.f, area.y + 10.f, c.Ellipsize(selected.filename().string(), std::max(80.f, files.w - 700.f), 12.5f), th.text, 12.5f);
		if ((kind == FileKind::Project || kind == FileKind::Scene) && action("Ouvrir"))
			open(selected, kind);
		if (IsText(kind) && action("Éditer"))
			OpenFileInEditor(selected.string());
		if (kind == FileKind::Object && action("Insérer"))
			open(selected, kind);
		if (kind == FileKind::Model && action("Importer"))
			open(selected, kind);
		const fs::path scene = m_path.empty() ? fs::path() : fs::path(m_path).parent_path();
		if (kind == FileKind::Script && action("Attribuer au groupe", target != nullptr) && target) {
			ed.Checkpoint("Attribuer un script");
			target = ed.Doc().Find(ed.SelectedEntities().front());
			std::ifstream in(selected, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			target->behavior.scriptFile = String(fs::relative(selected, scene, ec).generic_string());
			target->behavior.script = String(text.str());
		}
		if (kind == FileKind::Sound && action("Attribuer au groupe", target != nullptr) && target) {
			ed.Checkpoint("Attribuer un son");
			ed.Doc().Find(ed.SelectedEntities().front())->behavior.sound = String(selected.filename().string());
		}
		if (kind == FileKind::Image) {
			Item2D *image = ed.Doc().Overlay().Find(m_item2D);
			if (action("Attribuer à l'image 2D", image && image->shape == Shape2D::Image) && image) {
				ed.Change("Image (2D)", false);
				ed.Doc().Overlay().Find(m_item2D)->image = String(fs::relative(selected, scene, ec).generic_string());
			}
		}
	}
	u.BeginScroll("explorer-files", files, {.axes = ui::ScrollAxes::Vertical});
	constexpr float kTileW = 104.f, kTileH = 86.f, kGap = 6.f;
	const int perRow = std::max(1, int((files.w - 14.f + kGap) / (kTileW + kGap))); // barre de défilement déduite
	std::string goInto;
	for (size_t i = 0; i < entries.size(); i += size_t(perRow)) {
		const sdl3::FRect line = u.Next(kTileH);
		for (size_t k = i; k < std::min(entries.size(), i + size_t(perRow)); ++k) {
			const fs::path &p = entries[k];
			const bool isDir = fs::is_directory(p, ec);
			const FileKind fk = isDir ? FileKind::Folder : KindOf(p);
			const sdl3::FRect cell{line.x + float(k - i) * (kTileW + kGap), line.y, kTileW, kTileH};
			u.PushId(p.string());
			const auto it = u.Interact(u.MakeId("file"), cell);
			u.PopId();
			const bool on = p.string() == m_explorerSel;
			if (on || it.hovered)
				c.FillRoundRect(cell, 6.f, on ? sdl3::WithAlpha(th.accent, 70) : th.widgetHover);
			c.Icon({cell.x, cell.y + 6.f, cell.w, 40.f}, IconOf(fk), fk == FileKind::Other ? th.textDim : th.accent, 34.f);
			c.TextCentered({cell.x + 2.f, cell.y + 52.f, cell.w - 4.f, 16.f}, c.Ellipsize(p.stem().string(), cell.w - 6.f, 11.5f), th.text, 11.5f);
			c.TextCentered({cell.x + 2.f, cell.y + 68.f, cell.w - 4.f, 14.f}, isDir ? std::string("dossier") : p.extension().string(), th.textDim, 10.f);
			if (it.leftClicked)
				m_explorerSel = isDir ? std::string() : p.string();
			if (it.doubleClicked) {
				if (isDir)
					goInto = p.string();
				else
					open(p, fk);
			}
		}
	}
	if (entries.empty())
		u.Wrapped("Dossier vide.", th.textDim, 12.f);
	u.EndScroll();
	if (!goInto.empty()) {
		m_explorerOpen.insert(goInto);
		m_explorerDir = goInto;
		m_explorerSel.clear();
	}
}

} // namespace sandbox
