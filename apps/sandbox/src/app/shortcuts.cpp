// shortcuts.cpp — Raccourcis clavier configurables du sandbox : enregistrement des actions
// et de leurs défauts, dispatch générique (au lieu d'une chaîne de `if` codée en dur), et
// modal « Réglages → Raccourcis clavier… » (liste, recherche, capture, détection de conflit,
// réinitialisation). La source de vérité est le `ui::KeyMap` persisté dans `settings.cpp`.
#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

using sdl3::keyboard::Key;

/// Touche d'un caractère imprimable (a–z, 0–9, espace) ; UNKNOWN sinon.
Key KeyForChar(char c) {
	if (c >= 'a' && c <= 'z')
		return static_cast<Key>(static_cast<uint32_t>(Key::A) + static_cast<uint32_t>(c - 'a'));
	if (c >= '0' && c <= '9')
		return static_cast<Key>(static_cast<uint32_t>(Key::_0) + static_cast<uint32_t>(c - '0'));
	if (c == ' ')
		return Key::SPACE;
	return Key::UNKNOWN;
}

/// Touche de modification (Ctrl / Maj / Alt / Super) : jamais une action en soi.
bool IsModifierKey(Key key) {
	switch (key) {
		case Key::LCTRL:
		case Key::RCTRL:
		case Key::LSHIFT:
		case Key::RSHIFT:
		case Key::LALT:
		case Key::RALT:
		case Key::LGUI:
		case Key::RGUI: return true;
		default: return false;
	}
}

/// Touche imprimable (produit du texte) : traitée par la boucle de texte, pas par les chords.
bool IsPrintableKey(Key key) {
	const uint32_t k = static_cast<uint32_t>(key);
	return k >= 32 && k <= 126;
}

bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) noexcept {
	if (needle.empty())
		return true;
	if (needle.size() > haystack.size())
		return false;
	for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
		bool ok = true;
		for (size_t j = 0; j < needle.size(); ++j)
			if (std::tolower(static_cast<unsigned char>(haystack[i + j])) != std::tolower(static_cast<unsigned char>(needle[j]))) {
				ok = false;
				break;
			}
		if (ok)
			return true;
	}
	return false;
}

} // namespace

const char *ToolActionId(modeling::ToolKind k) noexcept {
	switch (k) {
		case modeling::ToolKind::SELECT: return "outil.selection";
		case modeling::ToolKind::LINE: return "outil.ligne";
		case modeling::ToolKind::RECTANGLE: return "outil.rectangle";
		case modeling::ToolKind::CIRCLE: return "outil.cercle";
		case modeling::ToolKind::POLYGON: return "outil.polygone";
		case modeling::ToolKind::PUSH_PULL: return "outil.pousser_tirer";
		case modeling::ToolKind::OFFSET: return "outil.decalage";
		case modeling::ToolKind::FOLLOW_ME: return "outil.suivez_moi";
		case modeling::ToolKind::MOVE: return "outil.deplacer";
		case modeling::ToolKind::ROTATE: return "outil.rotation";
		case modeling::ToolKind::SCALE: return "outil.echelle";
		case modeling::ToolKind::ERASER: return "outil.gomme";
		case modeling::ToolKind::PAINT: return "outil.peinture";
		case modeling::ToolKind::TAPE: return "outil.metre";
	}
	return "outil.inconnu";
}

void SandboxApp::RegisterShortcuts() {
	m_keys.Add("fichier.nouveau", {Key::N, true});
	m_keys.Add("fichier.ouvrir", {Key::O, true});
	m_keys.Add("fichier.enregistrer", {Key::S, true});
	m_keys.Add("fichier.enregistrer_sous", {Key::S, true, true});
	m_keys.Add("fichier.quitter", {Key::Q, true});
	m_keys.Add("edition.annuler", {Key::Z, true});
	m_keys.Add("edition.retablir", {Key::Y, true});
	m_keys.Add("edition.tout_selectionner", {Key::A, true});
	m_keys.Add("edition.effacer", {Key::DELETE});
	m_keys.Add("edition.grouper", {Key::G, true});
	m_keys.Add("edition.dupliquer", {Key::D, true});
	m_keys.Add("edition.echap", {Key::ESCAPE});
	m_keys.Add("camera.zoom_etendu", {Key::Z, false, true});
	m_keys.Add("jeu.jouer", {Key::F5});
	m_keys.Add("nav.orbite", {Key::O});
	m_keys.Add("nav.panoramique", {Key::H});
	m_keys.Add("vue.iso", {Key::_1});
	m_keys.Add("vue.dessus", {Key::_2});
	m_keys.Add("vue.face", {Key::_3});
	m_keys.Add("vue.droite", {Key::_4});
	m_keys.Add("vue.arriere", {Key::_5});
	m_keys.Add("vue.gauche", {Key::_6});
	for (modeling::ToolKind k : modeling::ALL_TOOLS) {
		const char sc = modeling::ToolShortcut(k);
		const char lc = sc ? char(std::tolower(static_cast<unsigned char>(sc))) : '\0';
		const Key key = lc ? KeyForChar(lc) : Key::UNKNOWN;
		if (key != Key::UNKNOWN)
			m_keys.Add(ToolActionId(k), {key});
	}
}

void SandboxApp::DispatchAction(std::string_view action) {
	Editor &ed = m_editor;
	if (action == "fichier.nouveau")
		NewDocument();
	else if (action == "fichier.ouvrir")
		OpenDocument();
	else if (action == "fichier.enregistrer") {
		if (m_workspace == "script" && FileActive())
			(void)SaveOpenFile(); // fichier ouvert dans l'éditeur du centre
		else
			SaveDocument(false);
	} else if (action == "fichier.enregistrer_sous")
		SaveDocument(true);
	else if (action == "fichier.quitter")
		RequestQuit();
	else if (action == "edition.annuler")
		(void)ed.Undo();
	else if (action == "edition.retablir")
		(void)ed.Redo();
	else if (action == "edition.tout_selectionner")
		ed.SelectAll();
	else if (action == "edition.effacer")
		(void)ed.DeleteSelection();
	else if (action == "edition.grouper")
		(void)ed.GroupSelection();
	else if (action == "edition.dupliquer")
		(void)ed.DuplicateSelection();
	else if (action == "edition.echap") {
		m_navTool = Nav::None;
		ed.Escape();
	} else if (action == "camera.zoom_etendu")
		ed.camera.ZoomExtents(ed.Doc().Bounds(), m_viewport.Aspect());
	else if (action == "jeu.jouer")
		StartGame();
	else if (action == "nav.orbite")
		m_navTool = Nav::Orbit;
	else if (action == "nav.panoramique")
		m_navTool = Nav::Pan;
	else if (action == "vue.iso")
		SetView(StandardView::Iso);
	else if (action == "vue.dessus")
		SetView(StandardView::Top);
	else if (action == "vue.face")
		SetView(StandardView::Front);
	else if (action == "vue.droite")
		SetView(StandardView::Right);
	else if (action == "vue.arriere")
		SetView(StandardView::Back);
	else if (action == "vue.gauche")
		SetView(StandardView::Left);
	else if (action.starts_with("outil."))
		for (modeling::ToolKind k : modeling::ALL_TOOLS)
			if (action == ToolActionId(k)) {
				ed.SetTool(k);
				m_navTool = Nav::None;
				break;
			}
}

void SandboxApp::DispatchLetter(char c) {
	const Key key = KeyForChar(c);
	if (key == Key::UNKNOWN)
		return;
	const std::string_view action = m_keys.Match(key, false, false, false);
	if (action.empty())
		return;
	// Les vues (chiffres 1–6) ne s'appliquent qu'en outil Sélection (sinon les chiffres
	// alimentent la boîte de mesures, ce que TypeText a déjà traité en amont).
	if (action.starts_with("vue.") && m_editor.ToolKind() != modeling::ToolKind::SELECT)
		return;
	DispatchAction(action);
}

std::string SandboxApp::ShortcutLabel(std::string_view action) const {
	if (const ui::KeyMap::Binding *b = m_keys.Find(action))
		return b->chord.ToString();
	return {};
}

void SandboxApp::HandleKeyboard() {
	ui::Context &u = Ui();
	if (u.WantsKeyboard() || u.MenuOpen())
		return;
	if (ui::Widget menu = m_ui->Find("menu"); menu && menu->openMenu >= 0)
		return;
	Editor &ed = m_editor;
	// Touches de saisie de la boîte de mesures (intrinsèques au texte, non configurables).
	if (u.KeycodePressed(SDLK_RETURN) || u.KeycodePressed(SDLK_KP_ENTER))
		ed.SubmitMeasure();
	if (u.KeycodePressed(SDLK_BACKSPACE))
		ed.Backspace();
	// Raccourcis à modificateurs ou à touches spéciales, via le KeyMap.
	for (const SDL_Keycode kc : Events().PressedKeycodes()) {
		const Key key = static_cast<Key>(kc);
		if (IsModifierKey(key))
			continue;
		if (!u.Ctrl() && !u.Shift() && !u.Alt() && IsPrintableKey(key))
			continue; // lettre/chiffre sans modificateur : passe par la boucle de texte
		if (const std::string_view action = m_keys.Match(key, u.Ctrl(), u.Shift(), u.Alt()); !action.empty()) {
			DispatchAction(action);
			return;
		}
	}
	// Texte : boîte de mesures, puis raccourcis de lettres (outils, navigation, vues).
	const std::string_view text = u.TextInput();
	if (text.empty() || ed.TypeText(text))
		return;
	for (const char ch : text)
		DispatchLetter(char(std::tolower(static_cast<unsigned char>(ch))));
}

void SandboxApp::HandleShortcutCapture() {
	if (m_shortcutCapture.empty())
		return;
	ui::Context &u = Ui();
	if (u.HasFocus("##shortcut-search"))
		return; // l'utilisateur tape dans le champ de recherche, pas une nouvelle combinaison
	if (u.KeycodePressed(SDLK_ESCAPE)) { // Échap annule la capture
		m_shortcutCapture.clear();
		return;
	}
	for (const SDL_Keycode kc : Events().PressedKeycodes()) {
		const Key key = static_cast<Key>(kc);
		if (IsModifierKey(key))
			continue;
		const ui::KeyChord chord{key, u.Ctrl(), u.Shift(), u.Alt()};
		if (!chord.Valid())
			continue;
		if (const std::string_view conflict = m_keys.Conflict(chord, m_shortcutCapture); !conflict.empty()) {
			Ui().Notify(std::format("Conflit avec « {} » — choisissez une autre combinaison", conflict), Theme().danger, 4.f);
			m_shortcutCapture.clear();
		} else {
			(void)m_keys.Set(m_shortcutCapture, chord);
			SaveSettings();
			m_shortcutCapture.clear();
		}
		return;
	}
}

void SandboxApp::DrawShortcutsModal(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	c.FillRect(area, sdl3::Rgba(0, 0, 0, 150));

	const float w = std::min(area.w * 0.85f, 780.f);
	const float h = std::min(area.h * 0.85f, 620.f);
	const sdl3::FRect p{area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h};
	c.FillRoundRect(p, 12.f, th.panel);
	c.StrokeRoundRect(p, 12.f, th.panelBorder);

	// Titre et fermeture.
	c.Text(p.x + 16.f, p.y + 10.f, "Raccourcis clavier", th.text, 18.f, sdl3::CanvasFont::Bold);
	c.Text(p.x + 16.f, p.y + 32.f, "Cliquez « Modifier » puis appuyez sur la nouvelle combinaison ; Échap annule.", th.textDim, 12.f);
	if (u.IconButton({p.Right() - 34.f, p.y + 8.f, 24.f, 24.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::CLOSE), "Fermer"))
		m_showShortcuts = false;

	// Recherche.
	u.TextInputAt({p.x + 16.f, p.y + 52.f, p.w - 32.f, 28.f}, "##shortcut-search", m_shortcutFilter, nullptr, "Rechercher une action…");

	// Liste (filtrée, insensible à la casse sur l'action et la combinaison).
	const sdl3::FRect list{p.x + 12.f, p.y + 88.f, p.w - 24.f, p.h - 88.f - 50.f};
	u.BeginScroll("##shortcut-list", list, {.axes = ui::ScrollAxes::Vertical});
	for (const ui::KeyMap::Binding &b : m_keys.Bindings()) {
		const std::string chord = b.chord.ToString();
		if (!m_shortcutFilter.empty() && !ContainsIgnoreCase(b.action, m_shortcutFilter) && !ContainsIgnoreCase(chord, m_shortcutFilter))
			continue;
		const sdl3::FRect r = u.Next(30);
		const bool capturing = m_shortcutCapture == b.action;
		c.Text(r.x, r.y + 6.f, b.action, capturing ? th.accent : th.text, 14.f);
		c.Text(r.x + 280.f, r.y + 7.f, capturing ? "Appuyez sur une touche…" : chord.c_str(), capturing ? th.warning : th.textDim, 13.f);
		if (capturing) {
			if (u.ButtonAt({r.Right() - 80.f, r.y + 2.f, 72.f, 26.f}, "Annuler"))
				m_shortcutCapture.clear();
		} else {
			if (u.ButtonAt({r.Right() - 168.f, r.y + 2.f, 76.f, 26.f}, "Modifier"))
				m_shortcutCapture = b.action;
			if (u.ButtonAt({r.Right() - 84.f, r.y + 2.f, 76.f, 26.f}, "Défaut", b.chord != b.defaultChord)) {
				m_keys.Reset(b.action);
				SaveSettings();
			}
		}
	}
	u.EndScroll();

	// Pied.
	if (u.ButtonAt({p.x + 16.f, p.Bottom() - 42.f, 160.f, 30.f}, "Tout réinitialiser")) {
		m_keys.ResetAll();
		SaveSettings();
	}
	if (u.ButtonAt({p.Right() - 96.f, p.Bottom() - 42.f, 80.f, 30.f}, "Fermer"))
		m_showShortcuts = false;
}

} // namespace sandbox
