# Voxelia — monde, streaming, base de données et mods

> Voir aussi [guide.md](guide.md) : le guide complet organisé en cinq parties (modules, code
> du jeu, code du Studio, programmation des mods, utilisation du Studio). Ce document-ci
> détaille les aspects techniques internes (streaming, base, maillage, atelier).

`apps/voxelia` assemble `engine.voxel`, `foundation.ecs`, `foundation.sql`,
`foundation.script`, `graphics.gfx` et `graphics.ui`.

| Fichier | Rôle |
|---|---|
| `src/main.cpp` | Boucle SDL, rendu de l'interface conservée (`interface->Begin/Render/End`), console, captures |
| `src/menus.cpp` | Interface du jeu par **factory builder** : `BuildInterface` construit l'arborescence (`ui::WidgetTree`), chaque écran est un widget (`BuildNewWorldMenu`, `BuildSettingsMenu`…) ou un `ui::Custom` |
| `src/game.cpp` | Session : joueur, interactions, ordonnanceur ECS, streaming, maillages → GPU, HUD, surcouche F3 |
| `src/content.cpp` | Mods → registre de blocs → texture-tableau (rastérisation parallèle), réglages persistants |
| `src/scripting.cpp` | Interpréteur Script et API des mods (`voxel`, `world`, `entity`, `game`, `content`, `sql`) |
| `src/studio.cpp` | Voxelia Studio : fenêtre, dock, éditeur de scripts, vue 3D, console, panneaux du monde |
| `src/studio_panels.cpp` | Studio : espaces de travail, World Outliner, propriétés d'entité, gestionnaire de mods |
| `src/studio_editor.cpp` | Studio : éditeur de code (sélection, annulation, recherche, diagnostic en direct, complétion) |
| `src/world_editor.cpp` | Éditeur du monde en direct : caméra libre, gizmo, superpositions de débogage |
| `src/studio_worldgen.cpp` | Studio : générateur (relief, climat, structures), carte, banc d'essai |
| `src/hotreload.cpp` | Rechargement à chaud : surveillance des fichiers des mods |
| `src/studio_model.cpp` | Studio : atelier de modélisation (catalogue, vue 3D, arborescence, UV, peinture, animation) |

## Chunks

- Chunk de 32³ voxels, tableau plat 1D (`index = x + z·32 + y·32²`) de `BlockId` 16 bits.
- Représentation creuse : un chunk uniforme (ciel, roche pleine) n'alloue rien
  (`Chunk::Compact`) — environ 60 % des chunks chargés à la surface.
- Clé = **clé de Morton 3D** des coordonnées de chunk (21 bits par axe, coordonnées
  biaisées) : deux chunks voisins ont des clés proches, ce qui donne de la localité
  aux index ordonnés et aux fichiers de régions.
- Les chunks actifs sont dans une `core::RobinMap<ChunkKey, Chunk>` : recherche O(1),
  sondage borné, aucune pierre tombale.

## Flux de chargement et de sauvegarde (`voxel::World::Update`)

1. **Le joueur avance** : quand il change de chunk, la liste des chunks souhaités
   (cylindre `viewDistance` × `verticalDistance`) est recalculée et triée par distance.
2. **Requête DB** : pour chaque chunk manquant (au plus `maxPendingLoads` en vol), une
   tâche du pool (priorité haute) calcule la clé de Morton et interroge le
   `sql::RegionStore`. Absent → génération procédurale (`TerrainGenerator`, sans
   dépendance entre chunks, donc parallèle).
3. **Désérialisation** : les voxels RLE remplissent le tableau plat ; les entités sont
   décodées en `EntityRecord` (données pures, produites hors du thread principal).
4. **Mise en cache** : sur le thread principal, le chunk entre dans la `RobinMap`, ses
   entités reprennent vie dans l'ECS (`InstantiateEntity`) et sont inscrites dans sa
   liste ; le chunk et ses voisins sont marqués à remailler.
5. **Déchargement** : un chunk hors du champ (+2 de marge) est sérialisé (voxels +
   entités, qui sont retirées de l'ECS), envoyé au `RegionStore` de façon asynchrone
   (priorité basse), puis retiré de la table. Un chunk généré, intact et sans entité
   n'est pas écrit : il sera régénéré à l'identique.

Le maillage suit le même principe : copie rembourrée 34³ sur le thread principal,
maillage glouton avec AO sur le pool, téléversement groupé sur le GPU (budget en octets
par image).

## Grille spatiale d'entités

- Pas d'arbre global : chaque chunk tient la liste `Chunk::entities`.
- Le composant `GridCell {chunk, slot}` donne le chunk courant et le rang dans la liste :
  retrait O(1) par swap-remove (le rang de l'entité déplacée est corrigé).
- `World::UpdateEntityGrid` : détection **parallèle** des franchissements (lecture des
  colonnes `Transform`/`GridCell`, un tampon local par lot), puis application séquentielle :
  désinscription du chunk A, inscription dans B. Si B n'est pas chargé, l'entité reste
  rattachée à A.
- `World::ForEachEntityNear(pos, n)` parcourt le chunk courant et ses voisins dans un rayon
  de `n` chunks (`entityQueryRadius` dans les paramètres) ; `QueryEntities` filtre ensuite
  par distance exacte et par type.
- La simulation (errance, gravité, collisions AABB) ne touche que les entités des chunks
  à moins de `simulationDistance`.

## Base de données d'un monde (`data/voxelia/worlds/<nom>/`)

| Élément | Contenu |
|---|---|
| `regions/r.<clé>.vreg` | Blobs de chunks : une région = 16³ chunks (`clé >> 12`), journal en ajout seul, CRC par enregistrement, compaction auto (> 50 % d'espace mort), verrou par région |
| `world.vqdb` | Instantané CRC de la base relationnelle (écriture atomique) |
| table `meta` | graine, position du joueur, heure, données des mods (`data:<clé>`) |
| table `chunks` | chunks sauvegardés ; `zkey` (Morton) indexée en B+arbre pour les recherches par boîte |
| table `entities` | index spatial des entités des chunks **déchargés** (`/chunks`, `FindStoredEntities`) |
| table `config` | configuration fusionnée des mods, aplatie (`player.speed`) |
| table `events` | journal des événements du jeu et des mods (`tick` et `chunk` indexés) |
| table `mods` | mods, versions, activation, priorité |

Exemples dans la console :

```
/sql SELECT key, cx, cy, cz, entities FROM chunks WHERE entities > 0 LIMIT 10
/sql SELECT COUNT(*) FROM events WHERE type = 'block_placed'
/sql SELECT value FROM config WHERE key LIKE 'player.%'
/events 20
```

## Mods

Un mod est un dossier de `games/voxelia/mods/` :

| Fichier | Rôle |
|---|---|
| `mod.json` | `id`, `name`, `version`, `description`, `priority` (ordre de chargement ; `core` d'abord) |
| `blocks.json`, `blocks/*.json` | blocs : `id`, `displayName`, `tags`, `graphics.texture(s)`, `material` (`OPAQUE`, `CUTOUT`, `TRANSLUCENT`, `TRANSPARENT`), `light` |
| `config.json` | `values` fusionnées (une clé de premier niveau d'un mod plus tardif remplace la précédente) |
| `textures/assets.json` | alias de textures (`path`, `compose` avec `^`, `tint`, `opacity`, `lighting`, `procedural`, `biomeTint`) |
| `init.script`, `scripts/*.script` | scripts, exécutés dans l'ordre des noms |

Le menu **Mods** active/désactive un mod (réglage persistant, appliqué au monde suivant).

### API Script

- `voxel.on(événement, fn(données))`, `voxel.system(nom, fn(dt))` (20 Hz),
  `voxel.command(id, usage, description, fn(joueur, args))`, `voxel.setting(nom, défaut)`,
  `voxel.emit(nom, données)` (événement de mod, journalisé en base).
- Événements émis par le jeu : `world_load`, `player_join`, `block_placed`, `block_broken`,
  `block_use`, `entity_damaged`, `entity_died`.
- `world.get/set/fill/replace/count/surface/ground/raycast`, `world.entities(x, y, z, rayon, [type])`
  (grille spatiale), `world.spawn(type, x, y, z)`, `world.data/set_data` (persistant).
- `entity.pos/set_pos/players/nearest_player/kind/velocity/set_velocity/impulse/move/damage/remove/prop/set_prop/count/target/forward/look/gravity/set_gravity`.
- `game.say/notify/log/time/set_time/mode/rule/tick`, `content.config/get/list`.
- `sql.query(requête, paramètres…)` : lignes sous forme de tables (`{colonne: valeur}`).
- Globales historiques : `pos`, `get`, `set`, `fill`, `sphere`, `surface_height`, `notify`,
  `log`, `tp`, `block_id`, `block_name`, plus la bibliothèque standard (`format`, `join`,
  `split`, `range`, `keys`, `math.*`…).

Les fonctions de l'API Voxelia d'origine qui n'ont pas d'équivalent dans ce moteur
(structures, formules, sons, interfaces de formulaire, inventaire…) existent mais
retournent `nil` ; leur premier appel est signalé dans la console. Les 22 scripts des
mods fournis s'analysent sans erreur (test `Script_ParseAllVoxeliaMods`).

## Voxelia Studio

Le Studio (F10 en jeu, `--studio`) s'ouvre dans une seconde fenêtre SDL sur le **même**
device GPU et la **même** partie : rien n'est copié. L'Outliner et les propriétés lisent
et écrivent les composants de l'ECS vivant ; la vue 3D rend la scène de la partie dans une
texture (`ui::Viewport3D`). Tout se fait sur le thread principal, entre deux mises à jour.

### Espaces de travail

Un espace de travail est une disposition du dock (`ui::DockState` : onglets, onglet actif,
visibilité et taille des régions gauche / centre / droite / bas). Chaque onglet s'affiche
dans n'importe quelle région : on le déplace par glisser-déposer sur une autre barre
d'onglets. Chaque espace garde ses modifications ; le bouton ↶ de la barre d'outils
rétablit sa disposition d'origine. Les dispositions sont enregistrées à la fermeture dans
`<data>/studio_layout.json`.

| Espace | Gauche | Centre | Droite | Bas |
|---|---|---|---|---|
| Monde en jeu | Outliner, Mods | Vue 3D, Script | Propriétés, Monde | Console, Logique |
| Modélisation | Modèles, Arborescence, Explorateur | Modèle 3D, Vue 3D, Script | Élément, UV, Arborescence | Animation, Console |
| Peinture 2D | Textures, Modèles | Peinture, Modèle 3D | UV, Élément | Console (repliée) |
| Scripting | Explorateur, Mods | Script | Monde | Console, Logique |
| Propriétés d'entités | Outliner | Vue 3D | Propriétés | Logique, Console |

### Panneaux

- **Outliner** : entités vivantes (`voxel::CollectEntities`) groupées par catégorie
  (joueurs, créatures, objets, projectiles, autres), filtres par identifiant, catégorie et
  distance ; liste virtualisée rafraîchie à 5 Hz. Clic : sélection ; double-clic : cadrage.
- **Propriétés** : une section par composant — position, orientation, vitesse, boîte de
  collision, gravité, collisions, santé, errance, skin, données des mods — modifiables à la
  volée (`ui::Context::DragNumber` : glisser pour changer, Maj pour la précision, clic pour
  saisir). Actions : cadrer, suivre, supprimer.
- **Vue 3D** : barre d'outils pause / pas à pas de la simulation (`Game::SetPaused`,
  `Game::Step` : le streaming et le joueur continuent), gizmo déplacer / tourner, cadrer,
  suivre. Raccourcis quand la souris survole la vue : G (gizmo), F (cadrer).
- **Mods** : le jeu de base est le mod `core` ; version, dépendances, contenu fourni,
  activation (hors partie), rechargement à chaud des scripts, ouverture de `mod.json`,
  `config.json` et des scripts.
- **Peinture** : texture sélectionnée au pixel (molette : zoom vers le curseur, clic droit
  ou milieu : déplacer, coordonnées et couleur du pixel survolé).
- **Script**, **Explorateur**, **Console**, **Monde** (statistiques, entités, blocs, SQL
  chargés en arrière-plan), **Textures**, **UV**, **Logique** (éditeur nodal).

Raccourcis globaux : Ctrl+S, F5 (exécuter le script dans la partie), F7 (vérifier),
F8 (pause / reprise de la simulation), F9 (un pas de simulation en pause).
`voxelia --studio-tab ID` active un onglet ou un espace de travail au démarrage
(`world`, `modeling`, `painting`, `scripting`, `entities`, `3d` pour la vue plein écran,
`model:core:lamp` pour ouvrir un modèle dans l'atelier).

### Atelier de modélisation (phase 2)

Formes 3D des mods, lues et écrites par `voxel/model.hpp` (une représentation commune,
`voxel::Model` : parties avec pivot et rotation, cubes, maillages, collision, animations) :

| Format | Où | Édition |
|---|---|---|
| nodebox | bloc `nodebox: [[x0,y0,z0,x1,y1,z1]…]` (liste, `{fixed}`, `{type/shape: FIXED}`) | cubes (unités bloc, accroche 1/16) |
| croix | bloc `shape: "cross"` | lecture |
| json | modèle `format: json`, `data.boxes` (référencé par `shape: {type: model}`) | cubes |
| rig | modèle `format: rig` : `parts` (`pivot`, `rotation`, `walk`/`idle`/`look`), boîtes `from/size/uv/inflate/mirror` | cubes, pivots, rotations, box UV, animations |
| obj | modèle `format: obj` ou bloc `type: obj` (`o`/`g` → parties) | parties déplacées, réécrit en .obj |
| b3d | bloc `type: b3d` (Blitz3D : `NODE`/`MESH`/`VRTS`/`TRIS`) | lecture, export .obj |

- **Modèles** : catalogue des formes de tous les mods (lu en arrière-plan), recherche et
  filtre par format. Un bloc dont la forme est un modèle ouvre ce modèle. La rangée de
  **gabarits** (Cube, Porte, Dalle, Torche) crée un **modèle générique** (rig) enregistré
  dans `<core>/models/studio_rigs.json` puis ouvert ; la porte illustre la **mécanique**
  (panneau pivoté sur sa charnière + animation « ouvrir ») réutilisable par plusieurs objets.
- **Modèle 3D** : vue projetée (tri en profondeur, élimination des faces arrière cubes et
  maillages — bouton **Double-face** pour les voir), caméra orbitale, grille 1/16, boîte
  englobante et cotes en mètres, boîtes de collision. Modes **Édition** (clic : sélection ;
  gizmo W déplacer, E redimensionner, R tourner la partie ; Maj : précis, Ctrl : sans
  accroche ; Suppr, Ctrl+D), **Peinture** (pinceau directement sur les faces 3D) et
  **Animation** (le gizmo pose des images clés).
- **Sous-éléments** (type Sketchup/Blockbench) : trois modes de sélection — **sommet**,
  **arête**, **face** — avec **multi-sélection** (Maj + clic) et surbrillance. En mode
  **face**, glisser une face fait un **push/pull** : tirer vers l'extérieur extrude un
  nouveau cube, pousser vers l'intérieur creuse (rétrécit) la boîte le long de la normale,
  avec accroche à la grille. Le bouton **X** reste un extrude rapide au clic.
- **Arborescence** : parties et cubes (visibilité), collision ; ajouter, dupliquer, supprimer.
- **Élément** : modèle (enregistrer, ouvrir le JSON, exporter .obj, convertir en rig dans
  `<mod>/models/studio_rigs.json`, **Auto-UV**, **Importer une texture…**), **mécanique
  générique** (type « porte / tiroir / interrupteur », animation d'activation, bascule),
  **variante de bloc** (« Créer un bloc… » : aplatit le modèle en nodebox + texture +
  `interact.toggle`, enregistré dans `<mod>/blocks/studio_blocks.json`), partie (nom, pivot,
  rotation), cube (origine, taille, UV, gonflement, miroir), collision AABB.
- **UV** : dépliage « box UV » de la partie sélectionnée sur la texture ; glisser déplace
  le dépliage du cube. **Auto-UV** range toutes les boîtes dans une **grille sans chevauchement**
  (`voxel::AutoBoxUv`, emballage par étagères) en agrandissant la texture si nécessaire.
  **Importer une texture…** ouvre un dialogue PNG, l'associe au modèle (active le box UV) et
  applique le cadrillage automatique. Un **bloc nodebox à plusieurs textures de faces** (6
  textures `north/south/top/bottom/east/west`) est réuni en **une seule texture UV** (atlas
  3×2), quelle que soit la déclaration JSON : peinture et UV unifiés pour tous les éléments.
- **Peinture** : pixel art sur la texture du modèle (skin de la créature, image du maillage,
  texture du bloc) ou sur la source PNG d'une texture choisie dans « Textures » : crayon,
  gomme, pipette, remplissage, tailles 1 à 3, palette, couleur RGBA. L'enregistrement d'une
  texture de bloc la recharge à chaud dans la partie (couches 16×16 du renderer).
- **Animation** : animations du modèle (durée, boucle), pistes de rotation et de position
  par partie (`animations: [{name, length, loop, tracks: [{part, rotation: [[t,x,y,z]…],
  position}]}]`), tête de lecture, clic droit sur une clé : suppression ; aperçu des
  mouvements procéduraux des rigs.

Annulation / rétablissement (Ctrl+Z / Ctrl+Y, 64 étapes, une par geste) pour le modèle et
pour la peinture. L'enregistrement (Ctrl+S) ne remplace que la définition modifiée dans son
fichier (`dataio::ReplaceJsonArrayElement`) : commentaires et mise en forme du reste du
fichier sont conservés. Une collision `FROM_NODEBOX` modifiée devient `FIXED`.

### Modèles génériques, mécaniques et variantes

Un **modèle générique** est un rig (`models/studio_rigs.json`) destiné à être **réutilisé par
plusieurs objets** : le gabarit **Porte** (panneau pivoté sur sa charnière + animation
« ouvrir ») illustre le principe.

- La **mécanique générique** est déclarée sur le rig via `mechanic: {type, animation, toggle}`
  (éditable dans l'onglet « Élément ») : `type` décrit le comportement (« door », « drawer »,
  « switch »), `animation` l'animation jouée à l'activation (rotation du panneau autour du
  pivot), `toggle` l'aller-retour. Parse/write par `voxel::ParseRig` / `RigToJson`.
- La **variante de bloc** (« Créer un bloc… ») aplatit le modèle en nodebox, y associe une
  texture et, si la mécanique est une bascule, pose `interact.toggle` vers la variante
  `_on` ; le bloc est écrit dans `<mod>/blocks/studio_blocks.json`. Plusieurs blocs peuvent
  ainsi partager la même forme avec des textures différentes (une porte en chêne, en fer…).

La prise en charge au **rendu** d'un bloc qui référence un rig animé (forme `shape: {type:
model}` avec animation) reste une évolution ultérieure du moteur ; la variante aplatit
aujourd'hui la pose de repos en nodebox, comme le font les blocs `core:lamp`.

## Phase 3 : formes, collisions, mécanique, lumière et son

### Formes des blocs dans le jeu

`BlockDef` porte maintenant la forme du bloc (`boxes` : nodebox, `mesh` : triangles d'un
`.obj` / `.b3d` du mod), ses boîtes de collision (`collision`) et sa connectivité
(`connect`). Le mesher produit, à côté des cubes gloutons, un **maillage détaillé** par
chunk (`DetailVertex`, positions flottantes, UV alignées sur le monde) rendu par un
pipeline dédié (`detail.vert` + `voxel.frag`). Une face posée contre un cube opaque est
omise. Les blocs à forme ne cachent plus leurs voisins (`IsOpaque()` faux).

Connectivité (exemple : `core:fence`) :

```json
"connects": { "to": ["#fence", "#solid"],
              "north": [[-0.06, 0.1, -0.5, 0.06, 0.25, -0.125]], "east": [...], ... }
```

Les boîtes d'une face s'ajoutent quand le voisin de ce côté est cité (identifiant,
`#étiquette`, `#solid` = cube plein opaque). Dans l'atelier de modélisation, chaque face
est une partie `connect_<face>` (section « Connectivité » de l'onglet Élément).

La physique (`World::Collide`) teste les boîtes de collision des blocs (cube plein par
défaut ; une rangée de marge en Y pour les boîtes qui débordent du bloc) : dalles,
barrières, étagères arrêtent les entités à leur vraie hauteur.

### Physique des blocs (chute, liquides, explosions)

Un bloc = **1 m³**, la gravité vaut 9,81 m/s². `World::Update(focus, dt)` reçoit le temps
écoulé : la simulation est indépendante de la fréquence d'images.

- **Chute** (`simulation.falling.enabled`, ex. sable/gravier) : le bloc n'est plus téléporté
  d'une case à l'autre ; il **accélère sous la gravité** (vitesse en m/s, limite ≈ 40 m/s) et
  ne descend d'une cellule que lorsque son avancement atteint 1 m. Il atterrit sur le premier
  support et les blocs au-dessus peuvent alors tomber à leur tour.
- **Liquides** (`liquid.range`, `liquid.tick`) : écoulement vertical immédiat puis étalement
  horizontal borné par la portée, avec **viscosité** (`tick`) ; les sources pleines
  régénèrent une source selon la règle 2×2 (eau infinie).
- **Explosions** (`gameplay.explosion.radius/delay` sur un bloc, ex. TNT ; `blastResistance`
  par bloc) : `World::Explode` échantillonne des rayons depuis le centre, atténue leur
  intensité à travers les blocs (résistance), détruit ce qui cède et applique dégâts et recul
  aux entités (retombée linéaire avec la distance). Les blocs explosifs atteints sont
  renvoyés pour enchaînement. `game.explode(x, y, z, rayon, dégâts [, grief])` est exposé aux
  scripts ; `grief = faux` désactive la destruction (règle « mobGriefing »).

### Mécanique des entités (`voxel/mechanics.hpp`)

Composants : `Uid` (identifiant stable, sauvegardé), `Mass`, `LightEmitter`,
`SoundEmitter`, `Joints`. Un joint appartient à une entité A et vise B par son `Uid` :

| Joint | Effet | Moteur / limites |
|---|---|---|
| Attache | B suit A rigidement | — |
| Charnière | B tourne autour d'un axe vertical à l'ancre de A | °/s, [min, max] en ° |
| Glissière | B glisse le long d'un axe du repère de A | m/s, [min, max] en m |
| Poulie | y(A) + ratio·y(B) = longueur ; masses, gravité, moteur à force limitée | m/s, N, hauteurs |
| Corde | distance(ancre, B) ≤ longueur | — |

Le système `mechanics.joints` s'exécute après l'intégration physique (suspendu avec la
simulation). Les enregistrements d'entités passent en version 2 (`uid` + JSON de la
mécanique) ; les chunks en version 1 restent lisibles.

### Lumière et son

- Jusqu'à 16 lumières ponctuelles (les plus proches de la caméra) éclairent les chunks
  (atténuation quadratique dans `voxel.frag`), dans le jeu et dans la vue 3D du Studio.
- Les sons des entités sont joués en boucle, spatialisés (`MixTrack::Set3DPosition`) et
  atténués par la distance. Identifiants : `sounds/assets.json` des mods, ou fichiers posés
  dans `<mod>/sounds/` (`elevators:sounds/elevator_motor_loop.wav`).

### Studio

- **Propriétés** : sections Éclairage (émettre, intensité, portée, couleur, position),
  Son (son, volume, portée, boucle), Masse, Mécanique (joints portés : type, ancre, axe,
  rapport, longueur, limites, moteur ; joints reçus ; ajout d'un joint vers une entité proche).
- **Mécanismes** (onglet du bas) : entités en nœuds, joints en liens colorés par type ;
  glisser d'une sortie (●) vers une entité crée un joint ; clic sur l'étiquette d'un lien :
  réglages ; « Ascenseur de démonstration » (cabine sur poulie, contrepoids, porte
  coulissante, lumière, son du moteur) et « Porte de démonstration » (charnière motorisée).
- **Vue 3D** : lumières dynamiques, sources de lumière, poulies et câbles, ancres des joints.

## Phase 4 : scripting et rechargement à chaud

### Rechargement à chaud

`HotReload` analyse une fois par seconde, sur un thread du pool, les dates de modification
des fichiers des mods (`.script`, `.json`, `.png`, `.obj`, `.b3d` ; dossiers `sounds/` et
`tools/` exclus). Le thread principal applique les changements (`App::ApplyHotReload`),
que le fichier ait été enregistré par le Studio ou par un autre éditeur :

| Fichier modifié | Effet | Monde |
|---|---|---|
| `.script` | `Game::ReloadScripts` : hooks, systèmes et commandes réenregistrés | entités, blocs et données conservés |
| JSON de contenu, `.obj` / `.b3d` | `Content::ReloadDefinitions` : blocs redéfinis dans le même registre (identifiants stables), textures et tables de maillage refaites, chunks remaillés | conservé |
| `.png` | `Content::ReloadTextureFiles` : seules les couches concernées sont recalculées | conservé |
| `models/*.json` | catalogue de l'atelier relu | — |

Chaque rechargement est journalisé (console, notification, panneau Performances) ; il se
désactive dans le panneau Mods. Un document ouvert et non modifié dans le Studio est relu.

### Éditeur de code

Sélection (souris, double-clic, Maj + flèches, Ctrl+A), presse-papiers (Ctrl+C/X/V, ligne
entière sans sélection), annuler / rétablir (Ctrl+Z / Ctrl+Y, frappe regroupée),
rechercher (Ctrl+F, F3) et remplacer (Ctrl+H), aller à la ligne (Ctrl+G), commenter
(Ctrl+/), dupliquer la ligne (Ctrl+D), indentation de la sélection (Tab / Maj+Tab).
Diagnostic en direct 0,4 s après la dernière frappe (analyseur Script ou JSON) : marqueur
dans la gouttière, soulignement ondulé, message cliquable. Complétion (après « . », dès deux
lettres, ou Ctrl+Espace) : modules et fonctions de l'interpréteur de la partie
(`Interpreter::GlobalNames`, membres des modules), mots-clés.

### Console et métriques

- Console : couleurs par nature (erreur, avertissement, commande, rechargement), filtres
  avec compteurs, liens `fichier.script:ligne:colonne` qui ouvrent le fichier à l'endroit
  de l'erreur, historique (↑ / ↓), complétion (Tab) des commandes `/…` et du code.
- Performances : image, mise à jour, chaque système ECS, entités, chunks, rendu (courbes
  échantillonnées à 10 Hz, figeables), mécanique, derniers rechargements.

## Phase 5 : éditeur du monde en direct

### Mode éditeur dans la fenêtre du jeu (F4, ou `--editor`)

- **Caméra libre** : la caméra se détache du joueur (qui reste immobile) ; ZQSD/WASD,
  Espace ou E pour monter, Maj ou Q pour descendre, Ctrl rapide, regard au clic droit
  maintenu, molette : vitesse. Le streaming des chunks suit la caméra
  (`Game::SetCameraOverride`).
- **Sélection** au clic : entité (boîte traversée en premier par le rayon, masquée par un
  bloc plus proche), sinon bloc visé (contour, nom, coordonnées).
- **Gizmo** (`TransformGizmo`, dessiné en 2D sur l'image) : flèches X / Y / Z (G), un axe à
  la fois, accroche au 1/16 de bloc ; cercle de cap autour de Y (R), accroche à 15° ;
  Ctrl : sans accroche. Suppr : supprimer l'entité sélectionnée.
- **Panneau** : mode du gizmo, superpositions, « Joueur ici » (téléporte le joueur sous la
  caméra), retour au joueur.

### Superpositions de débogage (jeu et vue 3D du Studio)

`CollectDebugLines` produit des segments 3D, projetés sur le canvas (`Projector`) :
boîtes AABB des entités (sélection en jaune, joueur en bleu) avec leur cap, vecteurs vitesse
(trajet sur 0,5 s), bordures des chunks voisins (orange : modifié, bleu : généré), portée
(trois cercles) et rayons des lumières dynamiques arrêtés au premier bloc, ancres et liens
des joints. Dans le Studio, la barre de la vue 3D active chaque superposition, et la vue se
pilote au clavier quand la souris la survole (ZQSD/WASD, Q/E, Maj).

## Phase 6 : outils de génération du monde

### Générateur paramétrable (`voxel/terrain.hpp`)

`TerrainSettings` décrit tout le générateur, enregistré en JSON dans la base du monde (méta
`terrain`) et dans `world.json` ; un monde plus ancien (méta `generator`/`relief`) garde son
générateur. Les valeurs par défaut reproduisent exactement l'ancien relief.

- `TerrainShape` : continents (échelle, amplitude), collines, crêtes montagneuses et étendue
  des massifs, détail du sol, octaves ; climat (échelle, biais de température et
  d'humidité) ; grottes, fosses abyssales, densité des arbres.
- `StructureRule` : structure d'un mod (`world/structures.json`) placée automatiquement —
  chance par cellule, espacement (au plus une par cellule, position tirée dans la cellule),
  biomes autorisés. Le placement est déterministe et indépendant de l'ordre de génération
  des chunks : chaque chunk pose la partie des structures qui le recoupe.
- `TerrainGenerator::Sample(x, z)` (altitude, biome, température, humidité) et
  `StructuresIn` servent aux aperçus.

### Reliefs aquatiques, lacs de lave et liquides

- **Recouvrement des océans** : l'eau est posée de `c.height+1` à `kSeaLevel` (fosses
  abyssales comprises) ; le sol sous-marin garde une couche de surface solide, donc l'océan
  ne se vide pas.
- **Lacs de lave souterrains** : `LavaLakeAt` place des poches sphériques de `core:lava`
  liquide (plus fréquentes avec la profondeur, vers la mer de lave du noyau), chacune scellée
  par une croûte d'**obsidienne** — les failles (`IsFault`) ne les percent pas.
- **Mer de lave du noyau externe** : continue (aucune faille ne la creuse), contrairement au
  manteau et à la croûte où les failles percent des crevasses verticales.
- **Sources infinies** : `liquid.renewable` (eau, source chaude) active la règle 2×2 dans
  `FlowLiquid` ; la lave (`renewable` absent) s'épuise.

### Studio : espace « Génération »

- **Générateur** : préréglages (Normal, Plat, Amplifié, Archipel, Collines, réglages de la
  partie ouverte), graine, type, relief, curseurs des bruits et du climat, grottes, arbres,
  créatures, règles de structures (structure, chance, espacement, biomes) ; « Créer un monde
  avec ces réglages ».
- **Carte** : vue de dessus calculée en arrière-plan 0,3 s après le dernier réglage
  (biomes, relief ombré, profondeur de l'eau, neige des sommets, structures, origine,
  joueur de la partie si même graine) ; glisser : déplacer, molette : zoom ; altitude et
  biome du point survolé ; profil d'altitude de la ligne centrale ; légende.
- **Banc d'essai** : « Lancer la simulation » recrée le monde `banc_d_essai` (plat et
  neutre, ou réglages du générateur ; sans créatures, jour fixe) puis, une fois chargé, pose
  devant le joueur une structure, un bloc, la démonstration d'ascenseur ou de porte, ou
  exécute une commande (`/elevator build` du mod `elevators`) ; le Studio passe à « Monde en
  jeu » sur le résultat. Bilan : blocs posés, entités créées, joints actifs, entités
  coincées dans un bloc, erreurs de la console.

## HUD, inventaires, interactions et avatar du joueur

### Placement dynamique (`ui/layout.hpp`)

Fonctions pures recalculées à chaque image depuis la taille de la fenêtre : `Place` (rectangle
ancré : coins, bords, centre, avec marges), `FitScale` (échelle d'interface, 1 à 1280×720),
`FitAspect`, découpes `CutTop/Bottom/Left/Right`, `Row`/`Column` flexibles (`FlexItem` : taille,
part `grow`, bornes `min`/`max` ; les éléments rétrécissent quand la place manque), `Grid`
(cases carrées, `Cell`, `HitTest`), `AnchorStack` (empilement sans chevauchement à partir d'un
bord) et `KeepInside`. Le HUD et les menus du jeu n'utilisent plus de coordonnées fixes.

### Interface conservée (`ui/retained.hpp`, `games/voxelia/src/menus.cpp`)

Les **menus du jeu** sont construits par une **arborescence déclarative de widgets** inspirée de
Flutter (factory builder), puis affichés automatiquement par le module `ui` :

- `BuildInterface(app)` crée un `ui::WidgetTree` autour du `ui::Context` existant et enregistre
  une fonction de construction `runApp(fn)` **ré-évaluée à chaque image** (comme le `build` de
  Flutter) — les listes dynamiques (mondes, mods, skins) restent donc à jour.
- La boucle principale appelle `interface->Begin(input, dt)` / `interface->Render()` /
  `interface->End()` ; `Render` reconstruit l'arbre courant puis le dessine et déclenche les
  fonctions de rappel (`onPressed`, `onChanged`, `onSubmit`).
- Les écrans « formulaires » (nouveau monde, paramètres du monde, paramètres globaux, configuration
  des mods) sont décomposés en widgets : `ui::Dialog`, `ui::PageHeader`, `ui::ScrollView`,
  `ui::Section`, `ui::SettingText/Choice/Int/Float/Toggle` (valeur par copie + `onChanged(nouvelle
  valeur)`), `ui::ButtonAt`/`ui::PrimaryButton`/`ui::Footer`.
- Les écrans au dessin personnalisé (menu principal avec personnage 3D et lecteur de musique,
  grilles de mondes/mods/skins) sont des **`ui::Custom`** qui gardent leur code de dessin immédiat
  ; la console de jeu est elle aussi un `ui::Custom` montré pendant l'écran « Jeu ».

### HUD (`games/voxelia/src/hud.cpp`)

- Barre d'outils ancrée en bas (icônes isométriques des blocs tirées d'un atlas généré à partir
  des textures, objets des mods), quantités en survie, case de la main gauche à côté.
- Au-dessus, empilés : cœurs et faim (survie), armure et air (si utiles), nom de l'objet en main
  (s'efface après 2,5 s).
- Sous le viseur : nom de l'objet ou de l'entité visée (avec ses PV) et l'action du clic droit
  (« ouvrir / fermer », « allumer / éteindre », « s'asseoir », « se coucher », « utiliser »).
- En haut : état du joueur (accroupi, assis, couché, nage et son style, chute libre, geste en
  cours, vue de la caméra) ; aide des touches les premières secondes ; écran assombri en dormant.

### Inventaires

- Objets des mods (`items/*.json`, `type: item`) chargés avec leur icône : `ItemId` unifie blocs
  et objets ; pile maximale, armure (`armor.slot`, `armor.points`).
- **Survie** : personnage (aperçu de la skin avec l'armure portée), 4 emplacements d'armure,
  main gauche, sac 3 × 9 et barre d'outils, fabrication (recettes réalisables d'abord, quantités
  possédées / requises, étiquettes `#planks`), four. Clic : prendre / poser / échanger ; clic
  droit : moitié / un seul ; Maj+clic : transfert barre ↔ sac. L'inventaire est sauvegardé.
- **Créatif** : onglets (Construction, Nature, Mobilier, Mécanismes, Liquides, Objets, Armures),
  recherche par nom ou identifiant, réserve infinie (clic : une pile, clic droit : un, Maj+clic :
  dans la case active), corbeille.

### Clics sur les objets du jeu et fonctions des mods

Clic gauche (casser / frapper) et clic droit (utiliser / poser) sur un bloc ou une entité :

1. évènement `block_click` / `entity_click` (`button`, `x`, `y`, `z`, `root_x…`, `nx…`, `block`,
   `entity`, `kind`, `player`, `sneaking`, `held`) ;
2. gestionnaires `voxel.interact(cible, fn)` — cible : bloc, `#étiquette`, genre d'entité,
   `block`, `entity` ou `*` ;
3. fonction déclarée par le bloc : `"interact": {"call": "press_button"}` ;
4. sinon l'action par défaut : `"interact": {"toggle": …, "sound": …}` (portes, lampes — les
   parties d'un bloc multiple basculent ensemble), `"action": "sit"` (chaise) ou `"sleep"` (lit),
   `block_use`, puis la pose du bloc tenu ; clic gauche : casser (bloc multiple entier) ou frapper.

Un gestionnaire qui renvoie `false` annule l'action par défaut. Les paramètres des fonctions de
script sont liés **par nom** aux champs de l'évènement (`fn(x, y, z, block, actor)` ;
`fn(ev)` reçoit la table). Accroupi, le clic droit pose au lieu d'interagir (E interagit toujours).
Exemples dans `mods/core/scripts/interactions.script` : bouton (`core:button`) qui bascule les
lampes voisines, lampe (`core:lamp` ↔ `core:lamp_off`), caresser les animaux.

API de script ajoutée : `entity.give/take/count/held/sneaking/armor/equip`, `game.sound`,
`player.emote/swim_style/state/sit/sleep/stand/crouch/click/look/camera/inventory/gamemode`,
fonctions globales `starts_with`, `ends_with`, `contains`.

### Avatar du joueur (`voxel/avatar.hpp`)

Bipède à 6 parties articulées (skin 64×64, ancien format 64×32 converti), posé à chaque image
(`ComputeAvatarPose` + `BlendPose` pour des transitions douces), avec les objets tenus dans les
deux mains et les pièces d'armure (atlas 128×64 : skin, objets, couleurs d'armure).

Le **rig éditable** (`BuildAvatarRig`, enregistré dans `<core>/models/player_avatar.json`)
reprend exactement cette géométrie : parties en absolu autour de leur pivot (cou, épaules,
hanches), avatar centré de 1,8 bloc (pieds à −0,9). Il est rendu par le jeu quand le fichier
existe ; sinon, le maillage procédural s'y substitue. La forme et les animations du rig se
modifient dans l'atelier de modélisation (onglet « Modèles » → « Avatar du joueur »).

- **Première personne** : la caméra est à l'avant du visage, à hauteur des yeux ; la tête n'est
  pas dessinée ; le bras droit (et le gauche s'il tient un objet) suit le regard et reste dans le
  champ ; en baissant les yeux on voit son torse, ses jambes et ses pieds.
- Postures : debout / marche, **accroupi** (Maj : torse penché, jambes fléchies, boîte de
  collision abaissée, pas de chute au bord des blocs), **assis** (clic droit sur une chaise, dos au
  dossier), **couché** (clic droit sur un lit, la nuit passe après quelques secondes),
  **nage** (dans un liquide : crawl, brasse, dos crawlé — K pour changer ; nage sur place quand on
  s'arrête, flottaison), **chute libre** (bras et jambes écartés au-delà de 4 blocs de chute),
  vol.
- Gestes : **coucou** (V), **grands signes** (B), **petit coucou** (N), **enfiler une armure**
  (joué en équipant une pièce : casque, plastron, jambières, bottes) ; balancement du bras à chaque
  action.
- Caméra (F6) : première personne, vue de dos, de face, de côté. X : échanger les mains.

Captures automatisées : `--no-input` ignore la souris et le clavier (seules les commandes
`--exec` pilotent le joueur), par exemple
`--exec 'player.camera("side")' --exec 'player.swim_style("brasse")'`.
