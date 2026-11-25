#pragma once
// Voxelia — déclarations partagées entre les fichiers de l'application.
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <dataio/json.hpp>
#include <ecs/ecs.hpp>
#include <events/events.hpp>
#include <app/application.hpp>
#include <jobs/jobs.hpp>
#include <script/script.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/render/image_ops.hpp>
#include <sdl3/input/mixer.hpp>
#include <sdl3/core/time.hpp>
#include <ui/retained.hpp>
#include <ui/ui.hpp>
#include <voxel/voxel.hpp>

namespace voxelia {

namespace fs = std::filesystem;

inline constexpr std::string_view kVersion = "1.1.0";

// ── Réglages globaux (data/voxelia/settings.json) ─────────────────────────
// Les valeurs initialisées ici sont les valeurs par défaut (bouton « défaut »).
struct Settings {
	// Graphismes
	int viewDistance = 8;
	int verticalDistance = 3;
	float fov = 75.f;
	bool vsync = true;
	float fogStart = 0.55f; // fraction de la distance de vue
	float brightness = 1.f;
	bool showEntities = true;
	int entityRenderRadius = 3;
	/// Nombre maximal d'entités (pnj, animaux, monstres) qui peuvent apparaître
	/// autour du joueur.
	int maxEntities = 64;
	/// Intervalle (en blocs) entre deux niveaux de détail des textures : au-delà de
	/// `textureLodInterval` blocs on saute un pixel de plus, etc. (16 par défaut).
	float textureLodInterval = 16.f;
	/// Intensité des réflexions spéculaires (nombre de rebonds d'un rayon lumineux).
	float reflectionBounces = 2.f;
	// Commandes
	float sensitivity = 2.5f; // milli-radians par pixel
	bool invertY = false;
	float flySpeed = 11.f;
	float sprintFlySpeed = 32.f;
	// Audio
	float masterVolume = 0.8f;
	float musicVolume = 0.5f;
	float effectsVolume = 0.8f;
	bool menuMusic = true;
	bool shuffleMusic = true;
	// Interface
	float uiScale = 1.f;
	bool showDebug = false;
	bool showTips = true;
	bool menuBackground = true;
	// Performances
	int simulationDistance = 4;
	int entityQueryRadius = 1;
	int maxPendingLoads = 48;
	int maxPendingMeshes = 24;
	int uploadBudgetMB = 16;
	// Jouabilité
	int difficulty = 0; // 0 normal, 1 difficile (plus d'ennemis, plus de vie)
	// État
	std::string lastWorld = "default";
	std::string playerSkin; // skin du joueur (identifiant qualifié)
	std::set<std::string> disabledMods;

	static Settings Load(const fs::path &file);
	bool Save(const fs::path &file) const;
};

// ── Réglages d'un monde (worlds/<nom>/world.json) ─────────────────────────
struct WorldSettings {
	std::string name = "Nouveau monde";
	std::string description;
	int gameMode = 1; // 0 survie, 1 créatif
	bool cheats = true;
	int generator = 0; // voxel::GeneratorType
	float relief = 1.f;
	std::string terrain; // réglages complets du générateur (JSON, phase 6) ; vide : générateur + relief
	uint64_t seed = 0;
	bool creatures = true;
	bool trees = true;
	bool dayCycle = true;
	int dayLengthMinutes = 10;
	float startTime = 0.3f;
	std::string created, lastPlayed;
	double playSeconds = 0;

	static std::optional<WorldSettings> Load(const fs::path &worldDir);
	bool Save(const fs::path &worldDir) const;
	[[nodiscard]] voxel::TerrainSettings Terrain() const;
};

/// Nom de dossier sûr dérivé d'un nom de monde.
[[nodiscard]] std::string WorldFolderName(std::string_view name);
[[nodiscard]] std::string NowText();

// ── Paramètres des mods (data/voxelia/mod_settings.json) ──────────────────
class ModSettingsStore {
public:
	void Load(const fs::path &file);
	bool Save(const fs::path &file) const;
	[[nodiscard]] dataio::NodePtr Get(const voxel::ModInfo &mod, const voxel::ModSetting &s) const;
	void Set(const std::string &mod, const std::string &id, dataio::NodePtr value);
	void Reset(const std::string &mod, const std::string &id);
	void ResetMod(const std::string &mod);
	[[nodiscard]] bool IsOverridden(const std::string &mod, const std::string &id) const;
	[[nodiscard]] dataio::NodePtr &Raw() noexcept { return m_values; }

private:
	dataio::NodePtr m_values = dataio::Node::MakeObject();
};

// ── Contenu : mods, registre de blocs, texture-tableau ────────────────────
/// Skin du joueur enregistré par un mod via le script `voxel.skin`.
struct SkinDef {
	std::string id;		// « core:prince » (qualifié)
	std::string name;	// « prince » (affichage)
	std::string texture; // « core:skins/skin_prince » (résolu dans textures/skins)
	std::string mod;	 // mod propriétaire
};

/// Modèle d'entité (rig) pour le rendu en jeu : associe un kind d'entité à un rig.
struct EntityRig {
	std::string model; // identifiant qualifié du rig (« core:biped »)
	float scale = 1.f; // facteur d'échelle appliqué au modèle
	std::string skin;  // expression de texture du skin (« core:…^colorize(…) »)
};

/// Nom d'affichage d'un identifiant de skin : « core:skins/skin_prince » → « prince ».
[[nodiscard]] inline std::string SkinName(std::string_view id) {
	const size_t colon = id.rfind(':');
	std::string_view tail = colon == std::string_view::npos ? id : id.substr(colon + 1);
	const size_t slash = tail.rfind('/');
	if (slash != std::string_view::npos)
		tail = tail.substr(slash + 1);
	if (tail.starts_with("skin_"))
		tail.remove_prefix(5);
	return std::string(tail);
}

/// Recette de fabrication (chargée depuis `recipes/*.json` des mods) :
/// sans forme (`ingredients`) ou avec motif (`pattern` + `key`).
struct Recipe {
	std::string id;
	std::vector<std::string> pattern;		   // lignes du motif (vide : sans forme)
	std::map<char, std::string> key;		   // caractère → bloc ou « #étiquette »
	std::vector<std::string> ingredients;	   // sans forme
	std::string result;						   // bloc résultant
	int count = 1;
};

/// Objet d'inventaire : 0 = vide ; < `kItemBase` : bloc (`BlockId`) ; ≥ `kItemBase` :
/// objet des mods (`items/*.json`, indice + `kItemBase`).
using ItemId = uint32_t;
inline constexpr ItemId kItemBase = 0x10000;
[[nodiscard]] constexpr bool IsBlockItem(ItemId id) noexcept { return id != 0 && id < kItemBase; }

/// Objet déclaré par un mod (`items/*.json`, `"type": "item"`).
/// Outil / arme porté par un objet (« tool » du JSON : pioche, hache, pelle, épée…).
struct ToolDef {
	std::string kind;	// « pickaxe », « axe », « shovel », « sword »…
	float speed = 1.f;  // multiplicateur de vitesse de minage
	int damage = 1;		// dégâts de base (arme)
	int level = 0;		// palier (bois 0, pierre 1, fer 2…) : blocs nécessitant un niveau
};

struct ItemDef {
	std::string name;		 // « core:leather_helmet »
	std::string displayName; // « Casque en cuir »
	std::string mod;
	std::vector<std::string> tags;
	int maxStack = 64;
	std::string armorSlot; // « head », « chest », « legs », « feet » (vide : pas une armure)
	int armorPoints = 0;
	std::vector<uint8_t> icon; // RGBA 16×16 (vide : pas d'image)
	sdl3::Color color{160, 160, 170, 255}; // couleur moyenne de l'icône (armure portée)
	std::optional<ToolDef> tool;		  // outil / arme (vide : objet ordinaire)
	[[nodiscard]] bool HasTag(std::string_view t) const noexcept;
	[[nodiscard]] bool IsTool() const noexcept { return tool.has_value(); }
	/// L'outil convient au bloc (son `kind` correspond, ou outil à main nulle : 1×).
	[[nodiscard]] float MiningSpeed(std::string_view bestTool) const noexcept;
};

/// Emplacements d'armure (ordre des cases de l'inventaire).
enum class ArmorSlot : uint8_t { Head, Chest, Legs, Feet, Count };
[[nodiscard]] std::optional<ArmorSlot> ParseArmorSlot(std::string_view id) noexcept;

/// Pile d'objets : un objet (bloc ou objet de mod) et un nombre.
struct ItemStack {
	ItemId id = 0;
	int count = 0;
	[[nodiscard]] bool Empty() const noexcept { return count <= 0 || id == 0; }
};

struct Content {
	voxel::ModManager mods;
	voxel::BlockRegistry blocks;
	std::shared_ptr<const voxel::MeshContext> meshContext;
	std::vector<uint8_t> textureLayers; // RGBA voxel::kTextureSize² par couche
	uint32_t layerCount = 0;
	std::vector<voxel::BlockId> hotbarCandidates;
	std::vector<Recipe> recipes; // recettes de fabrication et de cuisson
	size_t missingTextures = 0;
	double loadSeconds = 0;
	core::RobinMap<std::string, uint16_t, core::Hasher<std::string>> layerOf;
	size_t syncedBlocks = 0;
	std::vector<SkinDef> skins; // skins enregistrés par les scripts des mods
	std::vector<ItemDef> items; // objets des mods (hors blocs)
	std::map<std::string, ItemId, std::less<>> itemIds;
	std::map<std::string, EntityRig, std::less<>> entityRigs;  // kind → rig (rendu en jeu)
	std::map<std::string, voxel::Model, std::less<>> rigModels; // rig qualifié → modèle
	std::map<std::string, dataio::NodePtr, std::less<>> contentDefs; // « block:core:tnt » → JSON brut

	/// Objet par nom (bloc d'abord, puis objet de mod) ; 0 si inconnu.
	[[nodiscard]] ItemId FindItem(std::string_view name) const;
	[[nodiscard]] const ItemDef *Item(ItemId id) const noexcept;
	[[nodiscard]] std::string ItemName(ItemId id) const;
	[[nodiscard]] std::string ItemLabel(ItemId id) const;
	[[nodiscard]] int MaxStack(ItemId id) const noexcept;
	/// Charge les objets des mods (`items/*.json`) et leurs icônes.
	void LoadItems();

	bool Load(const fs::path &modsDir, const std::set<std::string> &disabled, jobs::ThreadPool &pool);
	bool SyncNewBlocks();
	/// Rechargement à chaud des définitions (JSON des blocs, alias de textures) et des
	/// textures, dans le même registre (identifiants stables) : à suivre d'un remaillage.
	bool ReloadDefinitions(jobs::ThreadPool &pool);
	/// Recalcule les couches des textures dont l'image source est l'un de `files` ;
	/// retourne le nombre de couches mises à jour.
	size_t ReloadTextureFiles(std::span<const fs::path> files);
	/// Couches de texture et tables de maillage (étapes communes au chargement et au rechargement).
	size_t BuildLayers(jobs::ThreadPool &pool);
	/// Enregistre un skin (remplace si l'identifiant existe déjà).
	void RegisterSkin(SkinDef skin);
	[[nodiscard]] const SkinDef *FindSkin(std::string_view id) const noexcept;
	/// Chemin d'image d'un skin, ou `nullopt` si introuvable.
	[[nodiscard]] std::optional<fs::path> SkinTexturePath(std::string_view texture) const;
	/// Charge les rigs des créatures (kind → rig) et les modèles rig des mods.
	void LoadEntityRigs();
	/// Charge les définitions brutes (blocs, objets, recettes, entités) pour `content.get`/`content.list`.
	void LoadContentDefinitions();
	/// Résout le skin d'une entité en image RGBA pleine résolution (compose + teinte).
	[[nodiscard]] std::optional<sdl3::Surface> EntitySkinImage(const EntityRig &rig) const;
	/// Récupère le rig d'un kind d'entité (ou `nullptr`).
	[[nodiscard]] const EntityRig *FindEntityRig(std::string_view kind) const noexcept;
};

/// Son émis par une entité (`SoundEmitter`) cette image.
struct SoundSource {
	uint64_t key = 0; // entité (bits ECS)
	std::string sound;
	math::FVector3 position;
	float volume = 1.f, range = 16.f;
	bool loop = true;
};

// ── Rechargement à chaud : surveillance des fichiers des mods ─────────────
/// Fichiers modifiés depuis la dernière analyse, par nature.
struct FileChanges {
	std::vector<fs::path> scripts;	   // .script : logique rechargée (monde conservé)
	std::vector<fs::path> definitions; // JSON de contenu, maillages .obj/.b3d : blocs redéfinis
	std::vector<fs::path> textures;	   // .png : couches de texture recalculées
	std::vector<fs::path> models;	   // models/*.json : catalogue de l'atelier
	[[nodiscard]] bool Empty() const noexcept { return scripts.empty() && definitions.empty() && textures.empty() && models.empty(); }
	[[nodiscard]] size_t Count() const noexcept { return scripts.size() + definitions.size() + textures.size() + models.size(); }
};

/// Analyse des dates de modification des fichiers des mods, une fois par seconde, sur un
/// thread du pool (aucun blocage de l'image) ; le thread principal applique les changements.
class HotReload {
public:
	bool enabled = true;
	void Update(const voxel::ModManager &mods, jobs::ThreadPool &pool);
	/// Changements détectés depuis le dernier appel (et oubliés).
	[[nodiscard]] FileChanges Take();
	/// Le fichier vient d'être écrit par le Studio : changement appliqué à la prochaine analyse.
	[[nodiscard]] uint64_t Scans() const noexcept { return m_scans; }

private:
	using Snapshot = std::map<fs::path, fs::file_time_type>;
	struct ScanResult {
		Snapshot snapshot;
		FileChanges changes;
	};
	Snapshot m_known;
	bool m_primed = false;
	uint64_t m_next = 0, m_scans = 0;
	jobs::AsyncValue<ScanResult> m_scan;
	FileChanges m_pending;
};

// ── Audio : musique (lecture en continu, fondu enchaîné) et sons d'interface ──
class AudioSystem {
public:
	/// Index des sons des mods (`sounds/assets.json` : `{type: sound, id, path|paths}`).
	void IndexSounds(const voxel::ModManager &mods);
	/// Fichier d'un son : identifiant qualifié (« core:step ») ou chemin « mod:sounds/x.ogg ».
	[[nodiscard]] std::optional<fs::path> ResolveSound(std::string_view id) const;
	[[nodiscard]] const std::vector<std::string> &SoundIds() const noexcept { return m_soundIds; }
	/// Émetteurs des entités : pistes en boucle démarrées/arrêtées, gain selon la distance,
	/// position 3D dans le repère de l'auditeur (x à droite, y en haut, z vers l'arrière).
	void UpdateEmitters(std::span<const SoundSource> sources, const math::FVector3 &listener, float yaw);
	bool Init(const fs::path &musicDir, const fs::path &uiSoundDir);
	void SetVolumes(float master, float music, float effects);
	void SetShuffle(bool shuffle) noexcept { m_shuffle = shuffle; }
	/// Ambiance de menu (piste « main ») ou playlist de jeu.
	void PlayMenu();
	void PlayGame();
	void Stop();
	void Next();
	void Click();
	void Update(float dt);
	[[nodiscard]] bool Ready() const noexcept { return m_mixer.has_value(); }
	[[nodiscard]] std::string CurrentTrack() const;
	[[nodiscard]] const std::vector<fs::path> &Tracks() const noexcept { return m_tracks; }

private:
	struct Voice {
		std::optional<sdl3::MixTrack> track;
		std::optional<sdl3::MixAudio> audio;
		float gain = 0, target = 0;
		fs::path path;
	};
	void Start(const fs::path &file, bool loop);

	std::optional<sdl3::MixerContext> m_context;
	std::optional<sdl3::Mixer> m_mixer;
	Voice m_voices[2];
	int m_current = 0;
	std::vector<fs::path> m_tracks;
	fs::path m_menuTrack;
	std::optional<sdl3::MixAudio> m_click;
	std::optional<sdl3::MixTrack> m_clickTrack;
	float m_master = 0.8f, m_music = 0.5f, m_effects = 0.8f;
	bool m_shuffle = true, m_inGame = false;
	size_t m_next = 0;
	uint64_t m_rng = 0x9e3779b97f4a7c15ULL;
	std::map<std::string, fs::path> m_sounds;
	std::vector<std::string> m_soundIds;
	struct Emitter {
		std::string sound;
		std::optional<sdl3::MixTrack> track;
		bool seen = false;
	};
	std::map<uint64_t, Emitter> m_emitters;
	std::map<fs::path, sdl3::MixAudio> m_audioCache;
};

class Game;
class InGameEditor;

// ── Hôte de scripts : interpréteur + liaisons vers le jeu ─────────────────
class ScriptHost {
public:
	ScriptHost(Game &game, const ModSettingsStore &modSettings);
	void LoadMods(const voxel::ModManager &mods);
	std::string RunConsole(const std::string &line);
	/// Exécute un fichier source dans l'interpréteur de la partie (Studio).
	std::string RunSource(const std::string &source, const std::string &name);
	/// Émet un évènement ; vrai si un gestionnaire a renvoyé `false` (action par défaut annulée).
	/// Les paramètres des gestionnaires sont liés par nom aux clés de la table (`fn(x, y, z, block)`).
	bool Emit(std::string_view name, const script::Value &payload);
	/// Clic sur un objet du jeu : gestionnaires `voxel.interact(cible, fn)` dont la cible est
	/// l'une de `targets` (« core:lamp », « #door », « block », « entity », « * »), puis la
	/// fonction globale `call` déclarée par le bloc ; vrai si l'action par défaut est annulée.
	bool Interact(std::span<const std::string> targets, const script::Value &event, std::string_view call = {});
	[[nodiscard]] size_t InteractionCount() const noexcept { return m_interactions.size(); }
	void Tick(float dt);
	[[nodiscard]] size_t HookCount() const;
	[[nodiscard]] size_t SystemCount() const noexcept { return m_systems.size(); }
	[[nodiscard]] size_t CommandCount() const noexcept { return m_commands.size(); }
	[[nodiscard]] script::Interpreter &Vm() noexcept { return m_vm; }

	struct Command {
		std::string id, usage, description;
		script::Value fn;
	};
	[[nodiscard]] const std::vector<Command> &Commands() const noexcept { return m_commands; }

private:
	void BindApi();
	script::Value EntityValue(ecs::Entity e) const;
	ecs::Entity ToEntity(const script::Value &v) const;
	script::Value SettingValue(std::string_view id) const;

	Game &m_game;
	const ModSettingsStore &m_modSettings;
	const voxel::ModManager *m_mods = nullptr;
	script::Interpreter m_vm;
	events::NamedEvents<script::Value> m_hooks;
	std::vector<events::Connection> m_connections;
	struct System {
		std::string name;
		script::Value fn;
		bool failed = false;
	};
	std::vector<System> m_systems;
	std::vector<Command> m_commands;
	struct Interaction {
		std::string target, mod;
		script::Value fn;
	};
	std::vector<Interaction> m_interactions;
	bool m_cancelled = false; // un gestionnaire de l'évènement courant a renvoyé `false`
	std::string m_currentMod;
};

// ── Session de jeu ─────────────────────────────────────────────────────────
struct PlayerInput {
	float moveX = 0, moveZ = 0;
	bool jump = false, descend = false, sprint = false;
	bool crouch = false; // Maj (au sol) : s'accroupir
	float lookDX = 0, lookDY = 0;
	bool primary = false, secondary = false, use = false;
	int hotbarDelta = 0;
	int hotbarSelect = -1;
	// Évènements ponctuels de l'image (remis à zéro après chaque mise à jour).
	int gesture = 0;			   // `voxel::AvatarGesture` (0 : aucun)
	bool cycleSwimStyle = false;   // K : nage suivante (crawl, brasse, dos crawlé)
	bool cycleCamera = false;	   // F6 : première personne → vue de dos → vue de face → vue de côté
	bool swapHands = false;		   // X : échange main droite / main gauche
};

/// Vue de la caméra du joueur.
enum class CameraMode : uint8_t { FirstPerson, ThirdBack, ThirdFront, ThirdSide, Count };

struct GameOptions {
	bool scripts = true;	 // exécuter les mods (faux : fond animé du menu)
	bool persistent = true;	 // sauvegarder à la fermeture
	bool cinematic = false;	 // caméra en rotation lente (fond du menu)
	bool preview = false;	 // aperçu du menu : monde partiel (surface seule), sans entités
};

class Game {
public:
	Game(Content &content, Settings &settings, const ModSettingsStore &modSettings, jobs::ThreadPool &pool, voxel::Renderer &renderer,
		 ui::Context &ui);
	~Game();

	bool Open(const fs::path &worldDir, const WorldSettings &world, GameOptions options = {});
	void Update(const PlayerInput &input, float dt);
	void Render(app::Application &ctx);
	void DrawHud(sdl3::Canvas &canvas, float dt);
	void Save();
	void ApplySettings();
	void ApplyWorldSettings(const WorldSettings &w);
	/// Recharge la logique des mods (scripts) sans redémarrer le monde :
	/// l'état (entités, blocs, base) est conservé, seuls les hooks, systèmes et
	/// commandes sont réenregistrés.
	void ReloadScripts();
	/// Demande un rechargement à chaud (différé : sûr depuis un script ou la console).
	void RequestReloadScripts() noexcept { m_reloadPending = true; }
	/// Relâche les modèles rig des entités (poignées GPU) : les rigs modifiés par le
	/// Studio (modèles et créatures) sont re-téléversés à la prochaine image.
	void ReloadEntityModels();

	[[nodiscard]] voxel::World &World() noexcept { return *m_world; }
	[[nodiscard]] ecs::World &Ecs() noexcept { return m_ecs; }
	[[nodiscard]] ecs::Entity Player() const noexcept { return m_player; }
	[[nodiscard]] Content &GetContent() noexcept { return m_content; }
	[[nodiscard]] ScriptHost *Scripts() noexcept { return m_scripts.get(); }
	[[nodiscard]] WorldSettings &WorldInfo() noexcept { return m_worldSettings; }
	[[nodiscard]] const fs::path &WorldDir() const noexcept { return m_worldDir; }
	[[nodiscard]] float TimeOfDay() const noexcept { return m_timeOfDay; }
	void SetTimeOfDay(float t) noexcept { m_timeOfDay = t - std::floor(t); }
	[[nodiscard]] bool Flying() const noexcept { return m_flying; }
	void SetFlying(bool f) noexcept { m_flying = f; }
	[[nodiscard]] bool CanFly() const noexcept { return m_worldSettings.gameMode == 1 || m_worldSettings.cheats; }
	void Teleport(const math::FVector3 &p);
	/// Applique un skin au joueur (composant ECS `Skin`).
	void SetPlayerSkin(std::string_view id);
	/// Téléverse la texture de skin du joueur vers le renderer.
	void UploadPlayerSkin();
	[[nodiscard]] const voxel::Camera &Camera() const noexcept { return m_camera; }
	/// Caméra imposée (mode éditeur : caméra libre) ; le streaming des chunks la suit.
	void SetCameraOverride(std::optional<voxel::Camera> camera) noexcept { m_cameraOverride = camera; }
	[[nodiscard]] bool CameraOverridden() const noexcept { return m_cameraOverride.has_value(); }
	void Say(std::string text, sdl3::Color c = {0, 0, 0, 0});
	void Log(std::string text);
	/// Journalise et diffuse l'évènement aux mods ; vrai si un gestionnaire l'a annulé.
	bool Emit(std::string_view type, const script::Value &payload, std::string_view mod = "game");
	[[nodiscard]] std::deque<std::string> &ConsoleLines() noexcept { return m_console; }
	std::string ExecuteConsole(const std::string &line);
	size_t SpawnCrowd(std::string_view kind, size_t count, float radius);
	[[nodiscard]] voxel::BlockId SelectedBlock() const noexcept;
	[[nodiscard]] int64_t Tick() const noexcept { return m_tickCount; }
	[[nodiscard]] bool WorldLoaded() const noexcept { return m_worldLoadEmitted; }
	[[nodiscard]] double LastUpdateMs() const noexcept { return m_lastUpdateMs; }
	[[nodiscard]] const ecs::Schedule &Schedule() const noexcept { return m_schedule; }
	/// Lumières dynamiques (`LightEmitter`) les plus proches de `eye` (au plus 16).
	void CollectLights(const math::FVector3 &eye, std::vector<voxel::PointLight> &out);
	/// Sons des entités (`SoundEmitter`) audibles depuis `listener`.
	void CollectSounds(const math::FVector3 &listener, std::vector<SoundSource> &out);
	/// Définit la force d'un effet de post-traitement (« underwater », « heat »,
	/// « glare », « stun », « suffocate ») depuis les scripts de mods.
	void SetPostEffect(std::string_view name, float strength);
	/// Ouvre/ferme le menu d'inventaire (fabrication, four).
	void ToggleInventory() noexcept;
	[[nodiscard]] bool Creative() const noexcept { return m_worldSettings.gameMode == 1; }
	/// Difficulté courante (0 normale, 1 difficile) — réglage global.
	[[nodiscard]] int Difficulty() const noexcept { return m_settings.difficulty; }
	/// Multiplicateur de vie des créatures selon la difficulté.
	[[nodiscard]] float DifficultyHealthScale() const noexcept { return m_settings.difficulty >= 1 ? 1.5f : 1.f; }

	// ── Avatar du joueur : postures, nages, gestes ───────────────────────
	[[nodiscard]] voxel::AvatarState PlayerState() const noexcept { return m_avatarState; }
	[[nodiscard]] voxel::SwimStyle SwimStyle() const noexcept { return m_swimStyle; }
	void SetSwimStyle(voxel::SwimStyle s) noexcept { m_swimStyle = s; }
	[[nodiscard]] bool Crouching() const noexcept { return m_crouching; }
	/// Joue un geste (coucou, grands signes, petit coucou, enfilage d'armure).
	void PlayGesture(voxel::AvatarGesture g);
	/// S'assoit sur le bloc (chaise) / se couche dans le lit en (x, y, z) ; faux si impossible.
	bool SitOn(const math::IVec3 &block);
	bool SleepIn(const math::IVec3 &block);
	/// Se relève (siège, lit).
	void StandUp();
	[[nodiscard]] CameraMode GetCameraMode() const noexcept { return m_cameraMode; }
	/// Oriente le regard (radians).
	void SetLook(float yaw, float pitch) noexcept {
		m_yaw = yaw;
		m_pitch = std::clamp(pitch, -1.55f, 1.55f);
	}
	[[nodiscard]] float Yaw() const noexcept { return m_yaw; }
	/// Accroupi maintenu sans touche (scripts, accessibilité).
	void SetCrouchLock(bool on) noexcept { m_crouchLock = on; }
	/// Clic simulé sur l'objet visé à la prochaine mise à jour (1 : gauche, 2 : droit).
	void QueueClick(bool right) noexcept { m_pendingClick = right ? 2 : 1; }
	/// Texte de recherche de l'inventaire créatif.
	void SetCreativeSearch(std::string text) { m_creativeSearch = std::move(text); }
	void SetCameraMode(CameraMode m) noexcept { m_cameraMode = m; }

	// ── Inventaire ────────────────────────────────────────────────────────
	/// Ajoute `count` × `item` (barre d'outils puis inventaire) ; retourne le reste non rangé.
	int Give(ItemId item, int count);
	/// Retire jusqu'à `count` × `item` ; retourne le nombre retiré.
	int Take(ItemId item, int count);
	[[nodiscard]] int CountOf(ItemId item) const noexcept;
	/// Objet tenu en main droite (case active de la barre) ou gauche.
	[[nodiscard]] const ItemStack &Held(bool rightHand = true) const noexcept;
	[[nodiscard]] const ItemStack &ArmorPiece(ArmorSlot s) const noexcept { return m_armor[size_t(s)]; }
	/// Équipe la pièce d'armure (anime le geste « enfiler ») ; l'ancienne pièce est rendue.
	bool EquipArmor(ItemId item);
	[[nodiscard]] int ArmorPoints() const noexcept;
	/// Son ponctuel à la position donnée (porte, lampe, bouton…).
	void PlaySound(std::string sound, const math::FVector3 &at, float volume = 1.f);
	[[nodiscard]] bool InventoryOpen() const noexcept { return m_inventoryOpen; }
	[[nodiscard]] const voxel::MechanicsStats &Mechanics() const noexcept { return m_mechanics; }
	/// Simulation suspendue (ticks, scripts, entités) : le streaming des chunks, le
	/// joueur et le rendu continuent (Studio : pause / pas à pas).
	void SetPaused(bool paused) noexcept { m_paused = paused; }
	[[nodiscard]] bool Paused() const noexcept { return m_paused; }
	/// Avance la simulation suspendue de `ticks` pas fixes.
	void Step(int ticks = 1) noexcept { m_stepRequests += ticks; }

private:
	void CreatePlayer();
	void UpdatePlayer(const PlayerInput &in, float dt);
	void Interact(const PlayerInput &in, float dt);
	void FixedTick(float dt);
	void PumpMeshes();
	void DrawInventoryMenu(sdl3::Canvas &c);
	void DrawSurvivalInventory(sdl3::Canvas &c, const sdl3::FRect &area, float s);
	void DrawCreativeInventory(sdl3::Canvas &c, const sdl3::FRect &area, float s);
	/// Case d'inventaire : icône, quantité, clic gauche (prendre/poser/échanger), clic droit (moitié/un).
	void DrawSlot(sdl3::Canvas &c, const sdl3::FRect &r, ItemStack &slot, std::string_view id, float s, bool creativeSource = false,
				  std::optional<ArmorSlot> armor = std::nullopt);
	void DrawItemIcon(sdl3::Canvas &c, const sdl3::FRect &r, ItemId item, uint8_t alpha = 255);
	void DrawTooltip(sdl3::Canvas &c, float x, float y, ItemId item, float s);
	void DrawPaperDoll(sdl3::Canvas &c, const sdl3::FRect &r);
	/// Ingrédients d'une recette (objet ou « #étiquette » → quantité).
	[[nodiscard]] std::map<std::string, int> RecipeNeeds(const Recipe &rec) const;
	[[nodiscard]] int CountMatching(std::string_view ingredient) const;
	[[nodiscard]] bool CanCraft(const Recipe &rec) const;
	void Craft(const Recipe &rec);
	/// Atlas des icônes (blocs en perspective isométrique, objets) pour le HUD.
	void EnsureIconAtlas(sdl3::Canvas &c);
	void UpdateAvatar(const PlayerInput &in, float dt);
	void UpdateSwimming(const PlayerInput &in, float dt, math::FVector3 &v, voxel::Body &b);
	void UploadAvatarMesh();
	void RefreshAvatarTexture();
	/// (Re)charge le rig édité de l'avatar (`<core>/models/player_avatar.json`) si modifié.
	void EnsureAvatarRig();
	/// Pose du rig de l'avatar : animation nommée selon l'état, sinon pose procédurale.
	[[nodiscard]] std::vector<voxel::PartOffset> AvatarRigPose() const;
	/// Poignée GPU du rig d'une entité (téléversé une fois, mis en cache par kind).
	[[nodiscard]] uint64_t EntityModel(std::string_view kind);
	/// Clic gauche/droit sur un bloc ou une entité : fonctions des mods, puis action par défaut.
	bool ClickBlock(const voxel::RayHit &hit, bool right);
	[[nodiscard]] script::Value ClickPayload(bool right) const;
	bool ClickEntity(ecs::Entity e, bool right);
	[[nodiscard]] bool SetCrouch(bool crouch);
	/// Pose le bloc et ses parties (portes, lits) ; faux si une cellule est occupée.
	bool PlaceBlock(const math::IVec3 &p, voxel::BlockId id);
	/// Casse le bloc et les autres cellules de son bloc multiple.
	voxel::BlockId BreakBlock(const math::IVec3 &p);
	/// Minage progressif : avance le cassage du bloc ciblé (dureté × outil tenu) et le casse
	/// quand l'avancement atteint 1. `dt` : durée de l'image.
	void MineBlock(const math::IVec3 &p, float dt);
	/// Interrompt le minage (cible perdue ou bouton relâché).
	void StopMining() noexcept;
	[[nodiscard]] math::IVec3 RootOf(const math::IVec3 &p, voxel::BlockId id) const;

	Content &m_content;
	Settings &m_settings;
	const ModSettingsStore &m_modSettings;
	jobs::ThreadPool &m_pool;
	voxel::Renderer &m_renderer;
	ui::Context &m_ui;
	ecs::World m_ecs;
	ecs::Schedule m_schedule;
	std::unique_ptr<voxel::World> m_world;
	std::unique_ptr<ScriptHost> m_scripts;
	GameOptions m_options;
	WorldSettings m_worldSettings;
	fs::path m_worldDir;
	ecs::Entity m_player;
	voxel::Camera m_camera;
	float m_yaw = 0, m_pitch = 0;
	bool m_flying = true;
	float m_timeOfDay = 0.3f;
	float m_tickAccumulator = 0;
	bool m_paused = false;
	std::optional<voxel::Camera> m_cameraOverride;
	voxel::MechanicsStats m_mechanics;
	int m_stepRequests = 0;
	int64_t m_tickCount = 0;
	int64_t m_savedTick = 0;
	int m_hotbarIndex = 0;
	static constexpr size_t kHotbarSlots = 9, kInventorySlots = 36; // barre (0..8) + 3 rangées
	std::vector<ItemStack> m_slots;										 // barre d'outils puis inventaire principal
	std::array<ItemStack, size_t(ArmorSlot::Count)> m_armor{};
	ItemStack m_offhand; // main gauche
	ItemStack m_noItem;
	std::optional<voxel::RayHit> m_target;
	std::deque<std::string> m_console;
	std::vector<voxel::MeshResult> m_meshScratch;
	std::vector<voxel::ChunkKey> m_unloadScratch;
	std::vector<voxel::BoxInstance> m_boxes;
	std::map<std::string, float> m_postEffects; // effets de post-traitement définis par les scripts
	float m_hunger = 20.f;					   // faim (0..20)
	float m_air = 10.f;						   // air restant sous l'eau (0..10)
	bool m_inventoryOpen = false;			   // menu d'inventaire / fabrication / four ouvert
	ItemStack m_cursor;						   // pile tenue par le curseur (clic d'inventaire)
	std::string m_creativeSearch;			   // recherche de l'inventaire créatif
	int m_creativeTab = 0;					   // catégorie de l'inventaire créatif
	ItemId m_hoverItem = 0;					   // objet survolé (info-bulle)
	// Icônes du HUD : atlas de 32×32 par objet (blocs puis objets).
	std::optional<sdl3::Texture> m_iconAtlas;
	size_t m_iconAtlasBlocks = 0, m_iconAtlasItems = 0;
	static constexpr int kIconSize = 32, kIconColumns = 64;
	// Avatar et états du joueur.
	voxel::AvatarState m_avatarState = voxel::AvatarState::Stand;
	voxel::SwimStyle m_swimStyle = voxel::SwimStyle::Crawl;
	voxel::AvatarPose m_pose;
	voxel::AvatarGesture m_gesture = voxel::AvatarGesture::None;
	float m_gestureTime = 0, m_animTime = 0, m_walkPhase = 0, m_swing = 1.f, m_horizontalSpeed = 0;
	bool m_crouching = false;
	bool m_crouchLock = false;
	int m_pendingClick = 0;
	std::optional<math::IVec3> m_seat, m_bed; // bloc occupé (chaise, lit)
	math::FVector3 m_bodyOrigin{};			   // centre du modèle (monde) ; cap du corps
	float m_bodyYaw = 0;
	math::FVector3 m_restAnchor{};			   // centre du corps assis / couché
	std::optional<std::pair<ecs::Entity, float>> m_targetEntity; // entité visée (distance)
	bool m_leftCancelled = false;			   // clic gauche pris en charge par un mod (pas de casse)
	math::FVector3 m_lieDirection{0, 0, 1};	   // direction de la tête dans le lit
	float m_sleepTime = 0;
	float m_fallStartY = 0;
	bool m_wasOnGround = true;
	CameraMode m_cameraMode = CameraMode::FirstPerson;
	std::string m_avatarTextureKey; // objets tenus / armure de la texture courante
	std::vector<voxel::ModelVertex> m_avatarMesh;
	std::vector<uint8_t> m_avatarAtlas; // atlas 128×64 (skin + objets + armure), partagé avec le rig
	// Avatar édité (rig, forme + animations) rechargé depuis <core>/models/player_avatar.json.
	std::optional<voxel::Model> m_avatarRig;
	fs::path m_avatarRigPath;
	std::filesystem::file_time_type m_avatarRigMtime{};
	uint64_t m_avatarModelHandle = 0; // poignée UploadModel du rig (0 : avatar procédural)
	// Entités : poignée de modèle par kind (rig téléversé une fois).
	std::map<std::string, uint64_t, std::less<>> m_entityModels;
	float m_entityAnimTime = 0;
	bool m_prevPrimary = false, m_prevSecondary = false;
	struct OneShot {
		uint64_t key;
		std::string sound;
		math::FVector3 position;
		float volume, remaining;
	};
	std::vector<OneShot> m_oneShots;
	uint64_t m_nextSoundKey = 1;
	std::string m_actionHint; // indication sous le viseur (« Clic droit : ouvrir »)
	float m_itemNameTimer = 0;	 // affichage du nom de l'objet en main après un changement
	int m_lastShownSlot = -1;
	ItemId m_lastShownItem = 0;
	std::optional<sdl3::Texture> m_skinPreview; // skin du joueur (aperçu de l'inventaire)
	std::string m_skinPreviewKey;
	bool m_worldLoadEmitted = false;
	double m_lastUpdateMs = 0;
	float m_breakCooldown = 0;
	// Minage progressif : bloc ciblé et avancement (0..1), prochain son « hit ».
	math::IVec3 m_miningBlock{};
	float m_miningProgress = 0.f;
	float m_hitSoundTimer = 0.f;
	float m_stepDistance = 0.f; // distance parcourue depuis le dernier pas (son de marche)
	float m_swimSoundTimer = 0.f; // prochain son d'ambiance de nage
	bool m_reloadPending = false; // rechargement à chaud demandé (différé)
};

class App;

// ── Voxelia Studio : IDE dans une seconde fenêtre ─────────────────────────
class Studio {
public:
	explicit Studio(App &app);
	~Studio();
	bool Open();
	void Close();
	[[nodiscard]] bool IsOpen() const noexcept { return m_window != nullptr; }
	[[nodiscard]] sdl3::WindowID WindowId() const noexcept;
	void Frame(float dt);
	void Raise();
	/// Enregistre la prochaine image du Studio en PNG.
	void RequestScreenshot(std::string path);
	/// Ouvre un fichier dans un onglet (à la prochaine image).
	void QueueOpen(fs::path path);
	/// Des fichiers des mods ont été rechargés à chaud (caches, catalogue, documents ouverts).
	void FilesChanged(const FileChanges &changes);
	/// Active l'onglet `id` du dock, quelle que soit sa région (« 3d » : vue 3D plein écran).
	void ActivateTab(std::string_view id);
	/// Avant de détruire la partie : annule les chargements de panneaux et attend leur
	/// thread (il lit la base du monde).
	void ReleaseGame();

private:
	struct Impl;
	App &m_app;
	std::unique_ptr<Impl> m_impl;
	sdl3::Window *m_window = nullptr; // non possédée (appartient à Impl)
	bool m_closeRequested = false;	  // fermeture demandée (détectée via EventsManager)
	size_t m_closeBindingId = 0;	  // liaison EventsManager de fermeture de fenêtre
};

// ── Application : état partagé par la boucle, les menus et le Studio ──────
enum class Screen { Main, NewWorld, Worlds, WorldSettings, Mods, ModDetail, ModSettings, Settings, Skins, Playing, Paused };

struct Options {
	std::string world;
	bool fresh = false;
	uint64_t seed = 0;
	bool seedSet = false;
	fs::path mods = "games/voxelia/mods";
	fs::path data; // ~/.local/share/apps-lib/voxelia (posé par ParseArgs)
	int frames = -1;
	std::string screenshot;
	std::vector<std::string> exec;
	bool vsyncOverride = false, vsync = true;
	bool studio = false;
	std::vector<fs::path> studioFiles; // --open : fichiers ouverts dans le Studio
	std::vector<std::string> studioTabs; // --studio-tab : onglets du dock activés (captures)
	bool editor = false;				  // --editor : partie lancée en mode éditeur du monde
	std::string screen; // capture d'un écran de menu précis (tests)
	bool noInput = false; // --no-input : souris et clavier ignorés par le joueur (captures automatisées)
};

class App : public app::Application {
public:
	Options opt;
	Settings settings, draft; // draft : copie éditée par la page Paramètres
	fs::path settingsFile, modSettingsFile;
	ModSettingsStore modSettings, modDraft;
	jobs::ThreadPool &pool = jobs::ThreadPool::Global();
	std::unique_ptr<ui::Ui> interface; // interface retenue (factory builder), rendue par Begin/Render/End
	std::unique_ptr<voxel::Renderer> renderer;
	Content content;
	bool contentDirty = false;
	std::unique_ptr<Game> game;		  // partie en cours
	std::unique_ptr<Game> background; // monde animé derrière les menus
	AudioSystem audio;
	HotReload hotReload; // rechargement à chaud des scripts, définitions et textures des mods
	std::vector<std::string> reloadLog; // derniers rechargements (affichés par le Studio)
	std::unique_ptr<Studio> studio;
	Screen screen = Screen::Main;
	Screen settingsReturn = Screen::Main;
	PlayerInput player;
	std::unique_ptr<InGameEditor> editor; // mode éditeur de la fenêtre du jeu (F4)
	float editorWheel = 0;
	bool execDone = false; // commandes --exec appliquées une fois le monde chargé
	bool consoleOpen = false;
	std::string consoleLine;
	bool mouseCaptured = false;
	float displayScale = 1.f;
	// État des menus
	WorldSettings newWorld;
	std::string newSeedText;
	std::string selectedWorld;
	WorldSettings editWorld;
	std::string editWorldFolder;
	bool confirmDelete = false;
	std::string selectedMod;
	Screen modReturn = Screen::Mods;
	int settingsCategory = 0;
	std::string modFilter;
	int modCategory = 0;
	float tipTimer = 0;
	size_t tipIndex = 0;
	// Sélecteur de skin
	std::string selectedSkin;		 // skin affiché dans le menu
	Screen skinReturn = Screen::Main; // écran de retour
	std::string skinQuery;			 // filtre de recherche
	int skinPage = 0;
	/// Orientation de l'aperçu 3D du skin (glisser pour tourner, inertie, rotation au repos).
	struct SkinView {
		float yaw = 0.45f, pitch = 0.08f;
		float velocity = 0.f;
		float idle = 0.f;
		float lastX = 0.f, lastY = 0.f;
		bool dragging = false;
	} skinView;
	std::map<std::string, sdl3::Texture> skinTextures; // aperçus mis en cache

	[[nodiscard]] fs::path WorldsDir() const { return opt.data / "worlds"; }
	[[nodiscard]] std::vector<std::pair<std::string, WorldSettings>> ListWorlds() const;
	void CaptureMouse(bool on);
	bool StartGame(const std::string &folder, const std::optional<WorldSettings> &fresh);
	void LeaveGame();
	/// Détruit la partie (sauvegarde) après avoir libéré les lectures du Studio.
	void ResetGame();
	void EnsureBackground();
	void ApplySettings();
	void OpenStudio();
	void ReloadContentIfNeeded();
	/// Applique les fichiers modifiés détectés par `hotReload` (scripts, définitions, textures).
	void ApplyHotReload();

protected:
	bool OnInit() override;
	void OnUpdate(double frameSeconds) override;
	void OnOffscreen() override;
	void OnRender(const sdl3::FRect &area, double alpha) override;
	void OnShutdown() override;
};

/// Console de jeu (menus.cpp).
void DrawConsole(App &app);
/// Construit l'interface conservée (factory builder) à l'initialisation (menus.cpp).
void BuildInterface(App &app);

} // namespace voxelia
