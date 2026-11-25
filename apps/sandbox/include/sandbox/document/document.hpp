#pragma once
/**
 * @file document.hpp
 * @brief Document du sandbox : ce que l'utilisateur a modélisé, indépendant de l'affichage.
 *
 * Comme dans SketchUp :
 *  - la géométrie LIBRE (`Root`) : arêtes et faces qui collent entre elles ;
 *  - des GROUPES (`Entity`) : une géométrie isolée dans son propre repère (transformation),
 *    que l'on déplace d'un bloc et que l'on « ouvre » pour la modifier ;
 *  - une palette de MATÉRIAUX commune (les indices des faces s'y rapportent dans tous les
 *    modèles, recopiée dans chacun par `SyncMaterials` pour les opérations qui fusionnent) ;
 *  - des BALISES (visibilité par groupe) et des SCÈNES (points de vue nommés).
 *
 * Aucune dépendance au GPU ni à l'interface : le document se teste et s'enregistre seul
 * (JSON lisible, `.sandbox`). L'annulation garde des instantanés (`History`).
 */
#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <core/core.hpp>
#include <dataio/file.hpp>
#include <math/math.hpp>

#include "sandbox/document/overlay.hpp"
#include "sandbox/modeling/model.hpp"
#include "sandbox/modeling/tools.hpp"

namespace sandbox {

using EntityId = uint64_t;
using ComponentId = uint32_t; ///< définition de composant (0 : groupe unique)
inline constexpr EntityId kRootId = 0; ///< contexte d'édition : la géométrie libre

/// Rôle physique d'un groupe dans le mode Jeu.
enum class BodyKind : uint8_t {
	Solid,	 ///< décor immobile qui arrête le joueur et les objets
	Dynamic, ///< objet qui tombe, rebondit, se pousse
	Ghost,	 ///< traversable (zone de déclenchement des scripts)
};
/// Moment où le son d'un groupe part.
enum class SoundTrigger : uint8_t { Start, Touch, Use, Script };

/// Comportement d'un groupe en mode Jeu : corps, son, lumière, script.
enum class LightKind : uint8_t { Point, Spot };

/// Valeur réglée d'un paramètre de script (`export let vitesse = 4` dans la classe du script).
struct ScriptParam {
	String name;
	std::variant<double, bool, String> value;
	[[nodiscard]] bool operator==(const ScriptParam &) const = default;
};
using ScriptParams = std::vector<ScriptParam>;

struct Behavior {
	BodyKind body = BodyKind::Solid;
	float mass = 1.f;
	float bounce = 0.2f;   ///< restitution 0..1
	float friction = 0.6f; ///< 0..1
	String sound;		   ///< fichier de assets/sounds (vide : muet)
	float volume = 1.f;
	bool loop = false;
	bool spatial = true; ///< atténué et réparti selon la position du joueur
	SoundTrigger soundTrigger = SoundTrigger::Start;
	bool light = false; ///< lampe au centre du groupe
	LightKind lightKind = LightKind::Point;
	float spotAngle = 35.f; ///< spot : demi-angle du cône (degrés), dirigé selon l'axe −Y du groupe
	float penumbra = 0.3f;	///< spot : part adoucie du bord du cône (0..1)
	modeling::Color lightColor{1.f, 0.82f, 0.55f, 1.f};
	float lightIntensity = 10.f;
	float lightRange = 8.f;
	String script;	   ///< fonctions OnStart(), OnUpdate(dt), OnTouch(autre), OnUse()
	String scriptFile; ///< fichier du script, relatif au document (vide : script intégré)
	/// Zone de déclenchement (corps Fantôme) : évènement transmis à `OnTrigger` quand le joueur
	/// ou un objet dynamique y entre ; `triggerOnce` : une seule fois par partie.
	String triggerEvent;
	bool triggerOnce = false;
	ScriptParams params; ///< paramètres du script réglés pour ce groupe (sinon valeur par défaut)

	[[nodiscard]] bool operator==(const Behavior &) const = default;
};

/// Environnement de la scène (comme le WorldEnvironment de Godot) : ciel, soleil, brouillard,
/// exposition ; utilisé par la vue d'édition et par le mode Jeu.
struct EnvironmentSettings {
	modeling::Color zenith{0.624f, 0.761f, 0.902f, 1.f}; ///< haut du ciel
	modeling::Color horizon{0.933f, 0.949f, 0.957f, 1.f};
	modeling::Color ground{0.741f, 0.737f, 0.698f, 1.f}; ///< sous l'horizon
	modeling::Color sunColor{1.f, 0.97f, 0.92f, 1.f};
	float sunHour = 14.f;	   ///< direction du soleil (6 h : est, 18 h : ouest)
	float sunIntensity = 2.2f;
	float ambient = 1.f;	   ///< part du ciel dans l'éclairage
	modeling::Color fog{0.725f, 0.804f, 0.878f, 1.f};
	float fogDensity = 0.f;	   ///< 0 : sans brouillard
	float exposure = 1.05f;
	[[nodiscard]] bool operator==(const EnvironmentSettings &) const = default;
};

/// Ambiances prêtes à l'emploi (Jour, Couchant, Nuit, Brume).
[[nodiscard]] std::span<const std::pair<std::string_view, EnvironmentSettings>> EnvironmentPresets();

/// Réglages du mode Jeu (visite à la première personne).
struct GameSettings {
	float gravity = 9.81f;
	float walkSpeed = 4.f, runSpeed = 7.5f;
	float jumpSpeed = 4.8f;
	float eyeHeight = 1.65f;
	float stepHeight = 0.35f; ///< marche franchie sans sauter
	String script;			  ///< script de la scène : OnStart(), OnUpdate(dt), OnTouch(nom, autre)
	String scriptFile;		  ///< son fichier, relatif au document (vide : intégré)
	ScriptParams params;	  ///< paramètres du script de la scène
	[[nodiscard]] bool operator==(const GameSettings &) const = default;
};

struct Entity {
	EntityId id = 0;
	String name;
	math::FMatrix4 transform = math::FMatrix4::Identity();
	modeling::Model model; ///< géométrie dans le repère du groupe
	uint32_t tag = 0;	   ///< balise (0 : « Sans balise »)
	bool visible = true;
	bool locked = false;
	Behavior behavior;
	/// Composant (comme dans SketchUp) : les groupes d'un même composant sont des instances qui
	/// partagent leur géométrie ; chacune garde sa transformation, son nom et son comportement.
	ComponentId component = 0;
	/// Hiérarchie (comme l'arbre de scène de Godot) : groupe parent (0 : racine). `transform`
	/// reste celle du monde ; déplacer un parent (`SetTransform`) emporte ses descendants.
	EntityId parent = 0;
	String type = "Node3D"; ///< type de nœud (Node3D, MeshInstance, Light, Camera, SpawnPoint…)
};

/// Définition de composant : la géométrie vit dans ses instances (identiques), enregistrée une fois.
struct ComponentDef {
	ComponentId id = 0;
	String name;
};

struct Tag {
	String name;
	bool visible = true;
};

/// Point de vue nommé (« Scène » de SketchUp).
struct SceneView {
	String name;
	math::FVector3 eye{8.f, 6.f, 10.f}, target{};
	float fovDegrees = 35.f;
	bool orthographic = false;
	float orthoHeight = 10.f;
};

class Document {
public:
	Document();

	// ── Géométrie ────────────────────────────────────────────────────────
	[[nodiscard]] modeling::Model &Root() noexcept { return m_root; }
	[[nodiscard]] const modeling::Model &Root() const noexcept { return m_root; }
	[[nodiscard]] std::vector<Entity> &Entities() noexcept { return m_entities; }
	[[nodiscard]] const std::vector<Entity> &Entities() const noexcept { return m_entities; }
	[[nodiscard]] Entity *Find(EntityId id) noexcept;
	[[nodiscard]] const Entity *Find(EntityId id) const noexcept;
	/// Modèle d'un contexte d'édition (racine ou groupe) ; nullptr si le groupe n'existe pas.
	[[nodiscard]] modeling::Model *ModelOf(EntityId id) noexcept;
	/// Transformation d'un contexte (identité pour la racine).
	[[nodiscard]] math::FMatrix4 TransformOf(EntityId id) const noexcept;

	/// Nouveau groupe (le modèle reçoit la palette du document).
	Entity &AddEntity(String name, modeling::Model model, const math::FMatrix4 &transform = math::FMatrix4::Identity());
	bool RemoveEntity(EntityId id);
	/// Faces et arêtes libres sélectionnées → nouveau groupe (retirées de la racine). 0 si rien.
	EntityId MakeGroup(const modeling::Selection &selection, String name = {});
	/// Le groupe redevient de la géométrie libre (transformation appliquée). Faux s'il n'existe pas.
	bool Explode(EntityId id);
	/// Copie d'un groupe (et de ses descendants) décalée de `offset`. 0 si le groupe n'existe pas.
	EntityId Duplicate(EntityId id, const math::FVector3 &offset);

	// ── Hiérarchie ───────────────────────────────────────────────────────
	/// Enfants directs (0 : groupes de premier niveau), dans l'ordre du document.
	[[nodiscard]] std::vector<EntityId> Children(EntityId parent) const;
	/// Descendants (profondeur d'abord), sans le groupe lui-même.
	[[nodiscard]] std::vector<EntityId> Descendants(EntityId id) const;
	/// `ancestor` est un parent (direct ou non) de `id`.
	[[nodiscard]] bool IsAncestor(EntityId ancestor, EntityId id) const noexcept;
	/// Ancêtre de `id` (ou `id`) dont le parent est `context` ; 0 si `id` n'est pas sous `context`.
	[[nodiscard]] EntityId TopUnder(EntityId id, EntityId context) const noexcept;
	/// Nouveau parent (0 : racine), position du monde conservée ; faux si cycle ou inconnu.
	bool Reparent(EntityId id, EntityId parent);
	/// Transformation du monde d'un groupe ; ses descendants le suivent rigidement.
	void SetTransform(EntityId id, const math::FMatrix4 &world);
	/// Regroupe des groupes sous un nouveau groupe vide (origine : bas de leur boîte) ; rend son id.
	EntityId GroupEntities(std::span<const EntityId> ids, String name = {});
	/// Verrouillé lui-même ou par un ancêtre.
	[[nodiscard]] bool IsLocked(const Entity &e) const noexcept;
	/// Boîte englobante (repère du monde) de la géométrie visible.
	[[nodiscard]] math::FAABB Bounds() const;
	/// Boîte d'un groupe dans le repère du monde.
	[[nodiscard]] math::FAABB WorldBounds(const Entity &e) const;
	[[nodiscard]] String UniqueName(std::string_view base) const;

	// ── Composants (instances partagées) ─────────────────────────────────
	/// Fait du groupe la première instance d'un nouveau composant (nommé comme lui) ; rend son id.
	ComponentId MakeComponent(EntityId id);
	/// Le groupe quitte son composant (géométrie conservée). Faux s'il n'en avait pas.
	bool MakeUnique(EntityId id);
	[[nodiscard]] const ComponentDef *FindComponent(ComponentId id) const noexcept;
	[[nodiscard]] const std::vector<ComponentDef> &Components() const noexcept { return m_components; }
	[[nodiscard]] size_t InstanceCount(ComponentId id) const noexcept;
	/// Recopie la géométrie de `source` dans les autres instances de son composant ; vrai si
	/// au moins une a changé.
	bool SyncInstances(EntityId source);

	// ── Matériaux, balises, scènes ───────────────────────────────────────
	[[nodiscard]] const std::vector<modeling::Material> &Materials() const noexcept { return m_materials; }
	uint32_t AddMaterial(String name, modeling::Color color);
	void SetMaterialColor(uint32_t index, modeling::Color color);
	/// Remplace un matériau de la palette (couleur, rendu, nom) dans tous les modèles.
	void SetMaterial(uint32_t index, const modeling::Material &material);
	/// Recopie la palette dans tous les modèles (avant Append / ExtractFaces).
	void SyncMaterials();
	[[nodiscard]] std::vector<Tag> &Tags() noexcept { return m_tags; }
	[[nodiscard]] const std::vector<Tag> &Tags() const noexcept { return m_tags; }
	[[nodiscard]] bool IsVisible(const Entity &e) const noexcept;
	[[nodiscard]] std::vector<SceneView> &Views() noexcept { return m_views; }
	[[nodiscard]] const std::vector<SceneView> &Views() const noexcept { return m_views; }
	/// Calque 2D (HUD du mode Jeu).
	[[nodiscard]] Layer2D &Overlay() noexcept { return m_overlay; }
	[[nodiscard]] const Layer2D &Overlay() const noexcept { return m_overlay; }
	[[nodiscard]] EnvironmentSettings &Environment() noexcept { return m_environment; }
	[[nodiscard]] const EnvironmentSettings &Environment() const noexcept { return m_environment; }
	[[nodiscard]] GameSettings &Game() noexcept { return m_game; }
	[[nodiscard]] const GameSettings &Game() const noexcept { return m_game; }

	// ── Objets réutilisables (fichiers `.object`, format « sandbox.object ») ──
	/// Enregistre le groupe : géométrie (et sa palette), comportement, script (contenu inclus).
	[[nodiscard]] Option<String> SaveObject(EntityId id, const std::filesystem::path &file, String description = {}) const;
	/// Insère l'objet en nouveau groupe posé en `at` (matériaux rapprochés par nom) ; rend son id.
	[[nodiscard]] Result<EntityId, String> InsertObject(const std::filesystem::path &file, const math::FVector3 &at);

	// ── Fichier ──────────────────────────────────────────────────────────
	[[nodiscard]] String ToJson() const;
	[[nodiscard]] static Result<Document, String> FromJson(std::string_view json);
	/// Enregistre le document et, à côté de lui, les scripts qui ont un fichier (`scriptFile`) :
	/// un projet = un dossier avec la scène (`.sandbox`) et ses scripts. `options` permet de
	/// fractionner un fichier volumineux (voir `dataio::WriteFile`).
	[[nodiscard]] Option<String> Save(const String &path, const dataio::FileOptions &options = {}) const;
	/// Charge le document et lit les fichiers de scripts (relatifs à son dossier) ; réassemble
	/// automatiquement un fichier fractionné (`nom.1.ext`, `nom.2.ext`…).
	[[nodiscard]] static Result<Document, String> Load(const String &path);

	/// Avertissements du dernier chargement (objets ou modèles introuvables…), à signaler.
	[[nodiscard]] std::vector<String> &LoadWarnings() noexcept { return m_loadWarnings; }

	/// Incrémentée à chaque modification hors géométrie (palette, groupes, balises) : cache d'affichage.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }
	void Touch() noexcept;

private:
	modeling::Model m_root;
	std::vector<Entity> m_entities;
	std::vector<ComponentDef> m_components;
	std::vector<modeling::Material> m_materials;
	std::vector<Tag> m_tags;
	std::vector<SceneView> m_views;
	GameSettings m_game;
	EnvironmentSettings m_environment;
	Layer2D m_overlay;
	std::vector<String> m_loadWarnings;
	EntityId m_nextId = 1;
	uint64_t m_revision = 1;
};

/// Annulation par instantanés du document (copie complète : simple et sûre à cette échelle).
class History {
public:
	explicit History(size_t capacity = 64) : m_capacity(capacity) {}
	/// Mémorise l'état AVANT une modification nommée (« Pousser/Tirer »…).
	void Push(const Document &before, String label);
	[[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
	[[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }
	[[nodiscard]] const String &UndoLabel() const noexcept { return m_undo.back().label; }
	[[nodiscard]] const String &RedoLabel() const noexcept { return m_redo.back().label; }
	[[nodiscard]] size_t UndoCount() const noexcept { return m_undo.size(); }
	[[nodiscard]] size_t RedoCount() const noexcept { return m_redo.size(); }
	/// Libellés des commandes annulables, de la plus récente à la plus ancienne.
	[[nodiscard]] std::vector<String> UndoLabels() const;
	/// Libellés des commandes à rétablir, de la plus récente à la plus ancienne.
	[[nodiscard]] std::vector<String> RedoLabels() const;
	/// Remplace `doc` par l'état précédent (l'état courant passe dans « rétablir »).
	bool Undo(Document &doc);
	bool Redo(Document &doc);
	/// Rétablit l'état d'avant la dernière modification sans la proposer à « rétablir »
	/// (correction d'une cote : l'opération est refaite autrement).
	bool Revert(Document &doc);
	void Clear() noexcept;

private:
	struct Step {
		Document state;
		String label;
	};
	std::deque<Step> m_undo, m_redo;
	size_t m_capacity;
};

} // namespace sandbox
