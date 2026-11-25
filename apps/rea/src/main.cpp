// rea — application de rétro-ingénierie de binaires.
//
// Charge un exécutable ELF64 x86-64 (sections, symboles, chaînes, désassemblage,
// lifting C, graphe d'appels) ou une ROM brute (ARM/Thumb/MIPS32), selon
// l'architecture choisie dans le menu. Le travail (architecture, annotations)
// se sauvegarde dans un projet JSON, réouvert plus tard — inspiré de Ghidra.
//
//   rea <binaire|rom>          ouvre directement un fichier
//
//   cpp-maker run rea -- build/host/debug/bin/some-elf
#include <app/app.hpp>
#include <disasm/disasm.hpp>
#include <rea/rea.hpp>
#include <ui/dock.hpp>
#include <ui/ui.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

class ReaApp final : public app::Application {
public:
	explicit ReaApp(std::string path) : m_path(std::move(path)) {}

protected:
	bool OnInit() override {
		// Disposition ancrable (dock) : régions gauche / centre / droite à onglets.
		m_dock.SetTabs(ui::DockLayout::Side::Left, {{"symbols", "Symboles", ui::icons::Codepoint(ui::icons::MaterialIcons::LIST)}});
		m_dock.SetTabs(ui::DockLayout::Side::Center, {{"code", "Code", ui::icons::Codepoint(ui::icons::MaterialIcons::CODE)}});
		m_dock.SetTabs(ui::DockLayout::Side::Right, {
			{"graphe", "Graphe", ui::icons::Codepoint(ui::icons::MaterialIcons::TIMELINE)},
			{"appels", "Appels"},
			{"references", "Références"},
			{"chaines", "Chaînes"},
			{"octets", "Octets"},
			{"securite", "Sécurité"},
			{"signatures", "Signatures"},
			{"entete", "En-tête"},
		});
		m_dock.Show(ui::DockLayout::Side::Bottom, false);
		m_dock.SetSize(ui::DockLayout::Side::Left, 260.f);
		m_dock.SetSize(ui::DockLayout::Side::Right, 320.f);
		if (!m_path.empty())
			Load(m_path);
		return true;
	}

	void OnUpdate(double) override {
		if (Events().KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
			RequestQuit();
		// Navigation au clavier : Alt+← / Alt+→.
		if (Events().Alt()) {
			if (Events().KeyScan(SDL_SCANCODE_LEFT).IsJustPressed())
				GoBack();
			if (Events().KeyScan(SDL_SCANCODE_RIGHT).IsJustPressed())
				GoForward();
		}
		// Raccourcis : Ctrl+G (aller à), Ctrl+S (enregistrer), Ctrl+O (ouvrir).
		if (Events().Ctrl()) {
			if (Events().KeyScan(SDL_SCANCODE_G).IsJustPressed() && m_loaded)
				m_prompt = "goto";
			if (Events().KeyScan(SDL_SCANCODE_S).IsJustPressed() && m_loaded) {
				if (m_projectPath.empty()) m_prompt = "saveas"; else SaveProjectTo(m_projectPath);
			}
			if (Events().KeyScan(SDL_SCANCODE_O).IsJustPressed())
				m_prompt = "open";
		}
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "back", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::ARROW_BACK),
						.tooltip = "Précédent (Alt+←)", .enabled = !m_back.empty()});
		left.push_back({.id = "fwd", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::ARROW_FORWARD),
						.tooltip = "Suivant (Alt+→)", .enabled = !m_forward.empty()});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "back")
			GoBack();
		else if (id == "fwd")
			GoForward();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();

		u.BeginColumn(area);
		DrawMenu(u);

		if (!m_loaded) {
			DrawLoadPrompt(u);
			u.EndColumn();
			return;
		}

		// Invite de chemin (ouvrir / enregistrer sous).
		if (!m_prompt.empty()) {
			DrawPathPrompt(u);
			u.Space(4);
		}

		if (m_isRom) {
			DrawRom(u);
			u.EndColumn();
			return;
		}

		// Bandeau d'information.
		u.Label(std::format("{} — entrée 0x{:x} · {} fonction(s) · {} chaîne(s)", m_path, m_elf->EntryPoint(),
							m_funcs.size(), m_strings.size()),
				th.textDim, 12);
		if (!m_error.empty())
			u.Label(m_error, th.danger, 12);
		u.Space(4);

		// Disposition ancrable (dock) : onglets multi-régions, comme sandbox.
		const sdl3::FRect content = u.Remaining();
		m_dock.Draw(u, content, [this](ui::DockLayout::Side, const sdl3::FRect &r, std::string_view tab) { DrawDockTab(r, tab); });
		u.EndColumn();
	}

private:
	/// Charge un fichier : ELF (analyse complète) ou ROM brute (désassemblage
	/// linéaire avec l'architecture courante).
	void Load(const std::string &path) {
		m_error.clear();

		// Détection ELF par le magic.
		std::ifstream in(path, std::ios::binary);
		char magic[4] = {};
		if (in) {
			in.read(magic, 4);
			in.close();
		}
		if (magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
			LoadElf(path);
			return;
		}
		LoadRom(path);
	}

	void LoadElf(const std::string &path) {
		auto elf = rea::Elf::Load(path);
		if (elf.IsError()) {
			m_error = std::string(elf.Error().View());
			m_loaded = false;
			return;
		}
		m_elf = std::move(elf.Value());
		m_funcs = rea::AnalyzeFunctions(*m_elf);
		m_strings = rea::ExtractStrings(*m_elf, 4);
		m_signatures = rea::SearchSignatures(*m_elf, rea::BuiltinSignatures());
		m_funcIndex.clear();
		for (size_t i = 0; i < m_funcs.size(); ++i)
			m_funcIndex[m_funcs[i].symbol->value] = int(i);
		m_selected = 0;
		m_focusAddr = 0;
		m_back.clear();
		m_forward.clear();
		m_isRom = false;
		m_path = path;
		m_project.binaryPath = path;
		m_project.arch = rea::Arch::X86_64;
		m_loaded = true;
	}

	void LoadRom(const std::string &path) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			m_error = std::format("impossible d'ouvrir « {} »", path);
			m_loaded = false;
			return;
		}
		m_rom.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		m_isRom = true;
		m_path = path;
		m_project.binaryPath = path;
		DisassembleRom();
		m_loaded = true;
	}

	/// (Re)désassemble la ROM brute avec l'architecture courante.
	void DisassembleRom() {
		m_romLines = rea::Disassemble(m_rom.data(), m_rom.size(), 0, m_project.arch);
	}

	// ── Projet (création / ouverture / enregistrement) ───────────────────────
	void NewProject() {
		m_loaded = false;
		m_elf.reset();
		m_funcs.clear();
		m_strings.clear();
		m_funcIndex.clear();
		m_rom.clear();
		m_romLines.clear();
		m_isRom = false;
		m_path.clear();
		m_project = rea::Project{};
		m_projectPath.clear();
		m_error.clear();
	}

	void SaveProjectTo(const std::string &path) {
		m_project.binaryPath = m_path;
		if (m_project.Save(path)) {
			m_projectPath = path;
			m_error.clear();
		} else {
			m_error = std::format("enregistrement de « {} » impossible", path);
		}
	}

	void OpenProject(const std::string &path) {
		auto project = rea::Project::Load(path);
		if (project.IsError()) {
			m_error = std::string(project.Error().View());
			return;
		}
		m_project = std::move(project.Value());
		m_projectPath = path;
		if (!m_project.binaryPath.empty())
			Load(m_project.binaryPath);
		else
			NewProject();
	}

	/// Sélectionne la fonction à l'adresse `addr` (navigation xrefs).
	void SelectByAddress(uint64_t addr) {
		if (const auto it = m_funcIndex.find(addr); it != m_funcIndex.end())
			NavigateTo(it->second, 0);
	}

	/// Nom d'une fonction à une adresse (ou « sub_xxxx »).
	std::string NameOf(uint64_t addr) const {
		if (const auto it = m_funcIndex.find(addr); it != m_funcIndex.end())
			return m_funcs[size_t(it->second)].symbol->name;
		return std::format("sub_{:x}", addr);
	}

	// ── Pile de navigation (précédent / suivant) ─────────────────────────────
	struct NavLocation {
		int func = 0;
		uint64_t focus = 0;
	};

	/// Navigue vers une fonction (et éventuellement un bloc), en empilant la
	/// position courante et en vidant l'historique « avant ».
	void NavigateTo(int func, uint64_t focus = 0) {
		if (func < 0 || size_t(func) >= m_funcs.size())
			return;
		m_back.push_back({m_selected, m_focusAddr});
		if (m_back.size() > 64)
			m_back.erase(m_back.begin());
		m_forward.clear();
		m_selected = func;
		m_focusAddr = focus;
	}

	void GoBack() {
		if (m_back.empty())
			return;
		m_forward.push_back({m_selected, m_focusAddr});
		const NavLocation loc = m_back.back();
		m_back.pop_back();
		m_selected = loc.func;
		m_focusAddr = loc.focus;
	}

	void GoForward() {
		if (m_forward.empty())
			return;
		m_back.push_back({m_selected, m_focusAddr});
		const NavLocation loc = m_forward.back();
		m_forward.pop_back();
		m_selected = loc.func;
		m_focusAddr = loc.focus;
	}

	// ── Écran d'accueil (aucun binaire chargé) ──────────────────────────────
	void DrawLoadPrompt(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Heading("REA — rétro-ingénierie");
		u.Label("Chargez un exécutable ELF64 x86-64, ou une ROM brute (ARM / Thumb / MIPS32).", th.textDim);
		u.Space(8);
		u.Label("Chemin du fichier :");
		bool submitted = false;
		if (u.TextInput("path", m_loadPath, &submitted, "/chemin/vers/binaire"))
			(void)0;
		if (submitted && !m_loadPath.empty())
			Load(m_loadPath);
		if (!m_error.empty())
			u.Label(m_error, th.danger);
		u.Space(12);
		u.Label("Astuce : menu « Fichier → Ouvrir… », ou lancez « rea <fichier> ».", th.textDim, 12);
	}

	// ── Barre de menus ───────────────────────────────────────────────────────
	void DrawMenu(ui::Context &u) {
		std::vector<ui::MenuEntry> file = {
			{"Nouveau projet", "", true, false},
			{"Ouvrir…", "Ctrl+O", true, false},
			{"Ouvrir un projet…", "", true, false},
			{"", "", true, false},
			{"Enregistrer", "Ctrl+S", m_loaded, false},
			{"Enregistrer sous…", "", m_loaded, false},
			{"", "", true, false},
			{"Quitter", "Ctrl+Q", true, false},
		};
		std::vector<ui::MenuEntry> arch = {
			{"x86-64", "", true, m_project.arch == rea::Arch::X86_64},
			{"ARM", "", true, m_project.arch == rea::Arch::Arm},
			{"Thumb", "", true, m_project.arch == rea::Arch::Thumb},
			{"MIPS32", "", true, m_project.arch == rea::Arch::Mips32},
		};
		std::vector<ui::MenuEntry> exportMenu = {
			{"Exporter le C (par fonction)…", "", m_loaded && !m_isRom, false},
		};
		std::vector<ui::MenuEntry> nav = {
			{"Aller à l'adresse…", "Ctrl+G", m_loaded, false},
		};
		const ui::BarMenu menus[] = {
			{"Fichier", file},
			{"Navigation", nav},
			{"Architecture", arch},
			{"Export", exportMenu},
		};

		const sdl3::FRect bar = u.Next(24);
		const ui::MenuChoice choice = u.MenuBar(bar, menus);
		if (choice.menu == 0) { // Fichier
			switch (choice.item) {
			case 0: NewProject(); break;
			case 1: m_prompt = "open"; break;
			case 2: m_prompt = "openproject"; break;
			case 4: if (m_projectPath.empty()) m_prompt = "saveas"; else SaveProjectTo(m_projectPath); break;
			case 5: m_prompt = "saveas"; break;
			case 7: RequestQuit(); break;
			default: break;
			}
		} else if (choice.menu == 1) { // Navigation
			if (choice.item == 0)
				m_prompt = "goto";
		} else if (choice.menu == 2) { // Architecture
			const rea::Arch archs[] = {rea::Arch::X86_64, rea::Arch::Arm, rea::Arch::Thumb, rea::Arch::Mips32};
			if (choice.item >= 0 && choice.item < 4 && m_project.arch != archs[choice.item]) {
				m_project.arch = archs[choice.item];
				if (m_isRom)
					DisassembleRom();
			}
		} else if (choice.menu == 3 && choice.item == 0) { // Export
			m_prompt = "export";
		}
	}

	// ── Invite de chemin / adresse ────────────────────────────────────────────
	void DrawPathPrompt(ui::Context &u) {
		const ui::Theme &th = u.Style();
		const char *label = "Chemin :";
		if (m_prompt == "saveas") label = "Enregistrer le projet sous :";
		else if (m_prompt == "openproject") label = "Ouvrir un projet :";
		else if (m_prompt == "export") label = "Exporter le C dans le dossier :";
		else if (m_prompt == "goto") label = "Adresse (hexadécimal 0x… ou décimal) :";
		u.Label(label, th.info);
		bool submitted = false;
		if (u.TextInput("prompt", m_promptPath, &submitted, m_prompt == "goto" ? "0x…" : "/chemin"))
			(void)0;
		u.Row({1, 1});
		if (u.Button("Valider")) submitted = true;
		if (u.Button("Annuler")) { m_prompt.clear(); m_promptPath.clear(); }
		if (submitted && !m_promptPath.empty()) {
			const std::string target = m_promptPath;
			const std::string kind = m_prompt;
			m_prompt.clear();
			m_promptPath.clear();
			if (kind == "open") Load(target);
			else if (kind == "openproject") OpenProject(target);
			else if (kind == "saveas") SaveProjectTo(target);
			else if (kind == "export") ExportCTo(target);
			else if (kind == "goto") GoToAddress(target);
		}
	}

	/// Interprète une adresse (hex ou décimale) et y saute dans le listing.
	void GoToAddress(const std::string &text) {
		if (m_isRom)
			return;
		// Analyse sans exception (projet compilé avec -fno-exceptions).
		const char *p = text.c_str();
		int base = 10;
		if (text.starts_with("0x") || text.starts_with("0X")) {
			base = 16;
			p += 2;
		}
		char *end = nullptr;
		const uint64_t address = std::strtoull(p, &end, base);
		if (end == p) {
			m_error = std::format("adresse invalide : « {} »", text);
			return;
		}
		// Fonction dont l'intervalle [value, value+size) contient l'adresse.
		int best = -1;
		for (size_t i = 0; i < m_funcs.size(); ++i)
			if (address >= m_funcs[i].symbol->value && address < m_funcs[i].symbol->value + m_funcs[i].symbol->size) {
				best = int(i);
				break;
			}
		if (best < 0) {
			m_error = std::format("aucune fonction ne contient l'adresse 0x{:x}", address);
			return;
		}
		NavigateTo(best, address);
		m_dock.Activate(ui::DockLayout::Side::Center, "code"); // onglet « Code » (listing)
	}

	void ExportCTo(const std::string &dir) {
		if (m_isRom) return;
		const auto result = rea::ExportC(m_funcs, dir);
		if (result.IsOk())
			m_error.clear();
		else
			m_error = std::string(result.Error().View());
	}

	// ── Vue ROM (désassemblage linéaire) ────────────────────────────────────
	void DrawRom(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Label(std::format("{} — ROM {} ({} octet(s)) · architecture {}", m_path, rea::ArchName(m_project.arch),
							m_rom.size(), rea::ArchName(m_project.arch)),
				th.textDim, 12);
		u.Space(4);
		const sdl3::FRect content = u.Remaining();
		u.BeginPanel(content, "Désassemblage");
		const sdl3::FRect body = u.Remaining();
		u.BeginScroll("rom", body);
		for (const rea::Line &line : m_romLines)
			u.Label(std::format("{:08x}  {:<9} {}", line.address, line.mnemonic, line.operands), th.text, 13);
		u.EndScroll();
		u.EndPanel();
	}

	// ── Panneau gauche : arborescence des symboles ───────────────────────────
	void DrawSymbols(ui::Context &u, const sdl3::FRect &r) {
		u.BeginPanel(r, "Symboles");
		const sdl3::FRect body = u.Remaining();
		if (m_funcs.empty()) {
			u.Label("aucun symbole de fonction (binaire stripé ?)", u.Style().textDim);
			u.EndPanel();
			return;
		}

		// Fonctions groupées par section (Program tree de Ghidra).
		std::map<std::string, std::vector<size_t>> bySection;
		for (size_t i = 0; i < m_funcs.size(); ++i) {
			std::string section = "?";
			for (const rea::ElfSection &s : m_elf->Sections())
				if (m_funcs[i].symbol->value >= s.address && m_funcs[i].symbol->value < s.address + s.size) {
					section = s.name;
					break;
				}
			bySection[section].push_back(i);
		}

		u.BeginScroll("symbols", body);
		for (const auto &[section, indices] : bySection) {
			const bool collapsed = m_collapsedSections.contains(section);
			const std::string header = std::format("{} {} ({})", collapsed ? "▸" : "▾", section, indices.size());
			if (u.Selectable(header, false, u.Style().textDim)) {
				if (collapsed)
					m_collapsedSections.erase(section);
				else
					m_collapsedSections.insert(section);
			}
			if (collapsed)
				continue;
			for (size_t idx : indices) {
				const rea::FunctionAnalysis &f = m_funcs[idx];
				const std::string label = std::format("  {:<26} 0x{:x}", DisplayName(f), f.symbol->value);
				const bool sel = int(idx) == m_selected;
				if (u.Selectable(label, sel, f.xrefs > 0 ? u.Style().info : sdl3::Color{}))
					NavigateTo(int(idx), 0);
			}
		}
		u.EndScroll();
		u.EndPanel();
	}

	/// Nom affiché d'une fonction (renommage d'annotation sinon symbole).
	std::string DisplayName(const rea::FunctionAnalysis &f) const {
		if (const rea::Annotation *a = m_project.Find(f.symbol->value); a && !a->name.empty())
			return a->name;
		return f.symbol->name;
	}

	/// Commentaire d'annotation à une adresse (vide si aucun).
	std::string_view CommentFor(uint64_t address) const {
		if (const rea::Annotation *a = m_project.Find(address))
			return a->comment;
		return {};
	}

	// ── Onglet d'une région du dock (structure CodeBrowser) ──────────────────
	void DrawDockTab(const sdl3::FRect &r, std::string_view tab) {
		ui::Context &u = Ui();
		if (tab == "symbols") {
			DrawSymbols(u, r);
			return;
		}
		if (tab == "code") {
			DrawCodeView(u, r);
			return;
		}
		if (tab == "graphe") {
			DrawCfg(u, r);
			return;
		}
		// Vues auxiliaires : panneau + défilement.
		u.BeginPanel(r);
		const sdl3::FRect body = u.Remaining();
		u.BeginScroll(std::format("tab-{}", tab), body);
		if (tab == "appels") DrawXrefs(u);
		else if (tab == "references") DrawReferences(u);
		else if (tab == "chaines") DrawStrings(u);
		else if (tab == "octets") DrawBytes(u);
		else if (tab == "securite") DrawSecurity(u);
		else if (tab == "signatures") DrawSignatures(u);
		else DrawHeader(u);
		u.EndScroll();
		u.EndPanel();
	}

	/// Vue « Code » : listing (désassemblage) à gauche, décompilé (C) à droite,
	/// annotations (commentaire / renommage) en bas — la structure CodeBrowser.
	void DrawCodeView(ui::Context &u, const sdl3::FRect &body) {
		if (m_selected < 0 || size_t(m_selected) >= m_funcs.size())
			return;
		const float split = body.w * 0.55f;
		const float bottomH = 64.f;

		// Listing (désassemblage).
		u.BeginPanel({body.x, body.y, split, body.h - bottomH}, "Listing");
		const sdl3::FRect listBody = u.Remaining();
		u.BeginScroll("listing", listBody);
		DrawListing(u);
		u.EndScroll();
		u.EndPanel();

		// Décompilé (C).
		u.BeginPanel({body.x + split, body.y, body.w - split, body.h - bottomH}, "Décompilé");
		const sdl3::FRect decompBody = u.Remaining();
		u.BeginScroll("decompiled", decompBody);
		DrawDecompiled(u);
		u.EndScroll();
		u.EndPanel();

		// Annotations.
		DrawAnnotations(u, {body.x, body.y + body.h - bottomH, body.w, bottomH});
	}

	void DrawListing(ui::Context &u) {
		const ui::Theme &th = u.Style();
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		u.Label(std::format("{} @ 0x{:x} — {} octets, {} appelé(e)s, {} bloc(s)", DisplayName(f), f.symbol->value,
							f.symbol->size, f.xrefs, f.blocks.size()),
				th.textDim, 12);
		u.Separator();

		// Références de données par instruction (chaînes / globales).
		std::unordered_map<uint64_t, std::string> refsAt;
		for (const rea::DataRef &d : f.dataRefs)
			refsAt[d.from] += (refsAt[d.from].empty() ? "" : ", ") + d.what;

		// Point de départ : adresse ciblée (clic dans le graphe) avec un peu de contexte.
		size_t start = 0;
		if (m_focusAddr != 0)
			for (size_t i = 0; i < f.instructions.size(); ++i)
				if (f.instructions[i].address == m_focusAddr) {
					start = i >= 3 ? i - 3 : 0;
					break;
				}
		if (start > 0) {
			u.Label(std::format("▲ {} instruction(s) masquée(s) avant la cible", start), th.textDim, 11);
			u.Separator();
		}

		for (size_t i = start; i < f.instructions.size(); ++i) {
			const rea::Instruction &insn = f.instructions[i];
			const bool focused = insn.address == m_focusAddr;
			const sdl3::Color col = focused ? th.warning : insn.isBranch ? th.accent : insn.isCall ? th.info : th.text;
			std::string line = std::format("{:08x}  {:<9} {}", insn.address, insn.mnemonic, insn.operands);
			if (insn.isCall && insn.hasTarget)
				line += "  ; -> " + NameOf(insn.target);
			if (const auto it = refsAt.find(insn.address); it != refsAt.end())
				line += "  ; " + it->second;
			if (focused)
				line = "▶ " + line;
			u.Label(line, col, 13);
			// Commentaire d'annotation sur cette adresse.
			if (const std::string_view c = CommentFor(insn.address); !c.empty())
				u.Label(std::format("    ; {}", c), th.textDim, 12);
		}
	}

	void DrawDecompiled(ui::Context &u) {
		const ui::Theme &th = u.Style();
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		rea::Function fn;
		fn.name = DisplayName(f);
		fn.address = f.symbol->value;
		fn.instructions = f.instructions;
		const String code = rea::LiftFunction(fn);
		size_t pos = 0;
		while (pos <= code.size()) {
			const size_t nl = code.find('\n', pos);
			const size_t end = nl == String::npos ? code.size() : nl;
			const std::string_view line(code.data() + pos, end - pos);
			const bool isComment = line.find("//") != std::string_view::npos || line.starts_with("/*");
			u.Label(line, isComment ? th.textDim : th.text, 13);
			if (nl == String::npos)
				break;
			pos = nl + 1;
		}
	}

	void DrawAnnotations(ui::Context &u, const sdl3::FRect &r) {
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		u.BeginPanel(r, "Annotations (sauvegardées dans le projet)");
		const sdl3::FRect body = u.Remaining();
		u.BeginColumn(body);
		u.Row({1, 1.8f});
		bool rename = false;
		if (u.TextInput("rename", m_renameBuf, &rename, "renommer la fonction…"))
			(void)0;
		bool comment = false;
		if (u.TextInput("comment", m_commentBuf, &comment, "commentaire…"))
			(void)0;
		if ((rename || comment) && (!m_renameBuf.empty() || !m_commentBuf.empty())) {
			if (!m_renameBuf.empty())
				m_project.Rename(f.symbol->value, m_renameBuf);
			if (!m_commentBuf.empty())
				m_project.Comment(f.symbol->value, m_commentBuf);
			m_renameBuf.clear();
			m_commentBuf.clear();
		}
		u.EndColumn();
		u.EndPanel();
	}

	// ── Références (unifiées) ────────────────────────────────────────────────
	void DrawReferences(ui::Context &u) {
		const ui::Theme &th = u.Style();
		if (m_selected < 0 || size_t(m_selected) >= m_funcs.size()) {
			u.Label("sélectionnez une fonction", th.textDim);
			return;
		}
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		u.Heading("Références");
		u.Label("Références de données (chaînes / globales chargées) :", th.textDim, 12);
		for (const rea::DataRef &d : f.dataRefs)
			u.Label(std::format("  @0x{:x} → 0x{:x}  {}", d.from, d.target, d.what), th.text, 12);
		if (f.dataRefs.empty())
			u.Label("  (aucune)", th.textDim, 12);
		u.Separator();
		u.Label("Appels émis :", th.textDim, 12);
		for (size_t i = 0; i < f.calls.size(); ++i)
			if (u.Selectable(std::format("  {} @ 0x{:x}", f.callNames[i], f.calls[i]), false, th.info))
				SelectByAddress(f.calls[i]);
		if (f.calls.empty())
			u.Label("  (aucun)", th.textDim, 12);
	}

	// ── Vue octets (hexadécimale) ────────────────────────────────────────────
	void DrawBytes(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Heading("Octets");
		if (m_selected < 0 || size_t(m_selected) >= m_funcs.size()) {
			u.Label("sélectionnez une fonction", th.textDim);
			return;
		}
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		uint64_t off = 0;
		if (!m_elf->AddressToOffset(f.symbol->value, off)) {
			u.Label("octets non disponibles", th.textDim);
			return;
		}
		const auto &raw = m_elf->Raw();
		const size_t size = std::min<uint64_t>(f.symbol->size, raw.size() - off);
		for (size_t i = 0; i < size; i += 16) {
			std::string hex, ascii;
			for (size_t j = 0; j < 16 && i + j < size; ++j) {
				const uint8_t b = raw[size_t(off) + i + j];
				hex += std::format("{:02x} ", b);
				ascii += (b >= 0x20 && b <= 0x7e) ? char(b) : '.';
			}
			u.Label(std::format("{:08x}  {:<48} {}", f.symbol->value + i, hex, ascii), th.text, 12);
		}
	}

	/// Dessine le graphe de flot de contrôle (blocs de base + sauts) du corps.
	void DrawCfg(ui::Context &u, const sdl3::FRect &area) {
		const ui::Theme &th = Theme();
		if (m_selected < 0 || size_t(m_selected) >= m_funcs.size())
			return;
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		if (f.blocks.empty())
			return;

		// Index adresse → bloc (pour résoudre les successeurs).
		std::unordered_map<uint64_t, const rea::BasicBlock *> byAddr;
		for (const rea::BasicBlock &b : f.blocks)
			byAddr[b.address] = &b;

		// Niveaux par BFS depuis le bloc d'entrée.
		const uint64_t entry = f.blocks.front().address;
		std::unordered_map<uint64_t, int> level;
		std::unordered_map<uint64_t, bool> visited;
		std::deque<uint64_t> q;
		for (const rea::BasicBlock &b : f.blocks)
			level[b.address] = 0;
		visited[entry] = true;
		q.push_back(entry);
		while (!q.empty()) {
			const uint64_t cur = q.front();
			q.pop_front();
			const auto it = byAddr.find(cur);
			if (it == byAddr.end())
				continue;
			for (uint64_t v : it->second->successors)
				if (!visited[v]) {
					visited[v] = true;
					level[v] = level[cur] + 1;
					q.push_back(v);
				}
		}
		int maxLevel = 0;
		for (const rea::BasicBlock &b : f.blocks)
			if (visited[b.address])
				maxLevel = std::max(maxLevel, level[b.address]);
		int nextLevel = maxLevel + 1;
		for (const rea::BasicBlock &b : f.blocks)
			if (!visited[b.address])
				level[b.address] = nextLevel++; // blocs isolés (après une boucle infinie…)

		// Regroupe par niveau, ordonne par adresse.
		std::map<int, std::vector<const rea::BasicBlock *>> byLevel;
		for (const rea::BasicBlock &b : f.blocks)
			byLevel[level[b.address]].push_back(&b);
		int maxRows = 0;
		for (auto &[lvl, vec] : byLevel) {
			std::sort(vec.begin(), vec.end(), [](const rea::BasicBlock *a, const rea::BasicBlock *b) { return a->address < b->address; });
			maxRows = std::max(maxRows, int(vec.size()));
		}

		// Échelle pour tenir dans la zone (en réservant la ligne d'indication).
		const float topPad = 18.f;
		const float gapX = 48.f, gapY = 26.f;
		float blockW = 200.f, blockH = 58.f;
		const float neededW = float(byLevel.size()) * (blockW + gapX) + gapX;
		const float neededH = float(maxRows) * (blockH + gapY) + gapY;
		const float scale = std::min({1.f, area.w / neededW, (area.h - topPad) / neededH});
		blockW *= scale;
		blockH *= scale;
		const float gx = gapX * scale, gy = gapY * scale;
		const float fontScale = scale < 0.6f ? 0.8f : 1.f;

		// Positions des blocs (dans les coordonnées logiques de la zone).
		const float originX = area.x + gx;
		const float originY = area.y + topPad + gy;
		std::unordered_map<uint64_t, sdl3::FRect> pos;
		{
			int lvl = 0;
			for (auto &[l, vec] : byLevel) {
				for (size_t i = 0; i < vec.size(); ++i)
					pos[vec[i]->address] = {originX + float(l) * (blockW + gx), originY + float(i) * (blockH + gy), blockW, blockH};
				++lvl;
			}
		}

		sdl3::Canvas &c = Canvas();
		const sdl3::Color edge = sdl3::Rgba(96, 132, 190, 200);
		const sdl3::Color backEdge = sdl3::Rgba(236, 180, 72, 220);

		// Arêtes.
		for (const rea::BasicBlock &b : f.blocks) {
			const sdl3::FRect s = pos[b.address];
			for (uint64_t succ : b.successors) {
				const auto tIt = pos.find(succ);
				if (tIt == pos.end())
					continue;
				const sdl3::FRect t = tIt->second;
				const bool back = level[succ] <= level[b.address];
				const sdl3::Color col = back ? backEdge : edge;
				const float x0 = s.x + s.w * 0.5f, y0 = s.y + s.h;
				const float x1 = t.x + t.w * 0.5f, y1 = t.y;
				c.Line(x0, y0, x1, y1, col, 1.5f);
				// Pointe de flèche.
				const float dx = x1 - x0, dy = y1 - y0;
				const float len = std::sqrt(dx * dx + dy * dy);
				if (len < 1.f)
					continue;
				const float ux = dx / len, uy = dy / len;
				const float ah = 7.f * fontScale;
				c.Line(x1, y1, x1 - ux * ah - uy * ah * 0.5f, y1 - uy * ah + ux * ah * 0.5f, col, 1.5f);
				c.Line(x1, y1, x1 - ux * ah + uy * ah * 0.5f, y1 - uy * ah - ux * ah * 0.5f, col, 1.5f);
			}
		}

		// Indication.
		c.Text(area.x + 4.f, area.y + 2.f, "cliquez sur un bloc pour le voir dans l'assembleur", th.textDim, 11.f);

		// Blocs (survol + clic → navigation vers l'assembleur).
		uint64_t hovered = 0;
		for (const rea::BasicBlock &b : f.blocks)
			if (pos[b.address].Contains(u.MouseX(), u.MouseY()))
				hovered = b.address;
		if (hovered && u.MousePressed(sdl3::MouseButton::Left)) {
			NavigateTo(m_selected, hovered);
			m_dock.Activate(ui::DockLayout::Side::Center, "code"); // bascule sur l'onglet « Code »
		}

		for (const rea::BasicBlock &b : f.blocks) {
			const sdl3::FRect r = pos[b.address];
			const bool isEntry = b.address == entry;
			const bool isHover = b.address == hovered;
			c.FillRoundRect(r, 5.f * fontScale, isHover ? sdl3::Rgba(52, 64, 92, 255) : sdl3::Rgba(34, 42, 62, 255));
			if (isEntry)
				c.FillRoundRect({r.x, r.y, r.w, 3.f}, 1.5f, th.accent);
			c.Text(r.x + 6.f, r.y + 6.f, std::format("{:x}", b.address), isEntry ? th.accent : th.text, 12.f * fontScale, sdl3::CanvasFont::Mono);
			std::string mnems;
			for (size_t i = 0; i < b.instructions.size() && i < 3; ++i) {
				if (i)
					mnems += " ";
				mnems += b.instructions[i].mnemonic;
			}
			c.Text(r.x + 6.f, r.y + 24.f, mnems, th.textDim, 11.f * fontScale, sdl3::CanvasFont::Mono);
		}
	}

	void DrawXrefs(ui::Context &u) {
		const ui::Theme &th = u.Style();
		if (m_selected < 0 || size_t(m_selected) >= m_funcs.size()) {
			u.Label("sélectionnez une fonction", th.textDim);
			return;
		}
		const rea::FunctionAnalysis &f = m_funcs[size_t(m_selected)];
		u.Heading("Références croisées");
		u.Label(std::format("appelé(e) par {} fonction(s) — cliquez pour naviguer", f.callers.size()), th.textDim, 12);
		for (size_t i = 0; i < f.callers.size(); ++i)
			if (u.Selectable(std::format("{} @ 0x{:x}", f.callerNames[i], f.callers[i]), false, th.warning))
				SelectByAddress(f.callers[i]);
		if (f.callers.empty())
			u.Label("(aucun appelant — point d'entrée ?)", th.textDim, 12);
		u.Separator();
		u.Label(std::format("appelle {} fonction(s)", f.calls.size()), th.textDim, 12);
		for (size_t i = 0; i < f.calls.size(); ++i)
			if (u.Selectable(std::format("{} @ 0x{:x}", f.callNames[i], f.calls[i]), false, th.info))
				SelectByAddress(f.calls[i]);
		if (f.calls.empty())
			u.Label("(aucun appel — feuille du graphe)", th.textDim, 12);
	}

	void DrawStrings(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Label(std::format("{} chaîne(s) ASCII (≥ 4 caractères)", m_strings.size()), th.textDim, 12);
		u.Separator();
		for (const rea::StringRef &s : m_strings)
			u.Label(std::format("{:<12} 0x{:x}  {}", s.section, s.address, s.text), th.text, 13);
	}

	void DrawSecurity(ui::Context &u) {
		const ui::Theme &th = u.Style();
		const rea::SecurityReport sec = m_elf->Security();
		u.Heading("Durcissement");
		auto flag = [&](bool on) { return on ? "oui" : "non"; };
		u.Label(std::format("PIE (position independent) : {}", flag(sec.pie)), sec.pie ? th.success : th.textDim);
		u.Label(std::format("NX (pile non exécutable)  : {}", flag(sec.nx)), sec.nx ? th.success : th.danger);
		u.Label(std::format("RELRO (relocations RO)    : {}{}", sec.relro ? (sec.fullRelro ? "complet" : "partiel") : "non",
							sec.fullRelro ? "" : (sec.relro ? "" : "")),
				sec.fullRelro ? th.success : sec.relro ? th.warning : th.textDim);
		u.Label(std::format("Canari de pile            : {}", flag(sec.stackCanary)), sec.stackCanary ? th.success : th.textDim);
		u.Label(std::format("Fortification (_FORTIFY)  : {}", flag(sec.fortify)), sec.fortify ? th.success : th.textDim);
		if (!sec.buildId.empty())
			u.Label(std::format("Build-id : {}", sec.buildId), th.info, 12);

		u.Separator();
		const auto imports = m_elf->Imports();
		u.Label(std::format("{} fonction(s) importée(s)", imports.size()), th.textDim);
		for (const rea::ElfSymbol *s : imports)
			u.Label(std::format("  {}", s->name), th.text, 12);

		u.Separator();
		const auto notes = m_elf->Notes();
		u.Label(std::format("{} note(s) ELF", notes.size()), th.textDim);
		for (const rea::ElfNote &n : notes)
			u.Label(std::format("  {} (type {}) — {} octet(s)", n.owner, n.type, n.desc.size()), th.text, 12);
	}

	void DrawSignatures(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Heading("Signatures d'algorithmes");
		u.Label(std::format("{} occurrence(s) trouvée(s)", m_signatures.size()), th.textDim, 12);
		u.Separator();
		if (m_signatures.empty()) {
			u.Label("aucune signature connue (AES, SHA-256, MD5) dans ce binaire.", th.textDim);
			return;
		}
		for (const rea::ByteSignatureMatch &m : m_signatures)
			u.Label(std::format("{} @ 0x{:x} — {}", m.name, m.address, m.description), th.warning, 13);
	}

	void DrawHeader(ui::Context &u) {
		const ui::Theme &th = u.Style();
		u.Heading("En-tête ELF");
		const rea::BinaryIdentity id = rea::DetectIdentity(*m_elf);
		u.Label(std::format("entrée : 0x{:x} · machine : x86-64 ({})", m_elf->EntryPoint(), m_elf->Machine()));
		u.Label(std::format("langage : {} · outil : {} · {} · {}", id.language, id.toolchain.empty() ? "?" : id.toolchain,
							id.stripped ? "stripé" : "symboles présents", id.staticallyLinked ? "liaison statique" : "liaison dynamique"),
				th.info, 13);
		u.Separator();
		u.Label(std::format("{} segment(s) de programme", m_elf->Segments().size()), th.textDim);
		for (const rea::ElfSegment &p : m_elf->Segments())
			u.Label(std::format("  type {} · 0x{:x}→0x{:x} · {}+{} octets", p.type, p.offset, p.vaddr, p.filesz, p.memsz), th.text, 12);
		u.Separator();
		u.Label(std::format("{} section(s)", m_elf->Sections().size()), th.textDim);
		for (const rea::ElfSection &s : m_elf->Sections()) {
			const bool exec = s.IsExecutable();
			u.Label(std::format("  {:<16} 0x{:x}  {} octets{}", s.name, s.address, s.size, exec ? "  [X]" : ""),
					exec ? th.accent : th.text, 12);
		}
	}

	// ── État ─────────────────────────────────────────────────────────────────
	std::string m_path;
	std::string m_loadPath;
	std::string m_error;
	bool m_loaded = false;
	std::optional<rea::Elf> m_elf;
	std::vector<rea::FunctionAnalysis> m_funcs;
	std::vector<rea::StringRef> m_strings;
	std::vector<rea::ByteSignatureMatch> m_signatures;
	ui::DockLayout m_dock; // disposition ancrable (régions + onglets)
	std::unordered_map<uint64_t, int> m_funcIndex; // adresse → index dans m_funcs
	int m_selected = 0;
	uint64_t m_focusAddr = 0; // adresse ciblée (clic dans le graphe) → surlignée dans l'assembleur
	std::vector<NavLocation> m_back;	// pile « précédent »
	std::vector<NavLocation> m_forward; // pile « suivant »

	// Projet + ROM + invite de chemin.
	rea::Project m_project;
	std::string m_projectPath;
	bool m_isRom = false;
	std::vector<uint8_t> m_rom;
	std::vector<rea::Line> m_romLines;
	std::string m_prompt;	  // « open », « openproject », « saveas », « export » (vide : aucune)
	std::string m_promptPath;
	std::string m_renameBuf;  // tampon de renommage (onglet Code)
	std::string m_commentBuf; // tampon de commentaire (onglet Code)
	std::set<std::string> m_collapsedSections; // sections repliées de l'arborescence
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "REA — rétro-ingénierie x86-64";
	config.width = 1500;
	config.height = 900;
	config.minWidth = 900;
	config.minHeight = 560;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CODE);
	auto positional = config.ParseArgs(argc, argv);
	std::string path;
	if (!positional.empty())
		path = positional[0];
	return ReaApp(std::move(path)).Run(config);
}
