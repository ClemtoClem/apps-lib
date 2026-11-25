// Tests du mode Jeu du sandbox, sans fenêtre ni son (la physique est testée par
// engine.physics) : session (joueur, objets, scripts, sons muets) et document intact.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <iterator>
#include <fstream>

#include <sandbox/document/document.hpp>
#include <sandbox/document/project.hpp>
#include <sandbox/editor/editor.hpp>
#include <sandbox/game/session.hpp>
#include <sandbox/game/sound.hpp>
#include <sandbox/modeling/operations.hpp>

using namespace sandbox;
using namespace sandbox::game;

namespace {

modeling::Model BoxModel(float w, float h, float d) {
	modeling::Model m;
	if (auto f = m.AddRectangle({0.f, 0.f, 0.f}, {w, 0.f, 0.f}, {0.f, 0.f, d}))
		(void)modeling::PushPull(m, *f, m.Faces()[*f].normal.y > 0.f ? h : -h);
	return m;
}

void Run(GameSession &s, float seconds, const GameInput &input = {}) {
	for (int i = 0; i < int(seconds * 120.f); ++i)
		s.Update(1.f / 120.f, input);
}

} // namespace

TEST(Game, SessionPlayerObjectsAndScripts) {
	Document doc;
	(void)doc.Root().AddRectangle({-10.f, 0.f, -10.f}, {20.f, 0.f, 0.f}, {0.f, 0.f, 20.f}); // sol libre
	// Identifiants gardés : une référence d'Entity ne survit pas à l'ajout suivant.
	const auto add = [&](const char *name, modeling::Model model, math::FVector3 at) { return doc.AddEntity(name, std::move(model), math::FMatrix4::Translation(at)).id; };
	(void)add("Mur", BoxModel(0.3f, 2.5f, 14.f), {4.f, 0.f, -6.f}); // de z = -6 à 8
	const EntityId crateId = add("Caisse", BoxModel(0.8f, 0.8f, 0.8f), {0.f, 3.f, -3.f});
	const EntityId lampId = add("Lampe", BoxModel(0.3f, 0.3f, 0.3f), {-3.f, 2.f, 0.f});
	const EntityId plateId = add("Plaque", BoxModel(1.f, 0.05f, 1.f), {-0.5f, 0.f, 5.f}); // derrière le joueur
	const EntityId brokenId = add("Cassé", BoxModel(0.2f, 0.2f, 0.2f), {6.f, 0.f, 6.f});
	doc.Find(crateId)->behavior.body = BodyKind::Dynamic;
	Behavior &lamp = doc.Find(lampId)->behavior;
	lamp.body = BodyKind::Ghost;
	lamp.light = true;
	lamp.script = R"(
		let t = 0
		fn OnStart() { game.say("Lampe prête") }
		fn OnUpdate(dt) {
			t += dt
			if (t > 0.5 and t - dt <= 0.5) { self.move(0, 1, 0) }
		}
		fn OnUse() { self.light(false) }
	)";
	Behavior &plate = doc.Find(plateId)->behavior;
	plate.body = BodyKind::Ghost;
	plate.script = R"(fn OnTouch(qui) { game.say("touchée par " .. qui) })";
	Behavior &broken = doc.Find(brokenId)->behavior;
	broken.script = "fn OnStart() { appel_inconnu() }";
	broken.sound = "absent.ogg"; // son au départ : sans sortie audio, accepté et muet
	const std::string before = std::string(doc.ToJson());

	SoundSystem mute; // non ouvert : muet
	GameSession s(doc, {0.f, 5.f, 4.f}, {0.f, 1.f, -4.f}, &mute);
	CHECK_EQ(s.ScriptCount(), 3u);
	Run(s, 1.5f);
	CHECK_NEAR(s.PlayerFeet().y, 0.f, 0.02f); // posé sous la caméra de l'éditeur
	CHECK(s.PlayerGrounded());
	CHECK_NEAR(s.CenterOf(crateId).y, 0.4f, 0.02f); // la caisse est tombée
	CHECK_NEAR(s.CenterOf(lampId).y, 3.15f, 1e-3f); // déplacée par son script à 0,5 s
	sdl3::Lighting lit;
	s.Lights(lit);
	REQUIRE(lit.points.size() == 1);
	bool ready = false, failed = false;
	for (const GameMessage &m : s.Messages()) {
		ready |= m.text == "Lampe prête";
		failed |= m.error && m.text.find("Cassé") != std::string::npos;
	}
	CHECK(ready);
	CHECK(failed); // le script en erreur est signalé, la partie continue
	// Marcher vers la plaque (fantôme) : OnTouch("joueur").
	GameInput forward;
	forward.forward = 1.f;
	Run(s, 0.1f, forward);
	bool touched = false;
	for (int i = 0; i < 240 && !touched; ++i) {
		s.Update(1.f / 120.f, forward);
		for (const GameMessage &m : s.Messages())
			touched |= m.text == "touchée par joueur";
	}
	CHECK(!touched); // la plaque est derrière le joueur (il regarde vers -z)
	GameInput back;
	back.forward = -1.f;
	for (int i = 0; i < 360 && !touched; ++i) {
		s.Update(1.f / 120.f, back);
		for (const GameMessage &m : s.Messages())
			touched |= m.text == "touchée par joueur";
	}
	CHECK(touched);
	// Marcher vers le mur (x = 4) en se tournant à droite : arrêté devant.
	GameInput turn;
	turn.lookX = (math::PI * 0.5f) / 0.0025f; // quart de tour vers +x
	s.Update(1.f / 120.f, turn);
	Run(s, 3.f, forward);
	CHECK_NEAR(s.PlayerFeet().x, 4.f - 0.3f, 0.05f);
	// Le document n'a pas bougé.
	CHECK(std::string(doc.ToJson()) == before);
}

TEST(Game, SpatialSoundGains) {
	const auto [l, r] = SoundSystem::SpatialGains({}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 1.f);
	CHECK(r > l); // à droite
	const auto [l2, r2] = SoundSystem::SpatialGains({}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, 1.f);
	CHECK_NEAR(l2, r2, 1e-6f); // devant : centré
	const auto [lf, rf] = SoundSystem::SpatialGains({}, {1.f, 0.f, 0.f}, {0.f, 0.f, -20.f}, 1.f);
	CHECK(lf + rf < (l2 + r2) * 0.3f); // loin : atténué
	SoundSystem mute;
	CHECK(mute.Play("x.ogg", 1.f, false) != 0); // muet : accepté
	mute.Update();
	CHECK_EQ(mute.VoiceCount(), 0u); // son bref muet : achevé aussitôt
}

TEST(Game, WalkIntoTheSampleHouse) {
	// Projet d'exemple : depuis la scène « Départ », marcher droit devant passe le paillasson et
	// bute sur la porte fermée (groupe solide scripté : collision exacte) ; E l'ouvre d'un quart
	// de tour autour de sa charnière, on franchit le seuil et le mur du fond arrête le joueur.
	auto project = sandbox::Project::Load("apps/sandbox/projects/maison/maison.project");
	REQUIRE(project.IsOk());
	auto loaded = Document::Load(String(project.Value().ActiveScene()->string()));
	REQUIRE(loaded.IsOk());
	const Document &doc = loaded.Value();
	// Modules du projet (`import "interactions"` de la lanterne et de la radio).
	const sandbox::Project &proj = project.Value();
	const GameSession::ModuleLoader modules = [&proj](std::string_view name) -> Result<std::string, std::string> {
		const auto file = proj.ScriptFile(name);
		if (!file)
			return Err(std::string("introuvable"));
		std::ifstream in(*file);
		return Ok(std::string(std::istreambuf_iterator<char>(in), {}));
	};
	const SceneView *start = nullptr;
	for (const SceneView &v : doc.Views())
		if (v.name == "Départ")
			start = &v;
	REQUIRE(start != nullptr);
	SoundSystem mute;
	GameSession s(doc, start->eye, start->target, &mute, modules);
	CHECK_EQ(s.ScriptCount(), 5u); // scène, lanterne, radio, paillasson, porte
	// Chaque script a instancié la classe de son groupe.
	CHECK(s.ClassOf(0) == "Maison");
	for (const Entity &e : doc.Entities()) {
		if (e.name == "Lanterne")
			CHECK(s.ClassOf(e.id) == "Lanterne");
		if (e.name == "Porte")
			CHECK(s.ClassOf(e.id) == "Porte");
	}
	for (const GameMessage &m : s.Messages())
		CHECK(!m.error);
	Run(s, 0.5f);
	CHECK(s.PlayerGrounded());
	GameInput forward;
	forward.forward = 1.f;
	bool welcomed = false;
	const auto walk = [&](float seconds) {
		for (int i = 0; i < int(120 * seconds); ++i) {
			s.Update(1.f / 120.f, forward);
			for (const GameMessage &m : s.Messages())
				welcomed |= m.text.starts_with("Bienvenue ! Entrez");
		}
	};
	walk(4.f);
	CHECK(welcomed); // paillasson
	CHECK_NEAR(s.PlayerFeet().z, 3.92f + 0.3f, 0.05f); // contre la porte fermée
	EntityId door = 0;
	for (const Entity &e : doc.Entities())
		if (e.name == "Porte")
			door = e.id;
	CHECK_EQ(s.Aimed(), door);
	GameInput use;
	use.use = true;
	s.Update(1.f / 120.f, use);
	Run(s, 1.f); // quart de tour à 120°/s
	const math::FVector3 hinge{2.61f, 0.105f, 3.88f}, tip = s.TransformOf(door).TransformPoint({0.88f, 0.f, 0.f});
	CHECK_NEAR(tip.z, hinge.z - 0.88f, 0.02f); // battant tourné vers l'intérieur
	CHECK_NEAR(tip.x, hinge.x, 0.02f);
	walk(3.f);
	const math::FVector3 feet = s.PlayerFeet();
	CHECK_NEAR(feet.y, 0.1f, 0.03f);	   // sur le plancher
	CHECK_NEAR(feet.z, 0.2f + 0.3f, 0.05f); // arrêté par le mur du fond (face intérieure en z = 0,2)
	CHECK(feet.x > 2.6f && feet.x < 3.5f); // passé par la porte
	// Les caisses ont fini de tomber et reposent sur le terrain ou les unes sur les autres.
	for (const Entity &e : doc.Entities())
		if (e.behavior.body == BodyKind::Dynamic)
			CHECK(s.CenterOf(e.id).y > 0.2f && s.CenterOf(e.id).y < 2.f);
}

TEST(Game, ScriptsDriveTheOverlay) {
	Document doc;
	(void)doc.Root().AddRectangle({-5.f, 0.f, -5.f}, {10.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
	Item2D &score = doc.Overlay().Add(Shape2D::Text, {20.f, 20.f});
	score.name = "score";
	doc.Overlay().Add(Shape2D::Rect, {0.f, 0.f}).name = "panneau";
	doc.Game().script = "let n = 0\n"
						"fn OnUpdate(dt) {\n"
						"\tn += 1\n"
						"\thud.text(\"score\", \"Pas \" .. n)\n"
						"\thud.move(\"panneau\", 100, 50)\n"
						"\thud.color(\"panneau\", \"#ff0000\")\n"
						"\thud.show(\"inconnu\", false)\n" // nom inconnu : sans effet
						"\tdraw2d.rect(10, 700, n, 8, \"#00ff00\")\n"
						"\tdraw2d.text(640, 360, \"centre\")\n"
						"}\n";
	SoundSystem mute;
	GameSession s(doc, {0.f, 1.7f, 3.f}, {0.f, 1.7f, 0.f}, &mute);
	Run(s, 3.f / 120.f); // trois pas
	const auto named = [&](std::string_view name) {
		const auto it = std::ranges::find_if(s.Overlay().items, [&](const Item2D &i) { return std::string_view(i.name) == name; });
		return it == s.Overlay().items.end() ? nullptr : &*it;
	};
	REQUIRE(named("score") != nullptr);
	CHECK(named("score")->text == "Pas 3");
	CHECK_NEAR(named("panneau")->position.x, 100.f, 1e-6f);
	CHECK_NEAR(named("panneau")->fill.r, 1.f, 1e-6f);
	REQUIRE(s.Drawn().size() == 2u); // dessins du dernier pas seulement
	CHECK_NEAR(s.Drawn()[0].size.x, 3.f, 1e-6f);
	CHECK_NEAR(s.Drawn()[0].fill.g, 1.f, 1e-6f);
	CHECK(s.Drawn()[1].text == "centre");
	CHECK(doc.Overlay().FindByName("score")->text == "Texte"); // le document n'est pas modifié
	// Couleur invalide : erreur de script signalée, la partie continue.
	Document bad = doc;
	bad.Game().script = "fn OnUpdate(dt) { draw2d.rect(0, 0, 1, 1, \"rouge\") }\n";
	GameSession b(bad, {0.f, 1.7f, 3.f}, {0.f, 1.7f, 0.f}, &mute);
	Run(b, 1.f / 120.f);
	REQUIRE(!b.Messages().empty());
	CHECK(b.Messages().back().error);
	CHECK(b.Messages().back().text.find("couleur") != std::string::npos);
}

TEST(Game, EnvironmentAndSpotLamps) {
	Document doc;
	CHECK(doc.ToJson().find("\"environment\"") == std::string::npos); // par défaut : non écrit
	const auto presets = EnvironmentPresets();
	REQUIRE(presets.size() == 4u);
	CHECK(presets[2].first == "Nuit");
	doc.Environment() = presets[2].second;
	doc.Environment().fogDensity = 0.05f;
	Entity &lamp = doc.AddEntity("Projecteur", BoxModel(0.2f, 0.2f, 0.2f),
								 math::FMatrix4::Translation({0.f, 3.f, 0.f}) * math::FMatrix4::Rotation({0.f, 0.f, 1.f}, math::PI * 0.5f));
	lamp.behavior.light = true;
	lamp.behavior.lightKind = LightKind::Spot;
	lamp.behavior.spotAngle = 30.f;
	auto back = Document::FromJson(doc.ToJson());
	REQUIRE(back.IsOk());
	CHECK(back.Value().Environment() == doc.Environment());
	const Behavior &b = back.Value().Entities()[0].behavior;
	CHECK(b.lightKind == LightKind::Spot);
	CHECK_NEAR(b.spotAngle, 30.f, 1e-5f);
	// Spot : axe −Y du groupe, ici tourné d'un quart de tour autour de Z → vers +X.
	sdl3::Lighting lighting;
	game::AddLamp(b, back.Value().Entities()[0].transform, {0.f, 3.f, 0.f}, lighting);
	REQUIRE(lighting.spots.size() == 1u);
	CHECK(lighting.points.empty());
	CHECK_NEAR(lighting.spots[0].direction.x, 1.f, 1e-4f);
	CHECK_NEAR(lighting.spots[0].angle, 30.f * math::DEG2RAD, 1e-5f);
	// En partie : lampe allumée par défaut, éteinte par le script.
	SoundSystem mute;
	GameSession s(back.Value(), {0.f, 1.7f, 5.f}, {0.f, 1.7f, 0.f}, &mute);
	sdl3::Lighting on;
	s.Lights(on);
	CHECK_EQ(on.spots.size(), 1u);
	s.SetLight(back.Value().Entities()[0].id, false);
	sdl3::Lighting off;
	s.Lights(off);
	CHECK(off.spots.empty());
}

TEST(Game, JournalKeepsScriptSourceAndLine) {
	Document doc;
	(void)doc.Root().AddRectangle({-5.f, 0.f, -5.f}, {10.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
	Entity &box = doc.AddEntity("Boîte", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({2.f, 0.f, 0.f}));
	box.behavior.script = "fn OnStart() {\n\tprint(\"prêt\")\n}\nfn OnUpdate(dt) {\n\tinconnu()\n}\n";
	const EntityId id = box.id;
	doc.Game().script = "fn OnStart() { game.say(\"scène\") }\n";
	SoundSystem mute;
	GameSession s(doc, {0.f, 1.7f, 4.f}, {0.f, 1.7f, 0.f}, &mute);
	Run(s, 2.f / 120.f);
	const std::vector<GameMessage> journal = s.TakeJournal();
	bool scene = false, print = false, error = false;
	for (const GameMessage &m : journal) {
		scene |= m.script && m.source == 0 && m.text == "scène";
		print |= m.script && m.source == id && m.text.ends_with("prêt");
		if (m.error && m.source == id) {
			error = true;
			CHECK_EQ(m.line, 5); // appel inconnu ligne 5
		}
	}
	CHECK(scene);
	CHECK(print);
	CHECK(error);
	CHECK(s.TakeJournal().empty()); // déjà lu
}

TEST(Game, TriggerZonesCallOnTrigger) {
	Document doc;
	(void)doc.Root().AddRectangle({-10.f, 0.f, -10.f}, {20.f, 0.f, 0.f}, {0.f, 0.f, 20.f});
	Entity &once = doc.AddEntity("Arrivée", BoxModel(4.f, 2.f, 1.f), math::FMatrix4::Translation({-2.f, 0.f, 0.f}));
	once.behavior.body = BodyKind::Ghost;
	once.behavior.triggerEvent = "gagné";
	once.behavior.triggerOnce = true;
	once.behavior.script = "fn OnTrigger(objet, evenement) { game.say(\"zone \" .. objet .. \" \" .. evenement) }\n";
	Entity &plate = doc.AddEntity("Plaque", BoxModel(1.f, 0.3f, 1.f), math::FMatrix4::Translation({4.f, 0.f, 0.f}));
	plate.behavior.body = BodyKind::Ghost;
	plate.behavior.triggerEvent = "poids";
	Entity &crate = doc.AddEntity("Caisse", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({4.25f, 3.f, 0.25f}));
	crate.behavior.body = BodyKind::Dynamic;
	doc.Game().script = "fn OnTrigger(zone, objet, evenement) { game.say(zone .. \"|\" .. objet .. \"|\" .. evenement) }\n";
	SoundSystem mute;
	GameSession s(doc, {0.f, 1.7f, 4.f}, {0.f, 1.7f, 0.f}, &mute);
	GameInput forward, back;
	forward.forward = 1.f;
	back.forward = -1.f;
	Run(s, 0.3f);
	Run(s, 2.f, forward); // traverse la zone « Arrivée » (z de 0 à 1)
	Run(s, 2.f, back);	  // et revient : « une seule fois »
	Run(s, 2.f, forward);
	int arrived = 0, zoneScript = 0, weighed = 0;
	for (const GameMessage &m : s.TakeJournal()) {
		arrived += m.text == "Arrivée|joueur|gagné" ? 1 : 0;
		zoneScript += m.text == "zone joueur gagné" ? 1 : 0;
		weighed += m.text == "Plaque|Caisse|poids" ? 1 : 0;
	}
	CHECK_EQ(arrived, 1);
	CHECK_EQ(zoneScript, 1);
	CHECK_EQ(weighed, 1); // la caisse tombée sur la plaque (objet dynamique)
	auto back2 = Document::FromJson(doc.ToJson());
	REQUIRE(back2.IsOk());
	CHECK(back2.Value().Entities()[0].behavior.triggerEvent == "gagné");
	CHECK(back2.Value().Entities()[0].behavior.triggerOnce);
}

TEST(Game, ScriptClassesBindTheirGroup) {
	Document doc;
	(void)doc.Root().AddRectangle({-5.f, 0.f, -5.f}, {10.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
	Entity &a = doc.AddEntity("Bloc", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({2.f, 0.f, 0.f}));
	// Classe parente de script + base native ; la plus dérivée est instanciée.
	a.behavior.script = "class Mobile extends Behaviour { fn pas() { return 1 } }\n"
						"class Bloc extends Mobile, Light3D {\n"
						"\tlet n = 0\n"
						"\tfn OnUpdate(dt) { this.n += this.pas()  this.move(0, 0, 0.01) }\n"
						"\tfn OnUse() { this.light(false) }\n"
						"}\n";
	const EntityId blockId = a.id;
	Entity &b = doc.AddEntity("Double", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({-2.f, 0.f, 0.f}));
	b.behavior.script = "class A extends Behaviour {}\nclass B extends Behaviour {}\n"; // ambigu
	Entity &c = doc.AddEntity("Orphelin", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({0.f, 0.f, -3.f}));
	c.behavior.script = "class Orphelin extends Behaviour {\n\tfn OnStart() { let x = Orphelin()\n\t\tx.move(1, 0, 0) }\n}\n";
	SoundSystem mute;
	GameSession s(doc, {0.f, 1.7f, 4.f}, {0.f, 1.7f, 0.f}, &mute);
	const float z0 = s.CenterOf(blockId).z;
	Run(s, 10.f / 120.f);
	CHECK(s.ClassOf(blockId) == "Bloc");
	CHECK_NEAR(s.CenterOf(blockId).z - z0, 0.1f, 1e-4f); // move de Behaviour, via la classe parente
	bool ambiguous = false, orphan = false;
	for (const GameMessage &m : s.TakeJournal()) {
		ambiguous |= m.error && m.text.find("plusieurs classes Behaviour") != std::string::npos;
		orphan |= m.error && m.text.find("instance sans groupe") != std::string::npos;
	}
	CHECK(ambiguous);
	CHECK(orphan);
	CHECK_EQ(s.ScriptCount(), 2u); // le script ambigu n'est pas chargé
}

TEST(Game, ChildrenFollowAScriptedParent) {
	Document doc;
	(void)doc.Root().AddRectangle({-10.f, 0.f, -10.f}, {20.f, 0.f, 0.f}, {0.f, 0.f, 20.f});
	Entity &cart = doc.AddEntity("Chariot", BoxModel(1.f, 0.2f, 1.f), math::FMatrix4::Translation({0.f, 0.f, 0.f}));
	cart.behavior.script = "class Chariot extends Behaviour { fn OnUpdate(dt) { this.move(dt, 0, 0) } }\n";
	const EntityId cartId = cart.id;
	Entity &crate = doc.AddEntity("Bac", BoxModel(0.5f, 0.5f, 0.5f), math::FMatrix4::Translation({0.25f, 0.2f, 0.25f})); // solide, sans script
	const EntityId crateId = crate.id;
	REQUIRE(doc.Reparent(crateId, cartId));
	SoundSystem mute;
	GameSession s(doc, {0.f, 1.7f, 6.f}, {0.f, 1.7f, 0.f}, &mute);
	const float x0 = s.TransformOf(crateId).m[3][0];
	Run(s, 1.f);
	CHECK_NEAR(s.TransformOf(cartId).m[3][0], 1.f, 0.02f);
	CHECK_NEAR(s.TransformOf(crateId).m[3][0] - x0, 1.f, 0.02f); // l'enfant suit
	// Sa collision aussi (maillage cinématique) : un rayon le touche à sa nouvelle place.
	const auto hit = s.Physics().Raycast({{1.5f, 3.f, 0.5f}, {0.f, -1.f, 0.f}}, 5.f);
	REQUIRE(hit.has_value());
	CHECK_EQ(hit->user, physics::UserId(crateId));
}

TEST(Game, ScriptParametersDriveNodeProperties) {
	const std::string sceneScript = "class Niveau extends Scene {\n"
									"\texport let vitesse = 4.0\n"
									"\texport let saut = 5\n"
									"\tfn OnStart() { this.set_player_speed(this.vitesse)  this.set_jump(this.saut) }\n"
									"}\n";
	const std::string lampScript = "class Lampe extends Behaviour, Light3D {\n"
								   "\texport let puissance = 3\n"
								   "\texport let couleur = \"#ff8000\"\n"
								   "\texport let allumee = true\n"
								   "\tfn OnStart() { this.set_intensity(this.puissance)  this.set_color(this.couleur)  this.light(this.allumee) }\n"
								   "}\n";
	// Inspecteur : paramètres et valeurs par défaut, sans lancer de partie.
	auto params = game::ScriptExports(lampScript, false, {});
	REQUIRE(params.IsOk());
	REQUIRE(params.Value().size() == 3u);
	CHECK(params.Value()[0].first == "puissance");
	CHECK_EQ(params.Value()[0].second.AsInt(), 3);
	CHECK(params.Value()[1].second.AsString() == "#ff8000");
	REQUIRE(game::ScriptExports(sceneScript, true, {}).IsOk());
	CHECK_EQ(game::ScriptExports(sceneScript, true, {}).Value().size(), 2u);
	CHECK(game::ScriptExports("class X extends Behaviour {", false, {}).IsError()); // erreur de syntaxe signalée
	// Valeurs réglées : posées avant init, elles changent les propriétés des nœuds.
	Document doc;
	(void)doc.Root().AddRectangle({-5.f, 0.f, -5.f}, {10.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
	doc.Game().script = sceneScript;
	doc.Game().params = {{"vitesse", 9.0}};
	Entity &lamp = doc.AddEntity("Lampe", BoxModel(0.2f, 0.2f, 0.2f), math::FMatrix4::Translation({0.f, 2.f, 0.f}));
	lamp.behavior.script = lampScript;
	lamp.behavior.light = true;
	lamp.behavior.params = {{"puissance", 25.0}, {"couleur", String("#00ff00")}};
	auto back = Document::FromJson(doc.ToJson()); // et enregistrées dans la scène
	REQUIRE(back.IsOk());
	CHECK(back.Value().Entities()[0].behavior.params == lamp.behavior.params);
	SoundSystem mute;
	GameSession s(back.Value(), {0.f, 1.7f, 4.f}, {0.f, 1.7f, 0.f}, &mute);
	Run(s, 2.f / 120.f);
	CHECK_NEAR(s.Settings().walkSpeed, 9.f, 1e-5f); // réglée (défaut 4)
	CHECK_NEAR(s.Settings().jumpSpeed, 5.f, 1e-5f); // défaut du script
	sdl3::Lighting lit;
	s.Lights(lit);
	REQUIRE(lit.points.size() == 1u);
	CHECK_NEAR(lit.points[0].intensity, 25.f, 1e-5f);
	CHECK(lit.points[0].color.y > lit.points[0].color.x); // vert (couleur linéaire x, y, z = r, v, b)
}

namespace {
/// Journal d'une scène du projet dungeon jouée quelques pas (module `ui` simulé) : ses erreurs.
std::vector<std::string> DungeonErrors(const char *scene, float seconds) {
	auto project = sandbox::Project::Load("apps/sandbox/projects/dungeon/dungeon.project");
	auto doc = Document::Load(String((std::string("apps/sandbox/projects/dungeon/scenes/") + scene).c_str()));
	if (!project || !doc)
		return {"chargement impossible"};
	const sandbox::Project &proj = project.Value();
	const GameSession::ModuleLoader modules = [&proj](std::string_view name) -> Result<std::string, std::string> {
		const auto file = proj.ScriptFile(name);
		if (!file)
			return Err(std::string("introuvable"));
		std::ifstream in(*file);
		return Ok(std::string(std::istreambuf_iterator<char>(in), {}));
	};
	const auto stubUi = [](script::Interpreter &interp, EntityId) {
		const auto widget = [](script::Interpreter &, std::span<const script::Value> a) -> Result<script::Value, std::string> {
			script::Map m;
			m.emplace("text", a.empty() ? script::Value() : script::Value(a[0].ToString()));
			m.emplace("value", script::Value(0.0));
			m.emplace("set_bg", script::Value::MakeNative("set_bg", [](script::Interpreter &, std::span<const script::Value>) -> Result<script::Value, std::string> {
						  return Ok(script::Value());
					  }));
			return Ok(script::Value(std::move(m)));
		};
		interp.RegisterModule("ui", {{"label", widget}, {"column", widget}, {"row", widget}, {"button", widget}, {"input", widget}, {"progress", widget}, {"separator", widget}});
	};
	SoundSystem mute;
	const SceneView *start = doc.Value().Views().empty() ? nullptr : &doc.Value().Views().front();
	GameSession s(doc.Value(), start ? start->eye : math::FVector3{0.f, 1.7f, 4.f}, start ? start->target : math::FVector3{}, &mute, modules, stubUi);
	Run(s, seconds);
	std::vector<std::string> errors;
	for (const GameMessage &m : s.TakeJournal())
		if (m.error)
			errors.push_back(m.text);
	return errors;
}
} // namespace

TEST(Game, DungeonMainMenuRuns) {
	if (!std::filesystem::exists("apps/sandbox/projects/dungeon/dungeon.project"))
		SKIP("projet dungeon absent");
	const auto errors = DungeonErrors("main_menu.scene", 0.5f);
	for (const std::string &e : errors)
		std::fprintf(stderr, "    erreur : %s\n", e.c_str());
	CHECK(errors.empty());
}

TEST(Game, DungeonScenesReport) {
	if (!std::filesystem::exists("apps/sandbox/projects/dungeon/dungeon.project"))
		SKIP("projet dungeon absent");
	for (const char *scene : {"generation.scene", "dungeon.scene", "sewers.scene", "dungeon_upper_floor.scene", "parts_workshop.scene"}) {
		const auto errors = DungeonErrors(scene, 0.3f);
		const bool expectedClean = std::string_view(scene) != "generation.scene" && std::string_view(scene) != "dungeon.scene"; // async, ObjectAsset : à venir
		if (!errors.empty())
			std::fprintf(stderr, "    %s : %zu erreur(s) — %s\n", scene, errors.size(), errors.front().c_str());
		if (expectedClean)
			CHECK(errors.empty());
	}
}

TEST(Game, ExportComments) {
	const std::string_view src = "class X extends Behaviour {\n"
								 "\texport let vitesse = 4.0   # marche du joueur (m/s)\n"
								 "\texport let saut = 5\n"
								 "\texport let couleur = \"#ff8000\" # teinte de la lampe\n"
								 "}";
	const auto comments = game::ExportComments(src);
	CHECK_EQ(comments.at("vitesse"), "marche du joueur (m/s)");
	CHECK_EQ(comments.at("couleur"), "teinte de la lampe");
	CHECK(comments.find("saut") == comments.end()); // pas de commentaire
}
