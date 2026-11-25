# sandbox

Modeleur 3D à la SketchUp, refonte de la sandbox de SDL3_Wrapper sur les modules d'apps-lib
(`graphics.app`, `graphics.ui`, rendu 3D intégré à `platform.sdl3`).

```sh
cpp-maker run sandbox                       # projet d'exemple chargé : projects/maison
cpp-maker run sandbox -- modele.sandbox     # ouvrir un document
cpp-maker run sandbox -- projects/maison    # ouvrir un dossier de projet (sa scene.sandbox)
cpp-maker run sandbox -- --workspace=overlay # onglet de travail au démarrage (overlay, script)
cpp-maker run sandbox -- --empty --tool=r --view=top --tab=2 --shadows
```

## Architecture

| Dossier | Rôle | Dépend de |
|---|---|---|
| `modeling/` | Noyau : maillage éditable à faces planes (trous, arêtes adoucies), inférences, outils interactifs en machines à états (Ligne, Rectangle, Cercle, Polygone, Pousser/Tirer, Décalage, Suivez-moi, Déplacer, Rotation, Échelle, Gomme, Peinture, Mètre), primitives, JSON et export OBJ/STL/PLY | `core`, `math`, `dataio` |
| `document/` | Document : géométrie libre, groupes (repère propre), palette commune, balises, scènes, fichiers `.sandbox`, annulation par instantanés | `modeling` |
| `editor/` | Éditeur sans fenêtre : contexte (racine ou groupe ouvert), outil, sélections, boîte de mesures, commandes, caméra de navigation | `document`, `sdl3::Camera3D` |
| `app/` | Interface : menus, barre d'outils, vue 3D (cache de maillages par modèle et par matériau), plateau, barre d'état | `graphics.*` |

Le noyau, le document et l'éditeur se testent sans GPU (`tests/`, 58 tests) : la souris y
est simulée par des rayons de la caméra de navigation.

## Accueil, inspecteur, fichiers

Au lancement (sans fichier), l'écran d'accueil propose les projets récents, les exemples, d'ouvrir
un projet ou d'en créer un (`.project`, `scenes/main.scene`, `scripts/main.script`) ; l'éditeur
s'affiche ensuite (`--sample` : la maison directement, `--empty` : scène vide, Fichier → Accueil).
L'onglet « Inspecteur » (à droite) règle le nœud choisi comme dans Godot : géométrie, comportement
(corps, son, lampe, zone), script et **paramètres du script** ; sans sélection, la scène
(environnement, visite, script de scène). En bas, l'**explorateur de fichiers** du projet (arbre des
dossiers, vignettes ; double-clic : ouvrir une scène, éditer un script ou un JSON dans l'onglet
Script du centre, insérer un objet, importer un modèle ; attribuer un script, un son ou une image),
les Ressources et la Console.

## Paramètres de script

Une classe déclare ses paramètres avec `export let nom = défaut` ; l'inspecteur les montre (nombre,
case, texte) avec la valeur par défaut, et chaque nœud (ou la scène) garde ses valeurs, posées sur
l'instance avant `init`. Les scripts s'en servent pour régler les nœuds : `this.set_player_speed`,
`set_jump`, `set_gravity` (Scene), `set_intensity`, `set_range`, `set_color` (Light3D), `set_mass`
(PhysicsBody), `set_rotation`, `set_scale` (Behaviour)…

```
class Porte extends Behaviour {
    export let vitesse = 120     # degrés par seconde (réglable dans l'inspecteur)
    export let ouverture = 90
}
```

## Interface des scripts (module `ui`)

`graphics.ui` lie l'interface retenue aux scripts (`ui::ScriptUi`) : `ui.column`, `row`, `label`,
`button`, `input`, `progress`, `separator`, `checkbox`, `slider`, `choice`, `remove`, `clear`, avec
`{parent, anchor, offset, w, h, gap, pad, bg, radius, color, font_size, bold, align}` ; chaque widget
est un objet dont `text`, `value`, `max`, `checked`, `visible` sont synchronisés à chaque image ; les
rappels (`fn(widget)`, `fn(widget, texte)`) sont appelés après le dessin. En visite, ces interfaces
s'affichent par-dessus la vue ; `input.set_mouse_mode("free")` (ou F7) libère la souris pour cliquer.

## Projets

Aucune scène n'est codée dans le sandbox : l'exemple est un **projet externe**,
`projects/maison/`. Le fichier d'entrée d'un projet porte l'extension `.project` et commence par
`"format": "sandbox.project"` ; il recense les fichiers du projet :

```
projects/maison/
├── maison.project           index : nom, scène active, scènes, objets, modèles, scripts
├── scenes/maison.scene      scène (« sandbox.scene ») : géométrie, groupes, matériaux, vues, HUD
└── scripts/
    ├── scene.script         class Maison extends Scene
    ├── porte.script         class Porte extends Behaviour : quart de tour autour de sa charnière
    ├── interactions.script  module commun : class Interrupteur extends Behaviour
    ├── lanterne.script      class Lanterne extends i.Interrupteur, Light3D  (import "interactions")
    └── radio.script · paillasson.script
```

Formats : `.project` (« sandbox.project »), `.scene` (« sandbox.scene »), `.object`
(« sandbox.object » : un groupe réutilisable avec géométrie, matériaux, comportement et script,
inséré depuis l'onglet Ressources), `.model` (« sandbox.model ») et `.script`. Un comportement
référence son script par `scriptFile` (relatif à la scène) ; « Enregistrer sous » réécrit les
scripts à côté de la nouvelle scène. Les anciens fichiers (`.sandbox`, « sandbox.document »)
s'ouvrent encore.

## Hiérarchie de nœuds

Comme l'arbre de scène de Godot : un groupe a un parent (`Entity::parent`) et un type de nœud
(`Node3D`, `MeshInstance`, `Light`, `Camera`, `SpawnPoint`…). Déplacer, tourner ou placer un
parent emporte ses descendants (en édition comme en jeu, y compris leurs collisions) ; un parent
masqué ou verrouillé s'impose à eux ; effacer ou dupliquer un groupe agit sur son sous-arbre ;
Ctrl+G sur des groupes crée un groupe parent ; « Éclater » verse la géométrie dans le parent.
Comme SketchUp, un clic choisit le groupe du niveau ouvert, un double-clic ouvre un niveau,
Échap remonte d'un niveau. L'onglet Structure est une arborescence : flèches pour replier,
clic pour révéler un nœud (ses ancêtres s'ouvrent), glisser-déposer pour changer de parent.

Les scènes et objets de l'ancien sandbox (arbre « scene.tree », p. ex. `projects/dungeon`)
s'ouvrent : un groupe par nœud, primitives reconstruites en faces éditables, corps, lumières,
scripts, instances d'objets développées ; les objets ou modèles introuvables sont signalés dans
la console.

## Scripts orientés objet

Chaque script déclare la classe de son groupe, dérivée de classes natives du moteur (C++23,
`script::ClassBinder`) : `Behaviour` (name, position, move, set_position, rotate, turn, show,
hide, distance), `PhysicsBody` (push, velocity), `Light3D` (light), `AudioSource` (play, stop) ;
le script de la scène dérive de `Scene` (say, time, player, position, move, turn, visible, light).
La classe la plus dérivée est instanciée pour le groupe ; ses méthodes `OnStart`, `OnUpdate(dt)`,
`OnTouch`, `OnUse`, `OnTrigger` sont les rappels. Le langage (`foundation.script`) offre
classes, héritage (une classe parente de script, plusieurs bases natives), champs `let`,
`init`, `this`, `super.m(…)` et `import "module"` (scripts du projet).

```
const i = import "interactions"

class Lanterne extends i.Interrupteur, Light3D {
    let t = 0
    fn OnUpdate(dt) { this.t += dt  this.move(0, math.cos(this.t * 2) * dt * 0.15, 0) }
    fn basculer(allumee) { this.light(allumee) }
}
```

## Interface

- Menus Fichier, Édition, Affichage, Caméra, Dessin, Outils, Jeu, Aide (`ui::Context::MenuBar`).
- Dock à onglets déplaçables à la Godot / Blender (`ui::DockLayout`) : au centre Vue 3D et
  Script, à gauche Structure et Scènes (repliés), à droite Entité, Matériaux, Balises, Jeu ;
  chaque onglet se glisse vers une autre région (bas compris), les séparateurs se tirent.
- Onglet Ressources (à gauche) : modèles glTF du projet et de `assets/models` à importer
  (`modeling::ImportGltf` : hiérarchie des nœuds, matériaux ajoutés à la palette, triangles
  coplanaires fusionnés en faces éditables ; posé au sol devant la caméra, échelle réglable),
  sons et scripts du projet à attribuer au groupe sélectionné. `--import=F.gltf` au lancement.
- Composants (menu Édition, onglet Entité) : les copies d'un composant sont des instances qui
  partagent leur géométrie — modifier l'une (groupe ouvert) modifie les autres, chacune garde
  sa position, son nom et son comportement ; « Rendre unique » la détache. Dans le fichier, la
  géométrie d'un composant est écrite une fois (les trois caisses de la maison en sont).
- Onglet Vue 2D : le calque 2D du document (HUD du mode Jeu) — rectangles, cercles, lignes,
  textes et images nommés, placés dans un écran de référence (1280 × 720 par défaut, ajusté à
  la vue sans déformation) par-dessus la scène 3D. Clic : sélection ; glisser : déplacer
  (Maj : pas de 10) ; poignée : redimensionner ; flèches, Suppr ; propriétés dans l'onglet
  Entité ; images du projet attribuables depuis Ressources.
- Barre d'outils selon l'onglet de travail actif : modélisation pour la Vue 3D ; formes, ordre
  et effacement pour la Vue 2D ; vérifier, commenter, indenter, taille du texte pour le Script.
- Environnement de la scène (onglet Entité sans sélection, enregistré dans le document, comme
  le WorldEnvironment de Godot) : ambiances Jour / Couchant / Nuit / Brume, heure et intensité
  du soleil, part du ciel, brouillard, exposition ; lampes des groupes visibles aussi en édition.
- Console (région du bas, comme la Sortie de Godot ou l'Output Log d'Unreal) : messages des
  scripts (`print`, `game.say`), erreurs et résultats de « Vérifier » ; une erreur cliquée ouvre
  son script à la ligne fautive. Elle s'ouvre seule à la fin d'une visite qui a eu des erreurs.
- Onglet Script : liste des scripts du projet, puis des fonctions du script affiché (clic : aller
  à la ligne, rappels `on_*` en couleur) (scène et groupes), éditeur de code pleine taille
  (`ui::Context::CodeEditor` : numéros de ligne, coloration du langage, indentation
  automatique, Tab / Maj+Tab par blocs, crochets appariés), exemples insérables, « Vérifier »
  surligne la ligne en erreur.
- Barre d'outils groupée comme SketchUp ; vues Iso / Dessus / Face / Droite, projection parallèle.
- Vue : faces mates des deux côtés, arêtes sombres, axes rouge / bleu (vertical) / vert pleins
  côté positif et pointillés côté négatif, inférences colorées avec leur étiquette, groupe
  ouvert entouré de pointillés et reste du modèle estompé, ombres et rayons X facultatifs.
- Barre d'état : outil, consigne, boîte « Mesures ». Pendant un outil, taper une cote puis
  Entrée la fixe : `2,5`, `120cm`, `45°`, `3;2` (rectangle), `2;` ou `;1,5` (une seule cote
  du rectangle, l'autre suit la souris).
- Correction après coup, comme dans SketchUp : juste après un rectangle, un cercle, un
  pousser/tirer, un décalage, un déplacement (y compris d'un groupe), une rotation ou une mise
  à l'échelle, taper une nouvelle cote refait l'opération avec cette valeur (un seul pas
  d'annulation) ; un nouveau clic ferme la correction.
- Dimensions exactes (onglet Entité) : position, rotation et largeur / profondeur / hauteur d'un
  groupe, dimensions d'une sélection de géométrie libre ; champs à glisser ou à taper,
  proportions liées en option.

Souris : clic = outil ; double-clic = ouvrir / fermer un groupe ; molette = zoom vers le point
visé ; molette glissée = orbite, Maj ou clic droit glissé = panoramique.
Clavier : Espace sélection, L R C P F M Q S E B T, O orbite, H panoramique, 1–6 vues,
Maj+Z zoom étendu, Ctrl+Z / Y / S / O / N / A / G / D, Suppr, Échap.

## Mode Jeu : la visite

F5 (ou ▶ dans la barre d'outils, menu Jeu) : on parcourt le modèle à la première personne —
ZQSD / WASD ou flèches, Maj pour courir, Espace pour sauter, E ou clic pour utiliser l'objet
visé, Échap pour revenir à l'édition. La partie travaille sur une copie : le document n'est
jamais modifié. Départ : la scène nommée « Départ… », sinon sous la caméra de l'éditeur.

| Dossier | Rôle |
|---|---|
| `engine.physics` (module) | Boîtes et sphères contre un décor de triangles (grille de hachage, axes séparateurs) et des maillages cinématiques qui suivent une transformation, rebond, frottement, marches, piles, évènements de contact, rayons, pont ECS |
| `game/sound` | Sons en flux (`sdl3::AudioDecoder`), mélange stéréo spatial vers une `sdl3::AudioStream` ; muet sans périphérique |
| `game/session` | Partie : joueur, corps, lumières, sons déclenchés, scripts (un interpréteur `foundation.script` par groupe) |

Comportement d'un groupe (onglet Jeu) : corps **solide** (décor exact ; scripté, c'est un
maillage cinématique : une porte qui tourne heurte et repousse le joueur avec sa forme exacte), **dynamique** (masse, rebond, frottement) ou **fantôme** (zone traversable) ; son
(fichier de `assets/sounds`, au départ / au contact / à l'usage / par script, volume, boucle,
spatial) ; lampe ponctuelle ou spot dirigé selon l'axe −Y du groupe (couleur, intensité, portée, angle,
pénombre) ; script :

```
let allumee = true
fn OnUpdate(dt) { self.rotate(45 * dt) }
fn OnUse() { allumee = not allumee
              self.light(allumee) }
fn OnTouch(qui) { if (qui == "joueur") { game.say("Bienvenue !") } }
```

`self.name / position / move / set_position / rotate / turn / push / velocity / show / hide /
play / stop / light` (`turn` : autour de l'origine du groupe, sa charnière), `game.say / time /
player / distance / position / move / turn / visible / light`,
`input.key("w" | "space" | …)`, `print`. Calque 2D : `hud.text / show / move / size / color /
position(nom, …)` modifient ses éléments par leur nom (copie de la partie) ; `draw2d.rect /
circle / line / text(…, "#rrggbb")` dessinent pour un pas (horloge, jauges, viseurs). Un script en erreur est signalé et arrêté, la partie
continue. Exemples insérables depuis l'onglet (porte, ascenseur, interrupteur…).

Le modeleur perce aussi les murs comme SketchUp : une face poussée jusqu'à la face opposée
d'un solide laisse une ouverture (l'entrée de la maison d'exemple, fermée par la porte scriptée).

## Raccourcis configurables, catalogue et réglages

- **Raccourcis clavier** (`Réglages → Raccourcis clavier…`) : toutes les actions (menus, outils,
  navigation, vues) sont reconfigurables — « Modifier » puis la nouvelle combinaison, détection de
  conflit, « Défaut » par action et « Tout réinitialiser ». Les libellés des menus et le catalogue
  suivent la reconfiguration, persistée dans le dossier de préférences.
- **Catalogue** (`Aide → Catalogue…`, bouton `?` de la barre d'outils, ou depuis l'accueil) :
  chaque outil, panneau, notion et API de script y est décrit, avec son raccourci courant.
- **Qualité de rendu** (onglet Entité, sans sélection) : Basse / Moyenne / Élevée / Ultra
  (bloom, rayons, saturation, vignettage).
- **Gizmo de coin** (repère d'axes en haut à droite de la Vue 3D) : clic sur un axe = orienter la
  caméra, clic au centre = basculer perspective / parallèle.
- **Aimantation** : pas d'angle réglable (onglet Entité → Inférences), Maj désactive l'accroche ;
  en Vue 2D, déplacement avec **guides d'alignement** (bords/centres des autres éléments et de
  l'écran), Maj = grille (pas réglable).
- **Audio des scripts** : `audio.play(nom, {volume, loop, pitch, position})`, `audio.pitch(id, v)`,
  `audio.pause(id)`, `audio.resume(id)` — hauteur avec rééchantillonnage, courbe de distance réglable.

## Non repris (étapes suivantes)

Scène 2D de l'ancienne sandbox, navigateur de ressources et import glTF, objets réutilisables
(composants à instances), rotation des corps physiques.

# Plan d'amélioration de sandbox

> Document de travail. Objectif : faire évoluer **apps-lib** (bibliothèque C++23) et **sandbox** (éditeur de jeu 2D/3D à architecture scène/nœuds, langage interprété `script`) **sans dupliquer** l'existant et **sans alourdir** l'exécution.

---

## 1. Analyse de Fyrox

### 1.1 Vue d'ensemble (source : `Fyrox/ARCHITECTURE.md`)

Fyrox est un moteur **monolithique** à couplage interne fort mais unidirectionnel. Il est
composé de **4 caisses** :

| Caisse | Rôle | Dépendance |
|---|---|---|
| `fyrox-core` | briques de base : algèbre linéaire, structures d'accélération, couleurs, sérialisation | autonome |
| `fyrox-sound` | moteur audio logiciel (HRTF binaural) | autonome |
| `fyrox-ui` | bibliothèque d'interface **à nœuds**, agnostique du rendu, 30+ widgets, **docking manager**, messages, styles | autonome |
| `fyrox` | moteur : rendu (différé + forward), scènes, animations, ressources | réunit les 3 précédentes |

**Point clé** : Fyrox **n'utilise pas d'ECS**. Il utilise des **arènes générationnelles**
(`pool.rs`) : objets contigus en mémoire, accédés par des **handles** (paire d'indices avec
génération pour valider le handle). C'est la pierre angulaire du moteur.

### 1.2 Modules remarquables de `fyrox-core`

| Module | Rôle | Intérêt pour apps-lib |
|---|---|---|
| `pool.rs` | **arène générationnelle** (allocation/désallocation rapide, cache-friendly, handles validables) | à ajouter à `foundation.core` |
| `octree.rs` | accélération des ray-casts, probes, tests de boîtes | `foundation.math` (grille de voxel déjà présente, mais octree générique utile) |
| `rectpack.rs` | **bin-packing** (atlas de textures) | `foundation.math` / `graphics.ui` (atlas de glyphes) |
| `triangulator.rs` | triangulation de polygones (ear-clipping + quadrilatères) | déjà présent dans `sandbox/modeling` — ne pas dupliquer |
| `color.rs` / `color_gradient.rs` | couleurs HSV/RGB, **dégradé linéaire multi-points** (particules, ciel) | `foundation.math` |
| `numeric_range.rs` | intervalle numérique sans bornes incohérentes possibles | utile dans `foundation.math` |
| `visitor.rs` | **sérialiseur/désérialiseur par visiteurs** (nœuds), tout le moteur l'utilise | équivalent : `foundation.dataio` (JSON + binaire) déjà présent |
| `profiles.rs` | profileur intrusif (`scope_profile!`) | équivalent : `foundation.diagnostics` (traceur `.vcap`) déjà présent |

### 1.3 `fyrox-ui` — interface à nœuds

Bibliothèque d'interface **indépendante du rendu** (émet une file de commandes de dessin,
interprétée par n'importe quel backend). Widgets : `border`, `button`, `canvas`, `check_box`,
`color`, `decorator`, `dock` (**DockingManager** + `Tile`), `dropdown_list`, `expander`,
`file_browser`, `formatted_text`, `grid`, `image`, `list_view`, `menu`, `message`,
`numeric`, `popup`, `progress_bar`, `scroll_bar`, `scroll_panel`, `scroll_viewer`,
`stack_panel`, `tab_control`, `text`, `tree`, `window`, `wrap_panel`, etc.

**Communication par messages** (`message.rs`) : chaque widget reçoit des messages normalisés
(les interactions sont découplées du dessin).

→ **Correspondance avec apps-lib** : `graphics.ui` est déjà un système hybride
**retenu + immédiat** avec `Dock` (`Splitter` + `TabWidget` déplaçables), `TreeView`,
`ListView`, `TableView`, `CodeEditor`, `Modal`, `Expander`, `MenuBar`, `ContextMenu`…
La plupart des widgets de Fyrox ont déjà un équivalent. **Les vrais manques** sont
transversaux : un **registre de raccourcis clavier** et un **catalogue de fonctionnalités**.

### 1.4 L'éditeur Fyrox (analyse en cours, sera consolidée)

Points à retenir de l'éditeur (`editor/`, `editor-standalone/`) :

1. **Raccourcis clavier centralisés** : l'éditeur définit ses hotkeys dans un registre unique
   (association action → combinaison), les menus affichent ce registre, et une **interface de
   reconfiguration** permet de les modifier. C'est exactement ce qui manque à sandbox (raccourcis
   codés en dur dans `HandleKeyboard()` **et** dupliqués dans les libellés de menus).
   *(Détails : voir §1.7 — analyse terminée.)*
2. **Système de réglages persistés** : préférences sérialisées et rechargées au lancement.
   Sandbox n'a qu'un `settings.txt` minimal (fractionnement de fichiers) — à étendre.
3. **Inspecteur par réflexion** (`fyrox-core-derive`, traits `Reflect`/`Inspect`) : les
   propriétés d'un objet sont exposées génériquement et l'éditeur **génère automatiquement les
   éditeurs de propriétés**. Sandbox a un inspecteur **codé en dur** + des paramètres de script
   (`export let`) lus par réflexion partielle du `script::ClassBinder` — à unifier.
4. **Prévisualisation de scène / gizmos** : caméra orbit/pan/zoom, **picking par rayon**,
   **gizmos de transformation** (translation/rotation/échelle), grille, aimantation. Sandbox a
   déjà caméra + inférences + outils Déplacer/Rotation/Échelle + boîte de mesures (VCB).
   Améliorations possibles : poignées de gizmo par axe, aimantation à la grille, sélection
   par cadre.
5. **Navigateur de ressources** : sandbox a déjà l'explorateur + import glTF + onglet Ressources.

### 1.5 Détails consolidés — viewports & gizmos (analyse terminée)

Analyse détaillée de `editor/src/{scene_viewer, scene, interaction, world}`. Points
directement transférables :

1. **Abstraction `InteractionMode`** (trait + conteneur par UUID, modes Select/Move/Rotate/Scale/…,
   bascule par `SetInteractionMode`). → Sandbox a déjà l'équivalent : `modeling::Tool` (machines à
   états) + `Editor::SetTool`. **On étend, on ne recrée pas.**

2. **Gizmos par axe** : `MoveGizmo` (3 flèches + 3 **poignées de plan** XY/YZ/ZX + un « smart dot »),
   `RotationGizmo` (3 **anneaux** Pitch/Yaw/Roll), `ScaleGizmo` (3 axes + **cube uniforme**).
   La poignée survolée est **surlignée en jaune**, le gizmo est **mis à l'échelle par la distance**
   (taille constante à l'écran). → c'est la cible de la Phase 3 « gizmos par axe ».

3. **Sélection par cadre** : `SelectInteractionMode` projette la **boîte englobante monde** de
   chaque nœud à l'écran et teste l'intersection avec le rectangle tiré. → Phase 3.

4. **Gizmo de coin (`SceneGizmo`)** : mini-scène 85×85 en haut à droite (6 cônes d'axes + cube
   central) ; clic = orienter la caméra, clic central = basculer perspective/ortho. → **à ajouter**
   à sandbox (complète les boutons Iso/Dessus/Face/Droite déjà présents). *Fonctionnalité
   « professionnelle » à forte valeur.*

5. **Aimantation** : `grid_snapping` + pas X/Y/Z réglables, `angle_snapping` + pas par axe, via un
   menu déroulant `GridSnappingMenu` branché sur les réglages. → Phase 3. Sandbox **a déjà mieux**
   pour la saisie précise : la boîte de mesures (VCB) avec cotes tapées, absente de Fyrox.

6. **Créateur d'entités (`EntityCreator`)** : fenêtre modale **recherchable** listant tous les
   constructeurs de nœuds (groupés, filtrables), modes Créer enfant / parent / remplacement.
   → **pont naturel avec le catalogue** (§5) : le catalogue servira aussi de « créer un nœud ».

7. **Menu contextuel d'arborescence** : Créer enfant / Supprimer (Suppr) / Copier / Coller comme
   enfant / Créer parent / Remplacer / Définir racine / Enregistrer comme préfab / Réinitialiser.
   Sandbox a une partie de ça (menu Édition + glisser-déposer) ; à compléter (menu contextuel
   clic droit dans la Structure).

8. **Glisser-déposer d'asset dans la vue** : aperçu vivant + commande d'ajout. Sandbox importe
   depuis l'onglet Ressources ; le glisser-déposer direct depuis l'explorateur vers la vue 3D
   serait un plus (Phase 3, optionnel).

9. **Surbrillance de sélection** : passe de rendu **post-process** (contour). Sandbox surligne par
   inférence/rayons X ; un contour net (ou un surlignage des groupes sélectionnés) est à évaluer.

10. **2D** : Fyrox traite la 2D par caméra ortho + scènes UI + tilemaps (pas d'éditeur de sprites
    dédié). → Sandbox a déjà un **éditeur 2D de calque plus avancé** ; les améliorations 2D
    (guides, aimantation, sélection multiple) relèvent du modèle **Godot**, pas de Fyrox.

### 1.6 Ce que Fyrox **n'a pas** (et que sandbox a déjà)

- Un **modeleur** SketchUp-like (Pousser/Tirer, Décalage, Suivez-moi, inférences) — sandbox
  est déjà en avance ici.
- Un **éditeur 2D de calque** (HUD) aussi poussé — sandbox a déjà la Vue 2D.
- Un **langage de script interprété** maison intégré au nœud — sandbox a déjà
  `foundation.script` + `ClassBinder`.

---

### 1.7 Enseignements consolidés (analyse cœur & scènes terminée)

L'analyse détaillée de `fyrox-impl` + `fyrox-core` + `fyrox-ui` + `fyrox-resource` confirme et
précise :

1. **Réflexion = source de vérité unique.** `#[derive(Reflect, Visit)]` produit à la fois les
   **métadonnées** (nom, bornes, pas, doc, read-only), la **sérialisation** (`Visit`) et la
   **résolution par chemin** (`set_field_by_path("base.local_transform.position", …)`). L'inspecteur,
   le sérialiseur, l'**annulation** et les **remplacements de préfab** partagent tous cette
   réflexion. → Pour apps-lib : le `script::ClassBinder` et `export let` devraient tendre vers un
   équivalent (métadonnées + chemins), cf. §3.4.

2. **Annulation générique pilotée par la réflexion.** Le `CommandStack` de l'éditeur Fyrox exécute
   des commandes génériques (`SetPropertyCommand`, `AddCollectionItemCommand`…) qui opèrent sur des
   **chemins de propriété** + `Box<dyn Reflect>`. Toute propriété devient annulable « gratuitement ».
   → **Sandbox a déjà une annulation par instantanés** (adaptée à un modeleur SketchUp) : on **ne
   remplace pas**, mais l'idée de commande générique servira si l'on ajoute un inspecteur réflexif.

3. **Héritage de préfab.** `InheritableVariable<T>` (drapeau `MODIFIED`) distingue une valeur
   héritée d'une valeur surchargée, et n'exporte que les valeurs modifiées. → Sandbox a déjà les
   « composants à instances » mais **sans drapeau de surcharge par propriété** : à évaluer pour
   affiner le comportement des instances (Phase 5).

4. **`TrackedProperty<T>`** émet un message à chaque mutation (`DerefMut`) — c'est l'équivalent du
   système `Editor::Change(label, merge)` de sandbox (checkpoint), qui est déjà en place.

5. **Ressources** : `Resource<T>` (UUID + Embedded/External) + chargeurs asynchrones + registre
   UUID→chemin. Sandbox gère déjà sons/images/modèles via l'explorateur ; un **cache par UUID**
   pourrait éviter les relectures (Phase 5, optionnel).

### 1.8 Détails consolidés — éditeur : hotkeys, réglages, aide (analyse terminée)

Analyse détaillée de `editor/src/{settings, menu, plugins, utils, world}` :

1. **Hotkeys Fyrox** : struct `KeyBindings` (`settings/keys.rs`) avec deux types — `HotKey`
   (combinaison, ex. `Ctrl+Z`) et `KeyBinding` (touche maintenue, ex. `W`). Défauts centralisés dans
   `impl Default`. **Pas de registre** : le dispatch est une **chaîne `if hot_key == …`** dans
   `lib.rs::handle_hotkeys`. La reconfiguration passe par l'**inspecteur réflexif** (un éditeur
   `HotKeyPropertyEditorDefinition` dans `fyrox-ui`).
   ⚠️ **Défaut de Fyrox** : les **libellés de menus sont codés en dur** (« Ctrl+N ») et **ne suivent
   pas** une reconfiguration — le libellé peut diverger de la vraie touche.
   → **Sandbox fera mieux** : un `KeyMap` unique (action → combinaison) alimente **à la fois** le
   dispatch **et** les libellés de menus (jamais de divergence), avec **détection de conflit**.

2. **Réglages Fyrox** : `SettingsData` (`settings/mod.rs`) regroupe tout via `#[reflect(tag = "Group.*")]`
   (champs `#[reflect(hidden)]` exclus), persisté en **RON** (`settings.ron`), `DerefMut` marque
   « sale » et notifie les abonnés. Fenêtre Réglages = **barre de recherche + barre latérale de
   groupes + inspecteur** + boutons Défaut/OK. → Modèle pour étendre `settings.cpp` de sandbox.

3. **Aide Fyrox** : 4 surfaces — (a) menu Aide → URLs externes ; (b) **fenêtre de documentation**
   Markdown rendue dans l'UI (`utils/doc.rs`) ; (c) **doc par entité** extraite des commentaires de
   doc via la réflexion ; (d) **`EntityCreator`** (`world/create.rs`) : fenêtre modale
   **recherchable** de tous les types de nœuds, groupés, **filtre flou**, **description Markdown**,
   « récents ». Pas de boîte « À propos ». → `EntityCreator` est le **modèle direct du catalogue** (§5).

4. **Docking Fyrox** : mise en page déclarative par défaut + **`DockingManagerLayoutDescriptor`**
   persisté, avec **Enregistrer / Charger / Réinitialiser la mise en page** (menu Vue). → Sandbox a
   déjà le dock (`Splitter`+`TabWidget`) mais **sans persistance de la mise en page** : à ajouter
   (Phase 1, optionnel).

### 1.9 Détails consolidés — graphisme, exemples, build (analyse terminée)

1. **`fyrox-graphics*`** : trait `GraphicsServer` (abstraction de backend : GL / wgpu), struct
   `DrawParameters` (état de pipeline complet), et surtout **`QualitySettings`** (`renderer/settings.rs`) :
   ~20 réglages (ombres, SSAO, FXAA, parallax, occlusion, HDR/bloom) + **4 préréglages
   `ultra/high/medium/low`** (valeurs, `Copy`). → **Transférable** : sandbox a un rendu forward avec
   des réglages **épars** (`shadowMapSize`, `Post`) mais **pas de struct de qualité unifiée**.
   Action : `RenderQuality { ombres, tailleOmbre, bloom, rays, msaa, ssao, … }` + préréglages, injecté
   dans `sdl3::Renderer3D` (généralise le seul `shadowMapSize` existant) + liste Low/Med/High/Ultra
   dans l'onglet Environnement. **Coût quasi nul** (aucun nouveau shader). *(Le rendu différé
   G-Buffer de Fyrox n'est **pas** à porter — forward suffit.)*

2. **`fyrox/` façade** = simple ré-export (`fyrox-impl` par défaut, `fyrox-dylib` si feature). Confirme
   le motif « une surface publique, backend interchangeable » que sandbox a déjà (`sdl3::GpuDevice`).

3. **`examples/`** : il ne reste que `2d.rs` (caméra ortho + grille de sprites + lumière tournante)
   et `navmesh.rs` (raycast souris + agent de navigation + chemin débugué). Leur squelette
   (`Plugin`/`update`) = l'équivalent des rappels `OnStart`/`OnUpdate` de sandbox (déjà couvert).
   → Deux échantillons de script à ajouter : « **lampe animée** » (`Behaviour + Light3D`, portage
   direct du spot tournant) et « **clic → marqueur + chemin dessiné** » (`physics` + `draw2d.line`,
   portage de `navmesh.rs` sans l'algorithme). La **navigation/navmesh** est un manque réel mais
   optionnel (aucune structure équivalente dans sandbox).

4. **`fyrox-build-tools/`** : fenêtre de build (équivalent `cpp-maker` ✅) + **export de projet avec
   conversion d'assets** (`export/asset.rs`) : copie des **seuls assets utilisés**, conversion des
   formats texte en forme « livraison » (Fyrox : ASCII→binaire `.rgs`), cible par plateforme.
   → **Manque réel d'apps-lib** : pas d'étape « exporter/publier ». Action : `cpp-maker export`
   (ou `tools/export_project`) qui recopie les assets référencés et minifie/compresse les
   `.scene`/`.project`/`.script` JSON. *(La compilation de shaders est déjà couverte par
   `tools/compile_shaders.sh`, plus avancée que Fyrox.)*

### 1.10 Détails consolidés — animation, audio, scripts (analyse terminée)

1. **Animation (`fyrox-animation`)** : cœur = `Track` (propriété animée : `ValueBinding` Position/
   Échelle/Rotation ou chemin réflexif) + `Curve` (Constant/Linéaire/Cubique-Hermite, cache de
   segment) + `Animation` (lecteur : `tick(dt)`, boucle, vitesse, **signaux** de timeline →
   `AnimationEvent`). `SpriteSheetAnimation` = animation par trames (rects UV + signaux). La
   **machine à états** (`Machine`/`Layer`/`State`/`Transition`/`LogicNode`/`BlendSpace`) est
   surdimensionnée : n'en porter qu'une **version réduite** (une couche, `State`+`Transition` avec
   `transition_time`/`blend_factor`, paramètres `Rule`, `PlayAnimation`+`BlendAnimations`).
   → **Phase 6 (Animation)**.

2. **Audio (`fyrox-sound`)** : sandbox a déjà un mixeur stéréo spatial (puissance constante).
   Manques ordonnés : (a) **`pitch` + rééchantillonnage linéaire** (gain à bas coût) ;
   (b) **`DistanceModel`** (Inverse/Linear/Exponent) + `radius`/`rolloff`/`max_distance`
   (paramétrer la courbe codée en dur) ; (c) **pause** (`SetPause`/`IsPaused`) ; (d) **bus +
   reverb** (Freeverb, biquad) ; (e) **HRTF binaural** en dernier (chemin optionnel, ~4–5×).
   → **Phase 5 (audio)**.

3. **Scripts (`fyrox-scripts`)** : un seul script, `FlyingCameraController` (souris yaw/pitch borné
   + WASD + accél/décél par `Curve`). → **Phase 5** : porter sa *logique* en classe native
   `Camera` du langage interprété (adossée à `Curve`) pour rendre la caméra scriptable
   (aujourd'hui non exposée aux scripts).

### 1.11 Détails consolidés — graphe, autotile, matériaux, textures (analyse terminée)

1. **`fyrox-graph`** : traits `SceneGraph`/`NodeWrapper` (génériques) + `PrefabData`. Transferable :
   **handles à génération** + `Pool`, invariants `link/isolate/unlink`, `traverse_iter` (itératif),
   `find_map/find_up`, `change_hierarchy_root`/`LinkScheme` (ré-enracinement réversible, pour
   l'annulation de reparentage), `NodeHandleMap` (recâblage des références à la copie profonde).
   **À ne pas porter** : l'effacement de type `dyn Reflect` (utiliser `std::variant`/RTTI en C++).
   → **Phase 0/5** : auditer `EntityId` (génération), factoriser la traversée générique en
   `template<class Node> SceneGraph`, et un `HandleRemap` + `remap_handles` par type (copie
   profonde, import glTF).

2. **`fyrox-autotile`** — **ROI maximal** : `PatternBits` (masque 3×3 `i8`) + tables de 8 décalages
   et « bits de bordure » + `AutoTiler::find_pattern` ≈ 200 lignes, **sans dépendance graphique**.
   Structures : `TilePattern`(9×i8), carte terrain→motifs, carte motif→tuiles pondérées, carte de
   contraintes éparse, `WeightedSet`. Option : `WfcPropagator` (remplissage génératif). Intégration
   éditeur : le terrain = valeur centrale d'une propriété « nine-slice » de la tuile.
   → **Phase 4 (tilemap/autotile)**.

3. **`fyrox-material`** : `Material` = **référence de shader + carte nom→valeur de remplacement**
   (`MaterialProperty` = `std::variant<…>`). Les **défauts viennent du shader** → « réinitialiser »
   = effacer la clé. → **Phase 5 (palette de matériaux)**.

4. **`fyrox-texture`** : pipeline de chargement (décoder → mips → compression → mips concaténés) +
   taxonomie `TextureKind`/`TexturePixelKind`. L'atlas (`RectPacker`) vient de `rectutils` + usage
   police (`fyrox-ui/font`). → faible priorité (SDL3_image déjà en place) ; `RectPacker` seulement
   si atlas dynamique nécessaire.

5. **`fyrox-dylib`** : simple ré-export (optimisation de lien, **pas** un système de plugin) —
   rien à porter ; valide le choix « interpréteur d'abord » de sandbox.

## 2. Principes directeurs (contrainte imposée)

Avant d'ajouter quoi que ce soit, **analyser l'existant** pour :

1. **Ne pas dupliquer** : une fonctionnalité déjà présente (même partiellement) est **étendue**,
   pas recréée.
2. **Préférer étendre une classe existante** (ajouter une méthode) à créer un nouveau module.
3. **Ne pas ralentir l'exécution** : pas d'indirection inutile, pas d'allocation par image dans
   le chemin chaud, préférer les structures contiguës et les `RobinMap` existants.
4. **Respecter les couches** : `foundation → engine → platform → graphics → ui → applications`
   (un module ne dépend que de sa couche ou d'une couche inférieure).
5. Tout nouvel ajout doit avoir un **test** (les suites de tests existent par module).

---

## 3. Améliorations des modules apps-lib

> Chaque ajout est justifié par l'analyse Fyrox et placé dans le module **existant** le plus
> approprié (pas de nouveau module sauf cas d'espèce).

### 3.1 `foundation.core` — arène générationnelle `Pool`

- **Inspiration** : `fyrox-core/pool.rs`.
- **Pourquoi** : sandbox manipule déjà des handles (`EntityId`, ids d'entités ECS, symboles
  internés du script). Une arène générationnelle générique offrirait un stockage contigu +
  validation de handles réutilisable partout (notamment pour les scripts et le jeu).
- **Action** : ajouter `core/pool.hpp` (en-tête uniquement, `Pool<T>` : `insert`, `erase`,
  handle `{index, generation}`, itération). **Ne pas toucher** à l'ECS existant (il est plus
  riche) ; le `Pool` est une primitive de niveau inférieur.

### 3.2 `foundation.math` — primitives portées de Fyrox

Analyse détaillée (`fyrox-math` + `fyrox-core`) : liste précise des manques, ordonnée par valeur
pour un éditeur. *(Note : `sdl3::Color` existe mais dans la couche `platform` ; le but est une
couleur utilisable par `math`/`graphics`/`script` sans dépendre de SDL.)*

| Port | Fichier cible | Valeur | Note |
|---|---|---|---|
| `Color` (u8 RGBA) + `Hsv`/`Hsl` + palette nommée (140) + parse `#hex` + sRGB↔linéaire + `lerp` | `math/color.hpp` | ★★★ | aucun type couleur en `foundation` |
| `ColorGradient` (points triés, échantillonnage linéaire borné) | `math/color.hpp` (ou `gradient.hpp`) | ★★★ | ciel/brouillard/particules |
| `Curve` (clés Constant/Linéaire/Cubique-Hermite, `ValueAt` à cache de segment) | `math/curve.hpp` | ★★★ | courbes d'animation (seul Bézier existe) |
| `Rect<T>` (AABB 2D : `inflate/clip/contains/transform/push`) | `math/rect.hpp` | ★★★ | base pour la mise en page UI et `bounds()` |
| `Octree` (triangles, plat, requêtes rayon/AABB/sphère/point) + `AABB::Split` + test rayon-AABB (slab) | `math/octree.hpp` | ★★ | picking/accélération (la grille voxel de Morton reste à part) |
| `Triangulate` 3D planaire (cas rapide quad + ear-clipping) | `math/polygon.hpp` (étendre) | ★★ | complète le clipper 2D existant |
| `SmoothAngle` + `wrap_angle`/`round_to_step`/`ieee_remainder` | `math/math.hpp` (étendre) | ★★ | gizmos/caméra/aimantation |
| `RotationOrder` + Euler générique | `math/math.hpp` (étendre) | ★ | aujourd'hui YXZ seul |
| `NumericRange<T>` (bornes possédées + clamp/random) | `math/range.hpp` | ★ | trivial (`Clamp` existe déjà) |
| `RectPacker` (bin packing) | `math/rectpack.hpp` | ★ | tiers (`rectutils`) — porter un shelf/skyline **seulement si** un atlas est nécessaire |

**Règle** : chaque ajout est une **classe/fonction dans un fichier existant de `math`** (pas un
nouveau module) ; l'octree **ne remplace pas** la grille de Morton du voxel (usage différent :
picking de maillages).

### 3.3 `graphics.ui` — registre de raccourcis + liste catalogue

- **Inspiration** : éditeur Fyrox (hotkeys centralisées) + `fyrox-ui` (messages).
- **`ui/keymap.hpp/.cpp`** : type `KeyChord` (modificateurs + clé), `KeyMap` (action → chord,
  défauts, sérialisation `txt`), **capture de touche** (`Context::CaptureKey`) réutilisée par le
  menu de configuration. *C'est l'extension de l'existant le plus rentable* : aujourd'hui les
  raccourcis sont dispersés entre `interface.cpp` (libellés) et `sandbox_app.cpp`
  (`HandleKeyboard`).
- **`ui/catalog.hpp/.cpp`** : `CatalogEntry { id, catégorie, nom, icône, résumé, description,
  raccourci }` + `Context::CatalogList(...)` (liste filtrable/recherche). Réutilisable au-delà de
  sandbox.

> Ces deux ajouts **étendent `graphics.ui`** (couche `ui`), donc accessibles à sandbox sans
> nouveau module applicatif.

### 3.4 `foundation.script` — réflexion de propriétés (inspecteur générique)

- **Inspiration** : `fyrox-core-derive` (`Reflect`/`Visit`) + traits runtime.
- **Contrat runtime (sans macros)** : Fyrox sépare la réflexion en `FieldMetadata` + accès
  type-effacé (`&dyn Reflect`), énumérés par callback. En C++23 on reproduit le même contrat :
  - `FieldInfo { name, displayName, tag, doc, readOnly, min, max, step, precision }` ;
  - `FieldAccessor` (type effacé, `std::any`) : `get(void*)` / `set(void*, any)` ;
  - `TypeDescriptor { name, uuid, fields }` par type.
- **Point d'enregistrement** : `script::ClassBinder<T>` gagne `Fields(...)` (au même endroit que les
  méthodes) — l'inspecteur itère `T::Reflect().fields` pour générer ses lignes (libellé + widget
  selon `FieldInfo`/`ValueKind`), **sans réflexion par chaînes ni macros**.
- **Sérialisation** (`Visit`) : mapper vers `serialize/deserialize` avec **discriminant de variante
  avant le payload** (compat avant) et attributs `optional`/`rename`.
- *À planifier après les phases 1–2 ; débloque l'inspecteur générique (remplace `DrawScriptParams`
  codé en dur) et une annulation par commande si on la veut un jour.*

---

## 4. Améliorations de sandbox

### 4.1 Interface professionnelle

- **Menus** : tous les libellés de raccourcis tirés du `KeyMap` (plus de duplication
  `interface.cpp` / `HandleKeyboard`).
- **Réglages persistés** : étendre `settings.cpp` pour y stocker la table de raccourcis, le
  thème, les préférences d'édition (aimantation, pas).
- **Aide intégrée** : remplacer le toast « Raccourcis clavier » par l'ouverture du **catalogue**
  (voir §5).

### 4.2 Édition 3D (inspirée de l'éditeur Fyrox)

1. **Gizmos de transformation par axe** : sur les outils Déplacer/Rotation/Échelle, afficher des
   poignées X/Y/Z (flèches, cercles, carrés) comme Fyrox ; glisser une poignée contraint l'axe.
   *(Étend l'outil existant, ne remplace pas l'inférence.)*
2. **Aimantation à la grille / aux angles** : pas configurable (menu Réglages), Maj pour
   désactiver ponctuellement.
3. **Sélection par cadre** (marquee) sur l'outil Sélection (complète `SelectAll`/clic).
4. **Alignement / distribution** de plusieurs groupes (menu Édition).

### 4.3 Édition 2D (calque HUD)

1. **Guides d'alignement** (lignes de centrage/arête pendant le déplacement).
2. **Aimantation** au pas de 10 (déjà Maj) rendue configurable.
3. **Sélection multiple** + alignement des éléments 2D.

### 4.4 Mode Jeu

- Exposer dans le catalogue la documentation de chaque API de script (`Behaviour`, `Scene`,
  `PhysicsBody`, `Light3D`, `AudioSource`, `draw2d`, `hud`, `input`).

---

## 5. Catalogue des outils et fonctionnalités (nouveau)

- **Source de vérité** : une table `CatalogEntry` (voir §3.3) décrivant **chaque outil** (Ligne,
  Rectangle, Pousser/Tirer, …), **chaque panneau** (Inspecteur, Structure, Ressources, …),
  **chaque notion** (groupes/composants, matériaux, scènes, calque 2D, scripts, mode Jeu) et
  **chaque API de script**.
- **Accès** : menu `Aide → Catalogue` (et bouton `?` dans la barre d'outils), ouvrant un panneau
  recherchable (champ de texte + liste filtrée + volet de description).
- **Contenu par entrée** : nom, catégorie, icône, résumé, description détaillée (mode d'emploi),
  raccourci associé, éventuellement un exemple de script.
- **Liaison** : le raccourci affiché dans le catalogue est celui du `KeyMap` (reconfiguré en
  direct).

---

## 6. Menu de configuration des raccourcis clavier (nouveau)

- **Accès** : menu `Réglages → Raccourcis clavier…` (et bouton dans le catalogue).
- **Fonctionnement** :
  1. Liste de **toutes les actions** (nom, catégorie, raccourci actuel), filtrable.
  2. Clic sur une ligne → **capture de la nouvelle combinaison** (modificateurs + touche),
     détection de **conflit** avec une action existante (surligné, proposition).
  3. Boutons **« Rétablir le défaut »** (par action et global).
  4. **Persistance** dans `settings.txt` via `KeyMap` (§3.3).
- **Impact** : `HandleKeyboard()` devient **générique** — il consulte le `KeyMap` au lieu d'une
   chaîne de `if`. Les menus lisent le `KeyMap` pour les libellés. Le code est **moins** dupliqué,
   pas plus.
- **Mieux que Fyrox** : (a) **une seule source de vérité** (dispatch **et** libellés) → aucun
  libellé de menu ne peut diverger de la vraie touche (défaut constaté dans Fyrox) ; (b)
  **détection de conflit** entre actions (absente de Fyrox) ; (c) persistance dans `settings.txt`
  déjà en place.

---

## 7. Todo list

### Phase 0 — Fondations (modules)
- [x] `foundation.core` : `core/pool.hpp` (arène générationnelle) + tests.
- [x] `foundation.math` : `math/color.hpp` (`Color` + `Hsv`/`Hsl` + palette + parse + gamma) + `ColorGradient` + tests.
- [x] `foundation.math` : `math/rect.hpp` (`Rect<T>`), `math/curve.hpp` (`Curve` à clés) + tests.
- [x] `foundation.math` : `math/octree.hpp` (+ `AABB::Split`, test rayon-AABB) + `math/range.hpp` + tests.
- [x] `foundation.math` : étendre `polygon.hpp` (triangulation 3D planaire) + `math.hpp` (`SmoothAngle`, `round_to_step`, `RotationOrder`, `WrapAngle`, `QuatFromEuler`) + tests.
- [x] `graphics.ui` : `ui/keymap.hpp/.cpp` (`KeyChord`, `KeyMap`, sérialisation) + tests.
- [x] `graphics.ui` : `ui/catalog.hpp/.cpp` (`CatalogEntry`, liste filtrable) + tests.
- [x] `platform.sdl3` : wrappers `keyboard::KeyName(Key)` et `keyboard::KeyFromName` (extensions du wrapper).

### Phase 1 — Raccourcis configurables
- [x] `SandboxApp` : déclarer le `KeyMap` avec toutes les actions et leurs défauts (`RegisterShortcuts`, `shortcuts.cpp`).
- [x] Remplacer `HandleKeyboard()` par une consultation du `KeyMap` (chords + lettres, boîte de mesures préservée).
- [x] `interface.cpp` : libellés de raccourcis lus depuis le `KeyMap` (`ShortcutLabel`, `SyncMenuShortcuts`).
- [x] `settings.cpp` : persistance du `KeyMap` (sérialisation texte).
- [x] Menu `Réglages → Raccourcis clavier…` (modal : liste, recherche, capture, conflit, réinit) + `Aide → Raccourcis clavier…`.
- [x] Tests de sérialisation / conflit du `KeyMap` (Phase 0, `test_keymap.cpp`).
- [ ] (Optionnel) Persistance de la mise en page du dock (Enregistrer / Charger / Réinitialiser).

### Phase 2 — Catalogue
- [x] Peupler la table `CatalogEntry` (outils, navigation, panneaux, notions, API de script) — `PopulateCatalog`.
- [x] Panneau `Aide → Catalogue` (recherche + description) + bouton `?` dans la barre d'outils.
- [x] Lier les raccourcis affichés au `KeyMap` (les entrées d'action montrent leur combinaison courante).

### Phase 3 — Édition 3D
- [x] Gizmos de transformation : flèches (Déplacer), anneaux (Rotation), cube uniforme (Échelle) — survol surligné, glissé contraint, un pas d'annulation. *(Échelle par axe : à part.)*
- [x] Gizmo de coin (orienter la caméra / basculer projection) — `SceneGizmo` de Fyrox.
- [x] Aimantation configurable (pas d'angle réglable + Maj pour désactiver ; la grille au sol existait déjà).
- [x] Sélection par cadre sur l'outil Sélection (à la racine : sélectionne les groupes dont la boîte projetée coupe le cadre).
- [x] Alignement / distribution (menu Édition → Aligner / Répartir, sur les groupes sélectionnés).
- [x] `RenderQuality` (Low/Med/High/Ultra) + liste dans l'onglet Environnement.

### Phase 4 — Édition 2D
- [x] Guides d'alignement au déplacement (accroche aux bords/centres des autres éléments et de l'écran).
- [x] Pas d'aimantation configurable (curseur « Pas d'aimantation (Maj) » dans l'onglet Entité).
- [x] Sélection multiple (Ctrl+clic) + alignement des éléments 2D (gauche/centre/droite, haut/centre/bas).
- [x] Tilemap + autotile : `TilePattern` (masque 3×3) + `Autotile` + `Tilemap` (grille de terrains, motifs résolus à la demande) — primitives pures testées (`document/{autotile,tilemap}.hpp`).
- [x] Tilemap — rendu + outil d'édition : `TilemapLayer` (grille + palette de couleurs) dans le calque 2D, sérialisé (sauvegarde/relecture) ; rendu en damier (`DrawTilemap`) sous les éléments ; UI « Tuiles » (créer une grille, palette de terrains, peindre à gauche / effacer à droite, tout effacer, supprimer la grille) ; peinture par clic-glissé dans la vue, fusionnée en un seul pas d'annulation par trait.

### Phase 5 — Réflexion, audio & polish
- [x] `foundation.script` : métadonnées `export let` — le type est déduit de la valeur par défaut (nombre/booléen/texte → widget adapté), et la **description** (« # … ») est extraite et affichée sous chaque paramètre. *(Bornes min/max : à part.)*
- [x] Audio : `pitch` + rééchantillonnage linéaire, `radius`/`rolloff` (courbe de distance), pause.
- [x] Audio (suite) : réverbération Freeverb (8 peignes + 4 passe-tout, testée) câblée dans le mixeur (`SoundSystem::SetReverb`). *(HRTF : à part.)*
- [x] Classe native `Camera` (scriptable) : `camera.set(x,y,z,tx,ty,tz)` pose une caméra libre, `camera.reset()` revient au joueur (en plus de `camera.position`/`forward`).
- [ ] Palette matériaux : `Material` = carte nom→valeur, défauts du shader (réinitialiser = effacer).
- [ ] `HandleRemap` + `remap_handles` par type (copie profonde / import glTF).
- [x] Utiliser `ColorGradient` : module de script `gradient.sample(["#…", "#…"], t)` → couleur interpolée (dégradés pour `hud`/`draw2d`). *(Le ciel reste un dégradé zénith→horizon du shader, sans changement.)*
- [x] Écran d'accueil : lien vers le catalogue + derniers raccourcis.
- [x] Mise à jour de `docs/guide.md` et du `README`.

### Phase 6 — Animation
- [x] Lecteur `Animation` + signaux (adossé à `Curve` de la Phase 0) — primitive pure testée.
- [x] `SpriteSheetAnimation` (animation par trames, rects UV + signaux).
- [x] Machine à états réduite (une couche : `State` + `Transition`, paramètres booléens, fondu linéaire) — primitive pure testée.

---

## 8. Suivi

- L'analyse de Fyrox est **terminée** et consolidée dans la §1 (8 analyses couvrant `editor`,
  `fyrox-impl`, `fyrox-core`, `fyrox-ui`, `fyrox-resource`, `fyrox-math`, `fyrox-core-derive`,
  `fyrox-animation`, `fyrox-sound`, `fyrox-scripts`, `fyrox-graph`, `fyrox-autotile`,
  `fyrox-material`, `fyrox-texture`, `fyrox-graphics*`, `fyrox-dylib`, `fyrox-build-tools`,
  `examples`).
- Ordre d'implémentation recommandé : **Phase 0 → 1 → 2** (livrables autonomes à forte valeur,
  sans risque), puis **3 → 4** (éditions 3D/2D, dont tilemap), puis **5 → 6** (audio, animation,
  réflexion, polish).
