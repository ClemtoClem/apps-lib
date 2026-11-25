# Voxelia — Guide complet

Ce document décrit tout ce qui est implémenté dans Voxelia, organisé en cinq parties :

1. [Les modules](#1-les-modules) — la bibliothèque C++ en couches.
2. [Le code du jeu C++](#2-le-code-du-jeu-c) — la session de jeu et ses systèmes.
3. [Le code du Studio C++](#3-le-code-du-studio-c) — l'IDE des mods.
4. [Programmer des mods](#4-programmer-des-mods) — créer des blocs, objets, entités et scripts via JSON.
5. [Utiliser le Studio](#5-utiliser-le-studio) — mode d'emploi de l'atelier.

---

# 1. Les modules

Voxelia (`apps/voxelia`) assemble des modules C++23 répartis en couches ; un module ne
dépend que de sa couche ou d'une couche inférieure :

`foundation → engine → platform → graphics → ui → applications`

| Couche | Module | Rôle |
|---|---|---|
| foundation | `core` | `Result`/`Option`, chaînes UTF, hachage 64 bits, **clés de Morton** 2D/3D, **`RobinMap`** (hachage Robin Hood), journal, horloges |
| foundation | `math` | Vecteurs/matrices (profondeur [0,1]), quaternions, AABB, frustum, rayons, **bruit** simplex/fBm/ridged, RNG xoshiro |
| foundation | `jobs` | **`ThreadPool`** (`std::jthread`, 3 priorités), `ParallelFor`, `WaitGroup`, **`AsyncValue<T>`** (valeur calculée en arrière-plan) |
| foundation | `diagnostics` | Assertions, **traceur** par fil (zones/images/compteurs, captures `.vcap`) |
| foundation | `dataio` | **JSON tolérant** (commentaires `//`, virgules finales), flux binaires varint, CRC-32, RLE |
| foundation | `ecs` | ECS à **archétypes** (colonnes SoA, requêtes en cache, `ParEach`), ordonnanceur de systèmes |
| foundation | `events` | `Signal`, `EventBus` à double tampon (événements nommés des mods) |
| foundation | `sql` | Base embarquée (tables en colonnes, index B+arbre), SQL, instantané CRC, **`RegionStore`** |
| foundation | `script` | **Langage des mods** : lexeur, analyseur, interpréteur, liaisons hôte |
| engine | `voxel` | Le **moteur voxel** (détaillé ci-dessous) |
| platform | `sdl3` | Wrapper C++ de SDL3 (fenêtres, renderer 2D, GPU, image, audio) |
| graphics | `gfx` | Rendu **SDL_GPU** des chunks (shaders SPIR-V), **canvas 2D** par lots, composition d'images |
| ui | `ui` | **Interface en mode immédiat** (`ui::Context`) et **interface conservée** inspirée de Flutter (`ui::WidgetTree`, factory builder), `DockLayout`, `Viewport3D` |

## 1.1 Le moteur `voxel` (`libs/engine/voxel`)

| Fichier | Contenu |
|---|---|
| `types.hpp` | Constantes (chunk 32³, clé de Morton, faces), `BlockId`, plage verticale du monde |
| `blocks.hpp` | `BlockDef` : matériau, textures par face, lumière, forme, **liquides**, **blocs qui tombent**, **réflectance**, **orientation** ; `BlockRegistry` |
| `chunk.hpp` | `Chunk` : voxels 1D (creux si uniforme) + **orientations par voxel** + liste d'entités |
| `world.hpp` | `World` : streaming, accès voxels (`SetBlock`, `GetOrient`), structures, entités, physique |
| `terrain.hpp` | `TerrainGenerator` : relief, biomes, climat, **géologie profonde**, minerais, strates, structures |
| `avatar.hpp` | Bipède 6 parties : postures, nage, gestes, maillage posé, **rig éditable** |
| `model.hpp` | `Model` : formes (nodebox, croix, rig, .obj, .b3d), animations, `BuildQuads`/`BuildModelMesh` |
| `mesher.hpp` | Maillage glouton + AO, formes détaillées, connectivité |
| `mechanics.hpp` | Joints entre entités : attache, charnière, **glissière**, **poulie**, corde |
| `orientation.hpp` | **Orientation d'un bloc** : 24 rotations du cube, noms et angles |
| `entities.hpp` | Composants ECS : `Transform`, `Body`, `Kind`, `Skin`, `Health`, `Wander`… et sérialisation |
| `mods.hpp` | Découverte/chargement des mods : blocs, config, textures, **structures** |
| `storage.hpp` | Persistance : blobs de chunks (voxels + orientations + entités), base relationnelle |
| `inspect.hpp` | Collecte/filtre des entités pour l'Outliner du Studio |

## 1.2 `graphics.gfx` et `graphics.ui`

- **`gfx/voxel_renderer.hpp`** : rendu des chunks (opaque/translucide/détail), boîtes
  instanciées, **lumières typées**, **ombres**, **modèles texturés** (skin du joueur, rigs
  des entités), **post-traitement** plein écran.
- **`gfx/canvas.hpp`** : dessin 2D par lots (polices, icônes Material, formes, texte).
- **`gfx/image.hpp`** : images RGBA, composition `a^b^tint(...)`, redimensionnement.
- **`graphics/ui.hpp`** : widgets immédiats (boutons, curseurs, onglets, arborescence, champs de
  saisie, barres de défilement), `ui::Context::Tree` (arborescence de fichiers).
- **`ui/retained.hpp`** : interface **conservée** inspirée de Flutter — arborescence de widgets
  construite par **factory builder** (`ui::Column`, `ui::Dialog`, `ui::Setting*`, `ui::MenuButton`…),
  rendue automatiquement par `WidgetTree::Begin / Render / End` ; `runApp(fn)` ré-évalue l'arbre à
  chaque image (comme le `build` de Flutter), `ui::Custom` héberge le dessin personnalisé.
- **`ui/dock.hpp`** : disposition ancrable (gauche/centre/droite/bas, onglets déplaçables).
- **`ui/viewport3d.hpp`** : texture 3D de la scène du jeu réutilisée comme widget.
- **`ui/layout.hpp`** : placement dynamique (`Place`, `FitScale`, `Row`/`Column`, `Grid`).

---

# 2. Le code du jeu C++

`games/voxelia/src/` assemble le moteur dans une application SDL3.

| Fichier | Rôle |
|---|---|
| `main.cpp` | Boucle SDL, rendu de l'interface conservée (`interface->Begin/Render/End`), console, captures, **monde d'arrière-plan du menu** (aperçu partiel, sans entités), application du rechargement à chaud |
| `game.cpp` | **Session de jeu** : joueur, interactions, ordonnanceur ECS, streaming, maillages → GPU, HUD, orientation des blocs |
| `content.cpp` | Mods → registre de blocs → texture-tableau 128×128, **registre de contenu** (blocs/objets/recettes/entités), rigs des entités |
| `scripting.cpp` | Interpréteur Script et **API des mods** (`voxel`, `world`, `entity`, `game`, `content`, `sql`) |
| `hud.cpp` | HUD : barre d'outils, cœurs/faim/air/armure, aide, viseur |
| `menus.cpp` | Menus (principal, nouveau monde, mondes, paramètres, pause) — `BuildInterface` construit l'arborescence par **factory builder** ; chaque écran est un widget (`BuildNewWorldMenu`, `BuildSettingsMenu`…) ou un `ui::Custom` (menu principal, grilles de mods/skins) |
| `settings.cpp` | Réglages persistants, paramètres de monde et de mods |
| `hotreload.cpp` | **Rechargement à chaud** : surveillance des fichiers des mods |
| `world_editor.cpp` | Éditeur du monde en direct : caméra libre, gizmo, superpositions |
| `audio.cpp` | Sons et musiques (SDL3_mixer), spatialisation des émetteurs |

## 2.1 Le monde et sa génération

- **Chunk de 32³** voxels (`BlockId` 16 bits), tableau plat 1D, stockage **creux** (un chunk
  uniforme n'alloue rien). Chaque voxel porte en plus une **orientation** (`uint8_t`, 0..23),
  elle aussi creuse. Clé = **Morton 3D** ; table `RobinMap`.
- **Géologie réaliste** : la surface/les océans sont à Y = 0 ; en profondeur, croûte
  (35 km) → manteau (2 270 km) → noyau externe (2 370 km) → mer de lave (100 km) →
  **inversion de gravité** → **monde miroir** (surface inversée ≈ −4 740 288 m). `RockAt`
  et `OreAt` répartissent roches, strates (`m_strata`) et minerais par profondeur.
- **Biomes, climat, relief** : bruits de Perlin/simplex (continents, collines, montagnes),
  arbres, végétation, grottes 3D, structures auto-placées (`StructureRule`), créatures
  initiales.
- **Streaming asynchrone** : requêtes DB/génération sur le pool (sans dépendance entre
  chunks), maillage glouton avec AO en parallèle, téléversement GPU groupé (budget/image).
- **Physique des blocs** : blocs qui **tombent** (sable, gravier), **liquides** qui
  s'écoulent (viscosité via `liquidTick`, mélange lave/eau), déclenchés événementiellement
  (`ScheduleBlockPhysics`).
- **Grille spatiale d'entités** : chaque chunk liste ses entités ; `ForEachEntityNear` est
  O(1) par chunk voisin.

## 2.2 Le joueur et son avatar

- Bipède 6 parties articulées (skin 64×64, ancien format converti), postures (debout, marche,
  accroupi, assis, couché, nage, chute libre, vol), gestes (coucou, saluts, enfiler une
  armure), objets tenus et armure (atlas 128×64). **Première personne** : tête masquée,
  bras qui suivent le regard.
- **Avatar éditable** : le Studio édite le **rig du joueur** (`core/models/player_avatar.json`,
  forme + animations). Le jeu le recharge (détection par date de fichier), pose les animations
  nommées selon l'état (`walk`, `idle`, `swim`…) ou retombe sur la pose procédurale, et rend
  les **objets tenus et l'armure** posés sur les parties du rig.

## 2.3 Les entités et leurs rigs

- Composants ECS : `Transform`, `Velocity`, `Body`, `Health`, `Kind`, `Skin`, `Wander`,
  `LightEmitter`, `SoundEmitter`, `Joints`…
- **Rendu des entités** : chaque `kind` peut référencer un **rig** (modèle articulé + skin)
  dans `creatures/*.json` (`rig.model`, `scale`, `skins`). Le jeu charge ces rigs
  (`models/rigs.json`), résout les skins (composition `^colorize`, superpositions), et anime
  les créatures (marche quand elles se déplacent, `idle` sinon). Repli : boîte colorée.
- L'apparition naturelle utilise `content.get("entity", id)` et `content.list("entity")`.

## 2.4 Éclairage et rendu

- **Lumières typées** (jusqu'à 16) : ponctuelle, spot (cône), directionnelle ; couleur,
  intensité, portée.
- **Réflectance** : nombre de rebonds configurable (métaux, verre, glace brillants).
- **Ombres portées** : grille d'opacité 3D (48³) autour de la caméra + ray-marching ; les
  textures **transparentes** (feuillage, verre, eau) filtrent partiellement la lumière.
- **Textures 128×128** avec mipmaps (8 niveaux) et **LOD** par distance configurable.
- **Post-traitement** plein écran : sous-marin, chaleur, éblouissement, étourdissement,
  asphyxie — pilotables par les mods (`voxel.post_effect(nom, force)`).

## 2.5 HUD et inventaire

- HUD : cœurs, faim, air, armure ; barre d'outils (icônes isométriques), nom de l'objet.
- **Survie** : sac 3×9, fabrication (recettes avec motif `pattern`/`key` ou `ingredients`),
  **four** (cuisson), armure, main gauche.
- **Créatif** : onglets, recherche, réserve infinie, corbeille.
- Inventaire ouvert avec Tab, fermé avec Échap ; `PlayerInput` neutralisé quand ouvert.

## 2.6 Minage progressif, outils et difficulté

- **Minage progressif** : casser un bloc prend un temps proportionnel à sa **dureté**
  (`gameplay.hardness`) divisé par la **vitesse de l'outil tenu**. Le bon outil (pioche/hache/
  pelle, `gameplay.tool`) est rapide ; la main nue ou un mauvais outil sont lents. En **créatif**,
  tout se casse en un coup.
- **Outils** (`items/*.json`, `type: tool`) : `tool: {kind, speed, damage, level}`. `speed` est
  le multiplicateur de minage, `damage` les **dégâts de l'arme** (une épée frappe plus fort que
  la main nue).
- **Difficulté** (`Paramètres → Interface → Jouabilité`) : `game.difficulty` (0 normale,
  1 difficile) est exposé aux scripts des mods pour moduler l'apparition et la vie des créatures.
- **Sons de jeu spatialisés** : `game.sound(id, x, y, z [, volume])` joue un **son 3D ponctuel**
  depuis les scripts (jukebox, bruit des pnj/monstres…). Le jeu émet aussi des sons par
  **matériau de bloc** (`sounds` du bloc) : marche (`_footstep`), coup de minage (`_hit`), casse
  (`_dug`/`break`), pose (`_place`), ramassage (`pickup`) et nage (`water`). Le volume des effets
  se règle dans **Paramètres → Audio** (`effectsVolume`).

## 2.7 Orientation des blocs

- Quand on pose un bloc, il **fait face au joueur** (cap déduit de la position du joueur).
- **Accroupi**, le bloc est posé **verticalement** (tronc d'arbre), sans auto-face.
- La définition JSON du bloc peut **désactiver** l'auto-orientation (`auto: false`), imposer
  une **orientation par défaut**, ou restreindre `allowed`/`forbidden` (noms ou angles).
- L'orientation est **persistée** avec les chunks et rendue (formes tournées, textures des
  cubes réassignées). Les structures peuvent fixer l'orientation de chaque bloc.

## 2.8 Le menu principal

- **Aperçu du monde** en arrière-plan : un petit volume (surface seule) est généré, **sans
  entités ni joueur visible**, avec une caméra panoramique lente (`GameOptions::preview`).

---

# 3. Le code du Studio C++

Le Studio (F10 en jeu, ou `--studio`) est un **IDE des mods** dans une seconde fenêtre SDL
sur le **même device GPU et la même partie** (rien n'est copié).

| Fichier | Rôle |
|---|---|
| `studio.cpp` | Fenêtre, **dock**, **explorateur** (arborescence des mods sur un thread), vue 3D, console, panneaux du monde |
| `studio_editor.cpp` | **Éditeur de code** : sélection, annulation, recherche, diagnostic en direct, complétion |
| `studio_model.cpp` | **Atelier de modélisation** : catalogue, vue 3D, arborescence, UV, peinture, **animation**, **avatar du joueur** |
| `studio_panels.cpp` | Espaces de travail, **Outliner**, propriétés d'entité, gestionnaire de mods, mécanismes |
| `studio_worldgen.cpp` | Générateur (relief/climat/structures), carte, **banc d'essai** |

## 3.1 Espaces de travail et dock

- Un espace = une disposition du dock (onglets, onglet actif, tailles des régions
  gauche/centre/droite/bas), enregistrée dans `studio_layout.json`.
- Onglets déplaçables par glisser-déposer ; chaque espace garde ses modifications.
- L'**explorateur** affiche l'arborescence complète des mods (tous les sous-répertoires
  contenant `.script` ou `.json`), construite sur un **thread** à l'ouverture. Interactions :
  clic droit = sélection, double-clic = déplier/ouvrir, F2 = renommer, clic gauche = menu
  (ouvrir/renommer/dupliquer/supprimer).

## 3.2 Éditeur de code

Sélection, presse-papiers, annuler/rétablir, recherche/remplacement, aller à la ligne,
commenter, dupliquer, indentation. **Diagnostic en direct** (Script ou JSON), **complétion**
des modules/fonctions de l'interpréteur de la partie, liens d'erreur cliquables.

## 3.3 Vue 3D du monde en direct

Texture de la scène du jeu rendue par `ui::Viewport3D` : pause/pas à pas de la simulation,
gizmo déplacer/tourner, cadrer/suivre, superpositions (collisions, vitesses, chunks,
lumières, joints). Quand **aucun monde n'est ouvert**, la vue affiche « Ouvrez un monde dans
le menu du jeu ».

## 3.4 Atelier de modélisation

- **Catalogue** des formes de tous les mods (nodebox, croix, rig, .obj, .b3d), lu en
  arrière-plan, avec des **gabarits** (Cube, Porte, Dalle, Torche) qui créent un **modèle
  générique** (rig) réutilisable, enregistré dans `core/models/studio_rigs.json`.
- **Modèle 3D** : projection, caméra orbitale, gizmo (déplacer/redimensionner/tourner),
  modes Édition / Peinture / Animation.
- **Arborescence** (parties et cubes), **Élément** (propriétés, **mécanique générique** :
  type + animation + bascule, **variante de bloc** « Créer un bloc… »), **UV** (box UV +
  **Auto-UV** : cadrillage automatique sans chevauchement), **Peinture** (pixel art sur
  skin/texture, **Importer une texture…** pour associer un PNG au modèle), **Animation**
  (images clés de rotation/position par partie).
- Un **modèle générique** (rig) peut être **réutilisé par plusieurs blocs** : la mécanique
  (`mechanic`) déclare le comportement et « Créer un bloc… » génère des variantes de blocs
  partageant la même forme avec des textures différentes.
- L'**avatar du joueur** est un rig éditable (`core:player_avatar`) : forme et animations
  modifiables, enregistrées dans `core/models/player_avatar.json`, rechargées par le jeu.

## 3.5 Panneaux du monde et génération

- **Outliner** : entités groupées par catégorie, filtres, virtualisation.
- **Propriétés** : position, orientation, vitesse, collision, santé, skin, éclairage, son,
  masse, mécanique (joints).
- **Monde** : statistiques, entités, blocs, SQL (chargés en arrière-plan).
- **Génération** : préréglages, graine, curseurs de bruit/climat, structures ; **carte** vue
  de dessus ; **banc d'essai** (recrée un monde de test et pose structure/bloc/ascenseur).

---

# 4. Programmer des mods

Un mod est un dossier de `games/voxelia/mods/`. Mods fournis : `core` (jeu de base),
`elevators` (ascenseurs), `backrooms`, `edit`.

| Fichier | Rôle |
|---|---|
| `mod.json` | identité, priorité de chargement, dépendances, **paramètres** |
| `blocks.json` / `blocks/*.json` | blocs |
| `items/*.json` | objets (`type: item`) |
| `recipes/*.json` | recettes de fabrication et de cuisson |
| `creatures/*.json` | entités (`type: creature`/`entity`/`interactive`) |
| `models/*.json` | modèles `rig` (créatures) et autres formes |
| `world/structures.json` | structures préfabriquées |
| `config.json` | valeurs fusionnées (`values`) |
| `textures/assets.json` | alias de textures |
| `init.script` + `scripts/*.script` | scripts, dans l'ordre des noms |

## 4.1 `mod.json`

```json
{
  "id": "elevators", "name": "Ascenseurs", "version": "1.1.0",
  "description": "…", "dependencies": ["core"], "priority": 0,
  "settings": [
    { "id": "speed", "type": "float", "default": 3.0, "min": 0.5, "max": 12,
      "label": "Vitesse (blocs/s)", "description": "…", "group": "Général", "unit": "" }
  ]
}
```

Les paramètres se lisent en script par `voxel.setting(id, défaut)`.

## 4.2 Blocs (`blocks.json`)

```json
{
  "id": "example_log",
  "displayName": "Tronc d'exemple",
  "tags": ["wood", "tree"],
  "sounds": "core:wood",
  "graphics": {
    "textures": { "top": "example:log_top", "side": "example:log_side", "bottom": "example:log_top" },
    "material": "OPAQUE", "light": 0
  },
  "gameplay": { "hardness": 2, "tool": "axe", "explosion": { "delay": 2, "radius": 4 } },
  "orientation": { "auto": true, "default": "axis=y",
                   "allowed": ["axis=x", "axis=y", "axis=z"], "forbidden": [] }
}
```

Champs importants :

| Champ | Rôle |
|---|---|
| `graphics.texture(s)` | texture par face (`top`, `side`, `bottom`, `east`, `west`, `south`, `north`) |
| `graphics.material` | `OPAQUE`, `CUTOUT`, `TRANSLUCENT`, `TRANSPARENT` |
| `graphics.light` | lumière émise 0..15 |
| `graphics.reflectivity` | 0 mat, 1 miroir (ou sur la texture dans `textures/assets.json`) |
| `nodebox` | forme (boîtes `[[x0,y0,z0,x1,y1,z1]…]`) |
| `physics.collision` | `FIXED` + `fixed`, ou `FROM_NODEBOX` |
| `connects` | connectivité (clôtures) : `{ "to": ["#fence"], "north": [[…]] }` |
| `interact` | `{ "toggle": "…", "action": "sit", "sound": "…", "call": "fn" }` |
| `liquid` | liquide : `{ "range": 3, "tick": 1.5, "renewable": true, "flowingTexture": "…", "mix": [{ "with": "core:water", "source": "core:obsidian", "flowing": "core:stone" }] }` |
| `simulation.falling` | `{ "enabled": true }` si le bloc tombe (sable, gravier) |
| `gameplay.blastResistance` | résistance aux explosions (par défaut ≈ `hardness`) |
| `gameplay.explosion` | bloc explosif : `{ "radius": 3, "delay": 3 }` (TNT) |
| `orientation` | auto-face, défaut, listes `allowed`/`forbidden` |

L'**orientation** s'écrit par **noms** (`north`, `south`, `east`, `west`, `top`, `bottom`,
`axis=x/y/z`) ou par **angles** en degrés (`"90,0,0"`).

## 4.3 Objets (`items/*.json`)

```json
{ "type": "item", "id": "leather_helmet", "displayName": "Casque en cuir",
  "icon": "core:items/leather_helmet", "tags": ["armor", "head"], "maxStack": 1,
  "armor": { "slot": "head", "points": 1, "durability": 80 } }
```

## 4.4 Recettes (`recipes/*.json`)

```json
{ "type": "recipe", "id": "planks", "category": "building", "ingredients": ["oak_log"],
  "result": { "item": "oak_planks", "count": 4 } }
```
Avec motif : `"pattern": ["PPP","PLP","PPP"], "key": {"P": "oak_planks", "L": "oak_log"}`.
Cuisson : `"kind": "cooking", "ingredients": ["sand"], "result": "glass", "time": 3`.

## 4.5 Entités (`creatures/*.json`)

```json
{
  "type": "creature", "id": "sheep", "displayName": "Mouton",
  "tags": ["animal", "passive"], "health": 20,
  "physics": { "collider": [0.6, 1.0, 0.6], "mass": 30, "maxSpeed": 2.0 },
  "rig": { "model": "core:sheep", "scale": 1.0, "skins": ["core:entities/sheep/sheep^entities/sheep/sheep_fur.png"] },
  "spawn": { "dimensions": ["core:overworld"], "chance": 0.02, "max": 6 },
  "behaviors": ["core:chase"], "drops": [["core:wool", 1]], "xp": 2
}
```

- `rig.model` : un modèle `rig` de `models/rigs.json` (parties, pivots, animations `walk`/`idle`).
- `rig.skins` : texture (composition `^colorize`, superpositions `^fichier.png`).
- `spawn` : apparition naturelle ; `behaviors` : comportements déclarés par les scripts.

## 4.6 Structures (`world/structures.json`)

```json
{ "type": "structure", "id": "pillar", "anchor": [1, 0, 1],
  "palette": { "L": "oak_log@axis=y", "S": "stone" },
  "layers": [ [" S ", "SLS", " S "], [" L ", "LLL", " L "] ] }
```

La **palette** peut préciser l'orientation de chaque bloc (`bloc@orientation`, nom ou angles).
`world.structure(name, x, y, z)` pose une structure à l'orientation donnée. (Le module
`structure.*` — `save`/`load`/`paste`/`copy`… — n'est pas encore réimplémenté : il retourne
`nil`.)

## 4.7 Scripts

Les scripts (`.script`) déclarent des **hooks**, des **systèmes** (20 Hz), des **commandes** :

```js
voxel.on("block_use", fn(x, y, z, block, actor) { … return false /* annule */ })
voxel.system("mon_systeme", fn(dt) { … })
voxel.command("exemple", "/exemple", "description", fn(joueur, args) { return "ok" })
```

### API Script

- **`voxel`** : `on`, `interact`, `system`, `command`, `setting`, `emit`, `post_effect`.
- **`world`** : `get/set(x,y,z,bloc[,orientation])`, `param(x,y,z)` (orientation),
  `fill/replace/count`, `surface/ground`, `raycast`, `structure`, `entities`, `spawn`.
- **`entity`** : `pos/set_pos`, `kind`, `velocity`, `impulse/move`, `damage/remove`,
  `prop/set_prop`, `count`, `target`, `forward`, `look`, `give/take/held`, `armor/equip`.
- **`player`** : `sit/sleep/stand/crouch`, `click`, `look`, `camera`, `swim_style`, `emote`.
- **`game`** : `say/notify/log`, `time/set_time`, `mode`, `sound`, `rule`.
- **`content`** : `config(clé)`, `get(catégorie, id)` — catégories `block`, `item`, `entity`,
  `recipe` (définition JSON brute), `list(catégorie[, tag])`.
- **`sql`** : `query(requête, paramètres…)`.
- Globales : `format`, `join`, `split`, `range`, `keys`, `math.*`, `pos`, `tp`, `notify`…

---

# 5. Utiliser le Studio

## 5.1 Ouvrir et naviguer

- **F10** en jeu, ou lancer `voxelia --studio`. La seconde fenêtre s'ouvre sur la même partie.
- **Espaces de travail** (barre d'outils) : *Monde en jeu*, *Modélisation*, *Peinture 2D*,
  *Scripting*, *Propriétés d'entités*. Chaque espace est un agencement d'onglets
  (déplaçables par glisser-déposer).
- **Explorateur** : l'arborescence des mods ; double-clic ouvre un fichier, clic droit
  sélectionne, F2 renomme, clic gauche ouvre un menu (ouvrir/renommer/dupliquer/supprimer).

## 5.2 Éditer le monde (onglet Vue 3D / Outliner / Propriétés)

- **Vue 3D** : pause/pas à pas (F8/F9), gizmo (G), cadrer (F), suivre ; superpositions de
  débogage (collisions, vitesses, chunks, lumières, joints).
- **Outliner** : clic sélectionne une entité, double-clic cadre.
- **Propriétés** : modifier position, orientation, vitesse, santé, éclairage, son, masse,
  joints à la volée.

## 5.3 Créer/éditer des modèles, skins et animations

1. Onglet **Modèles** : chercher un modèle (nodebox, rig, .obj…) ou « **Avatar du joueur** ».
2. Onglet **Modèle 3D** : mode **Édition** (gizmo W/E/R : déplacer/redimensionner/tourner,
   Maj précis, Ctrl sans accroche), mode **Peinture**, mode **Animation**.
3. Onglet **Élément** : nom, pivot, rotation, origine/taille/UV des cubes, collision.
4. Onglet **UV** : déplacer le dépliage « box UV » d'une partie.
5. Onglet **Peinture** : pixel art sur la texture (crayon, gomme, pipette, remplissage).
6. Onglet **Animation** : ajouter des animations (durée, boucle), poser des images clés de
   rotation/position par partie.

**Ctrl+S** enregistre (seule la définition modifiée est réécrite, mise en forme conservée) ;
**Ctrl+Z/Ctrl+Y** annulent/rétablissent.

- Éditer l'**avatar du joueur** (forme + animations) se répercute en jeu à la sauvegarde.
- Éditer un **rig d'entité** (`core:biped`, `core:sheep`…) et l'enregistrer met à jour les
  créatures en jeu (rechargement à chaud), sans redémarrer.

## 5.4 Rechargement à chaud

Le Studio surveille les fichiers des mods une fois par seconde. Enregistrer un `.script`
recharge la logique (entités et monde conservés) ; un JSON de contenu redéfinit les blocs ;
un `.png` recalcule ses textures ; `models/*.json` recharge le catalogue et les rigs.

## 5.5 Raccourcis

| Touche | Action |
|---|---|
| Ctrl+S | enregistrer |
| Ctrl+Z / Ctrl+Y | annuler / rétablir |
| F5 / F7 | exécuter / vérifier le script |
| F8 / F9 | pause / pas de simulation |
| Ctrl+F / F3 | rechercher / suivant |
| Ctrl+H | remplacer |
| Ctrl+G | aller à la ligne |

Lancement ciblé : `voxelia --studio --studio-tab ID` (`world`, `modeling`, `painting`,
`scripting`, `entities`, `3d`, `model:core:lamp`).
