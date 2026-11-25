#pragma once
/**
 * @file session.hpp
 * @brief Partie en cours (mode Jeu) : visite à la première personne du document, avec gravité,
 *        collisions, objets physiques, lumières, sons et scripts de comportement.
 *
 * La session part d'une COPIE de l'état des groupes : jouer ne modifie jamais le document
 * (« Arrêter » revient exactement à l'édition). Groupes :
 *  - Solide sans script : ses triangles forment le décor (collisions exactes) ;
 *  - Solide avec script : maillage cinématique (forme exacte) que le script déplace ou tourne
 *    (une porte qui tourne repousse le joueur et les objets) ;
 *  - Dynamique : boîte qui tombe, rebondit, se pousse ; Fantôme : zone traversable.
 *
 * Scripts (langage `foundation.script`, un interpréteur par groupe), orientés objet : chaque
 * script déclare la classe de son groupe, dérivée de classes natives (C++, `script::ClassBinder`) :
 *  - `Behaviour` : name(), position(), move(dx, dy, dz), set_position(x, y, z), rotate(°), turn(°),
 *    show(), hide(), distance() (au joueur) ;
 *  - `PhysicsBody` : push(vx, vy, vz), velocity() ; `Light3D` : light([allumée]) ;
 *    `AudioSource` : play([fichier]), stop() ;
 *  - `Scene` (script de la scène) : say(texte), time(), player(), position(nom), move(nom, …),
 *    turn(nom, °), visible(nom, oui), light(nom, oui).
 *
 *     class Porte extends Behaviour, AudioSource {
 *         let ouverte = false
 *         fn OnUse() { this.ouverte = not this.ouverte  this.play() }
 *     }
 *
 * La classe la plus dérivée du script est instanciée pour le groupe ; ses méthodes sont les
 * rappels. `import "nom"` charge `nom.script` du dossier des scripts du projet (classes communes,
 * `class Zombie extends m.Monstre`). Un script sans classe (fonctions libres) reste accepté.
 * Rappels :
 * `OnStart()`, `OnUpdate(dt)`, `OnTouch(nom)` (contact du joueur « joueur » ou d'un groupe),
 * `OnUse()` (touche E en visant le groupe). Le script de la scène (`GameSettings::script`)
 * reçoit `OnStart`, `OnUpdate` et `OnTouch(groupe, autre)` pour tous les contacts. Une zone
 * fantôme déclenche `OnTrigger(zone, objet, évènement)` dans le script de la scène et
 * `OnTrigger(objet, évènement)` dans le sien (évènement et « une seule fois » réglés par groupe).
 * Fonctions offertes :
 *  - `self.name()`, `self.position()`, `self.move(dx, dy, dz)`, `self.set_position(x, y, z)`,
 *    `self.rotate(degrés)` (autour du centre), `self.turn(degrés)` (autour de l'origine du
 *    groupe : charnière d'une porte), `self.push(vx, vy, vz)`, `self.velocity()`, `self.show()`,
 *    `self.hide()`, `self.play([fichier])`, `self.stop()`, `self.light(allumée)` ;
 *  - `game.say(texte)`, `game.time()`, `game.player()`, `game.distance()`,
 *    `game.position(nom)`, `game.move(nom, dx, dy, dz)`, `game.turn(nom, degrés)`,
 *    `game.visible(nom, oui)`, `game.light(nom, oui)` ;
 *  - `input.key(nom)` (« w », « space », « e »…), `print(…)` ;
 *  - calque 2D (copie du document, coordonnées de l'écran de référence) : `hud.text(nom, texte)`,
 *    `hud.show(nom, oui)`, `hud.move(nom, x, y)`, `hud.size(nom, l, h)`, `hud.color(nom, "#rrggbb")`,
 *    `hud.position(nom)` ; dessin immédiat, refait à chaque pas : `draw2d.rect(x, y, l, h, [couleur])`,
 *    `draw2d.circle(x, y, rayon, [couleur])`, `draw2d.line(x1, y1, x2, y2, [couleur], [épaisseur])`,
 *    `draw2d.text(x, y, texte, [taille], [couleur])`.
 */
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <physics/world.hpp>
#include <script/script.hpp>
#include <sdl3/render/scene3d.hpp>

#include "sandbox/document/document.hpp"

namespace sandbox::game {

class SoundSystem;
class EntityScript;

/// Commandes du joueur pour un pas de simulation.
struct GameInput {
	float forward = 0.f, strafe = 0.f; ///< -1..1
	bool jump = false, run = false, use = false;
	float lookX = 0.f, lookY = 0.f; ///< pixels de souris depuis le pas précédent
	std::set<std::string> keys;		///< touches enfoncées (noms en minuscules) pour `input.key`
};

struct GameMessage {
	std::string text;
	double time = 0.0;
	bool error = false;
	EntityId source = 0; ///< groupe dont le script parle (0 : scène ou moteur)
	bool script = false; ///< message d'un script (print, say, erreur)
	int line = 0;		 ///< erreur de script : ligne (0 : inconnue)
};

/// Lampe d'un groupe (comportement, transformation, centre du monde) ajoutée à `lighting` :
/// ponctuelle au centre, ou spot dirigé selon l'axe −Y du groupe. Partagé par le mode Jeu et la
/// vue d'édition.
void AddLamp(const Behavior &behavior, const math::FMatrix4 &transform, const math::FVector3 &center, sdl3::Lighting &lighting);

/// Paramètres d'un script (`export let …` de sa classe du moteur) et leurs valeurs par défaut,
/// pour l'inspecteur ; `scene` : script de la scène (classe Scene) ou d'un groupe.
[[nodiscard]] Result<std::vector<std::pair<std::string, script::Value>>, std::string> ScriptExports(std::string_view source, bool scene,
																									  const std::function<Result<std::string, std::string>(std::string_view)> &modules);

/// Commentaire (« # … ») associé à chaque `export let nom = …` du script, par nom (pour l’inspecteur).
[[nodiscard]] std::unordered_map<std::string, std::string> ExportComments(std::string_view source);

class GameSession {
public:
	/// `eye` / `target` : caméra de l'éditeur ; le joueur se pose sous elle. `sound` facultatif.
	/// Source d'un module de script (`import "nom"`), fournie par le projet.
	using ModuleLoader = std::function<Result<std::string, std::string>(std::string_view name)>;
	/// Liaisons supplémentaires de chaque interpréteur de script (p. ex. le module `ui` de l'application).
	using ScriptHook = std::function<void(script::Interpreter &, EntityId)>;
	GameSession(const Document &doc, const math::FVector3 &eye, const math::FVector3 &target, SoundSystem *sound, ModuleLoader modules = {},
				ScriptHook hook = {});
	[[nodiscard]] const ScriptHook &Hook() const noexcept { return m_hook; }
	/// Souris libre (menus des scripts) plutôt que capturée (regard) : `input.set_mouse_mode`.
	[[nodiscard]] bool MouseFree() const noexcept { return m_mouseFree; }
	/// État partagé entre les scènes d'une même partie (`this.state()` d'une Scene).
	[[nodiscard]] script::Value &State() noexcept { return m_state; }
	void SetState(script::Value state) { m_state = std::move(state); }
	/// Scène demandée par `this.load(nom)` (vide : aucune), fin demandée par `this.quit()`.
	[[nodiscard]] const std::string &RequestedScene() const noexcept { return m_requestedScene; }
	[[nodiscard]] bool QuitRequested() const noexcept { return m_quitRequested; }
	void RequestScene(std::string name) { m_requestedScene = std::move(name); }
	void RequestQuit() noexcept { m_quitRequested = true; }
	/// Rotation absolue d'un groupe (degrés, Y puis X puis Z), position et échelle gardées.
	void SetEntityRotation(EntityId id, const math::FVector3 &degrees);
	/// Échelle absolue d'un groupe (rotation et position gardées).
	void SetEntityScale(EntityId id, const math::FVector3 &scale);
	/// Origine du groupe dans le monde (translation de sa transformation).
	[[nodiscard]] math::FVector3 OriginOf(EntityId id) const;
	/// Chemin d'un groupe dans l'arbre (« Salle/Torche 1/Flamme »).
	[[nodiscard]] std::string PathOf(EntityId id) const;
	/// Groupe d'une référence : chemin (fin de chemin acceptée) ou nom ; 0 sinon.
	[[nodiscard]] EntityId Resolve(std::string_view ref) const;
	/// Propriété (paramètre) d'un nœud ; nil si absente.
	[[nodiscard]] script::Value PropOf(EntityId id, std::string_view key) const;
	[[nodiscard]] float LightIntensityOf(EntityId id) const;
	[[nodiscard]] float LightRangeOf(EntityId id) const;
	[[nodiscard]] SoundSystem *Sound() const noexcept { return m_sound; }
	void SetMouseFree(bool free) noexcept { m_mouseFree = free; }
	~GameSession();
	GameSession(const GameSession &) = delete;
	GameSession &operator=(const GameSession &) = delete;

	/// Un pas de simulation (pas fixe conseillé : 1/120 s).
	void Update(float dt, const GameInput &input);

	// ── Lecture pour l'affichage ─────────────────────────────────────────
	[[nodiscard]] sdl3::Camera3D Camera() const;
	[[nodiscard]] math::FMatrix4 TransformOf(EntityId id) const;
	[[nodiscard]] bool IsVisible(EntityId id) const;
	/// Lampes allumées des groupes visibles (ponctuelles et spots).
	void Lights(sdl3::Lighting &lighting) const;
	[[nodiscard]] const std::deque<GameMessage> &Messages() const noexcept { return m_messages; }
	/// Messages parus depuis l'appel précédent, dans l'ordre (console de l'éditeur).
	[[nodiscard]] std::vector<GameMessage> TakeJournal() { return std::exchange(m_journal, {}); }
	/// Groupe visé au centre de l'image (à portée de main, 3 m) ; 0 sinon.
	[[nodiscard]] EntityId Aimed() const noexcept { return m_aimed; }
	[[nodiscard]] double Time() const noexcept { return m_time; }
	[[nodiscard]] math::FVector3 PlayerFeet() const;
	[[nodiscard]] bool PlayerGrounded() const;
	[[nodiscard]] const physics::World &Physics() const noexcept { return m_physics; }
	[[nodiscard]] size_t ScriptCount() const noexcept { return m_scripts.size(); }
	/// Classe de script instanciée pour un groupe (0 : la scène) ; vide si script sans classe.
	[[nodiscard]] std::string ClassOf(EntityId id) const;
	[[nodiscard]] Result<std::string, std::string> LoadModule(std::string_view name) const;
	/// Calque 2D de la partie (modifié par `hud.*`) et dessins `draw2d.*` du dernier pas.
	[[nodiscard]] const Layer2D &Overlay() const noexcept { return m_overlay; }
	[[nodiscard]] std::span<const Item2D> Drawn() const noexcept { return m_drawn; }

	// ── Actions (scripts) ────────────────────────────────────────────────
	void Say(std::string text, bool error = false);
	/// Message d'un script (`source` : son groupe, 0 pour la scène) ; `line` : ligne d'une erreur.
	void ScriptSay(std::string text, EntityId source, bool error = false, int line = 0);
	[[nodiscard]] EntityId FindByName(std::string_view name) const;
	[[nodiscard]] std::string NameOf(EntityId id) const;
	[[nodiscard]] math::FVector3 CenterOf(EntityId id) const;
	void MoveEntity(EntityId id, const math::FVector3 &delta);
	void SetEntityCenter(EntityId id, const math::FVector3 &center);
	/// Rotation autour de l'axe vertical passant par le centre du groupe ou par son origine (charnière).
	void RotateEntity(EntityId id, float degrees, bool aroundOrigin);
	void PushEntity(EntityId id, const math::FVector3 &velocity);
	[[nodiscard]] math::FVector3 VelocityOf(EntityId id) const;
	void SetVisible(EntityId id, bool visible);
	void SetLight(EntityId id, bool on);
	void PlaySound(EntityId id, const std::string &file);
	void StopSound(EntityId id);
	[[nodiscard]] bool KeyDown(std::string_view name) const;
	// Propriétés modifiables par les scripts (paramètres → nœuds).
	void SetPlayerSpeed(float walk, float run);
	void SetJumpSpeed(float speed) noexcept { m_settings.jumpSpeed = speed; }
	void SetGravity(float gravity) noexcept { m_settings.gravity = gravity; }
	[[nodiscard]] const GameSettings &Settings() const noexcept { return m_settings; }
	void SetLightIntensity(EntityId id, float intensity);
	void SetLightRange(EntityId id, float range);
	void SetLightColor(EntityId id, const modeling::Color &color);
	void SetMass(EntityId id, float mass);
	/// Caméra libre posée par un script (remplace la vue du joueur) ; `ResetCamera` la retire.
	void SetCamera(const math::FVector3 &position, const math::FVector3 &target);
	void ResetCamera() noexcept { m_freeCamera.reset(); }
	[[nodiscard]] bool CameraOverride() const noexcept { return m_freeCamera.has_value(); }
	[[nodiscard]] Item2D *HudItem(std::string_view name) { return m_overlay.FindByName(name); }
	void Draw2D(Item2D item) { m_drawing.push_back(std::move(item)); }
	// ── Scène créée à l'exécution (`scene.spawn`, `node.create_group`…) ────────
	/// Nœud créé par un script : repère invisible (boîte sans maillage) ou groupe vide.
	/// `parent` : nom du groupe parent (résolu par `Resolve`, 0 = racine). Rend l'id créé.
	EntityId SpawnNode(std::string name, std::string_view parent, const math::FVector3 &pos, bool visible, std::string tag);
	/// Groupe vide (hiérarchie, sans maillage ni corps) : `node.create_group`.
	EntityId CreateGroup(std::string name);
	/// Retire un nœud créé à l'exécution (et sa descendance) : `scene.remove`.
	void RemoveEntity(EntityId id);
	/// Noms des nœuds portant l'étiquette `tag` : `scene.find_tag`.
	[[nodiscard]] std::vector<std::string> FindByTag(std::string_view tag) const;
	/// Rotation d'un groupe en degrés (X : tangage, Y : lacet, Z : roulis).
	[[nodiscard]] math::FVector3 RotationOf(EntityId id) const;

private:
	struct Actor {
		EntityId id = 0;
		EntityId parent = 0; ///< hiérarchie du document : les enfants suivent leur parent
		std::string name;
		math::FMatrix4 transform;
		math::FVector3 scale{1.f, 1.f, 1.f}; ///< échelle d'origine (rotations absolues)
		math::FAABB local{};		  ///< boîte du modèle (repère du groupe)
		Behavior behavior;
		physics::BodyId body = -1;	  ///< corps physique (Dynamique, Fantôme)
		physics::ColliderId mesh = -1; ///< maillage cinématique (Solide scripté)
		math::FVector3 bodyOffset{};  ///< centre du corps − translation, pour suivre le corps
		bool visible = true;
		bool lightOn = false;
		bool triggered = false; ///< zone « une seule fois » déjà déclenchée
		bool runtime = false;   ///< créé à l'exécution (`scene.spawn`), retirable
		std::string tag;        ///< étiquette (`scene.find_tag`)
		int voice = 0;
	};
	[[nodiscard]] Actor *Find(EntityId id);
	[[nodiscard]] const Actor *Find(EntityId id) const;
	[[nodiscard]] math::FAABB WorldBox(const Actor &a) const;
	void SyncBody(Actor &a);
	/// Applique `delta` (monde) aux descendants de `a` : ils suivent leur parent.
	void CarryChildren(const Actor &a, const math::FMatrix4 &delta);
	void Touched(Actor &a, physics::UserId other);
	/// Entrée dans une zone fantôme : `OnTrigger` de la scène et de la zone.
	void Triggered(Actor &zone, physics::UserId other);
	void Used(Actor &a);
	void TriggerSound(Actor &a, SoundTrigger trigger);

	physics::World m_physics;
	GameSettings m_settings;
	std::vector<Actor> m_actors;
	std::vector<std::unique_ptr<EntityScript>> m_scripts;
	SoundSystem *m_sound = nullptr;
	physics::BodyId m_player = -1;
	float m_yaw = 0.f, m_pitch = 0.f;
	std::optional<sdl3::Camera3D> m_freeCamera; ///< caméra libre posée par un script
	math::FVector3 m_spawn;
	float m_floor = -50.f; ///< sous le modèle : retour au départ
	EntityId m_aimed = 0;
	std::deque<GameMessage> m_messages;
	std::vector<GameMessage> m_journal;
	Layer2D m_overlay;
	ModuleLoader m_modules;
	ScriptHook m_hook;
	bool m_mouseFree = false;
	script::Value m_state = script::Value(script::Map{});
	std::string m_requestedScene;
	bool m_quitRequested = false;
	std::vector<Item2D> m_drawn, m_drawing; ///< dessins du dernier pas, du pas en cours
	GameInput m_input;
	double m_time = 0.0;
	bool m_started = false, m_usePrev = false;
	EntityId m_nextRuntimeId = 1ull << 40; ///< identifiants des nœuds créés à l'exécution
};

} // namespace sandbox::game
