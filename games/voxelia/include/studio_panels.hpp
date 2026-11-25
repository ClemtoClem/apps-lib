#pragma once
// Voxelia Studio — panneaux de la phase 1 (architecture unifiée moteur + studio) :
// espaces de travail (dispositions du dock), World Outliner (entités vivantes de l'ECS),
// propriétés d'entité modifiables à la volée, gestionnaire de mods.
//
// Le Studio n'a aucune copie du monde : ces panneaux lisent et écrivent directement les
// composants de la partie en cours, sur le thread principal, entre deux mises à jour.
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ui/dock.hpp>
#include <ui/ui.hpp>
#include <voxel/inspect.hpp>

#include "app.hpp"

namespace voxelia::studio {

// ── Panneaux et espaces de travail ─────────────────────────────────────────

/// Tous les panneaux que le dock peut accueillir ; chacun s'affiche dans n'importe
/// quelle région (les onglets se déplacent d'une région à l'autre par glisser-déposer).
[[nodiscard]] const std::vector<ui::DockTab> &PanelCatalog();

/// Espace de travail : une disposition du dock dédiée à une activité (monde en jeu,
/// modélisation, peinture 2D, scripting, propriétés d'entités). Chaque espace garde
/// ses propres modifications (onglets déplacés, tailles, régions repliées).
struct Workspace {
	std::string id, title;
	char32_t icon = 0;
	std::string tooltip;
	ui::DockState layout;	// disposition courante
	ui::DockState defaults; // disposition d'origine (« Réinitialiser la disposition »)
};

[[nodiscard]] std::vector<Workspace> DefaultWorkspaces();
/// Dispositions enregistrées (JSON) ; les espaces et panneaux inconnus sont ignorés.
bool LoadWorkspaces(const fs::path &file, std::vector<Workspace> &workspaces, std::string &active);
bool SaveWorkspaces(const fs::path &file, const std::vector<Workspace> &workspaces, std::string_view active);

// ── World Outliner ─────────────────────────────────────────────────────────

struct OutlinerState {
	std::string text;
	int category = 0; // 0 : toutes, sinon EntityCategory + 1
	int distance = 0; // index dans les distances proposées (0 : sans limite)
	voxel::OutlinerResult cache;
	uint64_t nextRefresh = 0; // rafraîchissement à 5 Hz (et à chaque changement de filtre)
	float scrollY = 0;
	uint64_t lastClickTicks = 0, lastClickBits = 0; // double-clic
};

struct OutlinerEvent {
	ecs::Entity entity;
	bool focus = false; // double-clic : cadrer l'entité dans la vue 3D
};

/// Liste virtualisée des entités vivantes, groupées par catégorie. Retourne l'entité
/// cliquée (sélection) ou double-cliquée (cadrage).
std::optional<OutlinerEvent> DrawOutliner(ui::Context &u, const sdl3::FRect &r, OutlinerState &st, Game &game, ecs::Entity selected,
										  const math::FVector3 &from, uint64_t now);

// ── Propriétés de l'entité sélectionnée ────────────────────────────────────

struct PropertiesActions {
	bool focus = false;		// cadrer dans la vue 3D
	bool toggleFollow = false; // suivre / ne plus suivre avec la caméra
	bool despawned = false;	// entité supprimée du monde
};

/// Inspecteur : une section par composant présent, valeurs modifiables à la volée
/// (position, orientation, vitesse, taille de la boîte de collision, gravité, santé…).
PropertiesActions DrawProperties(ui::Context &u, const sdl3::FRect &r, Game &game, ecs::Entity e, bool following,
								  const std::vector<std::string> &sounds);

// ── Performances (phase 4) ─────────────────────────────────────────────────

/// Historique des mesures (échantillonnées 10 fois par seconde).
struct PerfState {
	std::vector<std::pair<std::string, std::deque<float>>> series;
	uint64_t next = 0;
	float frameMs = 0;
	bool paused = false;
};

/// Métriques en temps réel : image, mise à jour, systèmes ECS (courbes), entités, chunks,
/// rendu, mécanique, rechargements à chaud.
void DrawPerformance(ui::Context &u, const sdl3::FRect &r, PerfState &st, App &app, Game *game, float dt);

// ── Mécanismes (phase 3) ───────────────────────────────────────────────────

/// Éditeur nodal des mécanismes : entités en nœuds, joints en liens (glisser d'une sortie
/// vers une entité pour créer un joint), réglages du lien sélectionné, démonstrations.
struct MechanismState {
	std::vector<std::pair<uint64_t, math::FVector2>> nodes; // Uid → position dans le graphe
	float panX = 0, panY = 0;
	uint64_t dragNode = 0;
	math::FVector2 dragOffset{};
	uint64_t linkFrom = 0; // sortie en cours de glissement
	uint64_t selOwner = 0;
	int selJoint = -1;
	bool panning = false;
	float lastX = 0, lastY = 0;
};

struct MechanismEvent {
	ecs::Entity select; // entité à sélectionner (clic sur un nœud)
	bool focus = false;
};

std::optional<MechanismEvent> DrawMechanisms(ui::Context &u, const sdl3::FRect &r, MechanismState &st, Game &game, ecs::Entity selected);

/// Démonstration : cabine d'ascenseur sur poulie avec contrepoids, porte coulissante,
/// lumière et son ; posée devant `origin`. Retourne la cabine.
ecs::Entity BuildDemoElevator(Game &game, const math::FVector3 &origin, const std::vector<std::string> &sounds);
/// Démonstration : porte sur charnière (moteur) devant `origin`.
ecs::Entity BuildDemoDoor(Game &game, const math::FVector3 &origin);

// ── Gestionnaire de mods ───────────────────────────────────────────────────

using OpenFileFn = std::function<void(const fs::path &, const std::string &)>;

/// Mods chargés (le jeu de base est lui-même le mod « core ») : version, dépendances,
/// contenu fourni, activation, rechargement à chaud des scripts, ouverture des fichiers.
void DrawMods(ui::Context &u, const sdl3::FRect &r, App &app, Game *game, std::string &status, const OpenFileFn &openFile);

} // namespace voxelia::studio
