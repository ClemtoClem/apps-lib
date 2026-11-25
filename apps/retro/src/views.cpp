// retro — dessin : barre de titre, vue de jeu (bibliothèque, écran, états) et vue de
// débogage (processeur, désassemblage, mémoire vidéo, mémoire, console, performances).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>

#include <consoles/gbc/gbc.hpp>
#include <ui/layout.hpp>

#include "retro/app.hpp"

namespace retro {

namespace {

constexpr sdl3::Color kAddress = sdl3::Rgba(120, 190, 255), kValue = sdl3::Rgba(120, 230, 140), kMnemonic = sdl3::Rgba(240, 200, 90);

std::string Lower(std::string_view text) {
	std::string out(text);
	for (char &c : out)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

/// Couleur stable dérivée d'un texte (vignette d'une ROM sans capture).
sdl3::Color ColorOf(std::string_view text) {
	uint32_t h = 2166136261u;
	for (const char c : text)
		h = (h ^ uint8_t(c)) * 16777619u;
	return sdl3::Rgba(uint8_t(60 + (h & 0x5F)), uint8_t(60 + ((h >> 8) & 0x5F)), uint8_t(80 + ((h >> 16) & 0x6F)));
}

/// Étoile à cinq branches tracée (la police n'a pas le caractère).
void DrawStar(sdl3::Canvas &c, float cx, float cy, float radius, sdl3::Color color) {
	float points[22];
	for (int i = 0; i <= 10; ++i) {
		const float angle = -1.5707963f + float(i) * 0.6283185f, r = (i % 2) ? radius * 0.45f : radius;
		points[i * 2] = cx + std::cos(angle) * r;
		points[i * 2 + 1] = cy + std::sin(angle) * r;
	}
	c.Polyline(points, 11, color, 2.f);
}

} // namespace

sdl3::FRect App::Panel(const sdl3::FRect &area, std::string_view title) {
	sdl3::Canvas &c = m_canvas;
	const ui::Theme &th = m_ui->Style();
	c.FillRoundRect(area, 10, th.panel);
	c.StrokeRoundRect(area, 10, th.panelBorder);
	sdl3::FRect body = area.Inset(12);
	if (!title.empty()) {
		const sdl3::FRect head = ui::CutTop(body, 26, 8);
		c.Text(head.x, head.y + 3, title, th.text, 15, sdl3::CanvasFont::Bold);
	}
	return body;
}

std::pair<float, float> App::ScreenViewSize() const noexcept {
	if (IsDualScreen() && m_dualLayout == DualLayout::SideBySide)
		return {float(m_screenW) * 2.f, float(m_screenH) / 2.f};
	return {float(m_screenW), float(m_screenH)};
}

void App::DrawScreen(const sdl3::FRect &area, bool interactive) {
	if (!m_session || !m_screenTexture || area.w < 8 || area.h < 8)
		return;
	const auto [viewW, viewH] = ScreenViewSize();
	float scale = std::min(area.w / viewW, area.h / viewH);
	if (scale >= 1.f)
		scale = std::floor(scale);
	const sdl3::FRect dst = ui::Place(area, viewW * scale, viewH * scale, ui::Anchor::Center);
	// Côte à côte : la moitié haute de l'image (écran du haut) à gauche, la basse à droite.
	const bool sideBySide = IsDualScreen() && m_dualLayout == DualLayout::SideBySide;
	const float w = float(m_screenW), half = float(m_screenH) / 2.f;
	if (sideBySide) {
		m_canvas.ImageRegion(*m_screenTexture, w, float(m_screenH), {0, 0, w, half}, {dst.x, dst.y, w * scale, half * scale});
		m_canvas.ImageRegion(*m_screenTexture, w, float(m_screenH), {0, half, w, half}, {dst.x + w * scale, dst.y, w * scale, half * scale});
	} else {
		m_canvas.Image(*m_screenTexture, dst);
	}
	if (!interactive)
		return;
	// Écran tactile : le bouton gauche de la souris sert de stylet dans la zone tactile.
	for (emu::TouchPanel *touch : m_session->Machine().TouchPanels()) {
		float x = float(touch->AreaX()), y = float(touch->AreaY());
		if (sideBySide && y >= half) { // zone de l'écran du bas : passée à droite
			x += w;
			y -= half;
		}
		const sdl3::FRect zone{dst.x + x * scale, dst.y + y * scale, float(touch->AreaWidth()) * scale, float(touch->AreaHeight()) * scale};
		const bool inside = zone.Contains(m_ui->MouseX(), m_ui->MouseY());
		if (inside)
			m_canvas.StrokeRect(zone, sdl3::WithAlpha(m_ui->Style().accent, 90));
		const bool pressed = m_ui->MouseDown(sdl3::MouseButton::Left) && (inside || touch->Pressed());
		touch->Set(int((m_ui->MouseX() - zone.x) / scale), int((m_ui->MouseY() - zone.y) / scale), pressed && !m_paused);
	}
}

void App::Draw(float dt) {
	(void)dt;
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	const sdl3::FRect window{0, 0, c.Width(), c.Height()};
	c.FillRect(window, th.background);

	if (m_fullscreen) { // plein écran : l'image seule
		c.FillRect(window, sdl3::Rgba(0, 0, 0));
		DrawScreen(window, true);
		return;
	}

	// ── Barre de titre : outils à gauche, vues à droite, boutons de la fenêtre ──
	// Elle reste toujours active, même quand un modal est ouvert (l'en-tête
	// custom de la fenêtre n'est jamais masqué par un overlay).
	m_ui->SetBlocked(false);
	const bool hasGame = m_session != nullptr;
	const ui::FrameButton left[] = {
		{.id = "save", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SAVE), .tooltip = "Nouvel état (F5)", .enabled = hasGame},
		{.id = "load", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::UNDO), .tooltip = "Chargement rapide (F9)", .enabled = hasGame && !m_states.empty()},
		{.id = "play", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW), .tooltip = "Continuer", .enabled = hasGame && m_paused, .tint = th.success, .separator = true},
		{.id = "pause", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PAUSE), .tooltip = "Pause (P)", .enabled = hasGame && !m_paused, .tint = th.warning},
		{.id = "reset", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::REPLAY), .tooltip = "Réinitialiser la machine", .enabled = hasGame, .tint = th.danger},
		{.id = "into", .label = "Pas à pas", .tooltip = "Exécuter une instruction (F11)", .enabled = hasGame, .separator = true},
		{.id = "over", .label = "Pas principal", .tooltip = "Exécuter jusqu'à l'instruction suivante (F10)", .enabled = hasGame},
		{.id = "continue", .label = "Continuer", .enabled = hasGame && m_paused},
	};
	const ui::FrameButton right[] = {
		{.id = "game", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SPORTS_ESPORTS), .label = "Jeu", .active = m_view == View::Game},
		{.id = "debug", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::MEMORY), .label = "Débogage", .tooltip = "Vue de débogage (F12)", .active = m_view == View::Debug},
	};
	ui::WindowFrameOptions frame;
	frame.title = "retro";
	frame.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SPORTS_ESPORTS);
	frame.left = left;
	frame.right = right;
	frame.maximized = m_chrome->Maximized();
	frame.focused = m_chrome->Focused();
	const ui::WindowFrameResult result = ui::WindowFrame(u, window, frame);
	m_chrome->Update(result.layout, 1.f);
	if (result.action == ui::FrameAction::Custom) {
		if (result.custom == "save")
			SaveNewState();
		else if (result.custom == "load")
			QuickLoad();
		else if (result.custom == "play" || result.custom == "continue")
			SetPaused(false);
		else if (result.custom == "pause")
			SetPaused(true);
		else if (result.custom == "reset") {
			m_session->Machine().Reset();
			Log(LogLevel::Info, "[INFO] machine réinitialisée");
		} else if (result.custom == "into")
			StepInto();
		else if (result.custom == "over")
			StepOver();
		else if (result.custom == "game")
			m_view = View::Game;
		else if (result.custom == "debug")
			m_view = View::Debug;
	}
	if (m_chrome->Apply(result.action))
		m_running = false;

	// Contenu (barre d'état + vues) : gelé tant que le modal de configuration des
	// touches est ouvert — survol, clics et défilement y sont ignorés.
	m_ui->SetBlocked(m_keysOpen);
	m_retained->SetBlocked(m_keysOpen);
	m_controls->SetBlocked(m_keysOpen);

	sdl3::FRect content = result.layout.content;
	const sdl3::FRect status = ui::CutBottom(content, 26);
	DrawStatusBar(status);
	content = content.Inset(12);
	if (m_view == View::Game)
		DrawGameView(content);
	else
		DrawDebugView(content);

	// Modal de configuration des touches : overlay au premier plan, au-dessus de
	// tout le contenu (l'en-tête de la fenêtre reste utilisable au-dessus).
	if (m_keysOpen)
		DrawKeyConfigModal(result.layout.content);
}

void App::DrawKeyConfigModal(const sdl3::FRect &area) {
	if (m_modalDirty) {
		m_modalDirty = false;
		BuildKeyConfigModal();
	}
	m_modal->Present(0.f, area);
}

void App::DrawStatusBar(const sdl3::FRect &area) {
	// Barre d'état construite par le builder factory, stylée en CSS
	// (assets/ui.css, classe `.status`) et affichée automatiquement.
	const std::string left = m_gamepad ? "Manette : " + m_gamepadName : std::string("Clavier (aucune manette)");
	std::string right;
	if (m_session) {
		const double rate = m_session->Machine().FrameRate();
		right = std::format("{} | {} | émulation {:.2f} ms/image ({:.0f} %) | image {}", m_session->System().description,
							m_audio ? "son actif" : "muet", m_lastEmulationMs, double(m_lastEmulationMs) * rate / 10.0,
							m_session->Machine().FrameCount());
	} else {
		right = std::format("{} ROM dans la bibliothèque", m_library.size());
	}
	m_retained->Find("status-left").SetText(left);
	m_retained->Find("status-right").SetText(right);
	m_retained->Present(0.f, area);
}

// ════════════════════════════════════════════════════════════════════════════
// Vue de jeu
// ════════════════════════════════════════════════════════════════════════════

void App::DrawGameView(const sdl3::FRect &area) {
	// Colonnes : bibliothèque et états de largeur fixe (réduites si la fenêtre est étroite),
	// l'écran prend le reste.
	const float side = std::clamp(area.w * 0.24f, 220.f, 330.f);
	const ui::FlexItem items[] = {{side, 0}, {0, 1}, {side, 0}};
	const auto columns = ui::Row(area, items, 12);
	DrawLibrary(columns[0]);
	DrawPlayer(columns[1]);
	DrawStates(columns[2]);
}

void App::DrawLibrary(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	sdl3::FRect body = Panel(area, "BIBLIOTHÈQUE");
	const sdl3::FRect search = ui::CutTop(body, 32, 8);
	u.TextInputAt(search, "##search", m_search, nullptr, "Rechercher…");
	const std::string needle = Lower(m_search);

	u.BeginScroll("##library", body);
	size_t shown = 0;
	for (RomEntry &rom : m_library) {
		if (!needle.empty() && Lower(rom.title).find(needle) == std::string::npos && Lower(rom.system).find(needle) == std::string::npos)
			continue;
		++shown;
		const sdl3::FRect row = u.Next(64);
		if (row.Bottom() < body.y || row.y > body.Bottom())
			continue; // hors de la zone visible : seule la place est réservée
		const bool current = m_session && m_session->RomPath() == rom.path;
		const auto it = u.Interact(u.MakeId("rom:" + rom.path.string()), row);
		if (current || it.hovered)
			c.FillRoundRect(row, 8, current ? th.widgetActive : th.widgetHover);
		// Vignette : dernière capture d'un état de la ROM, sinon un aplat aux initiales.
		const sdl3::FRect thumb{row.x + 6, row.y + 6, 58, 52};
		sdl3::Texture *image = rom.thumbnail.empty() ? nullptr : Thumbnail(rom.thumbnail);
		if (image) {
			c.Image(*image, thumb);
		} else {
			c.FillRoundRect(thumb, 6, ColorOf(rom.title));
			c.TextCentered(thumb, rom.title.substr(0, std::min<size_t>(2, rom.title.size())), sdl3::Rgba(255, 255, 255, 220), 18, sdl3::CanvasFont::Bold);
		}
		const float textX = thumb.Right() + 10, textW = row.Right() - textX - 30;
		c.Text(textX, row.y + 12, c.Ellipsize(rom.title, textW, 14), th.text, 14);
		const std::string sub = rom.recent >= 0 ? rom.system + " • joué récemment" : rom.system;
		c.Text(textX, row.y + 34, c.Ellipsize(sub, textW, 12), th.textDim, 12);
		// Étoile de favori.
		const sdl3::FRect star{row.Right() - 28, row.y + 20, 24, 24};
		const auto starIt = u.Interact(u.MakeId("fav:" + rom.path.string()), star);
		DrawStar(c, star.x + 12, star.y + 12, 8, rom.favorite ? th.warning : sdl3::WithAlpha(th.textDim, starIt.hovered ? 220 : 90));
		if (starIt.leftClicked) {
			rom.favorite = !rom.favorite;
			if (rom.favorite)
				m_favorites.insert(rom.path.string());
			else
				m_favorites.erase(rom.path.string());
		} else if (it.leftClicked && !current) {
			const std::filesystem::path path = rom.path; // LoadRom reconstruit la bibliothèque
			u.EndScroll();
			(void)LoadRom(path);
			return;
		}
	}
	if (shown == 0) {
		if (m_scan.valid())
			u.Loader("Analyse de la bibliothèque…");
		else
			u.Wrapped(m_library.empty() ? "Aucune ROM : lancez retro avec --roms DOSSIER." : "Aucun résultat.", th.textDim);
	}
	u.EndScroll();
}

void App::DrawPlayer(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	c.FillRoundRect(area, 10, th.panel);
	c.StrokeRoundRect(area, 10, th.panelBorder);
	sdl3::FRect body = area.Inset(14);
	sdl3::FRect controls = ui::CutBottom(body, 44, 10);

	// Écran dans un cadre sombre arrondi (téléviseur).
	c.FillRoundRect(body, 14, sdl3::Rgba(8, 9, 14));
	c.StrokeRoundRect(body, 14, sdl3::Rgba(60, 70, 100), 2);
	if (m_session) {
		DrawScreen(body.Inset(14), true);
		if (m_paused) {
			const sdl3::FRect badge = ui::Place(body, 110, 30, ui::Anchor::Top, 0, 16);
			c.FillRoundRect(badge, 6, sdl3::Rgba(0, 0, 0, 170));
			c.TextCentered(badge, "EN PAUSE", th.warning, 13, sdl3::CanvasFont::Bold);
		}
	} else {
		c.TextCentered(body, "Choisissez une ROM dans la bibliothèque", th.textDim, 16);
	}

	// Commandes : touches, lecture / pause, vitesses, volume, plein écran, disposition des écrans, cadence.
	const bool hasGame = m_session != nullptr;
	const sdl3::FRect keys = ui::CutLeft(controls, 36, 10);
	const auto keysIt = u.Interact(u.MakeId("##keys"), keys);
	c.FillRoundRect(keys.Inset(0, 4), 8, keysIt.hovered ? th.widgetHover : th.widget);
	u.Icon(keys, ui::icons::Codepoint(ui::icons::MaterialIcons::KEYBOARD), th.text, 20);
	if (keysIt.hovered)
		u.Tooltip("Configurer les touches du clavier");
	if (keysIt.leftClicked)
		OpenKeyConfig();
	const sdl3::FRect full = ui::CutRight(controls, 36, 8);
	if (u.IconButton(full.Inset(0, 4), ui::icons::Codepoint(ui::icons::MaterialIcons::CROP_FREE), "Plein écran (Échap pour revenir)", hasGame))
		ToggleFullscreen();
	// Disposition des deux écrans (Nintendo DS) : l'icône montre la disposition proposée.
	const sdl3::FRect layout = ui::CutRight(controls, 36, 4);
	const bool sideBySide = m_dualLayout == DualLayout::SideBySide;
	if (u.IconButton(layout.Inset(0, 4), sideBySide ? ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_AGENDA) : ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_COLUMN),
					 sideBySide ? "Écrans l'un au-dessus de l'autre (F3)" : "Écrans côte à côte (F3)", IsDualScreen()))
		ToggleDualLayout();
	// La cadence n'apparaît que s'il reste la place des commandes (~460 px) à côté.
	constexpr float kControlsWidth = 460.f, kRateWidth = 126.f;
	if (hasGame && controls.w > kControlsWidth + kRateWidth) {
		const std::string rate = std::format("{:.0f} img/s · {:.0f} %", m_fps, (m_fast ? 4.f : m_speed) * 100.f);
		const sdl3::FRect info = ui::CutRight(controls, kRateWidth, 6);
		c.Text(info.Right() - c.MeasureText(rate, 12), info.y + 15, rate, th.textDim, 12);
	}
	// Commandes (lecture, pause, volume, vitesses) : interface retenue — builder
	// factory + CSS + affichage automatique, valeurs liées aux membres de App.
	m_controls->Present(0.f, controls);
}

void App::DrawStates(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	sdl3::FRect body = Panel(area, "ÉTATS SAUVEGARDÉS");
	const sdl3::FRect actions = ui::CutTop(body, 32, 10);
	const ui::FlexItem two[] = {{0, 1}, {0, 1}};
	const auto buttons = ui::Row(actions, two, 8);
	if (u.ButtonAt(buttons[0], "Nouvel état (F5)", m_session != nullptr))
		SaveNewState();
	if (u.ButtonAt(buttons[1], "Charger (F9)", m_session != nullptr && !m_states.empty()))
		QuickLoad();

	u.BeginScroll("##states", body);
	for (size_t i = 0; i < m_states.size(); ++i) {
		const StateEntry &state = m_states[i];
		const sdl3::FRect card = u.Next(132);
		const auto it = u.Interact(u.MakeId(std::format("state{}", state.slot)), card);
		c.FillRoundRect(card.Inset(0, 3), 8, it.hovered ? th.widgetHover : th.widget);
		if (i == 0) // le plus récent : celui du chargement rapide
			c.StrokeRoundRect(card.Inset(0, 3), 8, th.accent, 1.5f);
		const sdl3::FRect thumb{card.x + 8, card.y + 11, 110, 99};
		if (sdl3::Texture *image = Thumbnail(state.file.string() + ".png"))
			c.Image(*image, thumb);
		else
			c.FillRoundRect(thumb, 4, sdl3::Rgba(10, 12, 18));
		const float bx = thumb.Right() + 10, bw = card.Right() - bx - 8;
		if (m_renaming == int(i)) {
			bool submitted = false;
			u.TextInputAt({bx, card.y + 12, bw, 28}, "##rename", m_renameText, &submitted, "Nom de l'état");
			if (submitted) {
				std::ofstream(state.file.string() + ".txt", std::ios::trunc) << m_renameText;
				RefreshStates();
				break;
			}
		} else {
			const std::string label = state.label.empty() ? std::format("État {}", state.slot) : state.label;
			c.Text(bx, card.y + 14, c.Ellipsize(label, bw, 13), th.text, 13, sdl3::CanvasFont::Bold);
			c.Text(bx, card.y + 34, state.date, th.textDim, 11);
		}
		if (u.ButtonAt({bx, card.y + 58, bw, 24}, std::format("Renommer##{}", state.slot))) {
			m_renaming = int(i);
			m_renameText = state.label;
			u.SetFocus("##rename");
		}
		if (u.ButtonAt({bx, card.y + 88, bw, 24}, std::format("Supprimer##{}", state.slot))) {
			DeleteState(i);
			break;
		}
		if (it.leftClicked && m_renaming != int(i)) {
			LoadStateFile(state);
		}
	}
	if (m_states.empty())
		u.Wrapped(m_session ? "Aucun état : F5 enregistre l'instant présent." : "Aucune ROM chargée.", th.textDim);
	u.EndScroll();
}

// ════════════════════════════════════════════════════════════════════════════
// Vue de débogage
// ════════════════════════════════════════════════════════════════════════════

void App::DrawDebugView(const sdl3::FRect &area) {
	if (!m_session) {
		m_canvas.TextCentered(area, "Aucune ROM chargée : choisissez-en une dans la vue « Jeu ».", m_ui->Style().textDim, 15);
		return;
	}
	// Deux rangées : processeur | désassemblage | mémoire vidéo ; mémoire | console | performances.
	const ui::FlexItem rows[] = {{0, 1.15f}, {0, 1}};
	const auto bands = ui::Column(area, rows, 10);
	const ui::FlexItem top[] = {{0, 0.9f, 250}, {0, 1.4f}, {0, 1.1f, 260}};
	const auto upper = ui::Row(bands[0], top, 10);
	DrawCpuPanel(upper[0]);
	DrawDisassembly(upper[1]);
	DrawVideoPanel(upper[2]);
	const ui::FlexItem bottom[] = {{0, 1.6f}, {0, 1.1f}, {0, 1}};
	const auto lower = ui::Row(bands[1], bottom, 10);
	DrawMemoryPanel(lower[0]);
	DrawConsole(lower[1]);
	DrawPerformance(lower[2]);
}

void App::DrawCpuPanel(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	const ui::Theme &th = m_ui->Style();
	const sdl3::FRect body = Panel(area, "PROCESSEUR ET REGISTRES");
	emu::Debuggable *debug = m_session->Machine().Debugger();
	if (!debug) {
		c.Text(body.x, body.y, "Ce système n'expose pas de débogueur.", th.textDim, 13);
		return;
	}
	c.PushClip(body);
	const float lh = 20;
	float y = body.y;
	const auto registers = debug->DebugRegisters();
	// Deux colonnes : registres larges puis registres 8 bits, valeur hexadécimale et binaire.
	for (const emu::RegisterValue &r : registers) {
		if (y + lh > body.Bottom())
			break;
		c.Text(body.x, y, std::format("{:<5}", r.name + ":"), th.textDim, 13, sdl3::CanvasFont::Mono);
		const std::string hex = r.bits > 16 ? std::format("{:08X}", r.value)
								: r.bits > 8 ? std::format("0x{:04X}", r.value)
								: (r.bits == 1 ? std::format("{}", r.value) : std::format("0x{:02X}", r.value));
		c.Text(body.x + 52, y, hex, kValue, 13, sdl3::CanvasFont::Mono);
		if (r.bits == 8)
			c.Text(body.x + 120, y, std::format("{:08b}", r.value), kAddress, 13, sdl3::CanvasFont::Mono);
		else if (r.bits == 16)
			c.Text(body.x + 120, y, std::format("{}", r.value), th.textDim, 13, sdl3::CanvasFont::Mono);
		y += lh;
	}
	y += 6;
	if (y + lh <= body.Bottom()) {
		c.Text(body.x, y, "Drapeaux :", th.textDim, 13, sdl3::CanvasFont::Mono);
		c.Text(body.x + 96, y, debug->FlagsText(), th.warning, 13, sdl3::CanvasFont::Mono);
		y += lh;
	}
	if (y + lh <= body.Bottom()) {
		c.Text(body.x, y, "État :", th.textDim, 13, sdl3::CanvasFont::Mono);
		c.Text(body.x + 96, y, m_paused ? "en pause" : "en cours", m_paused ? th.warning : th.success, 13, sdl3::CanvasFont::Mono);
		y += lh;
	}
	if (y + lh <= body.Bottom())
		c.Text(body.x, y, std::format("Points d'arrêt : {}", m_breakpoints.size()), th.textDim, 13, sdl3::CanvasFont::Mono);
	c.PopClip();
}

void App::DrawDisassembly(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	const sdl3::FRect body = Panel(area, "DÉSASSEMBLAGE");
	emu::Debuggable *debug = m_session->Machine().Debugger();
	if (!debug)
		return;
	const float lh = 20;
	const int lines = std::max(1, int(body.h / lh));
	const uint32_t pc = debug->ProgramCounter();
	// L'ancre reste stable tant que le compteur ordinal est visible ; sinon elle le rejoint
	// (on ne peut pas désassembler à rebours de façon fiable).
	uint32_t address = m_disasmAnchor;
	bool visible = false;
	for (int i = 0; i < lines - 2 && !visible; ++i) {
		if (address == pc)
			visible = true;
		address += debug->Disassemble(address).size;
	}
	if (!visible)
		m_disasmAnchor = pc;
	address = m_disasmAnchor;
	c.PushClip(body);
	for (int i = 0; i < lines; ++i) {
		const emu::DisassembledLine line = debug->Disassemble(address);
		const sdl3::FRect row{body.x, body.y + float(i) * lh, body.w, lh};
		const bool breakpoint = m_breakpoints.contains(address);
		const auto it = u.Interact(u.MakeId(std::format("dis{}", i)), row);
		if (address == pc)
			c.FillRect(row, sdl3::WithAlpha(th.accent, 70));
		else if (it.hovered)
			c.FillRect(row, th.widgetHover);
		if (breakpoint)
			c.FillCircle(row.x + 8, row.y + lh * 0.5f, 5, th.danger);
		if (address == pc)
			c.Text(row.x + 16, row.y + 1, "→", th.warning, 13, sdl3::CanvasFont::Mono);
		const bool wide = debug->ProgramSpace().AddressMask() > 0xFFFF;
		std::string bytes;
		if (wide) { // processeur 32 bits : le mot d'instruction (petit-boutiste), 2 ou 4 octets
			uint32_t word = 0;
			for (uint32_t b = 0; b < line.size && b < 4; ++b)
				word |= uint32_t(debug->ProgramSpace().Read8(address + b)) << (8 * b);
			bytes = line.size >= 4 ? std::format("{:08X}", word) : std::format("{:04X}", word);
		} else {
			for (uint32_t b = 0; b < line.size && b < 4; ++b)
				bytes += std::format("{:02X}", debug->ProgramSpace().Read8(address + b));
		}
		const float charW = c.MeasureText("0", 13, sdl3::CanvasFont::Mono);
		float x = row.x + 34;
		c.Text(x, row.y + 2, wide ? std::format("{:08X}", address) : std::format("{:04X}", address), kAddress, 13, sdl3::CanvasFont::Mono);
		x += charW * (wide ? 10.f : 6.f);
		c.Text(x, row.y + 2, bytes, th.textDim, 13, sdl3::CanvasFont::Mono);
		x += charW * 10.f;
		c.Text(x, row.y + 2, line.mnemonic, kMnemonic, 13, sdl3::CanvasFont::Mono);
		x += charW * 7.f;
		c.Text(x, row.y + 2, line.operands, th.text, 13, sdl3::CanvasFont::Mono);
		if (it.leftClicked) { // clic : pose ou retire un point d'arrêt
			if (breakpoint)
				m_breakpoints.erase(address);
			else
				m_breakpoints.insert(address);
		}
		if (it.hovered)
			u.Tooltip(breakpoint ? "Retirer le point d'arrêt" : "Poser un point d'arrêt");
		address += line.size;
	}
	c.PopClip();
}

void App::UpdateTileTexture() {
	// Tuiles de la mémoire vidéo de la Game Boy : 384 tuiles de 8 × 8 par banque (16 × 24).
	auto *board = m_session->Machine().Find<gbc::Board>("board");
	if (!board)
		return;
	constexpr int kW = 128, kH = 192;
	if (!m_tileTexture) {
		auto texture = sdl3::Texture::Create(*m_renderer, kW, kH, sdl3::PixelFormat::RGBA32);
		if (texture.IsError())
			return;
		m_tileTexture.emplace(std::move(texture.Value()));
		m_tileTexture->SetScaleMode(sdl3::ScaleMode::NEAREST);
	}
	static constexpr uint32_t kShades[4] = {emu::Rgb(0xE0, 0xF8, 0xD0), emu::Rgb(0x88, 0xC0, 0x70), emu::Rgb(0x34, 0x68, 0x56),
											emu::Rgb(0x08, 0x18, 0x20)};
	const std::span<const uint8_t> vram = board->Video().Vram(m_tileBank);
	std::vector<uint32_t> pixels(size_t(kW) * kH);
	for (int tile = 0; tile < 384; ++tile)
		for (int row = 0; row < 8; ++row) {
			const uint8_t lo = vram[size_t(tile * 16 + row * 2)], hi = vram[size_t(tile * 16 + row * 2 + 1)];
			for (int col = 0; col < 8; ++col) {
				const int index = ((lo >> (7 - col)) & 1) | (((hi >> (7 - col)) & 1) << 1);
				pixels[size_t((tile / 16) * 8 + row) * kW + size_t((tile % 16) * 8 + col)] = kShades[index];
			}
		}
	m_tileTexture->Update(pixels.data(), kW * 4);
}

void App::DrawVideoPanel(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	sdl3::FRect body = Panel(area, std::format("TUILES DE LA MÉMOIRE VIDÉO (banque {})", m_tileBank));
	auto *board = m_session->Machine().Find<gbc::Board>("board");
	// À droite : l'écran de la machine en petit, le choix de banque et l'état de l'unité vidéo.
	sdl3::FRect side = ui::CutRight(body, std::min(body.w * 0.45f, 190.f), 10);
	const auto [viewW, viewH] = ScreenViewSize();
	const sdl3::FRect preview = ui::CutTop(side, std::min(side.h * 0.5f, side.w * viewH / viewW), 8);
	c.FillRect(preview, sdl3::Rgba(0, 0, 0));
	DrawScreen(preview);
	if (!board) {
		c.Text(body.x, body.y, "Pas de visionneuse pour ce système.", th.textDim, 13);
		return;
	}
	UpdateTileTexture();
	if (m_tileTexture) {
		const float scale = std::max(1.f, std::floor(std::min(body.w / 128.f, body.h / 192.f)));
		c.Image(*m_tileTexture, ui::Place(body, 128 * scale, 192 * scale, ui::Anchor::TopLeft));
	}
	const sdl3::FRect bank = ui::CutTop(side, 26, 6);
	const ui::FlexItem two[] = {{0, 1}, {0, 1}};
	const auto banks = ui::Row(bank, two, 6);
	if (u.ButtonAt(banks[0], "Banque 0", true, m_tileBank == 0))
		m_tileBank = 0;
	if (u.ButtonAt(banks[1], "Banque 1", board->Color(), m_tileBank == 1))
		m_tileBank = 1;
	const gbc::Ppu &ppu = board->Video();
	float y = side.y;
	for (const std::string &line : {std::format("Ligne (LY) : {}", ppu.Line()), std::format("Mode : {}", ppu.Mode()),
									std::format("LCDC : {:02X}", ppu.Read(0xFF40)), std::format("STAT : {:02X}", ppu.Read(0xFF41)),
									std::format("Console : {}", board->Color() ? "couleur" : "monochrome"),
									std::format("Vitesse : {}", board->DoubleSpeed() ? "double" : "normale")}) {
		if (y + 18 > side.Bottom())
			break;
		c.Text(side.x, y, line, th.textDim, 12, sdl3::CanvasFont::Mono);
		y += 18;
	}
}

void App::DrawMemoryPanel(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	ui::Context &u = *m_ui;
	const ui::Theme &th = u.Style();
	sdl3::FRect body = Panel(area, "MÉMOIRE");
	emu::Debuggable *debug = m_session->Machine().Debugger();
	if (!debug)
		return;
	const emu::AddressSpace &bus = debug->ProgramSpace();
	// Adresse de départ : champ hexadécimal ou molette.
	sdl3::FRect head = ui::CutTop(body, 28, 6);
	c.Text(head.x, head.y + 6, "Adresse :", th.textDim, 13);
	bool submitted = false;
	const bool wide = bus.AddressMask() > 0xFFFF;
	const auto hex = [wide](uint32_t value) { return wide ? std::format("{:08X}", value) : std::format("{:04X}", value); };
	u.TextInputAt({head.x + 76, head.y, wide ? 100.f : 70.f, 26}, "##memaddr", m_memoryAddressText, &submitted, "C000");
	if (submitted) {
		m_memoryAddress = uint32_t(std::strtoul(m_memoryAddressText.c_str(), nullptr, 16)) & bus.AddressMask() & ~0xFu;
		m_memoryPrevious.clear();
	}
	float bx = head.x + (wide ? 188.f : 158.f);
	for (const auto &[label, address] : debug->MemoryRegions()) {
		const float w = c.MeasureText(label, 12) + 14;
		if (bx + w > head.Right())
			break;
		if (u.ButtonAt({bx, head.y, w, 26}, label + "##region")) {
			m_memoryAddress = address;
			m_memoryAddressText = hex(address);
			m_memoryPrevious.clear();
		}
		bx += w + 4;
	}
	if (u.MouseIn(body) && u.WheelY() != 0.f) {
		const int64_t next = int64_t(m_memoryAddress) - int64_t(u.WheelY() * 48.f);
		m_memoryAddress = uint32_t(std::clamp<int64_t>(next, 0, int64_t(bus.AddressMask()) - 0xF)) & ~0xFu;
		m_memoryAddressText = hex(m_memoryAddress);
		m_memoryPrevious.clear();
	}
	const float lh = 19;
	const int rows = std::max(1, int(body.h / lh));
	std::vector<uint8_t> current(size_t(rows) * 16);
	for (size_t i = 0; i < current.size(); ++i)
		current[i] = bus.Read8(m_memoryAddress + uint32_t(i));
	const bool compare = m_memoryPrevious.size() == current.size();
	const float charW = c.MeasureText("0", 13, sdl3::CanvasFont::Mono);
	c.PushClip(body);
	for (int r = 0; r < rows; ++r) {
		const float y = body.y + float(r) * lh;
		const float offset = wide ? 4.f : 0.f; // colonne d'adresse plus large en 32 bits
		c.Text(body.x, y, hex(m_memoryAddress + uint32_t(r * 16)) + ":", kAddress, 13, sdl3::CanvasFont::Mono);
		std::string ascii;
		for (int b = 0; b < 16; ++b) {
			const size_t i = size_t(r * 16 + b);
			const uint8_t value = current[i];
			// Octet modifié depuis l'image précédente : en rouge.
			const bool changed = compare && m_memoryPrevious[i] != value;
			const float x = body.x + charW * (7.f + offset + float(b) * 3.f + (b >= 8 ? 1.f : 0.f));
			c.Text(x, y, std::format("{:02X}", value), changed ? th.danger : (value ? th.text : th.textDim), 13, sdl3::CanvasFont::Mono);
			ascii.push_back(value >= 0x20 && value < 0x7F ? char(value) : '.');
		}
		c.Text(body.x + charW * (58.f + offset), y, ascii, th.textDim, 13, sdl3::CanvasFont::Mono);
	}
	c.PopClip();
	m_memoryPrevious = std::move(current);
}

void App::DrawConsole(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	const ui::Theme &th = m_ui->Style();
	const sdl3::FRect body = Panel(area, "CONSOLE");
	const float lh = 18;
	const int rows = std::max(1, int(body.h / lh));
	const size_t first = m_log.size() > size_t(rows) ? m_log.size() - size_t(rows) : 0;
	c.PushClip(body);
	float y = body.y;
	for (size_t i = first; i < m_log.size(); ++i) {
		const auto &[level, text] = m_log[i];
		const sdl3::Color color = level == LogLevel::Error ? th.danger : (level == LogLevel::Warn ? th.warning : th.success);
		c.Text(body.x, y, text, color, 12, sdl3::CanvasFont::Mono);
		y += lh;
	}
	c.PopClip();
}

void App::DrawPerformance(const sdl3::FRect &area) {
	sdl3::Canvas &c = m_canvas;
	const ui::Theme &th = m_ui->Style();
	sdl3::FRect body = Panel(area, "PERFORMANCES");
	const sdl3::FRect summary = ui::CutBottom(body, 40, 6);
	// Courbes : durée d'une image de l'interface (bleu) et temps d'émulation (vert), en ms.
	auto plot = [&](const sdl3::FRect &r, const std::vector<float> &values, sdl3::Color color, std::string_view label, float top) {
		c.FillRoundRect(r, 4, sdl3::Rgba(10, 13, 22));
		if (values.size() >= 2) {
			std::vector<float> points(values.size() * 2);
			for (size_t i = 0; i < values.size(); ++i) {
				points[i * 2] = r.x + r.w * float(i) / float(values.size() - 1);
				points[i * 2 + 1] = r.Bottom() - 2 - (r.h - 4) * std::clamp(values[i] / top, 0.f, 1.f);
			}
			c.Polyline(points.data(), values.size(), color, 1.5f);
		}
		c.Text(r.x + 6, r.y + 4, label, th.textDim, 11);
		const std::string last = values.empty() ? std::string() : std::format("{:.2f} ms", values.back());
		c.Text(r.Right() - 6 - c.MeasureText(last, 11), r.y + 4, last, color, 11);
	};
	const ui::FlexItem two[] = {{0, 1}, {0, 1}};
	const auto graphs = ui::Column(body, two, 8);
	plot(graphs[0], m_frameMs, th.info, "Image de l'interface (échelle 33 ms)", 33.f);
	plot(graphs[1], m_emulationMs, th.success, "Émulation par image (échelle 8 ms)", 8.f);
	const double rate = m_session->Machine().FrameRate();
	c.Text(summary.x, summary.y + 2, std::format("{:.1f} img/s (machine : {:.2f} Hz)", m_fps, rate), th.text, 12, sdl3::CanvasFont::Mono);
	c.Text(summary.x, summary.y + 20, std::format("Charge d'émulation : {:.1f} %", double(m_lastEmulationMs) * rate / 10.0), th.textDim, 12,
		   sdl3::CanvasFont::Mono);
}

} // namespace retro
