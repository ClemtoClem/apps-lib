// Mode Jeu du sandbox : visite à la première personne (démarrage, commandes, affichage tête
// haute) et onglet « Jeu » du plateau (comportement des groupes, réglages de la visite).
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include <script/script.hpp>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

sdl3::Color ToGfx(const modeling::Color &c) {
	return sdl3::Rgba(uint8_t(std::clamp(c.r, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.g, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.b, 0.f, 1.f) * 255.f));
}

modeling::Color ToModel(sdl3::Color c) { return {float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f, 1.f}; }

/// Exemples de scripts proposés dans l'onglet Jeu.
struct Snippet {
	std::string_view name, code;
};
// Exemples insérables : une classe complète par comportement (à placer dans un script vide).
constexpr std::array<Snippet, 7> kSnippets = {{
	{"Tourner sur place", "class Toupie extends Behaviour {\n\tfn OnUpdate(dt) { this.rotate(45 * dt) }\n}\n"},
	{"Porte (E pour ouvrir)",
	 "# Quart de tour autour de l'origine du groupe (la charnière).\nclass Porte extends Behaviour, AudioSource {\n\tlet ouverte = false\n\tlet angle = 0\n"
	 "\tfn OnUse() {\n\t\tthis.ouverte = not this.ouverte\n\t\tthis.play()\n\t}\n"
	 "\tfn OnUpdate(dt) {\n\t\tlet cible = 0\n\t\tif (this.ouverte) { cible = 90 }\n\t\tlet pas = math.min(math.max(cible - this.angle, -120 * dt), 120 * dt)\n"
	 "\t\tthis.angle += pas\n\t\tthis.turn(pas)\n\t}\n}\n"},
	{"Flotter", "class Flotteur extends Behaviour {\n\tlet t = 0\n\tfn OnUpdate(dt) {\n\t\tthis.t += dt\n\t\tthis.move(0, math.cos(this.t * 2) * dt * 0.4, 0)\n\t}\n}\n"},
	{"Interrupteur (E)", "class Lampe extends Behaviour, Light3D {\n\tlet allumee = true\n\tfn OnUse() {\n\t\tthis.allumee = not this.allumee\n\t\tthis.light(this.allumee)\n\t}\n}\n"},
	{"Objet à pousser (E)", "class Caisse extends Behaviour, PhysicsBody {\n\tfn OnUse() { this.push(0, 4, 0) }\n}\n"},
	{"Message au contact", "class Accueil extends Behaviour {\n\tfn OnTouch(qui) {\n\t\tif (qui == \"joueur\") { game.say(\"Bienvenue !\") }\n\t}\n}\n"},
	{"Script de scène", "class MaScene extends Scene {\n\tfn OnStart() { this.say(\"C'est parti !\") }\n\tfn OnUpdate(dt) {}\n\tfn OnTrigger(zone, objet, evenement) { this.say(objet .. \" → \" .. evenement) }\n}\n"},
}};

/// Touches transmises aux scripts (`input.key`) : position physique (ZQSD en AZERTY = WASD).
constexpr std::array<std::pair<SDL_Scancode, std::string_view>, 14> kScriptKeys = {{
	{SDL_SCANCODE_W, "w"}, {SDL_SCANCODE_A, "a"}, {SDL_SCANCODE_S, "s"}, {SDL_SCANCODE_D, "d"}, {SDL_SCANCODE_E, "e"}, {SDL_SCANCODE_F, "f"},
	{SDL_SCANCODE_SPACE, "space"}, {SDL_SCANCODE_LSHIFT, "shift"}, {SDL_SCANCODE_UP, "up"}, {SDL_SCANCODE_DOWN, "down"}, {SDL_SCANCODE_LEFT, "left"},
	{SDL_SCANCODE_RIGHT, "right"}, {SDL_SCANCODE_1, "1"}, {SDL_SCANCODE_2, "2"},
}};

} // namespace

void SandboxApp::StartGame() {
	if (m_game)
		return;
	if (!m_sound.IsOpen())
		(void)m_sound.Open(); // sans sortie audio : visite muette
	// Point de départ : une scène nommée « Départ… », sinon la caméra de l'éditeur.
	sdl3::Camera3D cam = m_editor.camera.Camera();
	math::FVector3 eye = cam.position, target = cam.target;
	for (const SceneView &v : m_editor.Doc().Views())
		if (std::string_view(v.name).starts_with("Départ")) {
			eye = v.eye;
			target = v.target;
			break;
		}
	m_editor.CloseAllGroups();
	m_gameErrors = 0;
	game::GameMessage start;
	start.text = "── Visite lancée ──";
	Log(std::move(start));
	m_scriptUis.clear();
	m_gameUi = std::make_unique<ui::Ui>(Canvas(), Events());
	m_gameUi->SetTheme(Theme()); // cohérent avec l'interface principale (même accent)
	m_game = std::make_unique<game::GameSession>(m_editor.Doc(), eye, target, &m_sound, ModuleLoader(), [this](script::Interpreter &interp, EntityId) {
		m_scriptUis.push_back(std::make_unique<ui::ScriptUi>(interp, *m_gameUi)); // module `ui` des scripts
	});
	m_lookX = m_lookY = 0.f;
	m_useQueued = false;
	m_ignoreLook = 2; // le passage en mode relatif produit un saut de souris
	Ui().ClearFocus();
	Window().SetRelativeMouseMode(true); // la souris tourne la tête
}

void SandboxApp::StopGame() {
	if (!m_game)
		return;
	for (game::GameMessage &m : m_game->TakeJournal())
		Log(std::move(m));
	m_scriptUis.clear(); // avant les interpréteurs qu'elles référencent
	m_gameUi.reset();
	m_game.reset(); // le document n'a pas été touché : retour exact à l'édition
	if (m_gameErrors > 0) // comme Godot : la sortie s'ouvre sur les erreurs de la partie
		ShowPanel("console");
	m_sound.StopAll();
	Window().SetRelativeMouseMode(false);
}

void SandboxApp::HandleGameKeys() {
	if (Ui().KeycodePressed(SDLK_ESCAPE) || Ui().KeycodePressed(SDLK_F5)) {
		StopGame();
		return;
	}
	if (Ui().KeycodePressed(SDLK_F7)) // comme l'ancienne version : libérer / reprendre la souris
		m_game->SetMouseFree(!m_game->MouseFree());
	if (Window().RelativeMouseMode() == m_game->MouseFree()) { // souris libre : menus des scripts cliquables
		Window().SetRelativeMouseMode(!m_game->MouseFree());
		m_ignoreLook = 2;
	}
	if (m_game->MouseFree())
		return; // pas de regard ni d'action à la souris
	if (m_ignoreLook > 0) {
		--m_ignoreLook;
		return;
	}
	// Bornes : un saut isolé (fenêtre reprise, curseur replacé) ne fait pas pivoter la vue.
	m_lookX += std::clamp(Events().MouseRelX(), -400.f, 400.f);
	m_lookY += std::clamp(Events().MouseRelY(), -400.f, 400.f);
	if (Ui().MousePressed())
		m_useQueued = true;
}

void SandboxApp::OnFixedUpdate(double step) {
	if (!m_game)
		return;
	const sdl3::EventsManager &ev = Events();
	const auto down = [&](SDL_Scancode s) { return ev[s].IsDown(); };
	game::GameInput input;
	input.forward = (down(SDL_SCANCODE_W) || down(SDL_SCANCODE_UP) ? 1.f : 0.f) - (down(SDL_SCANCODE_S) || down(SDL_SCANCODE_DOWN) ? 1.f : 0.f);
	input.strafe = (down(SDL_SCANCODE_D) || down(SDL_SCANCODE_RIGHT) ? 1.f : 0.f) - (down(SDL_SCANCODE_A) || down(SDL_SCANCODE_LEFT) ? 1.f : 0.f);
	input.jump = down(SDL_SCANCODE_SPACE);
	input.run = down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
	input.use = down(SDL_SCANCODE_E) || m_useQueued;
	input.lookX = m_lookX;
	input.lookY = m_lookY;
	for (const auto &[scan, name] : kScriptKeys)
		if (down(scan))
			input.keys.insert(std::string(name));
	m_lookX = m_lookY = 0.f; // la souris de l'image n'est appliquée qu'au premier pas
	m_useQueued = false;
	m_game->Update(float(step), input);
}

void SandboxApp::DrawGameHud(const sdl3::FRect &view) {
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	DrawLayer2D(m_game->Overlay(), m_game->Drawn(), view); // calque 2D du document, puis draw2d.*
	// Interfaces construites par les scripts (module `ui`) : champs → nœuds, dessin, rappels.
	if (m_gameUi) {
		for (auto &s : m_scriptUis)
			s->Sync();
		m_gameUi->Present(0.016f, view);
		for (auto &s : m_scriptUis) {
			s->Collect();
			for (const std::string &error : s->Errors()) {
				game::GameMessage m;
				m.text = "Interface : " + error;
				m.error = true;
				Log(std::move(m));
			}
			s->Errors().clear();
		}
	}
	c.PushClip(view);
	// Viseur.
	const float cx = view.x + view.w * 0.5f, cy = view.y + view.h * 0.5f;
	const sdl3::Color ink = sdl3::Rgba(255, 255, 255, 220), shade = sdl3::Rgba(0, 0, 0, 120);
	for (const auto &[dx, dy] : {std::pair{1.f, 1.f}, std::pair{0.f, 0.f}}) {
		const sdl3::Color col = dx > 0.f ? shade : ink;
		c.FillRect({cx - 9.f + dx, cy - 1.f + dy, 6.f, 2.f}, col);
		c.FillRect({cx + 3.f + dx, cy - 1.f + dy, 6.f, 2.f}, col);
		c.FillRect({cx - 1.f + dx, cy - 9.f + dy, 2.f, 6.f}, col);
		c.FillRect({cx - 1.f + dx, cy + 3.f + dy, 2.f, 6.f}, col);
	}
	// Objet visé.
	if (const EntityId aimed = m_game->Aimed()) {
		const std::string label = std::format("E ou clic : utiliser « {} »", m_game->NameOf(aimed));
		const float w = c.MeasureText(label, 13.f) + 20.f;
		const sdl3::FRect pill{cx - w * 0.5f, cy + 22.f, w, 24.f};
		c.FillRoundRect(pill, 12.f, sdl3::Rgba(20, 22, 30, 190));
		c.TextCentered(pill, label, sdl3::Rgba(240, 242, 248), 13.f);
	}
	// Aide et état.
	const std::string help = "ZQSD / flèches : marcher · Maj : courir · Espace : sauter · E / clic : utiliser · Échap : retour à l'édition";
	const sdl3::FRect top{view.x + 12.f, view.y + 12.f, c.MeasureText(help, 12.f) + 20.f, 24.f};
	c.FillRoundRect(top, 6.f, sdl3::Rgba(20, 22, 30, 170));
	c.Text(top.x + 10.f, top.y + 5.f, help, sdl3::Rgba(230, 232, 240), 12.f);
	// Messages des scripts (s'effacent en 6 s).
	float y = view.Bottom() - 16.f;
	const auto &messages = m_game->Messages();
	for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
		const double age = m_game->Time() - it->time;
		if (age > 6.0)
			continue;
		const uint8_t alpha = uint8_t(std::clamp(1.0 - (age - 4.5) / 1.5, 0.0, 1.0) * 220.0);
		const std::string text = c.Ellipsize(it->text, view.w * 0.6f, 14.f);
		const float w = c.MeasureText(text, 14.f) + 20.f;
		y -= 30.f;
		c.FillRoundRect({view.x + 12.f, y, w, 26.f}, 6.f, sdl3::Rgba(20, 22, 30, uint8_t(alpha * 0.85f)));
		c.Text(view.x + 22.f, y + 5.f, text, it->error ? sdl3::WithAlpha(th.danger, alpha) : sdl3::Rgba(240, 242, 248, alpha), 14.f);
	}
	const std::string stats = std::format("{:.0f} i/s · {} triangles de collision · {}", Fps(), m_game->Physics().TriangleCount(),
										  m_sound.IsOpen() ? "son actif" : "muet");
	c.Text(view.Right() - c.MeasureText(stats, 11.f) - 12.f, view.Bottom() - 20.f, stats, sdl3::Rgba(240, 242, 248, 170), 11.f);
	c.PopClip();
}

void SandboxApp::DrawBehavior() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	Entity *e = ed.SelectedEntities().size() == 1 ? doc.Find(ed.SelectedEntities().front()) : nullptr;
	if (!e) {
		// Réglages de la visite.
		u.Section(icon::Play, "Visite");
		u.Wrapped("Parcourez le modèle à la première personne, avec gravité et collisions. Sélectionnez un groupe pour lui donner un "
				  "comportement (objet physique, son, lumière, script). Le document n'est jamais modifié par la partie.",
				  th.textDim, 12.f);
		if (u.PrimaryButton(u.Next(32), "Jouer la visite (F5)"))
			StartGame();
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Réglages");
		GameSettings s = doc.Game();
		u.SliderFloat("Gravité", s.gravity, 0.f, 30.f, "{:.2f} m/s²");
		u.SliderFloat("Marche", s.walkSpeed, 0.5f, 12.f, "{:.1f} m/s");
		u.SliderFloat("Course", s.runSpeed, 1.f, 20.f, "{:.1f} m/s");
		u.SliderFloat("Saut", s.jumpSpeed, 0.f, 12.f, "{:.1f} m/s");
		u.SliderFloat("Yeux", s.eyeHeight, 0.5f, 2.5f, "{:.2f} m");
		u.SliderFloat("Marche franchie", s.stepHeight, 0.f, 0.8f, "{:.2f} m");
		if (s != doc.Game()) {
			ed.Change("Réglages du jeu", u.MouseDown());
			doc.Game() = s;
		}
		u.Wrapped("Départ : la scène nommée « Départ… » s'il y en a une, sinon sous la caméra actuelle.", th.textDim, 12.f);
		DrawScriptSection();
		return;
	}
	Behavior next = e->behavior;
	u.Section(icon::Play, std::format("« {} » en jeu", e->name.c_str()));
	static constexpr std::array<std::string_view, 3> kBodies = {"Solide (décor)", "Dynamique (physique)", "Fantôme (traversable)"};
	int body = int(next.body);
	u.Label("Corps", th.textDim, 13.f);
	if (u.Choice(u.Next(30), "body", body, kBodies))
		next.body = BodyKind(body);
	if (next.body == BodyKind::Dynamic) {
		u.SliderFloat("Masse", next.mass, 0.1f, 200.f, "{:.1f} kg");
		u.SliderFloat("Rebond", next.bounce, 0.f, 1.f);
		u.SliderFloat("Frottement", next.friction, 0.f, 1.f);
	} else {
		u.Wrapped(next.body == BodyKind::Solid ? "Arrête le joueur et les objets (collisions exactes, même déplacé ou tourné par un script : porte, plateau)."
											   : "Zone de déclenchement : le joueur ou un objet dynamique qui y entre appelle OnTrigger(zone, objet, "
												 "évènement) dans le script de la scène, OnTrigger(objet, évènement) dans celui du groupe.",
				  th.textDim, 12.f);
		if (next.body == BodyKind::Ghost) {
			std::string event = next.triggerEvent;
			u.Label("Évènement", th.textDim, 12.f);
			if (u.TextInput("trigger-event", event, nullptr, "nom libre (« ouvrir », « gagné »…)"))
				next.triggerEvent = String(event);
			u.Checkbox("Une seule fois par partie", next.triggerOnce);
		}
	}
	// Son.
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::VOLUME_UP), "Son");
	if (m_soundFiles.empty())
		m_soundFiles = m_sound.Files();
	std::vector<std::string_view> files{"Aucun"};
	int current = 0;
	for (size_t i = 0; i < m_soundFiles.size(); ++i) {
		files.push_back(m_soundFiles[i]);
		if (m_soundFiles[i] == next.sound.c_str())
			current = int(i) + 1;
	}
	if (u.Choice(u.Next(30), "sound", current, files))
		next.sound = current == 0 ? String() : String(m_soundFiles[size_t(current - 1)]);
	if (!next.sound.IsEmpty()) {
		static constexpr std::array<std::string_view, 4> kTriggers = {"Au départ", "Au contact", "À l'usage (E)", "Par script"};
		int trigger = int(next.soundTrigger);
		if (u.Choice(u.Next(30), "trigger", trigger, kTriggers))
			next.soundTrigger = SoundTrigger(trigger);
		u.SliderFloat("Volume", next.volume, 0.f, 2.f);
		u.Row({1.f, 1.f}, 28.f);
		u.Checkbox("En boucle", next.loop);
		u.Checkbox("Spatial", next.spatial);
	}
	// Lumière.
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::LIGHTBULB), "Lumière");
	u.Checkbox("Lampe au centre du groupe", next.light);
	if (next.light) {
		static constexpr std::array<std::string_view, 2> kLights = {"Ponctuelle", "Spot (axe −Y du groupe)"};
		int kind = int(next.lightKind);
		if (u.Choice(u.Next(30), "light-kind", kind, kLights))
			next.lightKind = LightKind(kind);
		if (next.lightKind == LightKind::Spot) {
			u.SliderFloat("Angle", next.spotAngle, 5.f, 85.f, "{:.0f}°");
			u.SliderFloat("Pénombre", next.penumbra, 0.f, 1.f);
		}
		u.SliderFloat("Intensité", next.lightIntensity, 0.f, 60.f, "{:.1f}");
		u.SliderFloat("Portée", next.lightRange, 1.f, 40.f, "{:.1f} m");
		sdl3::Color color = ToGfx(next.lightColor);
		if (u.ColorPicker("light-color", color, 110.f))
			next.lightColor = ToModel(color);
	}
	if (next != e->behavior) {
		ed.Change("Comportement", u.MouseDown());
		e = doc.Find(ed.SelectedEntities().front());
		e->behavior = next;
	}
	DrawScriptSection(); // après la validation de `next` : le script est modifié en place
}

// ── Éditeur de scripts ───────────────────────────────────────────────────────

namespace {

/// Coloration du langage des scripts : mots réservés, fonctions globales et modules du jeu.
const ui::SyntaxRules &ScriptSyntax() {
	static const ui::SyntaxRules rules = [] {
		ui::SyntaxRules r;
		for (const std::string_view k : script::Keywords())
			r.keywords.emplace_back(k);
		const script::Interpreter probe;
		r.builtins = probe.GlobalNames();
		for (const char *module : {"self", "game", "input", "hud", "draw2d", "Behaviour", "PhysicsBody", "Light3D", "AudioSource", "Scene"})
			r.builtins.emplace_back(module);
		return r;
	}();
	return rules;
}

} // namespace

std::pair<String *, String *> SandboxApp::EditedScript(std::string *label) {
	Document &doc = m_editor.Doc();
	if (!m_inspecting && FileActive()) { // onglet Script : fichier ouvert depuis l'explorateur
		OpenFile &f = m_files[size_t(m_activeFile)];
		if (label)
			*label = std::filesystem::path(f.path).filename().string();
		m_noFile.clear();
		return {&f.text, &m_noFile};
	}
	if (m_editor.SelectedEntities().size() == 1)
		if (Entity *e = doc.Find(m_editor.SelectedEntities().front())) {
			if (label)
				*label = std::string(e->name);
			return {&e->behavior.script, &e->behavior.scriptFile};
		}
	if (label)
		*label = "Scène";
	return {&doc.Game().script, &doc.Game().scriptFile};
}

void SandboxApp::CheckScript() {
	std::string label;
	const auto [source, file] = EditedScript(&label);
	script::Interpreter checker;
	const auto r = checker.Check(*source, file->IsEmpty() ? label : std::string(*file));
	m_scriptCheck = r.IsOk() ? std::string("Script valide") : r.Error().Format();
	m_scriptErrorLine = r.IsOk() ? 0 : r.Error().line;
	m_scriptCheckFor = label;
	game::GameMessage entry;
	entry.text = std::format("Vérifier « {} » : {}", label, m_scriptCheck);
	entry.error = r.IsError();
	entry.source = m_editor.SelectedEntities().size() == 1 ? m_editor.SelectedEntities().front() : 0;
	entry.script = true;
	entry.line = m_scriptErrorLine;
	Log(std::move(entry));
}

void SandboxApp::DrawScriptEditor(const sdl3::FRect &r) {
	std::string label;
	const auto [source, file] = EditedScript(&label);
	std::string text = *source;
	ui::CodeEditorOptions options;
	static const ui::SyntaxRules kJson = [] {
		ui::SyntaxRules rules;
		rules.keywords = {"true", "false", "null"};
		rules.lineComments.clear();
		rules.blockOpen.clear();
		return rules;
	}();
	const std::string ext = FileActive() ? std::filesystem::path(m_files[size_t(m_activeFile)].path).extension().string() : std::string(".script");
	options.syntax = ext == ".script" ? &ScriptSyntax() : &kJson;
	options.errorLine = m_scriptCheckFor == label ? m_scriptErrorLine : 0;
	options.placeholder = "# OnStart() · OnUpdate(dt) · OnTouch(…) · OnUse()";
	options.fontSize = m_codeFontSize;
	if (Ui().CodeEditor(r, "script:" + label, text, options)) {
		if (!FileActive())
			m_editor.Change("Script", true); // frappe continue : un seul pas d'annulation
		*EditedScript().first = String(std::move(text));
		m_scriptErrorLine = 0;
	}
	// Ligne demandée (console, liste des fonctions) : curseur en début de ligne, défilée en vue.
	if (ui::TextEdit *edit = Ui().EditState("script:" + label); edit && m_gotoLine > 0) {
		const std::string_view script = *EditedScript().first;
		size_t at = 0;
		for (int line = 1; line < m_gotoLine && at < script.size(); ++line) {
			const size_t next = script.find('\n', at);
			at = next == std::string_view::npos ? script.size() : next + 1;
		}
		edit->SetCursor(std::min(at, script.size()), false);
		const float size = options.fontSize > 0.f ? options.fontSize : Ui().Style().fontSize - 1.f;
		edit->scrollY = std::max(0.f, float(m_gotoLine - 4) * Canvas().LineHeight(size, sdl3::CanvasFont::Mono));
		Ui().SetFocus("script:" + label);
		m_gotoLine = 0;
	}
}

void SandboxApp::DrawScriptSection() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	std::string label;
	const auto [source, file] = EditedScript(&label);
	if (m_scriptCheckFor != label) { // autre script : ancien résultat effacé
		m_scriptCheck.clear();
		m_scriptErrorLine = 0;
		m_scriptCheckFor = label;
	}
	const bool scene = label == "Scène" && m_editor.SelectedEntities().size() != 1;
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::CODE), scene ? "Script de la scène" : "Script");
	u.Wrapped(scene ? "class X extends Scene : OnStart, OnUpdate(dt), OnTouch(groupe, autre), OnTrigger(zone, objet, évènement) — "
					  "this.say, time, player, position, move, turn, visible, light · hud.*, draw2d.*, input.key"
					: "class X extends Behaviour[, PhysicsBody, Light3D, AudioSource] : OnStart, OnUpdate(dt), OnTouch(nom), OnUse, "
					  "OnTrigger — this.move, rotate, turn, show, hide · push, velocity · light · play, stop · import \"module\"",
			  th.textDim, 11.f);
	// Fichier du script, relatif au document : enregistré à côté de lui (projet).
	std::string path = *file;
	u.Label("Fichier", th.textDim, 12.f);
	if (u.TextInput("script-file", path, nullptr, "intégré au document") && path != *file) {
		m_editor.Change("Fichier du script", true);
		*EditedScript().second = String(path);
	}
	u.Row({1.6f, 1.f, 1.f}, 30.f);
	std::vector<std::string_view> names{"Insérer un exemple…"};
	for (const Snippet &s : kSnippets)
		names.push_back(s.name);
	int snippet = 0;
	if (u.Choice(u.Next(), "snippet", snippet, names) && snippet > 0) {
		m_editor.Change("Script", false);
		String &code = *EditedScript().first;
		if (!code.empty() && code.back() != '\n')
			code += '\n';
		code += kSnippets[size_t(snippet - 1)].code;
	}
	if (u.Button("Vérifier"))
		CheckScript();
	if (u.Button("Éditer"))
		ShowPanel("script"); // l'éditeur de code est l'onglet Script du centre
	if (!m_scriptCheck.empty())
		u.Wrapped(m_scriptCheck, m_scriptCheck == "Script valide" ? th.success : th.danger, 12.f);
	DrawScriptParams();
}

void SandboxApp::DrawScriptParams() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	std::string label;
	const auto [source, file] = EditedScript(&label);
	const bool scene = ed.SelectedEntities().size() != 1;
	// Paramètres déclarés par la classe du script (`export let …`), relus quand le script change.
	const std::string key = label + "|" + std::to_string(std::hash<std::string_view>{}(std::string_view(*source)));
	if (key != m_paramsKey) {
		m_paramsKey = key;
		auto r = game::ScriptExports(*source, scene, ModuleLoader());
		m_paramsError = r.IsError() ? r.Error() : std::string();
		m_params = r.IsOk() ? std::move(r.Value()) : std::vector<std::pair<std::string, script::Value>>{};
		m_paramComments = game::ExportComments(*source); // descriptions « # … » des paramètres
	}
	if (m_params.empty() && m_paramsError.empty())
		return;
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Paramètres du script");
	if (!m_paramsError.empty()) {
		u.Wrapped(m_paramsError, th.danger, 11.f);
		return;
	}
	const auto params = [&]() -> ScriptParams * {
		if (scene)
			return &ed.Doc().Game().params;
		Entity *e = ed.Doc().Find(ed.SelectedEntities().front());
		return e ? &e->behavior.params : nullptr;
	};
	ScriptParams *values = params();
	if (!values)
		return;
	ScriptParams next = *values;
	for (const auto &[name, def] : m_params) {
		auto it = std::ranges::find(next, String(name), &ScriptParam::name);
		const bool overridden = it != next.end();
		const auto set = [&](std::variant<double, bool, String> v) {
			if (auto at = std::ranges::find(next, String(name), &ScriptParam::name); at != next.end())
				at->value = std::move(v);
			else
				next.push_back({String(name), std::move(v)});
		};
		u.PushId(std::string("param-") + name);
		u.Row({1.f, 0.18f}, 28.f);
		if (def.IsNumber()) {
			float v = float(overridden && std::holds_alternative<double>(it->value) ? std::get<double>(it->value) : def.AsNumber());
			if (u.DragFloats(name, std::span<float>(&v, 1), 0.05f, "{:.2f}"))
				set(double(v));
		} else if (def.Type() == script::ValueType::Bool) {
			bool v = overridden && std::holds_alternative<bool>(it->value) ? std::get<bool>(it->value) : def.Truthy();
			if (u.Checkbox(name, v))
				set(v);
		} else if (def.IsString()) {
			std::string v = overridden && std::holds_alternative<String>(it->value) ? std::string(std::get<String>(it->value)) : std::string(def.AsString());
			if (u.TextInput("value", v, nullptr, name))
				set(String(v));
		} else {
			u.Label(std::format("{} = {}", name, def.ToString()), th.textDim, 12.f); // listes, tables : lecture seule
		}
		if (u.Button("↺", overridden)) // retour à la valeur par défaut du script
			std::erase_if(next, [&](const ScriptParam &p) { return p.name == name; });
		u.PopId();
		// Description (« # … » du script), affichée sous le paramètre.
		if (auto comment = m_paramComments.find(name); comment != m_paramComments.end() && !comment->second.empty())
			u.Wrapped(comment->second, th.textDim, 11.f);
	}
	if (!(next == *values)) {
		ed.Change("Paramètre du script", u.MouseDown());
		if (ScriptParams *target = params())
			*target = std::move(next);
	}
}

void SandboxApp::ApplyToScript(const std::function<bool(ui::TextEdit &, std::string &)> &op) {
	std::string label;
	const auto [source, file] = EditedScript(&label);
	ui::TextEdit *edit = Ui().EditState("script:" + label);
	if (!edit)
		return; // éditeur jamais affiché : aucun curseur
	std::string text = *source;
	edit->multiline = edit->code = true;
	if (op(*edit, text) && text != *source) {
		m_editor.Change("Script", false);
		*EditedScript().first = String(std::move(text));
	}
	Ui().SetFocus("script:" + label); // le curseur reste dans l'éditeur
}

void SandboxApp::DrawScriptTools(float &x, float y, float size) {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	using ui::icons::MaterialIcons;
	const auto button = [&](MaterialIcons glyph, std::string_view tip, bool enabled = true) {
		const sdl3::FRect r{x, y, size, size};
		x += size + 2.f;
		return u.IconButton(r, ui::icons::Codepoint(glyph), tip, enabled);
	};
	if (button(MaterialIcons::CHECK_CIRCLE, "Vérifier le script"))
		CheckScript();
	if (button(MaterialIcons::COMMENT, "Commenter / décommenter les lignes"))
		ApplyToScript([](ui::TextEdit &e, std::string &t) { return e.ToggleComment(t, "#"); });
	if (button(MaterialIcons::FORMAT_INDENT_DECREASE, "Désindenter (Maj+Tab)"))
		ApplyToScript([](ui::TextEdit &e, std::string &t) { return e.IndentLines(t, true); });
	if (button(MaterialIcons::FORMAT_INDENT_INCREASE, "Indenter (Tab)"))
		ApplyToScript([](ui::TextEdit &e, std::string &t) { return e.IndentLines(t, false); });
	if (button(MaterialIcons::SELECT_ALL, "Tout sélectionner (Ctrl+A)"))
		ApplyToScript([](ui::TextEdit &e, std::string &t) {
			e.SelectAll(t);
			return false;
		});
	x += 6.f;
	const float base = m_codeFontSize > 0.f ? m_codeFontSize : th.fontSize - 1.f;
	if (button(MaterialIcons::TEXT_DECREASE, "Texte plus petit", base > 9.f))
		m_codeFontSize = base - 1.f;
	if (button(MaterialIcons::TEXT_INCREASE, "Texte plus grand", base < 28.f))
		m_codeFontSize = base + 1.f;
	// Script édité, rappelé dans la barre.
	std::string label;
	const auto [source, file] = EditedScript(&label);
	const std::string name = file->IsEmpty() ? label : label + " · " + std::string(*file);
	Canvas().Text(x + 8.f, y + (size - 14.f) * 0.5f, Canvas().Ellipsize(name, 260.f, 13.f), th.textDim, 13.f);
	x += std::min(260.f, Canvas().MeasureText(name, 13.f)) + 16.f;
}

void SandboxApp::DrawScriptWorkspace(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	std::string label;
	const auto [source, file] = EditedScript(&label);
	if (m_scriptCheckFor != label) {
		m_scriptCheck.clear();
		m_scriptErrorLine = 0;
		m_scriptCheckFor = label;
	}
	c.FillRect(area, th.panel);
	// ── Scripts du projet (à la Godot) : la scène, puis les groupes scriptés ──
	const float listW = std::clamp(area.w * 0.22f, 150.f, 240.f);
	const sdl3::FRect list{area.x, area.y, listW, area.h};
	c.FillRect(list, th.background);
	c.Text(list.x + 10.f, list.y + 8.f, "SCRIPTS DU PROJET", th.textDim, 11.f);
	const EntityId selected = ed.SelectedEntities().size() == 1 ? ed.SelectedEntities().front() : 0;
	std::vector<const Entity *> scripted;
	for (const Entity &e : ed.Doc().Entities())
		if (!e.behavior.script.IsEmpty() || !e.behavior.scriptFile.IsEmpty() || e.id == selected)
			scripted.push_back(&e);
	float y = list.y + 30.f;
	const auto row = [&](size_t index, std::string_view name, std::string_view detail, bool on) {
		const sdl3::FRect r{list.x + 4.f, y, list.w - 8.f, 36.f};
		y += 38.f;
		u.PushId(uint64_t(index) + 700);
		const auto it = u.Interact(u.MakeId("script-row"), r);
		u.PopId();
		if (on || it.hovered)
			c.FillRoundRect(r, th.radius, on ? sdl3::WithAlpha(th.accent, 70) : th.widgetHover);
		c.Text(r.x + 8.f, r.y + 3.f, c.Ellipsize(name, r.w - 14.f, 13.f), on ? th.text : th.textDim, 13.f);
		c.Text(r.x + 8.f, r.y + 19.f, c.Ellipsize(detail, r.w - 14.f, 10.5f), th.textDim, 10.5f);
		return it.leftClicked;
	};
	c.PushClip(list);
	// Fichiers ouverts depuis l'explorateur (● : modifié).
	if (!m_files.empty()) {
		for (size_t i = 0; i < m_files.size(); ++i) {
			const std::string name = (m_files[i].Dirty() ? "● " : "") + std::filesystem::path(m_files[i].path).filename().string();
			if (row(600 + i, name, m_files[i].path, int(i) == m_activeFile))
				m_activeFile = int(i);
		}
		y += 6.f;
	}
	if (row(0, "Scène", ed.Doc().Game().scriptFile.IsEmpty() ? "intégré au document" : std::string_view(ed.Doc().Game().scriptFile),
			selected == 0 && !FileActive())) {
		m_activeFile = -1;
		ed.CloseAllGroups();
		ed.ClearSelection();
	}
	for (size_t i = 0; i < scripted.size(); ++i) {
		const Entity &e = *scripted[i];
		const std::string_view detail = !e.behavior.scriptFile.IsEmpty() ? std::string_view(e.behavior.scriptFile)
										: e.behavior.script.IsEmpty()	 ? std::string_view("sans script")
																		 : std::string_view("intégré au document");
		if (row(i + 1, std::string_view(e.name), detail, e.id == selected && !FileActive())) {
			m_activeFile = -1;
			const EntityId id = e.id; // la sélection peut réallouer : identifiant gardé
			ed.CloseAllGroups();
			ed.ClearSelection();
			ed.SelectEntity(id);
		}
	}
	// Fonctions du script affiché (comme la liste des membres de Godot) : clic = aller à la ligne.
	y += 8.f;
	c.Text(list.x + 10.f, y, "STRUCTURE", th.textDim, 11.f);
	y += 20.f;
	const std::string_view listing = *source;
	int lineNo = 0;
	size_t index = 0;
	for (size_t at = 0; at <= listing.size();) {
		const size_t end = std::min(listing.find('\n', at), listing.size());
		std::string_view line = listing.substr(at, end - at);
		++lineNo;
		at = end + 1;
		const bool indented = !line.empty() && (line.front() == ' ' || line.front() == '\t');
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
			line.remove_prefix(1);
		const bool isClass = line.starts_with("class ");
		if (!line.starts_with("fn ") && !isClass)
			continue;
		const size_t skip = isClass ? 6 : 3, brace = line.find('{');
		const std::string_view signature = line.substr(skip, brace == std::string_view::npos ? std::string_view::npos : brace - skip);
		const sdl3::FRect r{list.x + 4.f, y, list.w - 8.f, 22.f};
		y += 23.f;
		u.PushId(uint64_t(++index) + 1700);
		const auto it = u.Interact(u.MakeId("fn-row"), r);
		u.PopId();
		if (it.hovered)
			c.FillRoundRect(r, th.radius, th.widgetHover);
		const bool hook = signature.starts_with("on_");
		const float indent = isClass ? 8.f : indented ? 20.f : 8.f; // méthodes sous leur classe
		c.Text(r.x + indent, r.y + 4.f, c.Ellipsize(signature, r.w - 36.f - indent, 12.f, isClass ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Mono),
			   isClass ? th.text : hook ? th.accent : th.textDim, 12.f, isClass ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Mono);
		c.Text(r.Right() - 30.f, r.y + 5.f, std::to_string(lineNo), th.textDim, 10.5f);
		if (it.leftClicked)
			m_gotoLine = lineNo;
	}
	c.PopClip();
	c.Line(list.Right() - 0.5f, list.y, list.Right() - 0.5f, list.Bottom(), th.panelBorder);
	// ── Barre : fichier, exemples, vérification ──
	const float x0 = list.Right() + 10.f, right = area.Right() - 10.f;
	c.Text(x0, area.y + 11.f, c.Ellipsize(label, 160.f, 14.f), th.text, 14.f);
	const float fileX = x0 + std::min(170.f, c.MeasureText(label, 14.f) + 14.f);
	if (FileActive()) { // fichier : chemin, enregistrer, fermer
		const OpenFile &f = m_files[size_t(m_activeFile)];
		c.Text(fileX, area.y + 11.f, c.Ellipsize(f.path, std::max(120.f, right - fileX - 340.f), 12.f), th.textDim, 12.f);
		if (u.ButtonAt({right - 320.f, area.y + 5.f, 100.f, 28.f}, "Enregistrer", f.Dirty()))
			(void)SaveOpenFile();
		if (u.ButtonAt({right - 214.f, area.y + 5.f, 100.f, 28.f}, "Fermer")) {
			m_files.erase(m_files.begin() + m_activeFile);
			m_activeFile = m_files.empty() ? -1 : std::min(m_activeFile, int(m_files.size()) - 1);
			return;
		}
	} else {
	std::string path = *file;
	if (u.TextInputAt({fileX, area.y + 5.f, std::max(120.f, right - fileX - 330.f), 28.f}, "script-file-ws", path, nullptr, "fichier (vide : intégré)") &&
		path != *file) {
		ed.Change("Fichier du script", true);
		*EditedScript().second = String(path);
	}
	std::vector<std::string_view> names{"Insérer un exemple…"};
	for (const Snippet &s : kSnippets)
		names.push_back(s.name);
	int snippet = 0;
	if (u.Choice({right - 320.f, area.y + 5.f, 210.f, 28.f}, "snippet-ws", snippet, names) && snippet > 0) {
		ed.Change("Script", false);
		String &code = *EditedScript().first;
		if (!code.empty() && code.back() != '\n')
			code += '\n';
		code += kSnippets[size_t(snippet - 1)].code;
	}
	}
	if (u.ButtonAt({right - 102.f, area.y + 5.f, 102.f, 28.f}, "Vérifier", !FileActive() || std::string_view(label).ends_with(".script")))
		CheckScript();
	// ── Éditeur pleine taille, résultat de la vérification dessous ──
	const bool message = !m_scriptCheck.empty();
	const float bottom = message ? 26.f : 8.f;
	DrawScriptEditor({x0 - 2.f, area.y + 40.f, right - x0 + 2.f, area.h - 40.f - bottom});
	if (message)
		c.Text(x0, area.Bottom() - 21.f, c.Ellipsize(m_scriptCheck, right - x0, 12.f), m_scriptCheck == "Script valide" ? th.success : th.danger, 12.f);
}

} // namespace sandbox
