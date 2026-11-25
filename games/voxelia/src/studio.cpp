// Voxelia Studio — IDE des mods dans une seconde fenêtre SDL (son propre sdl3::Renderer,
// son canvas et son contexte d'interface) : explorateur de fichiers des mods, éditeur de
// code (onglets, coloration, vérification, exécution dans la partie), panneaux Monde,
// Entités, Blocs et SQL, console de la partie.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <map>

#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/trace.hpp>
#include <jobs/jobs.hpp>
#include <sdl3/render/image.hpp>
#include <sdl3/input/input.hpp>
#include <ui/dock.hpp>
#include <ui/ui.hpp>
#include <ui/viewport3d.hpp>

#include "app.hpp"
#include "studio_editor.hpp"
#include "studio_model.hpp"
#include "studio_panels.hpp"
#include "studio_worldgen.hpp"
#include "world_editor.hpp"

namespace voxelia {

using Rect = sdl3::FRect;
using sdl3::Rgba;
namespace icons = ui::icons;

namespace {

using Document = studio::CodeDocument;
using studio::ByteToColumn;
using studio::ColumnToByte;

enum class ConsoleKind : uint8_t { Info, Command, Warning, Error, Reload };

// ── Arborescence des fichiers (construite sur un thread) ──────────────────
/// Vrai si un répertoire contient (récursivement) un script ou un JSON.
bool DirHasScripts(const fs::path &dir) {
	std::error_code ec;
	for (const auto &e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
		if (e.is_regular_file()) {
			const auto ext = e.path().extension();
			if (ext == ".script" || ext == ".json")
				return true;
		}
	}
	return false;
}

/// Nœud d'un répertoire ou d'un fichier ; les répertoires sans script/JSON sont omis.
ui::TreeNode BuildFileNode(const fs::path &path, std::string name) {
	ui::TreeNode node;
	node.id = path.string();
	node.label = std::move(name);
	node.isDir = fs::is_directory(path);
	if (node.isDir) {
		std::error_code ec;
		std::vector<fs::path> dirs, files;
		for (const auto &e : fs::directory_iterator(path, ec)) {
			if (e.is_directory())
				dirs.push_back(e.path());
			else {
				const auto ext = e.path().extension();
				if (ext == ".script" || ext == ".json")
					files.push_back(e.path());
			}
		}
		std::ranges::sort(dirs);
		std::ranges::sort(files);
		for (const auto &d : dirs)
			if (DirHasScripts(d))
				node.children.push_back(BuildFileNode(d, d.filename().string()));
		for (const auto &f : files)
			node.children.push_back(BuildFileNode(f, f.filename().string()));
	}
	return node;
}

/// Racines de l'arbre : un nœud par mod (id, dossier).
std::vector<ui::TreeNode> BuildFileTree(const std::vector<std::pair<std::string, fs::path>> &mods) {
	std::vector<ui::TreeNode> roots;
	roots.reserve(mods.size());
	for (const auto &[id, dir] : mods)
		if (fs::is_directory(dir))
			roots.push_back(BuildFileNode(dir, id));
	return roots;
}

ConsoleKind ClassifyConsole(std::string_view l) {
	if (l.starts_with(">"))
		return ConsoleKind::Command;
	if (l.starts_with("[rechargement"))
		return ConsoleKind::Reload;
	const std::string low = String(std::string(l)).ToLower();
	for (const char *w : {"erreur", "error", "désactivé", "échec", "introuvable", "inattendu", "attendu"})
		if (low.find(w) != std::string::npos)
			return ConsoleKind::Error;
	if (low.find(".script:") != std::string::npos)
		return ConsoleKind::Error;
	for (const char *w : {"n'est pas disponible", "attention", "avertissement", "ignoré", "warning"})
		if (low.find(w) != std::string::npos)
			return ConsoleKind::Warning;
	return ConsoleKind::Info;
}

/// Lien « chemin.script:ligne[:colonne] » (ou .json) dans un message de la console.
struct FileLink {
	std::string path;
	int line = 1, column = 1;
	size_t begin = 0, end = 0;
};

std::optional<FileLink> FindFileLink(std::string_view l) {
	for (std::string_view ext : {".script:", ".json:"}) {
		const size_t e = l.find(ext);
		if (e == std::string_view::npos)
			continue;
		size_t b = e;
		while (b > 0 && l[b - 1] != ' ' && l[b - 1] != '[' && l[b - 1] != '(' && l[b - 1] != '\'' && l[b - 1] != '"')
			--b;
		size_t p = e + ext.size();
		const size_t digits = p;
		while (p < l.size() && std::isdigit(static_cast<unsigned char>(l[p])))
			++p;
		if (p == digits)
			continue;
		FileLink link;
		link.path = std::string(l.substr(b, e + ext.size() - 1 - b));
		link.line = std::atoi(std::string(l.substr(digits, p - digits)).c_str());
		if (p + 1 < l.size() && l[p] == ':' && std::isdigit(static_cast<unsigned char>(l[p + 1]))) {
			const size_t c0 = ++p;
			while (p < l.size() && std::isdigit(static_cast<unsigned char>(l[p])))
				++p;
			link.column = std::atoi(std::string(l.substr(c0, p - c0)).c_str());
		}
		link.begin = b;
		link.end = p;
		return link;
	}
	return std::nullopt;
}

/// Chemin d'un fichier cité par un message (relatif au dossier des mods, au mod, ou suffixe).
std::optional<fs::path> ResolveModFile(App &app, std::string_view rel) {
	std::error_code ec;
	const fs::path direct = app.opt.mods / std::string(rel);
	if (fs::exists(direct, ec))
		return direct;
	for (const voxel::ModInfo &m : app.content.mods.Mods()) {
		if (fs::exists(m.directory / std::string(rel), ec))
			return m.directory / std::string(rel);
		for (const fs::path &script : m.scripts)
			if (script.generic_string().ends_with(std::string(rel)))
				return script;
	}
	return std::nullopt;
}

/// Complétion de l'éditeur et de la console : membres d'un module (« voxel.o ») ou noms
/// globaux et mots-clés, depuis l'interpréteur de la partie (sinon la bibliothèque standard).
std::vector<std::string> Completions(Game *game, std::string_view prefix) {
	static script::Interpreter fallback;
	script::Interpreter &vm = game && game->Scripts() ? game->Scripts()->Vm() : fallback;
	std::vector<std::string> names;
	std::string_view last = prefix;
	if (const size_t dot = prefix.rfind('.'); dot != std::string_view::npos) {
		const std::string_view module = prefix.substr(0, dot);
		last = prefix.substr(dot + 1);
		const script::Value v = vm.GetGlobal(module.substr(module.rfind('.') == std::string_view::npos ? 0 : module.rfind('.') + 1));
		if (v.IsMap())
			for (const auto &[k, _] : *v.AsMap())
				names.push_back(k);
	} else {
		if (prefix.empty())
			return {};
		names = vm.GlobalNames();
		for (const char *kw : {"let", "fn", "if", "else", "while", "for", "in", "return", "break", "continue", "and", "or", "not", "nil", "true",
							   "false", "voxel", "world", "entity", "game", "content", "sql", "math"})
			names.emplace_back(kw);
	}
	std::ranges::sort(names);
	names.erase(std::unique(names.begin(), names.end()), names.end());
	std::vector<std::string> starts, contains;
	for (std::string &n : names) {
		if (n.starts_with(last))
			starts.push_back(std::move(n));
		else if (!last.empty() && n.find(last) != std::string::npos)
			contains.push_back(std::move(n));
	}
	starts.insert(starts.end(), contains.begin(), contains.end());
	if (starts.size() > 60)
		starts.resize(60);
	return starts;
}

// ── Panneau de droite : onglets construits à l'ouverture, contenu chargé en arrière-plan ──
//
// Chaque panneau suit le même schéma : une capture légère sur le thread principal (seul
// propriétaire de l'ECS et du monde), puis le travail lourd (tri, filtrage, comptage, mise en
// forme, requêtes SQL) dans `jobs::AsyncValue` sur un thread dédié. Tant qu'aucune donnée n'est
// prête, le panneau affiche `ui::Context::Loader` ; pendant un rafraîchissement, l'ancienne
// valeur reste affichée.

enum class PanelKind : uint8_t { World, Entities, Blocks, Sql };

struct RightPanel {
	PanelKind kind;
	ui::TabItem tab;
	bool needsGame = true;
};

struct WorldView {
	std::vector<std::string> info;
	std::vector<std::pair<std::string, double>> systems; // trié par coût décroissant
	double totalMs = 0;
};
struct EntityView {
	size_t total = 0;
	std::vector<std::pair<std::string, size_t>> byKind; // trié par effectif décroissant
	std::vector<std::string> nearest;
};
struct BlockRow {
	std::string name, label;
};
struct BlockView {
	std::vector<BlockRow> rows;
	size_t matched = 0, total = 0;
};
struct SqlTablesView {
	std::vector<std::string> tables;
};
struct SqlQueryView {
	std::vector<std::string> columns;
	std::vector<std::string> lines; // lignes mises en forme (colonnes séparées par « | »)
	size_t rowCount = 0;
	std::string plan, error;
	double ms = 0;
};

struct PanelsState {
	std::vector<RightPanel> tabs;
	int active = 0;
	std::unique_ptr<jobs::ThreadPool> worker; // un thread : chargement des panneaux
	jobs::AsyncValue<WorldView> world;
	jobs::AsyncValue<EntityView> entities;
	jobs::AsyncValue<BlockView> blocks;
	jobs::AsyncValue<SqlTablesView> tables;
	jobs::AsyncValue<SqlQueryView> query;
	uint64_t nextRefresh = 0;
	const void *game = nullptr; // partie dont les données sont affichées
	std::string blockFilter, loadedBlockFilter = "\x01";
	size_t loadedBlockCount = 0;
	float entityRadius = 48.f, loadedRadius = -1.f;
	std::string sqlQuery = "SELECT type, mod, data FROM events ORDER BY tick DESC LIMIT 20";
	std::string structureFilter; // filtre du banc d'essai (structures)

	/// Annule tout et attend le thread (avant de libérer les données qu'il lit).
	void Reset() {
		world.Clear();
		entities.Clear();
		tables.Clear();
		query.Clear();
		if (worker)
			worker->WaitIdle();
		nextRefresh = 0;
		loadedRadius = -1.f;
	}
};

RightPanel MakePanel(PanelKind kind, std::string id, std::string label, char32_t icon, sdl3::Color color, std::string tooltip, bool needsGame) {
	RightPanel p{kind, {}, needsGame};
	p.tab.id = std::move(id);
	p.tab.label = std::move(label);
	p.tab.icon = icon;
	p.tab.iconColor = color;
	p.tab.buttons.clear(); // pas de bouton fermer
	p.tab.tooltip = std::move(tooltip);
	return p;
}

/// Onglets du panneau de droite (construits une fois, à l'ouverture du Studio).
std::vector<RightPanel> BuildPanels(const ui::Theme &th) {
	return {
		MakePanel(PanelKind::World, "world", "Monde", ui::icons::Codepoint(ui::icons::MaterialIcons::PUBLIC), th.info, "Monde lancé : statistiques, heure, systèmes ECS", true),
		MakePanel(PanelKind::Entities, "entities", "Entités", ui::icons::Codepoint(ui::icons::MaterialIcons::PETS), th.warning, "Entités autour du joueur (grille spatiale)", true),
		MakePanel(PanelKind::Blocks, "blocks", "Blocs", ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR), th.success, "Blocs enregistrés par les mods", false),
		MakePanel(PanelKind::Sql, "sql", "SQL", ui::icons::Codepoint(ui::icons::MaterialIcons::STORAGE), Rgba(198, 120, 221), "Requêtes sur la base du monde", true),
	};
}

// ── Monde ──
void WorldPanel(PanelsState &ps, Game &game, ui::Context &u, std::string &status, uint64_t now) {
	const ui::Theme &th = u.Style();
	if (now >= ps.nextRefresh && !ps.world.Loading()) { // capture légère, mise en forme en arrière-plan
		ps.nextRefresh = now + 500;
		struct Capture {
			std::string name;
			uint64_t seed;
			double updateMs;
			voxel::WorldStats stats;
			std::optional<math::FVector3> player;
			std::vector<std::pair<std::string, double>> systems;
		} cap{game.WorldInfo().name, game.World().Config().seed, game.LastUpdateMs(), game.World().Stats(), std::nullopt, {}};
		if (const auto *t = game.Ecs().Get<voxel::Transform>(game.Player()))
			cap.player = t->position;
		for (const auto &sys : game.Schedule().Stats())
			cap.systems.emplace_back(sys.name, sys.avgMs);
		ps.world.Request(*ps.worker, [cap = std::move(cap)](std::stop_token) mutable {
			WorldView v;
			const auto &st = cap.stats;
			v.info.push_back(std::format("« {} » · graine {}", cap.name, cap.seed));
			v.info.push_back(std::format("mise à jour {:.2f} ms · {} chunks ({:.1f} Mo)", cap.updateMs, st.loadedChunks, double(st.memoryBytes) / 1048576.0));
			v.info.push_back(std::format("{} entités · {} déplacements de grille", st.entities, st.gridMoves));
			v.info.push_back(std::format("base : {} lus, {} générés, {} sauvés, {} déchargés", st.chunksLoadedFromDisk, st.chunksGenerated, st.chunksSaved,
										 st.chunksUnloaded));
			v.info.push_back(std::format("en attente : {} chargements, {} maillages", st.pendingLoads, st.pendingMeshes));
			if (cap.player)
				v.info.push_back(std::format("joueur : {:.1f} {:.1f} {:.1f}", cap.player->x, cap.player->y, cap.player->z));
			v.systems = std::move(cap.systems);
			std::ranges::stable_sort(v.systems, std::greater{}, &std::pair<std::string, double>::second);
			for (const auto &sys : v.systems)
				v.totalMs += sys.second;
			return v;
		});
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::PUBLIC), "Monde");
	const WorldView *v = ps.world.Get();
	if (!v)
		u.Loader("Chargement des statistiques du monde…");
	else
		for (size_t i = 0; i < v->info.size(); ++i)
			u.Label(v->info[i], i ? th.textDim : th.text, i ? 13.f : 0.f);
	u.Section(icons::Codepoint(icons::MaterialIcons::WB_SUNNY), "Heure");
	float tod = game.TimeOfDay();
	if (u.SliderFloat("Heure du jour", tod, 0.f, 0.999f, "{:.2f}"))
		game.SetTimeOfDay(tod);
	u.Row({1, 1, 1});
	if (u.Button("Aube"))
		game.SetTimeOfDay(0.27f);
	if (u.Button("Midi"))
		game.SetTimeOfDay(0.5f);
	if (u.Button("Nuit"))
		game.SetTimeOfDay(0.0f);
	u.Section(icons::Codepoint(icons::MaterialIcons::SPEED), v ? std::format("Systèmes ECS · {:.3f} ms", v->totalMs) : std::string("Systèmes ECS"));
	if (!v)
		u.Loader({}, 60);
	else
		for (const auto &[name, ms] : v->systems)
			u.Label(std::format("{:<20} {:.3f} ms", name, ms), th.text, 13);
	u.Section(icons::Codepoint(icons::MaterialIcons::BUILD), "Actions");
	if (u.Button("Sauvegarder le monde")) {
		game.Save();
		status = "monde sauvegardé";
	}
	if (u.Button("Téléporter au point d'apparition"))
		game.Teleport({0.5f, float(game.World().SurfaceHeight(0, 0)) + 2.5f, 0.5f});
	// Banc d'essai : placer une structure devant le joueur (environnement neutre).
	u.Section(icons::Codepoint(icons::MaterialIcons::EXTENSION), "Banc d'essai — structures");
	u.TextInput("##structurefilter", ps.structureFilter, nullptr, "Filtrer (oak_tree, house, well…)…");
	const std::string sf = String(ps.structureFilter).ToLower();
	for (const auto &st : game.GetContent().mods.Structures()) {
		if (!sf.empty() && String(st.id).ToLower().find(sf) == std::string::npos)
			continue;
		if (u.Button(std::format("Placer « {} »", st.id))) {
			const auto *t = game.Ecs().Get<voxel::Transform>(game.Player());
			const int x = t ? int(std::floor(t->position.x)) : 0;
			const int z = t ? int(std::floor(t->position.z)) : 0;
			const int y = game.World().SurfaceHeight(x, z) + 1;
			const size_t placed = game.World().PlaceStructure(st, x, y, z);
			status = std::format("structure « {} » placée ({} blocs)", st.id, placed);
		}
	}
}

// ── Entités ──
void EntitiesPanel(PanelsState &ps, Game &game, ui::Context &u, uint64_t now) {
	const ui::Theme &th = u.Style();
	u.SliderFloat("Rayon (m)", ps.entityRadius, 8.f, 160.f, "{:.0f}");
	const bool radiusChanged = ps.entityRadius != ps.loadedRadius;
	if (radiusChanged || (now >= ps.nextRefresh && !ps.entities.Loading())) {
		ps.nextRefresh = now + 500;
		ps.loadedRadius = ps.entityRadius;
		// Capture : requête spatiale O(chunks voisins) et copie (type, distance) ; le comptage,
		// le tri et la mise en forme partent sur le thread de chargement.
		std::vector<std::pair<std::string, float>> samples;
		if (const auto *t = game.Ecs().Get<voxel::Transform>(game.Player())) {
			std::vector<ecs::Entity> found;
			game.World().QueryEntities(t->position, ps.entityRadius, found);
			samples.reserve(found.size());
			for (ecs::Entity e : found) {
				const auto *et = game.Ecs().Get<voxel::Transform>(e);
				const auto *k = game.Ecs().Get<voxel::Kind>(e);
				samples.emplace_back(k ? k->id : std::string("?"), et ? (et->position - t->position).Length() : 0.f);
			}
		}
		ps.entities.Request(*ps.worker, [samples = std::move(samples)](std::stop_token stop) mutable {
			EntityView v;
			v.total = samples.size();
			std::map<std::string, size_t> byKind;
			for (const auto &[kind, d] : samples)
				++byKind[kind];
			v.byKind.assign(byKind.begin(), byKind.end());
			std::ranges::stable_sort(v.byKind, std::greater{}, &std::pair<std::string, size_t>::second);
			if (stop.stop_requested())
				return v;
			const size_t n = std::min<size_t>(samples.size(), 40);
			std::ranges::partial_sort(samples, samples.begin() + std::ptrdiff_t(n), std::less{}, &std::pair<std::string, float>::second);
			for (size_t i = 0; i < n; ++i)
				v.nearest.push_back(std::format("{}  {:.1f} m", samples[i].first, samples[i].second));
			return v;
		});
	}
	const EntityView *v = ps.entities.Get();
	if (!v) {
		u.Loader("Recherche des entités…");
		return;
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::PETS), std::format("{} entité(s) (grille spatiale)", v->total));
	for (const auto &[k, n] : v->byKind) {
		u.Row({3, 1}, 24);
		u.Label(k, th.text, 13);
		u.Label(std::to_string(n), th.accent, 13);
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::GRID_ON), "Les plus proches");
	for (const std::string &line : v->nearest)
		u.Label(line, th.textDim, 12);
}

// ── Blocs ──
void BlocksPanel(PanelsState &ps, App &app, ui::Context &u, Document *doc, std::string &status) {
	const ui::Theme &th = u.Style();
	u.TextInput("##blockfilter", ps.blockFilter, nullptr, "Filtrer les blocs…");
	const size_t count = app.content.blocks.Count();
	if (ps.blockFilter != ps.loadedBlockFilter || count != ps.loadedBlockCount) {
		ps.loadedBlockFilter = ps.blockFilter;
		ps.loadedBlockCount = count;
		// Copie des noms (le registre peut être rechargé pendant que le thread filtre).
		std::vector<BlockRow> all;
		all.reserve(count);
		for (const auto &def : app.content.blocks.All())
			all.push_back({def.name, def.displayName});
		ps.blocks.Request(*ps.worker, [all = std::move(all), filter = String(ps.blockFilter).ToLower()](std::stop_token stop) mutable {
			BlockView v;
			v.total = all.size();
			for (BlockRow &row : all) {
				if (stop.stop_requested())
					break;
				if (!filter.empty() && String(row.name + " " + row.label).ToLower().find(filter) == std::string::npos)
					continue;
				if (++v.matched <= 300)
					v.rows.push_back(std::move(row));
			}
			return v;
		});
	}
	const BlockView *v = ps.blocks.Get();
	if (!v) {
		u.Loader("Chargement des blocs…");
		return;
	}
	for (const BlockRow &row : v->rows)
		if (u.Selectable(std::format("{}  —  {}##{}", row.name, row.label, row.name), false) && doc) {
			doc->lines[doc->cy].insert(std::min(doc->cx, doc->lines[doc->cy].size()), "\"" + row.name + "\"");
			doc->dirty = true;
			status = std::format("« {} » inséré", row.name);
		}
	u.Label(std::format("{} / {} bloc(s) — un clic insère le nom dans le fichier ouvert", v->matched, v->total), th.textDim, 12);
}

// ── SQL ──
void SqlPanel(PanelsState &ps, Game &game, ui::Context &u, uint64_t now) {
	const ui::Theme &th = u.Style();
	sdl3::Canvas &c = u.Canvas();
	sql::Database *db = &game.World().Storage().Db(); // thread-safe (verrou lecteurs/écrivain)
	bool submitted = false;
	u.TextInput("##sql", ps.sqlQuery, &submitted, "SELECT …");
	if ((u.Button("Exécuter la requête", !ps.query.Loading()) || submitted) && !ps.sqlQuery.empty()) {
		ps.query.Clear(); // le chargeur remplace l'ancien résultat
		ps.query.Request(*ps.worker, [db, sqlText = ps.sqlQuery](std::stop_token stop) {
			SqlQueryView v;
			const auto start = std::chrono::steady_clock::now();
			auto r = db->Execute(sqlText);
			v.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (!r) {
				v.error = r.Error();
				return v;
			}
			const sql::ResultSet &rs = r.Value();
			v.columns = rs.columns;
			v.rowCount = rs.rows.size();
			v.plan = rs.plan;
			for (size_t i = 0; i < rs.rows.size() && i < 500 && !stop.stop_requested(); ++i) {
				std::string line;
				for (size_t col = 0; col < rs.rows[i].size(); ++col)
					line += std::format("{}{}", col ? " | " : "", rs.rows[i][col].ToString());
				v.lines.push_back(std::move(line));
			}
			return v;
		});
	}
	if (ps.query.Loading() && !ps.query.Get()) {
		u.Loader("Exécution de la requête…");
	} else if (const SqlQueryView *v = ps.query.Get()) {
		if (!v->error.empty()) {
			u.Wrapped(v->error, th.danger, 13);
		} else {
			u.Label(std::format("{} ligne(s) · {:.2f} ms · plan : {}", v->rowCount, v->ms, v->plan.empty() ? "-" : v->plan), th.textDim, 12);
			// Lignes complètes en chasse fixe : la zone défile en X si elles dépassent.
			auto row = [&](const std::string &text, sdl3::Color col) {
				const Rect r = u.Next(18);
				const float w = c.Text(r.x, r.y + 1, text, col, 12, sdl3::CanvasFont::Mono);
				u.ExtendContent(r.x + w + 8);
			};
			if (!v->columns.empty()) {
				std::string header;
				for (size_t i = 0; i < v->columns.size(); ++i)
					header += std::format("{}{}", i ? " | " : "", v->columns[i]);
				row(header, th.accent);
			}
			for (const std::string &line : v->lines)
				row(line, th.text);
		}
	}
	if (now >= ps.nextRefresh && !ps.tables.Loading()) {
		ps.nextRefresh = now + 2000;
		ps.tables.Request(*ps.worker, [db](std::stop_token) { return SqlTablesView{db->TableNames()}; });
	}
	u.Section(icons::Codepoint(icons::MaterialIcons::TABLE_CHART), "Tables");
	const SqlTablesView *tables = ps.tables.Get();
	if (!tables) {
		u.Loader("Lecture du schéma…", 80);
		return;
	}
	for (const auto &name : tables->tables)
		if (u.Selectable(name, false))
			ps.sqlQuery = std::format("SELECT * FROM {} LIMIT 20", name);
}

/// Entité sous le rayon (AABB la plus proche), ou entité nulle.

/// Boîtes AABB des entités proches de `pos` (visualisation du viewport).
void BuildEntityBoxes(Game &g, const math::FVector3 &pos, int radius, std::vector<voxel::BoxInstance> &out) {
	out.clear();
	g.World().ForEachEntityNear(pos, radius, [&](ecs::Entity e) {
		const auto *t = g.Ecs().Get<voxel::Transform>(e);
		const auto *b = g.Ecs().Get<voxel::Body>(e);
		if (!t || !b)
			return;
		const uint64_t h = core::HashString(e == g.Player() ? "player" : (g.Ecs().Get<voxel::Kind>(e) ? g.Ecs().Get<voxel::Kind>(e)->id : "?"));
		out.push_back({t->position, b->halfExtents, uint8_t(90 + (h & 0x3f)), uint8_t(90 + ((h >> 8) & 0x3f)), uint8_t(110 + ((h >> 16) & 0x3f)), 255});
	});
}

/// Couleur moyenne d'une couche de texture 16×16 (pastille d'aperçu).
/// Repères de la mécanique dans la vue 3D : lumières (petits cubes de leur couleur), câbles
/// et poulie des contrepoids, ancres des autres joints.
void BuildMechanismBoxes(Game &g, std::vector<voxel::BoxInstance> &out) {
	ecs::World &w = g.Ecs();
	w.MakeQuery<const voxel::LightEmitter, const voxel::Transform>().Each([&](const voxel::LightEmitter &l, const voxel::Transform &t) {
		const math::FVector3 p = t.position + voxel::RotateYaw(l.offset, t.yaw);
		out.push_back({p, {0.08f, 0.08f, 0.08f}, uint8_t(std::min(1.f, l.color.x) * 255), uint8_t(std::min(1.f, l.color.y) * 255),
					   uint8_t(std::min(1.f, l.color.z) * 255), 255});
	});
	w.MakeQuery<const voxel::Joints, const voxel::Transform>().Each([&](const voxel::Joints &js, const voxel::Transform &ta) {
		for (const voxel::Joint &j : js.list) {
			const ecs::Entity b = voxel::FindByUid(w, j.target);
			const auto *tb = b ? w.Get<voxel::Transform>(b) : nullptr;
			const math::FVector3 anchor = ta.position + voxel::RotateYaw(j.anchor, ta.yaw);
			if (j.type == voxel::JointType::Pulley && tb) { // poulie au-dessus du plus haut, deux câbles verticaux
				const float top = std::max(ta.position.y, tb->position.y) + 3.f;
				const math::FVector3 pa{ta.position.x, top, ta.position.z}, pb{tb->position.x, top, tb->position.z};
				out.push_back({(pa + pb) * 0.5f, {std::fabs(pa.x - pb.x) * 0.5f + 0.3f, 0.25f, std::fabs(pa.z - pb.z) * 0.5f + 0.3f}, 120, 120, 130, 255});
				for (const math::FVector3 &q : {ta.position, tb->position}) {
					const float h = (top - q.y) * 0.5f;
					out.push_back({{q.x, q.y + h, q.z}, {0.03f, std::max(0.01f, h), 0.03f}, 40, 40, 44, 255});
				}
			} else {
				out.push_back({anchor, {0.06f, 0.06f, 0.06f}, 255, 200, 80, 255});
			}
		}
	});
}

sdl3::Color LayerColor(const Content &content, uint16_t layer) {
	const uint8_t *px = content.textureLayers.data() + size_t(layer) * voxel::kTextureSize * voxel::kTextureSize * 4;
	uint32_t acc[3] = {};
	for (int i = 0; i < voxel::kTextureSize * voxel::kTextureSize; ++i)
		for (int k = 0; k < 3; ++k)
			acc[k] += px[size_t(i) * 4 + size_t(k)];
	return sdl3::Rgba(uint8_t(acc[0] / voxel::kTextureSize * voxel::kTextureSize), uint8_t(acc[1] / voxel::kTextureSize * voxel::kTextureSize), uint8_t(acc[2] / voxel::kTextureSize * voxel::kTextureSize));
}

/// Texture 16×16 d'une couche (mise en cache par nom) pour l'aperçu, à l'échelle au plus proche.
sdl3::Texture *TexturePreview(sdl3::Renderer &ren, std::map<std::string, sdl3::Texture> &cache, const std::string &name,
							  const uint8_t *rgba) {
	if (auto it = cache.find(name); it != cache.end())
		return &it->second;
	auto tex = sdl3::Texture::Create(ren, voxel::kTextureSize, voxel::kTextureSize, sdl3::PixelFormat::RGBA32);
	if (!tex)
		return nullptr;
	tex.Value().SetScaleMode(sdl3::ScaleMode::NEAREST);
	tex.Value().Update(rgba, voxel::kTextureSize * 4);
	auto [ins, _] = cache.emplace(name, std::move(tex.Value()));
	return &ins->second;
}


} // namespace

struct Studio::Impl {
	sdl3::Window window;
	sdl3::Renderer renderer;
	sdl3::Canvas canvas;
	std::unique_ptr<ui::Context> ui;
	std::vector<Document> docs;
	int active = -1;
	bool editorFocus = false;
	std::set<std::string> treeExpanded;	// nœuds dépliés de l'arborescence
	std::string explorerSelected;		   // nœud sélectionné (focus) de l'explorateur
	std::string renamingPath;			   // fichier/dossier en cours de renommage (vide : aucun)
	std::string renameBuffer;
	jobs::AsyncValue<std::vector<ui::TreeNode>> fileTree; // arborescence construite sur un thread
	std::string consoleLine;
	std::string status = "Prêt";
	float charW = 8.f;
	std::string screenshot;
	bool firstFrame = true;
	std::vector<fs::path> pendingOpen;
	size_t lastCx = 0, lastCy = 0; // position du curseur à l'image précédente (défilement automatique)
	float consoleScrollX = 0, consoleScrollY = 0, consoleWidth = 0;
	bool consoleFollow = true;
	PanelsState panels; // panneau de droite (onglets, chargement asynchrone)
	ui::Viewport3D viewport; // vue 3D du monde en direct (caméra studio)
	ui::DockLayout dock;	 // disposition ancrable (gauche/centre/droite/bas)
	ecs::Entity selectedEntity; // entité sélectionnée dans le viewport
	std::string selectedTexture; // texture sélectionnée (onglets Textures / UV)
	std::map<std::string, sdl3::Texture> texturePreviews; // aperçus de textures (cache)
	studio::MechanismState mechanisms; // éditeur de mécanismes (phase 3)
	studio::CodeEditor codeEditor;	   // éditeur de code (phase 4)
	int consoleFilter = 0;			   // 0 tout, 1 erreurs, 2 avertissements, 3 commandes, 4 rechargements
	std::vector<std::string> consoleHistory;
	int historyIndex = -1;
	std::string consoleHint; // propositions de complétion
	studio::PerfState perf;	 // métriques en temps réel
	DebugView debugView;	 // superpositions de la vue 3D (phase 5)
	bool tabMode = false;	   // false = espaces, true = caractère de tabulation
	int tabSize = 4;		   // largeur de la tabulation (en espaces)
	bool tabConfigOpen = false; // panneau de configuration de la tabulation ouvert
	bool show3d = false;
	std::vector<std::string> pendingTabs; // onglets à activer (--studio-tab)
	// Phase 1 : espaces de travail, outliner, suivi caméra.
	std::vector<studio::Workspace> workspaces;
	size_t workspace = 0;
	fs::path layoutFile;
	studio::OutlinerState outliner;
	ecs::Entity follow;			  // entité suivie par la caméra de la vue 3D
	const void *followGame = nullptr; // partie à laquelle `follow` appartient
	float paintZoom = 0, paintPanX = 0, paintPanY = 0; // panneau « Peinture »
	std::unique_ptr<studio::ModelWorkshop> models;		 // phase 2 : atelier de modélisation
	std::unique_ptr<studio::WorldgenWorkshop> worldgen;	 // phase 6 : génération du monde
	std::string pendingModel;							 // --studio-tab model:ID

	void SwitchWorkspace(size_t index) {
		if (index >= workspaces.size())
			return;
		workspaces[workspace].layout = dock.Capture();
		workspace = index;
		dock.Apply(workspaces[workspace].layout, studio::PanelCatalog());
	}
	void SaveLayout() {
		if (workspaces.empty() || layoutFile.empty())
			return;
		workspaces[workspace].layout = dock.Capture();
		studio::SaveWorkspaces(layoutFile, workspaces, workspaces[workspace].id);
	}
};

Studio::Studio(App &app) : m_app(app) {}
Studio::~Studio() { Close(); }

sdl3::WindowID Studio::WindowId() const noexcept { return m_window ? m_window->GetId() : 0; }

void Studio::QueueOpen(fs::path path) {
	if (m_impl)
		m_impl->pendingOpen.push_back(std::move(path));
}

void Studio::RequestScreenshot(std::string path) {
	if (m_impl)
		m_impl->screenshot = std::move(path);
}

void Studio::Raise() {
	if (m_window)
		m_window->Raise();
}

bool Studio::Open() {
	if (m_impl) {
		Raise();
		return true;
	}
	auto impl = std::make_unique<Impl>();
	auto window = sdl3::Window::Create(String("Voxelia Studio"), 1400, 860, sdl3::windowFlags::RESIZABLE | sdl3::windowFlags::HIGH_PIXEL_DENSITY);
	if (!window) {
		core::log::Error("studio : {}", window.Error());
		return false;
	}
	impl->window = std::move(window.Value());
	// Renderer 2D sur le MÊME device GPU que la fenêtre principale : il peut
	// ainsi afficher la texture 3D du viewport (widget ui::Viewport3D).
	auto renderer = sdl3::Renderer::CreateOnGpuDevice(m_app.Device().Get(), impl->window);
	if (!renderer) {
		core::log::Error("studio : {}", renderer.Error());
		return false;
	}
	impl->renderer = std::move(renderer.Value());
	impl->renderer.SetVSync(sdl3::Renderer::VSync(1));
	const float scale = impl->window.DisplayScale();
	if (!impl->canvas.Init(impl->renderer,
						   sdl3::CanvasFontSet{"assets/fonts/DejaVuSans.ttf", "assets/fonts/DejaVuSans-Bold.ttf", "assets/fonts/DejaVuSansMono.ttf",
										"assets/fonts/MaterialIcons-Regular.otf"},
						   (scale > 0 ? scale : 1.f) * m_app.settings.uiScale))
		return false;
	impl->ui = std::make_unique<ui::Context>(impl->canvas, m_app.Events());
	impl->ui->Style().rowHeight = 28;
	m_window = &impl->window;
	m_window->StartTextInput();
	// Fermeture de la fenêtre du Studio (évènement routé via EventsManager).
	m_closeBindingId = m_app.Events().Bind(sdl3::EventType::WINDOW_CLOSE_REQUESTED, [this](const sdl3::Event &ev) {
		if (m_window && ev.Window().windowID == m_window->GetId())
			m_closeRequested = true;
	});
	m_impl = std::move(impl);
	m_impl->panels.tabs = BuildPanels(m_impl->ui->Style());
	m_impl->panels.worker = std::make_unique<jobs::ThreadPool>(1);
	// Arborescence des fichiers des mods construite sur un thread au démarrage.
	{
		std::vector<std::pair<std::string, fs::path>> mods;
		for (const voxel::ModInfo &m : m_app.content.mods.Mods())
			mods.emplace_back(m.id, m.directory);
		m_impl->fileTree.Request(jobs::ThreadPool::Global(), [mods = std::move(mods)](std::stop_token) {
			return BuildFileTree(mods);
		});
	}
	m_impl->models = std::make_unique<studio::ModelWorkshop>(m_app);
	m_impl->worldgen = std::make_unique<studio::WorldgenWorkshop>(m_app);
	// Espaces de travail (dispositions du dock) : défauts, puis dispositions enregistrées.
	m_impl->workspaces = studio::DefaultWorkspaces();
	m_impl->layoutFile = m_app.opt.data / "studio_layout.json";
	std::string activeWorkspace = "world";
	studio::LoadWorkspaces(m_impl->layoutFile, m_impl->workspaces, activeWorkspace);
	for (size_t i = 0; i < m_impl->workspaces.size(); ++i)
		if (m_impl->workspaces[i].id == activeWorkspace)
			m_impl->workspace = i;
	m_impl->dock.Apply(m_impl->workspaces[m_impl->workspace].layout, studio::PanelCatalog());
	return true;
}

void Studio::Close() {
	if (!m_impl)
		return;
	if (m_closeBindingId) {
		m_app.Events().Unbind(m_closeBindingId);
		m_closeBindingId = 0;
	}
	m_closeRequested = false;
	if (m_window)
		m_window->StopTextInput();
	m_impl->SaveLayout();
	m_impl->panels.Reset(); // le thread de chargement ne lit plus rien
	m_impl.reset();
	m_window = nullptr;
}

void Studio::ActivateTab(std::string_view id) {
	if (!m_impl)
		return;
	if (id == "3d") {
		m_impl->show3d = true;
		return;
	}
	if (id.starts_with("model:")) { // ouvre un modèle dès que le catalogue est lu
		m_impl->pendingModel = std::string(id.substr(6));
		return;
	}
	for (size_t i = 0; i < m_impl->workspaces.size(); ++i)
		if (m_impl->workspaces[i].id == id) {
			m_impl->SwitchWorkspace(i);
			return;
		}
	m_impl->pendingTabs.emplace_back(id); // appliqué après l'ouverture des fichiers (qui montre l'éditeur)
}

void Studio::FilesChanged(const FileChanges &changes) {
	if (!m_impl)
		return;
	m_impl->texturePreviews.clear(); // aperçus 16×16 à refaire
	if (!changes.models.empty() || !changes.definitions.empty())
		m_impl->models->ReloadCatalog();
	// Documents ouverts modifiés hors du Studio (et non modifiés ici) : relus.
	for (Document &d : m_impl->docs) {
		if (d.dirty)
			continue;
		for (const auto *list : {&changes.scripts, &changes.definitions, &changes.models}) {
			if (std::ranges::find(*list, d.path) == list->end())
				continue;
			std::ifstream f(d.path, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (text != d.Text() + "\n" && text != d.Text()) {
				const size_t cy = d.cy;
				d.SetText(text);
				d.cy = std::min(cy, d.lines.size() - 1);
				studio::CodeEditor::Check(d);
			}
		}
	}
}

void Studio::ReleaseGame() {
	if (m_impl)
		m_impl->panels.Reset();
}

void Studio::Frame(float dt) {
	// Fermeture demandée (détectée via EventsManager dans la boucle principale).
	if (m_closeRequested) {
		bool dirty = false;
		if (m_impl)
			for (const auto &d : m_impl->docs)
				dirty |= d.dirty;
		if (dirty)
			core::log::Warn("studio fermé avec des fichiers non enregistrés");
		Close();
		return;
	}
	if (!m_impl)
		return;
	DIAG_ZONE("Studio::Frame");
	Impl &s = *m_impl;
	App &app = m_app;
	sdl3::Canvas &c = s.canvas;
	s.renderer.SetDrawColor(sdl3::Color{16, 18, 24, 255});
	s.renderer.Clear();
	c.Begin();
	ui::Context &u = *s.ui;
	u.Begin(dt);
	const ui::Theme &th = u.Style();
	const float W = c.Width(), H = c.Height();
	Game *game = app.game.get();
	s.charW = c.MeasureText("M", 14, sdl3::CanvasFont::Mono);

	// ── Vue 3D du monde en direct (caméra studio) ────────────────────────
	if (s.show3d) {
		Game *g = game;
		if (g && app.renderer) {
			if (const auto *t = g->Ecs().Get<voxel::Transform>(g->Player()))
				s.viewport.SetTarget(t->position);
			s.viewport.Resize(app.Device(), s.renderer, s.renderer.GetOutputWidth(), s.renderer.GetOutputHeight(),
							   app.renderer->DepthFormat());
			voxel::SceneParams params;
			s.viewport.Render(app.Device(), *app.renderer, params, {}, nullptr);
		}
		c.FillRect({0, 0, W, H}, Rgba(10, 12, 16));
		s.viewport.Draw(u, {0, 0, W, H});
		if (!g)
			c.TextCentered({0, H * 0.5f - 10, W, 20}, "Ouvrez un monde dans le menu du jeu", th.textDim, 15);
		c.FillRect({0, 0, W, 46}, Rgba(22, 25, 32, 235));
		const float titleW = c.Text(14, (46 - c.LineHeight(16, sdl3::CanvasFont::Bold)) * 0.5f, "Vue 3D — monde en direct", th.text, 16, sdl3::CanvasFont::Bold);
		c.Text(14 + titleW + 18, (46 - c.LineHeight(12)) * 0.5f, "gauche : orbiter · molette : zoom · milieu / Maj+glisser : déplacer", th.textDim, 12);
		if (u.ButtonAt({W - 132, 7, 116, 32}, "Retour à l'IDE"))
			s.show3d = false;
		u.End();
		c.End();
		if (!s.screenshot.empty()) {
			if (auto pixels = s.renderer.ReadPixels())
				(void)sdl3::ImgSavePng(pixels.Value(), String(s.screenshot));
			s.screenshot.clear();
		}
		s.renderer.Present();
		
		return;
	}

	auto openFile = [&](const fs::path &path, const std::string &mod, bool show = true) {
		for (size_t i = 0; i < s.docs.size(); ++i)
			if (s.docs[i].path == path) {
				s.active = int(i);
				return;
			}
		Document d;
		d.path = path;
		d.mod = mod;
		std::ifstream f(path, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		d.SetText(text);
		studio::CodeEditor::Check(d);
		s.docs.push_back(std::move(d));
		s.active = int(s.docs.size()) - 1;
		s.editorFocus = true;
		if (const auto side = s.dock.Find("script"); show && side != ui::DockLayout::Side::Count)
			s.dock.Activate(side, "script"); // montrer l'éditeur (dans sa région courante)
	};
	auto check = [&](Document &d) {
		studio::CodeEditor::Check(d);
		s.status = d.errorMessage.empty() ? std::format("{} : aucune erreur", d.path.filename().string()) : d.errorMessage;
	};
	auto save = [&](Document &d) {
		std::ofstream f(d.path, std::ios::binary | std::ios::trunc);
		const std::string text = d.Text() + "\n";
		f.write(text.data(), std::streamsize(text.size()));
		d.dirty = !f;
		check(d);
		if (f && d.path.extension() == ".script" && game && game->Scripts()) {
			game->ReloadScripts(); // rechargement à chaud : l'état du monde est conservé
			s.status = std::format("enregistré + scripts rechargés : {}", d.path.string());
		} else {
			s.status = f ? std::format("enregistré : {}", d.path.string()) : std::format("échec d'écriture : {}", d.path.string());
		}
	};
	auto run = [&](Document &d) {
		check(d);
		if (!d.errorMessage.empty())
			return;
		if (!game || !game->Scripts()) {
			s.status = "aucune partie en cours : lancez un monde pour exécuter le script";
			return;
		}
		const std::string out = game->Scripts()->RunSource(d.Text(), (fs::path(d.mod) / d.path.filename()).string());
		game->Log(std::format("[studio] {} : {}", d.path.filename().string(), out));
		s.status = std::format("exécuté dans « {} » : {}", game->WorldInfo().name, out);
	};
	for (const fs::path &p : std::exchange(s.pendingOpen, {})) {
		std::string mod;
		for (const voxel::ModInfo &m : app.content.mods.Mods())
			if (p.string().starts_with(m.directory.string()))
				mod = m.id;
		if (fs::exists(p)) {
			openFile(p, mod);
			s.firstFrame = false;
		}
	}
	if (s.firstFrame) { // premier affichage : un script d'exemple ouvert
		s.firstFrame = false;
		if (const voxel::ModInfo *m = app.content.mods.Find("core"); m && !m->scripts.empty())
			openFile(m->scripts.front(), m->id, false); // sans changer d'onglet
	}
	Document *doc = s.active >= 0 && s.active < int(s.docs.size()) ? &s.docs[size_t(s.active)] : nullptr;
	for (const std::string &id : std::exchange(s.pendingTabs, {}))
		for (uint8_t side = 0; side < uint8_t(ui::DockLayout::Side::Count); ++side)
			s.dock.Activate(ui::DockLayout::Side(side), id);

	// Raccourcis.
	// Ctrl+S / Ctrl+Z / Ctrl+Y : vers l'atelier de modélisation s'il a reçu le dernier clic.
	if (u.Ctrl() && u.KeycodePressed(SDLK_S)) {
		if (s.models->Focused())
			s.models->Save();
		else if (doc)
			save(*doc);
	}
	if (s.models->Focused() && u.Ctrl() && !u.WantsKeyboard()) {
		if (u.KeycodePressed(SDLK_Z))
			u.Shift() ? s.models->Redo() : s.models->Undo();
		if (u.KeycodePressed(SDLK_Y))
			s.models->Redo();
	}
	if (doc && u.KeycodePressed(SDLK_F5))
		run(*doc);
	if (doc && u.KeycodePressed(SDLK_F7))
		check(*doc);
	if (game && u.KeycodePressed(SDLK_F8))
		game->SetPaused(!game->Paused());
	if (game && u.KeycodePressed(SDLK_F9) && game->Paused())
		game->Step();

	// ── Barre d'outils ───────────────────────────────────────────────────
	const float tb = 46;
	c.FillRect({0, 0, W, tb}, Rgba(22, 25, 32));
	c.FillRect({0, tb - 1, W, 1}, th.panelBorder);
	u.Icon({10, 0, 30, tb}, icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), th.info, 24);
	c.Text(46, (tb - c.LineHeight(17, sdl3::CanvasFont::Bold)) * 0.5f, "Voxelia Studio", th.text, 17, sdl3::CanvasFont::Bold);
	float bx = 210;
	auto tool = [&](char32_t icon, std::string_view tip, bool enabled) {
		const bool clicked = u.IconButton({bx, 7, 32, 32}, icon, tip, enabled);
		bx += 38;
		return clicked;
	};
	if (tool(icons::Codepoint(icons::MaterialIcons::SAVE), "Enregistrer (Ctrl+S)", doc && doc->dirty))
		save(*doc);
	if (tool(icons::Codepoint(icons::MaterialIcons::CHECK), "Vérifier la syntaxe (F7)", doc != nullptr))
		check(*doc);
	if (tool(icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), "Exécuter dans la partie (F5)", doc && game))
		run(*doc);
	if (tool(icons::Codepoint(icons::MaterialIcons::REFRESH), "Recharger le fichier depuis le disque", doc != nullptr)) {
		const fs::path p = doc->path;
		const std::string mod = doc->mod;
		s.docs.erase(s.docs.begin() + std::ptrdiff_t(s.active));
		openFile(p, mod);
		doc = &s.docs[size_t(s.active)];
	}
	if (tool(icons::Codepoint(icons::MaterialIcons::VISIBILITY), "Vue 3D du monde (caméra studio)", game != nullptr))
		s.show3d = true;
	if (tool(icons::Codepoint(icons::MaterialIcons::REPLAY), "Recharger les scripts des mods (à chaud)", game && game->Scripts()))
		game->ReloadScripts();
	// Espaces de travail : un bouton par disposition (libellés, ou icônes si la place manque).
	{
		const std::string gameText = game ? std::format("« {} » · {} entités · {} chunks{}", game->WorldInfo().name, game->Ecs().EntityCount(),
														 game->World().LoadedChunkCount(), game->Paused() ? " · en pause" : "")
										  : std::string("aucune partie en cours");
		float labelsW = 0;
		for (const studio::Workspace &ws : s.workspaces)
			labelsW += c.MeasureText(ws.title, 13) + 40;
		const float gameW = std::min(c.MeasureText(gameText, 13), W * 0.3f);
		const float start = bx + 16;
		const bool compact = start + labelsW + gameW + 40 > W;
		float wx = start;
		c.FillRect({start - 9, 9, 1, tb - 18}, th.panelBorder);
		for (size_t i = 0; i < s.workspaces.size(); ++i) {
			const studio::Workspace &ws = s.workspaces[i];
			const bool on = i == s.workspace;
			const float bw = compact ? 34.f : c.MeasureText(ws.title, 13) + 36;
			const Rect br{wx, 8, bw, tb - 16};
			u.PushId(ws.id);
			const auto it = u.Interact(u.MakeId("##ws"), br);
			c.FillRoundRect(br, 6, on ? Rgba(38, 92, 170) : (it.hovered ? th.widgetHover : Rgba(22, 25, 32)));
			u.Icon({br.x + (compact ? 0 : 6), br.y, compact ? bw : 22, br.h}, ws.icon, on ? th.text : th.textDim, 17);
			if (!compact)
				c.Text(br.x + 30, br.y + (br.h - c.LineHeight(13)) * 0.5f, ws.title, on ? th.text : th.textDim, 13);
			if (it.hovered)
				u.Tooltip(std::format("{} — {}", ws.title, ws.tooltip));
			if (it.leftClicked && !on)
				s.SwitchWorkspace(i);
			u.PopId();
			wx += bw + 4;
		}
		if (u.IconButton({wx + 2, 7, 32, 32}, icons::Codepoint(icons::MaterialIcons::UNDO), "Réinitialiser la disposition de cet espace de travail")) {
			s.workspaces[s.workspace].layout = s.workspaces[s.workspace].defaults;
			s.dock.Apply(s.workspaces[s.workspace].layout, studio::PanelCatalog());
		}
		const float textX = std::max(wx + 44, W - gameW - 16);
		c.Text(textX, (tb - c.LineHeight(13)) * 0.5f, c.Ellipsize(gameText, W - textX - 12, 13), game ? th.success : th.textDim, 13);
	}

	const float statusH = 24;
	const Rect dockArea{0, tb, W, H - tb - statusH};

	auto drawExplorer = [&](const Rect &left) {
		// ── Explorateur ──────────────────────────────────────────────────────
		c.FillRect(left, Rgba(20, 23, 30));
		c.FillRect({left.Right(), left.y, 1, left.h}, th.panelBorder);
		c.Text(left.x + 12, left.y + 10, "EXPLORATEUR — MODS", th.textDim, 12, sdl3::CanvasFont::Bold);

		auto modOf = [&](const fs::path &path) -> std::string {
			for (const voxel::ModInfo &m : app.content.mods.Mods())
				if (path.string().starts_with(m.directory.string()))
					return m.id;
			return {};
		};
		auto rebuildTree = [&]() {
			std::vector<std::pair<std::string, fs::path>> mods;
			for (const voxel::ModInfo &m : app.content.mods.Mods())
				mods.emplace_back(m.id, m.directory);
			s.fileTree.Request(jobs::ThreadPool::Global(), [mods = std::move(mods)](std::stop_token) {
				return BuildFileTree(mods);
			});
		};
		auto refreshDoc = [&]() { doc = s.active >= 0 && s.active < int(s.docs.size()) ? &s.docs[size_t(s.active)] : nullptr; };

		// Renommage en cours (F2 ou menu « Renommer »).
		if (!s.renamingPath.empty()) {
			const Rect r{left.x + 12, left.y + 34, left.w - 24, 30};
			bool submitted = false;
			u.TextInputAt(r, "##rename", s.renameBuffer, &submitted);
			if (submitted || u.KeyPressed(sdl3::keyboard::Key::ESCAPE)) {
				if (submitted && !s.renameBuffer.empty()) {
					const fs::path src(s.renamingPath);
					const fs::path dst = src.parent_path() / s.renameBuffer;
					std::error_code ec;
					if (src != dst)
						fs::rename(src, dst, ec);
					s.status = ec ? std::format("échec de renommage : {}", ec.message()) : std::format("renommé : {}", dst.filename().string());
					rebuildTree();
				}
				s.renamingPath.clear();
			}
		}

		s.fileTree.Poll();
		const std::vector<ui::TreeNode> *tree = s.fileTree.Get();
		u.BeginScroll("explorer", {left.x + 6, left.y + 32, left.w - 8, left.h - 36});
		if (!tree) {
			u.Loader("Analyse des mods…");
		} else {
			ui::TreeResult tr = u.Tree("mods", *tree, s.treeExpanded, s.explorerSelected);
			if (!tr.doubleClicked.empty() && !tr.doubleIsDir) { // double-clic sur un fichier : ouvrir
				openFile(fs::path(tr.doubleClicked), modOf(fs::path(tr.doubleClicked)));
				refreshDoc();
			}
			if (!tr.renameTarget.empty()) { // F2 : renommer le nœud sélectionné
				s.renamingPath = tr.renameTarget;
				s.renameBuffer = fs::path(tr.renameTarget).filename().string();
			}
			if (!tr.menuAction.empty()) {
				const fs::path target(tr.menuTarget);
				if (tr.menuAction == "open") {
					openFile(target, modOf(target));
					refreshDoc();
				} else if (tr.menuAction == "rename") {
					s.renamingPath = tr.menuTarget;
					s.renameBuffer = target.filename().string();
				} else if (tr.menuAction == "duplicate") {
					const fs::path dst = target.parent_path() / (target.stem().string() + " (copie)" + target.extension().string());
					std::error_code ec;
					if (fs::is_directory(target))
						fs::copy(target, dst, fs::copy_options::recursive, ec);
					else
						fs::copy(target, dst, ec);
					s.status = ec ? std::format("échec de duplication : {}", ec.message()) : std::format("dupliqué : {}", dst.filename().string());
					if (!ec)
						rebuildTree();
				} else if (tr.menuAction == "delete") {
					std::error_code ec;
					fs::remove_all(target, ec);
					s.status = ec ? std::format("échec de suppression : {}", ec.message()) : std::format("supprimé : {}", target.filename().string());
					std::erase_if(s.docs, [&](const Document &d) { return d.path.string().starts_with(target.string()); });
					s.active = s.docs.empty() ? -1 : std::min(s.active, int(s.docs.size()) - 1);
					refreshDoc();
					if (!ec)
						rebuildTree();
				}
			}
		}
		u.EndScroll();
	};

	auto drawEditor = [&](const Rect &center) {
	// ── Onglets et éditeur ───────────────────────────────────────────────
	c.FillRect(center, Rgba(14, 16, 21));
	// Barre d'onglets (ui::TabBar) : icône par type, libellé défilant au survol, « point »
	// si non enregistré, bouton fermer, défilement horizontal, glisser-déposer.
	ui::TabBarOptions tabOptions;
	const float tabsH = ui::Context::TabBarHeight(tabOptions);
	std::vector<ui::TabItem> tabItems;
	tabItems.reserve(s.docs.size());
	for (const Document &d : s.docs) {
		ui::TabItem t;
		const bool json = d.path.extension() == ".json";
		t.id = d.path.string();
		t.label = std::format("{} · {}", d.path.filename().string(), d.mod);
		t.icon = json ? icons::Codepoint(icons::MaterialIcons::DESCRIPTION) : icons::Codepoint(icons::MaterialIcons::CODE);
		t.iconColor = json ? th.warning : th.info;
		t.modified = d.dirty;
		if (!d.errorMessage.empty()) {
			t.statusIcons.push_back(icons::Codepoint(icons::MaterialIcons::ERROR));
			t.labelStyle.color = Rgba(255, 150, 150);
		}
		t.tooltip = d.path.string() + (d.dirty ? " — modifié" : "");
		tabItems.push_back(std::move(t));
	}
	const ui::TabBarResult tabResult = u.TabBar("studio-tabs", {center.x, center.y, center.w, tabsH}, tabItems, s.active, tabOptions);
	if (tabResult.activated >= 0) {
		s.active = tabResult.activated;
		s.editorFocus = true;
	}
	if (tabResult.Moved()) {
		const std::string activePath = s.active >= 0 ? s.docs[size_t(s.active)].path.string() : std::string();
		ui::MoveItem(s.docs, tabResult.moveFrom, tabResult.moveTo);
		for (size_t i = 0; i < s.docs.size(); ++i)
			if (s.docs[i].path.string() == activePath)
				s.active = int(i);
	}
	if (tabResult.buttonTab >= 0 && tabResult.button == 0) { // fermer
		s.docs.erase(s.docs.begin() + tabResult.buttonTab);
		if (s.active > tabResult.buttonTab || s.active >= int(s.docs.size()))
			--s.active;
		s.active = s.docs.empty() ? -1 : std::max(0, s.active);
	}
	doc = s.active >= 0 && s.active < int(s.docs.size()) ? &s.docs[size_t(s.active)] : nullptr;
	const Rect editor{center.x, center.y + tabsH, center.w, center.h - tabsH};
	if (!doc) {
		c.TextCentered(editor, "Ouvrez un fichier dans l'explorateur (scripts .script, mod.json, config.json).", th.textDim, 15);
	} else {
		studio::EditorEnv env;
		env.tabMode = s.tabMode;
		env.tabSize = s.tabSize;
		env.complete = [game](std::string_view prefix) { return Completions(game, prefix); };
		s.codeEditor.Draw(u, editor, *doc, env, s.editorFocus);
	}
	};

	auto drawConsole = [&](const Rect &console) {
	// ── Console de la partie : filtres, couleurs, liens vers les erreurs, historique,
	//    complétion (Tab) ───────────────────────────────────────────────────
	c.FillRect(console, Rgba(12, 14, 18));
	c.FillRect({console.x, console.y, console.w, 1}, th.panelBorder);
	u.PushId("console");
	static constexpr std::string_view kFilters[] = {"Tout", "Erreurs", "Avertissements", "Commandes", "Rechargements"};
	std::deque<std::string> empty;
	std::deque<std::string> &lines = game ? game->ConsoleLines() : empty;
	size_t counts[5] = {lines.size(), 0, 0, 0, 0};
	for (const std::string &l : lines) {
		const ConsoleKind k = ClassifyConsole(l);
		if (k == ConsoleKind::Error)
			++counts[1];
		else if (k == ConsoleKind::Warning)
			++counts[2];
		else if (k == ConsoleKind::Command)
			++counts[3];
		else if (k == ConsoleKind::Reload)
			++counts[4];
	}
	float fx = console.x + 8;
	for (int f = 0; f < 5; ++f) {
		const std::string label = std::format("{} {}##cf{}", kFilters[f], counts[f], f);
		const float fw = c.MeasureText(std::format("{} {}", kFilters[f], counts[f]), th.fontSize) + 22;
		if (u.ButtonAt({fx, console.y + 3, fw, 22}, label, true, s.consoleFilter == f)) {
			s.consoleFilter = f;
			s.consoleFollow = true;
		}
		fx += fw + 4;
	}
	if (u.ButtonAt({console.Right() - 78, console.y + 3, 70, 22}, "Effacer##clear", game != nullptr))
		lines.clear();
	if (game) {
		std::vector<size_t> shown;
		shown.reserve(lines.size());
		for (size_t i = 0; i < lines.size(); ++i) {
			const ConsoleKind k = ClassifyConsole(lines[i]);
			const bool keep = s.consoleFilter == 0 || (s.consoleFilter == 1 && k == ConsoleKind::Error) ||
							  (s.consoleFilter == 2 && k == ConsoleKind::Warning) || (s.consoleFilter == 3 && k == ConsoleKind::Command) ||
							  (s.consoleFilter == 4 && k == ConsoleKind::Reload);
			if (keep)
				shown.push_back(i);
		}
		const ui::ScrollbarOptions bars{};
		const Rect area{console.x, console.y + 28, console.w, console.h - 68 - (s.consoleHint.empty() ? 0.f : 18.f)};
		const Rect view{area.x, area.y, area.w - bars.thickness, area.h - bars.thickness};
		constexpr float kLineH = 17;
		const float contentH = float(shown.size()) * kLineH + 6;
		const float maxY = std::max(0.f, contentH - view.h);
		if (s.consoleFollow)
			s.consoleScrollY = maxY;
		const float maxX = std::max(0.f, s.consoleWidth + 20 - view.w);
		u.ScrollInput(view, s.consoleScrollX, s.consoleScrollY, maxX, maxY, kLineH * 3);
		u.ScrollbarV("##console-v", {view.Right(), view.y, bars.thickness, view.h}, s.consoleScrollY, contentH, view.h, bars);
		u.ScrollbarH("##console-h", {view.x, view.Bottom(), view.w, bars.thickness}, s.consoleScrollX, s.consoleWidth + 20, view.w, bars);
		c.FillRect({view.Right(), view.Bottom(), bars.thickness, bars.thickness}, Rgba(24, 27, 35));
		s.consoleFollow = s.consoleScrollY >= maxY - 1;
		c.PushClip(view);
		const size_t first = size_t(std::max(0.f, s.consoleScrollY / kLineH));
		for (size_t n = first; n < shown.size(); ++n) {
			const std::string &line = lines[shown[n]];
			const float y = view.y + 3 + float(n) * kLineH - s.consoleScrollY;
			if (y > view.Bottom())
				break;
			const ConsoleKind kind = ClassifyConsole(line);
			const sdl3::Color col = kind == ConsoleKind::Error	  ? Rgba(255, 140, 140)
								   : kind == ConsoleKind::Warning ? Rgba(236, 190, 100)
								   : kind == ConsoleKind::Command ? th.accentHover
								   : kind == ConsoleKind::Reload  ? Rgba(120, 180, 255)
																  : th.text;
			if (kind == ConsoleKind::Error || kind == ConsoleKind::Warning)
				c.FillRect({view.x + 2, y + 1, 3, kLineH - 3}, col);
			const float x0 = view.x + 10 - s.consoleScrollX;
			const float w = c.Text(x0, y, line, col, 13, sdl3::CanvasFont::Mono);
			s.consoleWidth = std::max(s.consoleWidth, w);
			// Lien « fichier.script:ligne:colonne » : ouvre le fichier à l'endroit de l'erreur.
			if (const auto link = FindFileLink(line)) {
				const float lx = x0 + c.MeasureText(std::string_view(line).substr(0, link->begin), 13, sdl3::CanvasFont::Mono);
				const float lw = c.MeasureText(std::string_view(line).substr(link->begin, link->end - link->begin), 13, sdl3::CanvasFont::Mono);
				const Rect lr{lx, y, lw, kLineH};
				const auto lit = u.Interact(u.MakeId(std::format("##link{}", shown[n])), lr);
				c.FillRect({lx, y + kLineH - 3, lw, 1}, lit.hovered ? th.text : sdl3::WithAlpha(col, 160));
				if (lit.hovered)
					u.Tooltip("ouvrir dans l'éditeur");
				if (lit.leftClicked)
					if (const auto path = ResolveModFile(app, link->path)) {
						std::string mod;
						for (const voxel::ModInfo &m : app.content.mods.Mods())
							if (path->string().starts_with(m.directory.string()))
								mod = m.id;
						openFile(*path, mod);
						doc = s.active >= 0 && s.active < int(s.docs.size()) ? &s.docs[size_t(s.active)] : nullptr;
						if (doc)
							s.codeEditor.Goto(*doc, link->line, link->column);
						s.editorFocus = true;
					}
			}
		}
		c.PopClip();
		if (!s.consoleHint.empty())
			c.Text(console.x + 10, console.Bottom() - 56, c.Ellipsize(s.consoleHint, console.w - 20, 12), th.textDim, 12, sdl3::CanvasFont::Mono);
	} else {
		c.Text(console.x + 10, console.y + 34, "Lancez un monde pour voir la console de la partie.", th.textDim, 13);
	}
	// Saisie : historique (↑ / ↓), complétion (Tab) des commandes « /… » et du code.
	bool submitted = false;
	const bool hadFocus = u.HasFocus("##studioconsole");
	if (hadFocus) {
		for (sdl3::Keycode code : u.Events().PressedKeycodes()) { const sdl3::keyboard::Key k = static_cast<sdl3::keyboard::Key>(code);
			if ((k == sdl3::keyboard::Key::UP || k == sdl3::keyboard::Key::DOWN) && !s.consoleHistory.empty()) {
				const int n = int(s.consoleHistory.size());
				s.historyIndex = k == sdl3::keyboard::Key::UP ? std::max(0, (s.historyIndex < 0 ? n : s.historyIndex) - 1) : s.historyIndex + 1;
				if (s.historyIndex >= n) {
					s.historyIndex = -1;
					s.consoleLine.clear();
				} else {
					s.consoleLine = s.consoleHistory[size_t(s.historyIndex)];
				}
			} else if (k == sdl3::keyboard::Key::TAB && game) {
				const size_t start = s.consoleLine.find_last_of(" (,") == std::string::npos ? 0 : s.consoleLine.find_last_of(" (,") + 1;
				const std::string word = s.consoleLine.substr(start);
				std::vector<std::string> cands;
				if (start == 0 && word.starts_with('/')) {
					std::vector<std::string> cmds = {"help", "tp", "fly", "time", "seed", "save", "reload", "stats", "sql", "spawn",
													 "structures", "structure", "near", "events", "chunks"};
					if (game->Scripts())
						for (const auto &cmd : game->Scripts()->Commands())
							cmds.push_back(cmd.id);
					for (const std::string &cmd : cmds)
						if (("/" + cmd).starts_with(word))
							cands.push_back("/" + cmd);
				} else {
					const size_t dot = word.rfind('.');
					for (const std::string &n : Completions(game, word))
						cands.push_back((dot == std::string::npos ? std::string() : word.substr(0, dot + 1)) + n);
				}
				std::ranges::sort(cands);
				cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
				if (cands.size() == 1) {
					s.consoleLine = s.consoleLine.substr(0, start) + cands[0];
					s.consoleHint.clear();
				} else if (!cands.empty()) {
					std::string common = cands[0];
					for (const std::string &cand : cands)
						while (!cand.starts_with(common))
							common.pop_back();
					s.consoleLine = s.consoleLine.substr(0, start) + common;
					s.consoleHint.clear();
					for (size_t k2 = 0; k2 < cands.size() && k2 < 12; ++k2)
						s.consoleHint += cands[k2] + "  ";
				}
				u.SetFocus("##studioconsole");
			}
		}
	}
	u.TextInputAt({console.x + 8, console.Bottom() - 36, console.w - 16, 30}, "##studioconsole", s.consoleLine, &submitted,
				  game ? "commande (/help) ou code Script · flèches : historique · Tab : complétion" : "aucune partie en cours");
	if (submitted && game && !s.consoleLine.empty()) {
		if (s.consoleHistory.empty() || s.consoleHistory.back() != s.consoleLine)
			s.consoleHistory.push_back(s.consoleLine);
		s.historyIndex = -1;
		s.consoleHint.clear();
		game->ExecuteConsole(s.consoleLine);
		s.consoleLine.clear();
		s.consoleFollow = true;
	}
	u.PopId();
	};

	auto drawRight = [&](const Rect &right) {
	// ── Panneau de droite ────────────────────────────────────────────────
	c.FillRect(right, Rgba(20, 23, 30));
	c.FillRect({right.x - 1, right.y, 1, right.h}, th.panelBorder);
	// Onglets construits à l'ouverture (`BuildPanels`) : ui::TabBar sans bouton fermer,
	// réordonnables ; le contenu est chargé en arrière-plan (indicateur pendant le chargement).
	PanelsState &ps = s.panels;
	if (ps.game != game) { // autre partie (ou aucune) : les données affichées sont périmées
		ps.Reset();
		ps.game = game;
	}
	ps.world.Poll();
	ps.entities.Poll();
	ps.blocks.Poll();
	ps.tables.Poll();
	ps.query.Poll();
	ui::TabBarOptions panelTabOptions;
	panelTabOptions.tabHeight = 30;
	panelTabOptions.minTabWidth = 70;
	panelTabOptions.maxTabWidth = 150;
	panelTabOptions.scrollbar.thickness = 8;
	panelTabOptions.scrollbar.arrows = false;
	const float panelTabsH = ui::Context::TabBarHeight(panelTabOptions);
	std::vector<ui::TabItem> panelItems;
	for (const RightPanel &p : ps.tabs)
		panelItems.push_back(p.tab);
	const int previousPanel = ps.active;
	const ui::TabBarResult panelResult = u.TabBar("right-panels", {right.x + 1, right.y + 4, right.w - 1, panelTabsH}, panelItems, ps.active, panelTabOptions);
	if (panelResult.activated >= 0)
		ps.active = panelResult.activated;
	if (panelResult.Moved()) {
		const std::string activeId = ps.tabs[size_t(ps.active)].tab.id;
		ui::MoveItem(ps.tabs, panelResult.moveFrom, panelResult.moveTo);
		for (size_t i = 0; i < ps.tabs.size(); ++i)
			if (ps.tabs[i].tab.id == activeId)
				ps.active = int(i);
	}
	if (ps.active != previousPanel)
		ps.nextRefresh = 0; // rafraîchir tout de suite le panneau affiché
	const RightPanel &panel = ps.tabs[size_t(ps.active)];
	const Rect body{right.x + 8, right.y + 4 + panelTabsH + 6, right.w - 10, right.h - panelTabsH - 14};
	u.BeginScroll("right-" + panel.tab.id, body);
	const uint64_t now = sdl3::GetTicksMS();
	if (!game && panel.needsGame) {
		u.Wrapped("Aucune partie en cours. Ce panneau montre l'état d'un monde lancé.", th.textDim);
	} else {
		switch (panel.kind) {
		case PanelKind::World: WorldPanel(ps, *game, u, s.status, now); break;
		case PanelKind::Entities: EntitiesPanel(ps, *game, u, now); break;
		case PanelKind::Blocks: BlocksPanel(ps, app, u, doc, s.status); break;
		case PanelKind::Sql: SqlPanel(ps, *game, u, now); break;
		}
	}
	u.EndScroll();
	};

	auto drawTextures = [&](const Rect &r) {
		// Liste des textures de blocs (couches 16×16) : pastille + nom.
		std::vector<std::pair<std::string, uint16_t>> texs;
		for (const auto &e : app.content.layerOf)
			texs.emplace_back(e.key, e.value);
		std::ranges::sort(texs, [](const auto &a, const auto &b) { return a.first < b.first; });
		u.BeginScroll("textures", r);
		for (const auto &[name, layer] : texs) {
			const Rect row = u.Next(26);
			const bool sel = name == s.selectedTexture;
			c.FillRoundRect(row, 4, sel ? th.widgetActive : (u.MouseIn(row) ? th.widgetHover : th.widget));
			const uint8_t *px = app.content.textureLayers.data() + size_t(layer) * voxel::kTextureSize * voxel::kTextureSize * 4;
			if (sdl3::Texture *t = TexturePreview(s.renderer, s.texturePreviews, name, px))
				c.Image(*t, {row.x + 4, row.y + 5, 16, 16});
			else
				c.FillRect({row.x + 4, row.y + 5, 16, 16}, LayerColor(app.content, layer));
			c.Text(row.x + 26, row.y + (row.h - c.LineHeight(13)) * 0.5f, c.Ellipsize(name, r.w - 40, 13), th.text, 13);
			if (u.Interact(u.MakeId(name), row).leftClicked)
				s.selectedTexture = name;
		}
		u.EndScroll();
	};

	auto drawUv = [&](const Rect &r) {
		if (s.selectedTexture.empty()) {
			u.BeginColumn(r.Inset(16, 20));
			u.Wrapped("Sélectionnez une texture dans l'onglet « Textures ».", th.textDim, 14);
			u.EndColumn();
			return;
		}
		uint16_t layer = 0;
		bool found = false;
		for (const auto &e : app.content.layerOf)
			if (e.key == s.selectedTexture) {
				layer = e.value;
				found = true;
				break;
			}
		if (!found) {
			c.TextCentered(r, "Texture introuvable.", th.textDim, 14);
			return;
		}
		c.Text(r.x + 8, r.y + 4, c.Ellipsize(s.selectedTexture, r.w - 16, 13), th.text, 13);
		const float sq = std::min(r.w, r.h - 40) - 16;
		const Rect dst{r.x + (r.w - sq) * 0.5f, r.y + 28, sq, sq};
		c.FillRect(dst, Rgba(24, 28, 36));
		const uint8_t *px = app.content.textureLayers.data() + size_t(layer) * voxel::kTextureSize * voxel::kTextureSize * 4;
		if (sdl3::Texture *t = TexturePreview(s.renderer, s.texturePreviews, s.selectedTexture, px))
			c.Image(*t, dst);
		for (int k = 0; k <= 4; ++k) { // grille UV 0..1
			const float f = float(k) / 4.f;
			c.Line(dst.x + dst.w * f, dst.y, dst.x + dst.w * f, dst.Bottom(), Rgba(255, 255, 255, 40));
			c.Line(dst.x, dst.y + dst.h * f, dst.Right(), dst.y + dst.h * f, Rgba(255, 255, 255, 40));
		}
		c.Text(r.x + 8, dst.Bottom() + 6, "UV 0..1 · texture 16×16", th.textDim, 12);
	};


	// ── Vue 3D du monde en direct (onglet « viewport ») ──────────────────
	// Barre d'outils au-dessus de l'image (pause / pas à pas, gizmo, cadrage, suivi),
	// puis le monde rendu dans une texture : le Studio encapsule le jeu.
	auto drawViewport = [&](const Rect &area) {
		Game *g = game;
		const float barH = 34;
		const Rect bar{area.x, area.y, area.w, barH};
		const Rect r{area.x, area.y + barH, area.w, area.h - barH};
		c.FillRect(bar, Rgba(18, 21, 27));
		c.FillRect({bar.x, bar.Bottom() - 1, bar.w, 1}, th.panelBorder);
		float x = bar.x + 6;
		auto button = [&](char32_t icon, std::string_view tip, bool enabled, bool on) {
			const Rect b{x, bar.y + 3, 28, 28};
			if (on)
				c.FillRoundRect(b, 5, Rgba(38, 92, 170));
			x += 31;
			return u.IconButton(b, icon, tip, enabled, on ? th.text : sdl3::Color{0, 0, 0, 0});
		};
		auto separator = [&] {
			c.FillRect({x + 2, bar.y + 8, 1, barH - 16}, th.panelBorder);
			x += 8;
		};
		const bool paused = game && game->Paused();
		if (button(paused ? icons::Codepoint(icons::MaterialIcons::PLAY_ARROW) : icons::Codepoint(icons::MaterialIcons::PAUSE), paused ? "Reprendre la simulation (F8)" : "Suspendre la simulation (F8)", game != nullptr,
				   paused))
			game->SetPaused(!paused);
		if (button(icons::Codepoint(icons::MaterialIcons::SKIP_NEXT), "Avancer d'un pas de simulation (F9)", paused, false))
			game->Step();
		separator();
		const bool translate = s.viewport.GetGizmo() == ui::Viewport3D::Gizmo::Translate;
		if (button(icons::Codepoint(icons::MaterialIcons::OPEN_WITH), "Gizmo : déplacer (G)", true, translate))
			s.viewport.SetGizmo(ui::Viewport3D::Gizmo::Translate);
		if (button(icons::Codepoint(icons::MaterialIcons::ROTATE_RIGHT), "Gizmo : tourner autour de Y (G)", true, !translate))
			s.viewport.SetGizmo(ui::Viewport3D::Gizmo::Rotate);
		separator();
		const bool hasSel = g && s.selectedEntity && g->Ecs().IsAlive(s.selectedEntity);
		bool focus = button(icons::Codepoint(icons::MaterialIcons::CENTER_FOCUS_STRONG), "Cadrer la sélection (F)", hasSel, false);
		const bool following = g && s.follow && g->Ecs().IsAlive(s.follow);
		if (button(icons::Codepoint(icons::MaterialIcons::MY_LOCATION), following ? "Ne plus suivre (caméra libre)" : "Suivre la sélection, sinon le joueur", g != nullptr, following))
			s.follow = following ? ecs::Entity{} : (hasSel ? s.selectedEntity : g->Player());
		separator();
		// Superpositions de débogage (phase 5).
		if (button(icons::Codepoint(icons::MaterialIcons::CROP_FREE), "Boîtes de collision (AABB)", true, s.debugView.boxes))
			s.debugView.boxes = !s.debugView.boxes;
		if (button(icons::Codepoint(icons::MaterialIcons::ARROW_FORWARD), "Vecteurs vitesse", true, s.debugView.velocity))
			s.debugView.velocity = !s.debugView.velocity;
		if (button(icons::Codepoint(icons::MaterialIcons::GRID_ON), "Bordures des chunks", true, s.debugView.chunks))
			s.debugView.chunks = !s.debugView.chunks;
		if (button(icons::Codepoint(icons::MaterialIcons::LIGHTBULB), "Lumières : portée et rayons", true, s.debugView.lights))
			s.debugView.lights = !s.debugView.lights;
		if (button(icons::Codepoint(icons::MaterialIcons::BUILD), "Joints", true, s.debugView.joints))
			s.debugView.joints = !s.debugView.joints;
		// État à droite de la barre.
		std::string state;
		if (game)
			state = std::format("{}tick {}", paused ? "EN PAUSE · " : "", game->Tick());
		if (following)
			if (const auto *k = g->Ecs().Get<voxel::Kind>(s.follow))
				state += std::format(" · suivi : {}", k->id);
		if (!state.empty()) {
			const float sw = std::min(c.MeasureText(state, 12), bar.Right() - x - 16);
			c.Text(bar.Right() - sw - 10, bar.y + (barH - c.LineHeight(12)) * 0.5f, c.Ellipsize(state, sw + 1, 12), paused ? th.warning : th.textDim, 12);
		}
		if (!g || !app.renderer) {
			c.FillRect(r, Rgba(10, 12, 16));
			c.TextCentered(r, "Ouvrez un monde dans le menu du jeu", th.textDim, 14);
			return;
		}
		if (s.followGame != g) { // autre monde : on suit son joueur, sélection oubliée
			s.followGame = g;
			s.follow = g->Player();
			s.selectedEntity = {};
			s.viewport.ClearSelection();
		}
		// Raccourcis quand la souris survole la vue.
		if (u.MouseIn(r) && !u.WantsKeyboard()) {
			if (u.MousePressed(sdl3::MouseButton::Left))
				s.editorFocus = false;
			if (u.KeycodePressed(SDLK_F))
				focus = hasSel;
			if (u.KeycodePressed(SDLK_G))
				s.viewport.SetGizmo(translate ? ui::Viewport3D::Gizmo::Rotate : ui::Viewport3D::Gizmo::Translate);
			// Vol au clavier (ZQSD/WASD, Q/E : descendre/monter, Maj : rapide) : la cible orbitale
			// se déplace dans le repère de la caméra.
			if (s.window.HasInputFocus() && !u.Ctrl()) {
				const voxel::Camera &cam = s.viewport.Camera();
				const math::FVector3 f = cam.Forward(), rt{std::cos(cam.yaw), 0, std::sin(cam.yaw)};
				const float sp = std::max(4.f, (cam.position - s.viewport.Target()).Length() * 1.2f) * dt *
								 (sdl3::keyboard::IsPressed(SDL_SCANCODE_LSHIFT) ? 3.f : 1.f);
				math::FVector3 move{};
				move = move + f * float(int(sdl3::keyboard::IsPressed(SDL_SCANCODE_W)) - int(sdl3::keyboard::IsPressed(SDL_SCANCODE_S)));
				move = move + rt * float(int(sdl3::keyboard::IsPressed(SDL_SCANCODE_D)) - int(sdl3::keyboard::IsPressed(SDL_SCANCODE_A)));
				move.y += float(int(sdl3::keyboard::IsPressed(SDL_SCANCODE_E)) - int(sdl3::keyboard::IsPressed(SDL_SCANCODE_Q)));
				if (move.LengthSq() > 0) {
					s.follow = {};
					s.viewport.SetTarget(s.viewport.Target() + move.Normalized() * sp);
				}
			}
		}
		if (s.follow && g->Ecs().IsAlive(s.follow))
			if (const auto *t = g->Ecs().Get<voxel::Transform>(s.follow))
				s.viewport.SetTarget(t->position);
		if (focus && hasSel)
			if (const auto *t = g->Ecs().Get<voxel::Transform>(s.selectedEntity)) {
				s.viewport.SetTarget(t->position);
				s.follow = {};
			}
		// Gizmo synchronisé sur l'entité (elle peut bouger toute seule).
		if (hasSel && !s.viewport.Dragging()) {
			if (const auto *t = g->Ecs().Get<voxel::Transform>(s.selectedEntity)) {
				s.viewport.SetSelection(t->position);
				s.viewport.SetSelectionYaw(t->yaw);
			}
		} else if (!hasSel && s.viewport.HasSelection()) {
			s.viewport.ClearSelection();
		}
		// Boîtes des entités (visualisation) + contour de la sélection.
		std::vector<voxel::BoxInstance> boxes;
		BuildEntityBoxes(*g, s.viewport.Camera().position, 4, boxes);
		voxel::BoxInstance outline;
		bool hasOutline = false;
		if (hasSel) {
			const auto *t = g->Ecs().Get<voxel::Transform>(s.selectedEntity);
			const auto *b = g->Ecs().Get<voxel::Body>(s.selectedEntity);
			if (t && b) {
				outline = {t->position, b->halfExtents, 255, 210, 90, 255};
				hasOutline = true;
			}
		}
		// Mécanique : sources de lumière, câbles des poulies, ancres des joints.
		BuildMechanismBoxes(*g, boxes);
		const float sc = c.Scale();
		s.viewport.Resize(app.Device(), s.renderer, int(r.w * sc), int(r.h * sc), app.renderer->DepthFormat());
		voxel::SceneParams params;
		g->CollectLights(s.viewport.Camera().position, params.lights);
		s.viewport.Render(app.Device(), *app.renderer, params, boxes, hasOutline ? &outline : nullptr);
		c.FillRect(r, Rgba(10, 12, 16));
		s.viewport.Draw(u, r);
		{ // superpositions de débogage projetées sur l'image
			std::vector<DebugLine> lines;
			CollectDebugLines(*g, s.debugView, s.viewport.Camera().position, s.selectedEntity, lines);
			DrawDebugLines(c, Projector::From(s.viewport.Camera(), r), lines);
		}
		// Picking + application du gizmo à l'entité sélectionnée.
		if (s.viewport.PickRequested()) {
			const math::FVector3 o = s.viewport.RayOrigin();
			const math::FVector3 d = s.viewport.RayDirection(u.MouseX(), u.MouseY(), r);
			const ecs::Entity hit = PickEntity(*g, o, d);
			s.selectedEntity = hit;
			if (!hit)
				s.viewport.ClearSelection();
			s.viewport.ClearPickRequest();
		}
		if (s.viewport.Dragging() && s.selectedEntity && g->Ecs().IsAlive(s.selectedEntity))
			if (auto *t = g->Ecs().Get<voxel::Transform>(s.selectedEntity)) {
				t->position = s.viewport.Selection();
				t->yaw = s.viewport.SelectionYaw();
			}
		const std::string hint = !s.viewport.HasSelection() ? "clic : sélectionner une entité · glisser : orbiter · molette : zoom · milieu / Maj : déplacer"
								 : translate				 ? "glisser un axe : déplacer · G : rotation · F : cadrer"
															 : "glisser : pivoter autour de Y · G : translation · F : cadrer";
		c.FillRect({r.x, r.Bottom() - 24, r.w, 24}, Rgba(10, 12, 16, 170));
		c.Text(r.x + 8, r.Bottom() - 19, c.Ellipsize(hint, r.w - 16, 12), th.textDim, 12);
	};

	// ── Peinture : texture sélectionnée au pixel (zoom, déplacement, pipette) ──
	auto drawPaint = [&](const Rect &r) {
		c.FillRect(r, Rgba(14, 16, 21));
		const uint16_t *layerPtr = s.selectedTexture.empty() ? nullptr : app.content.layerOf.Find(s.selectedTexture);
		if (!layerPtr) {
			u.BeginColumn(r.Inset(20, 24));
			u.Wrapped("Choisissez une texture dans l'onglet « Textures » : elle s'affiche ici au pixel près (molette : zoom, clic droit ou milieu "
					  ": déplacer).",
					  th.textDim, 14);
			u.EndColumn();
			return;
		}
		const uint8_t *px = app.content.textureLayers.data() + size_t(*layerPtr) * voxel::kTextureSize * voxel::kTextureSize * 4;
		const Rect view{r.x, r.y, r.w, r.h - 26};
		const float fit = std::max(1.f, std::floor(std::min(view.w, view.h) * 0.85f / 16.f));
		if (s.paintZoom <= 0)
			s.paintZoom = fit;
		const bool hover = u.MouseIn(view);
		if (hover && u.WheelY() != 0) { // zoom vers le curseur
			const float old = s.paintZoom;
			s.paintZoom = std::clamp(std::round(s.paintZoom * (u.WheelY() > 0 ? 1.25f : 0.8f)), 2.f, 96.f);
			const float mx = u.MouseX() - (view.x + view.w * 0.5f), my = u.MouseY() - (view.y + view.h * 0.5f);
			s.paintPanX = mx - (mx - s.paintPanX) * s.paintZoom / old;
			s.paintPanY = my - (my - s.paintPanY) * s.paintZoom / old;
		}
		static float lastX = 0, lastY = 0;
		if (hover && (u.MouseDown(sdl3::MouseButton::Right) || u.MouseDown(sdl3::MouseButton::Middle)) && !u.MousePressed(sdl3::MouseButton::Right) && !u.MousePressed(sdl3::MouseButton::Middle)) {
			s.paintPanX += u.MouseX() - lastX;
			s.paintPanY += u.MouseY() - lastY;
		}
		lastX = u.MouseX();
		lastY = u.MouseY();
		const float size = 16 * s.paintZoom;
		const Rect img{view.x + (view.w - size) * 0.5f + s.paintPanX, view.y + (view.h - size) * 0.5f + s.paintPanY, size, size};
		c.PushClip(view);
		for (int cy = 0; cy < 8; ++cy) // damier (transparence)
			for (int cx = 0; cx < 8; ++cx)
				c.FillRect({img.x + float(cx) * size / 8, img.y + float(cy) * size / 8, size / 8, size / 8},
						   (cx + cy) % 2 ? Rgba(60, 64, 72) : Rgba(44, 48, 56));
		if (sdl3::Texture *t = TexturePreview(s.renderer, s.texturePreviews, s.selectedTexture, px))
			c.Image(*t, img);
		if (s.paintZoom >= 8)
			for (int k = 0; k <= 16; ++k) {
				const float f = float(k) * s.paintZoom;
				const sdl3::Color gc = k % 8 == 0 ? Rgba(255, 255, 255, 70) : Rgba(255, 255, 255, 26);
				c.Line(img.x + f, img.y, img.x + f, img.Bottom(), gc);
				c.Line(img.x, img.y + f, img.Right(), img.y + f, gc);
			}
		std::string info = std::format("{} · 16×16 · zoom ×{:.0f}", s.selectedTexture, s.paintZoom);
		if (img.Contains(u.MouseX(), u.MouseY()) && hover) {
			const int ix = std::clamp(int((u.MouseX() - img.x) / s.paintZoom), 0, 15), iy = std::clamp(int((u.MouseY() - img.y) / s.paintZoom), 0, 15);
			const uint8_t *p = px + size_t(iy * 16 + ix) * 4;
			c.StrokeRect({img.x + float(ix) * s.paintZoom, img.y + float(iy) * s.paintZoom, s.paintZoom, s.paintZoom}, th.accent, 2);
			info += std::format(" · pixel ({}, {}) · #{:02X}{:02X}{:02X} · opacité {}", ix, iy, p[0], p[1], p[2], p[3]);
		}
		c.PopClip();
		c.FillRect({r.x, r.Bottom() - 26, r.w, 26}, Rgba(18, 21, 27));
		c.Text(r.x + 10, r.Bottom() - 20, c.Ellipsize(info, r.w - 250, 12), th.textDim, 12);
		// Peindre la source : image PNG du mod (pas les compositions « a^b »).
		const std::string texName = s.selectedTexture.substr(0, s.selectedTexture.find('|'));
		const auto src = app.content.mods.ResolveTexture(texName);
		const bool editable = src && src->compose.empty() && !src->path.empty() && fs::exists(src->path);
		if (u.ButtonAt({r.Right() - 228, r.Bottom() - 24, 126, 22}, "Peindre la source##paintsrc", editable) && editable)
			s.models->OpenImage(src->path, texName);
		if (u.ButtonAt({r.Right() - 96, r.Bottom() - 24, 88, 22}, "Ajuster##paintfit")) {
			s.paintZoom = fit;
			s.paintPanX = s.paintPanY = 0;
		}
	};

	// ── Disposition ancrable : chaque onglet s'affiche dans n'importe quelle région ──
	auto openFromPanel = [&](const fs::path &p, const std::string &mod) {
		openFile(p, mod);
		doc = s.active >= 0 && s.active < int(s.docs.size()) ? &s.docs[size_t(s.active)] : nullptr;
	};
	auto noWorld = [&](const Rect &r) {
		u.BeginColumn(r.Inset(16, 20));
		u.Wrapped("Aucun monde chargé : lancez une partie pour inspecter ses entités.", th.textDim, 13);
		u.EndColumn();
	};
	auto drawPanel = [&](const Rect &r, std::string_view tab) {
		Game *g = game ? game : app.background.get();
		if (tab == "explorer")
			drawExplorer(r);
		else if (tab == "viewport")
			drawViewport(r);
		else if (tab == "script")
			drawEditor(r);
		else if (tab == "world")
			drawRight(r);
		else if (tab == "texture")
			drawTextures(r);
		else if (tab == "uv") {
			if (!s.models->DrawUv(u, r, s.renderer))
				drawUv(r);
		} else if (tab == "paint") {
			if (!s.models->DrawPaint(u, r, s.renderer))
				drawPaint(r);
		} else if (tab == "models")
			s.models->DrawCatalog(u, r);
		else if (tab == "model")
			s.models->DrawView(u, r, s.renderer);
		else if (tab == "parts")
			s.models->DrawParts(u, r);
		else if (tab == "element")
			s.models->DrawElement(u, r, openFromPanel);
		else if (tab == "animation")
			s.models->DrawAnimation(u, r);
		else if (tab == "console")
			drawConsole(r);
		else if (tab == "nodal") {
			if (!g)
				return noWorld(r);
			if (auto ev = studio::DrawMechanisms(u, r, s.mechanisms, *g, s.selectedEntity)) {
				s.selectedEntity = ev->select;
				if (ev->focus)
					if (const auto *t = g->Ecs().Get<voxel::Transform>(ev->select)) {
						s.viewport.SetTarget(t->position);
						s.follow = ev->select;
					}
			}
		}
		else if (tab == "worldgen")
			s.worldgen->DrawSettings(u, r);
		else if (tab == "worldmap")
			s.worldgen->DrawMap(u, r, s.renderer, *s.panels.worker);
		else if (tab == "sandbox")
			s.worldgen->DrawSandbox(u, r);
		else if (tab == "perf")
			studio::DrawPerformance(u, r, s.perf, app, game ? game : app.background.get(), dt);
		else if (tab == "mods")
			studio::DrawMods(u, r, app, game, s.status, openFromPanel);
		else if (tab == "outliner") {
			if (!g)
				return noWorld(r);
			// Distances : depuis la caméra de la vue 3D, sinon depuis le joueur.
			math::FVector3 from{};
			if (s.viewport.Ready())
				from = s.viewport.Camera().position;
			else if (const auto *pt = g->Ecs().Get<voxel::Transform>(g->Player()))
				from = pt->position;
			if (auto ev = studio::DrawOutliner(u, r, s.outliner, *g, s.selectedEntity, from, sdl3::GetTicksMS())) {
				s.selectedEntity = ev->entity;
				if (ev->focus)
					if (const auto *t = g->Ecs().Get<voxel::Transform>(ev->entity)) {
						s.viewport.SetTarget(t->position);
						s.follow = {};
						if (const auto side = s.dock.Find("viewport"); side != ui::DockLayout::Side::Count)
							s.dock.Activate(side, "viewport");
					}
			}
		} else if (tab == "properties") {
			if (!g)
				return noWorld(r);
			const bool following = s.follow && s.follow == s.selectedEntity;
			const studio::PropertiesActions act = studio::DrawProperties(u, r, *g, s.selectedEntity, following, app.audio.SoundIds());
			if (act.despawned) {
				s.selectedEntity = {};
				s.status = "entité supprimée du monde";
			}
			if (act.toggleFollow)
				s.follow = following ? ecs::Entity{} : s.selectedEntity;
			if (act.focus)
				if (const auto *t = g->Ecs().Get<voxel::Transform>(s.selectedEntity)) {
					s.viewport.SetTarget(t->position);
					s.follow = {};
				}
		}
	};
	s.models->Update(*s.panels.worker, u, u.WantsKeyboard(), dt);
	// Banc d'essai : action une fois le monde chargé, puis vue « Monde en jeu » sur le résultat.
	s.worldgen->Update(app.game.get());
	if (const auto focusAt = s.worldgen->TakeFocus()) {
		for (size_t i = 0; i < s.workspaces.size(); ++i)
			if (s.workspaces[i].id == "world" && i != s.workspace)
				s.SwitchWorkspace(i);
		s.followGame = app.game.get(); // nouveau monde déjà pris en compte : pas de retour au joueur
		s.selectedEntity = {};
		s.follow = {};
		s.viewport.SetTarget(*focusAt);
		if (const auto side = s.dock.Find("viewport"); side != ui::DockLayout::Side::Count)
			s.dock.Activate(side, "viewport");
	}
	if (std::string st = s.worldgen->TakeStatus(); !st.empty())
		s.status = std::move(st);
	if (!s.pendingModel.empty() && s.models->Open(s.pendingModel))
		s.pendingModel.clear();
	if (u.MousePressed(sdl3::MouseButton::Left))
		s.models->SetFocused(false); // repris par un panneau de l'atelier s'il reçoit le clic
	s.dock.Draw(u, dockArea, [&](ui::DockLayout::Side, const Rect &r, std::string_view tab) { drawPanel(r, tab); });
	if (std::string st = s.models->TakeStatus(); !st.empty())
		s.status = std::move(st);
	if (s.models->TakeTexturesChanged())
		s.texturePreviews.clear(); // aperçus 16×16 à refaire (texture repeinte, rechargée à chaud)

	// ── Barre d'état ─────────────────────────────────────────────────────
	c.FillRect({0, H - statusH, W, statusH}, Rgba(8, 9, 12));
	std::string where = doc ? std::format("{} · ligne {}, colonne {}", doc->path.string(), doc->cy + 1, ByteToColumn(doc->lines[doc->cy], doc->cx) + 1) : "";
	const float whereW = std::min(c.MeasureText(where, 12), W * 0.4f);
	c.Text(W - whereW - 10, H - statusH + (statusH - c.LineHeight(12)) * 0.5f, c.Ellipsize(where, whereW + 1, 12), th.textDim, 12);
	// Bouton de configuration de la tabulation (à gauche de la position du curseur).
	const std::string tabLabel = s.tabMode ? std::format("Tab · {}", s.tabSize) : std::format("Espaces · {}", s.tabSize);
	const float tabW = c.MeasureText(tabLabel, 12) + 20;
	const Rect tabBtn{W - whereW - 10 - tabW - 8, H - statusH + 2, tabW, statusH - 4};
	// Message d'état : jusqu'au bouton de tabulation (jamais par-dessus).
	c.Text(10, H - statusH + (statusH - c.LineHeight(12)) * 0.5f, c.Ellipsize(s.status, std::max(60.f, tabBtn.x - 24), 12), th.textDim, 12);
	if (u.ButtonAt(tabBtn, std::format("{}##tabcfg", tabLabel), true, s.tabConfigOpen))
		s.tabConfigOpen = !s.tabConfigOpen;

	// ── Panneau de configuration de la tabulation ────────────────────────
	if (s.tabConfigOpen) {
		u.PushId("tabcfg");
		const Rect pop{W - 224, H - statusH - 104, 212, 96};
		c.FillRoundRect(pop, 8, Rgba(20, 23, 30, 248));
		c.StrokeRoundRect(pop, 8, th.panelBorder);
		c.Text(pop.x + 10, pop.y + 7, "Tabulation", th.text, 13, sdl3::CanvasFont::Bold);
		c.Text(pop.x + 10, pop.y + 32, "Type", th.textDim, 12);
		if (u.ButtonAt({pop.x + 48, pop.y + 28, 76, 22}, "Espaces", true, !s.tabMode))
			s.tabMode = false;
		if (u.ButtonAt({pop.x + 128, pop.y + 28, 52, 22}, "Tab", true, s.tabMode))
			s.tabMode = true;
		c.Text(pop.x + 10, pop.y + 60, "Taille", th.textDim, 12);
		static constexpr int kSizes[] = {2, 4, 8};
		float sx = pop.x + 48;
		for (int sz : kSizes) {
			if (u.ButtonAt({sx, pop.y + 56, 40, 22}, std::to_string(sz), true, s.tabSize == sz))
				s.tabSize = sz;
			sx += 46;
		}
		u.PopId();
	}

	u.DrawNotifications(dt, 54);
	u.End();
	c.End();
	if (!s.screenshot.empty()) {
		if (auto pixels = s.renderer.ReadPixels())
			(void)sdl3::ImgSavePng(pixels.Value(), String(s.screenshot));
		s.screenshot.clear();
	}
	s.renderer.Present();
	
}

} // namespace voxelia
