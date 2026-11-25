# engine.document — moteur de documents bureautiques

Moteur central de traitement documentaire : modèle en nœuds, propriétés en cascade (document,
groupes, nœud), mise en page dynamique et pagination automatique, rendu indépendant de
l'interface, persistance `.docdb` sur `foundation.sql`, export PDF / HTML / CSV, sélection,
commandes annulables ; structure des documents longs (titres numérotés, légendes, listes, liens
et renvois, tables des matières, signets et liens du PDF) et opérations sur les tableaux (lignes,
colonnes, grands tableaux). Utilisé par l'application `apps/scribe` ; n'en dépend jamais.

Espace de noms : `doc`. Dépendances :
- `foundation.core`, `foundation.math` (`math::Rect`), `foundation.jobs`, `foundation.sql`,
  `foundation.events`, `foundation.archive` (compression des ressources), `foundation.dataio` (CSV) ;
- `foundation.pdf` : métriques des polices standard, lecture des polices TrueType / OpenType
  installées (mesure du texte identique à l'écran et dans le PDF), écriture du PDF avec
  incorporation des polices ;
- `platform.sdl3`, pour ses types de base uniquement (`sdl3::Color`), comme `engine.voxel`.

Le moteur ne redéfinit aucun type déjà fourni par un autre module et n'ouvre ni fenêtre ni
contexte graphique (rendu en `DisplayList` indépendante de l'affichage). Aucune dépendance externe.

---

## 1. Cahier des charges

### 1.1 Objectif

Créer, modifier, organiser, sauvegarder, charger et exporter des documents complexes (texte,
tableaux, images, formes, en-têtes et pieds de page, pages de formats et d'orientations
différents) sans jamais bloquer l'interface : chargement, sauvegarde, mise en page, rendu et
export s'exécutent sur le pool de `foundation.jobs`.

### 1.2 Exigences fonctionnelles

| N° | Exigence | Réponse du moteur |
|---|---|---|
| F1 | Document en nœuds hiérarchiques, identifiants stables | `Node` (id 64 bits jamais réutilisé, type, parent, enfants, groupes, propriétés) |
| F2 | Types extensibles | `NodeType` + table `NodeTypeInfo` (nom, enfants admis, comportement de flux) |
| F3 | Créer, supprimer, déplacer, rechercher, parcourir | `Document::CreateNode / DeleteNode / MoveNode / Find / Visit`, contrôle des cycles et des enfants admis |
| F4 | Propriétés fonctionnelles, structurelles, visuelles typées | catalogue `PropertyId` (catégorie, type, héritable, défaut) et descripteurs typés `Prop<T>` |
| F5 | Explicite / hérité / défaut distingués | `Resolved{valeur, source}` : `Explicit`, `Group`, `Parent`, `Document`, `Default` + nœud d'origine |
| F6 | Groupes multiples, hiérarchiques, prioritaires | `GroupNode` (priorité, groupe parent) ; appartenances explicites ou automatiques |
| F7 | Priorité ≠ proximité | politique `GroupResolution` : priorité d'abord (défaut) ou proximité d'abord, choisie par document |
| F8 | Invalidation des propriétés résolues | révision de style : toute modification invalide les caches en O(1), recalcul paresseux |
| F9 | Pages indépendantes (format, orientation, marges, fond, bordures) | `PageNode` par page ; formats A3, A4, A5, Letter, Legal, personnalisé ; unités : points |
| F10 | En-têtes / pieds de page en nœuds, variantes première / paire / impaire | `HeaderNode` / `FooterNode` modèles de section (variante) ou propres à une page |
| F11 | Layout dynamique en flux, retour à la ligne, tableaux, images, débordement | `Layout()` : mesure par bloc, lignes, cellules, images à l'échelle |
| F12 | Pagination automatique et règles | `KeepTogether`, `KeepWithNext`, `PageBreakBefore`, `AllowSplit`, `MinimumLines`, `MaximumLines`, sauts explicites, lignes de tableau insécables, en-têtes de tableau répétés |
| F13 | Layout incrémental | cache de mesure par bloc (nœud, révision de contenu, largeur) ; seuls les blocs modifiés sont remesurés |
| F14 | Rendu indépendant de l'interface | `DisplayList` en points (rectangles, traits, ellipses, texte, images) |
| F15 | Persistance `.docdb` transactionnelle | schéma relationnel sur `foundation.sql`, écriture atomique, version et migrations |
| F16 | Export PDF (et autres formats) | PDF depuis les `DisplayList` via `foundation.pdf` ; HTML ; CSV des tableaux |
| F17 | Sélection | `Selection` : nœud, multiple, groupe, type, zone, inversion, par identifiant ; règles verrouillé / masqué / non sélectionnable / via groupe |
| F18 | Historique | commandes réversibles par identifiants, transactions, annuler / rétablir, historique consultable |

### 1.3 Exigences non fonctionnelles

- **Concurrence** : le `Document` est modifié par un seul fil (son propriétaire, l'interface).
  Les traitements de fond reçoivent un `Snapshot` immuable (partage des nœuds par copie sur
  écriture : prendre un instantané ne copie que des pointeurs). Résultats rendus par
  `jobs::AsyncValue` (la dernière demande gagne) : aucune donnée partagée modifiable.
- **Aucune exception** (`-fno-exceptions`) : erreurs par `Result<T, doc::Error>` et `Status`.
- **Unités physiques** : le point typographique (1/72 pouce), indépendant de l'écran.
- **Pas d'état global** : polices de référence, caches et pool passés explicitement.

---

## 2. Architecture

```
include/document/          API publique
  types.hpp                identifiants, erreurs, couleurs, unités, formats de page
  properties.hpp           catalogue des propriétés, valeurs, descripteurs typés
  node.hpp                 nœud, types de nœuds et leurs règles
  document.hpp             Document (modèle modifiable), Snapshot (instantané immuable),
                           résolution des propriétés
  fonts.hpp                FontLibrary : polices installées (inventaire, choix du fichier)
  layout.hpp               styles du texte, mise en page, pagination, cache incrémental
  outline.hpp              structure : titres numérotés, légendes, listes, renvois, liens « #id »
  render.hpp               DisplayList et rendu d'une page
  export.hpp               PDF, HTML, CSV
  persistence.hpp          sauvegarde / chargement .docdb
  commands.hpp             commandes, transactions, historique
  selection.hpp            sélection et contexte de sélection
  tables.hpp               tableaux : grille, création rapide, lignes et colonnes, largeurs,
                           hauteurs, bordures prédéfinies, lignes alternées
  text.hpp                 édition du texte : positions, ordre de lecture, curseur, plages,
                           saisie, coupure et fusion, styles, presse-papiers, glisser-déposer
  service.hpp              façade asynchrone (layout, rendu, sauvegarde, chargement, export)
src/core       modèle, propriétés, résolution, instantanés
src/layout     polices, structure, texte, blocs, pagination
src/render     liste d'affichage
src/export     PDF, HTML, CSV
src/persistence schéma SQL, sauvegarde, chargement, migrations
src/edit       commandes, historique, sélection, édition du texte, presse-papiers des nœuds,
               tableaux
src/service    traitements asynchrones
```

### 2.1 Arborescence documentaire

```
DocumentNode
├── GroupsNode                 conteneur des groupes (non rendu)
│   └── GroupNode …            groupes, hiérarchiques (un GroupNode peut contenir des GroupNode)
└── SectionNode …              séquence documentaire (flux continu de pages)
    ├── HeaderNode / FooterNode   modèles (variante : défaut, première, paire, impaire)
    ├── BodyNode               flux : ParagraphNode, TableNode, ImageNode, ShapeNode, DrawingNode, PageBreakNode
    │   └── ParagraphNode → TextNode (runs) ; TableNode → TableRowNode → TableCellNode → blocs ;
    │       ShapeNode → ParagraphNode (texte de la forme) ; DrawingNode → ShapeNode, ImageNode
    └── PageNode …             pages produites par la pagination (propriétés propres possibles,
                               en-tête / pied propres, formes positionnées sur la page)
```

Le contenu circule dans le `BodyNode` ; la pagination décide de sa répartition sur les pages
et crée ou supprime les `PageNode` nécessaires (nœuds « générés »). Une page peut porter ses
propres propriétés (orientation, format, marges, fond) ou appartenir à des groupes : le flux
y est remis en page à sa largeur. Chaque section crée un groupe automatique « pages de la
section » rejoint par chaque nouvelle page : modifier ce groupe modifie toutes ses pages.

### 2.2 Cascade des propriétés

Pour une propriété `P` d'un nœud `N` :

1. valeur explicite de `N` ;
2. groupes de `N` et leurs groupes ancêtres, ordonnés selon la politique du document :
   `PriorityFirst` (défaut : priorité décroissante, puis proximité — groupe direct avant groupe
   ancêtre) ou `ProximityFirst` (proximité, puis priorité) ; à égalité, l'appartenance la plus
   récente l'emporte ;
3. si `P` est héritable (police, couleur, alignement, mise en page de la page…) : valeur
   résolue du parent (une page hérite ainsi de sa section, puis du document) ;
4. valeur explicite du nœud Document ;
5. valeur par défaut du catalogue.

Les propriétés d'identité ou de contenu (`Name`, `Text`, `Resource`, `Field`, `Variant`,
`PageGroup` : `PropertyInfo::cascades` faux) ne se transmettent jamais : valeur explicite ou
défaut. Le nom du document ne devient pas celui de chaque nœud.

Les étapes 2 et 4 ne s'appliquent qu'aux types de nœuds que `P` cible, sauf si `P` est
héritable : un fond posé sur un groupe colore les cellules membres, pas leurs paragraphes. Le
nœud Document et les groupes ne sont jamais membres d'un groupe (les groupes s'imbriquent par
l'arborescence). Une propriété est refusée sur un nœud qu'elle ne cible pas, sauf sur les
groupes et le document, qui la portent pour leurs membres et descendants.

La source et le nœud d'origine sont rendus avec la valeur (panneau des propriétés).

### 2.3 Mise en page et pagination

`Layout(view, &cache)` lit une vue en lecture seule (en pratique un `Snapshot`, sur un fil de
fond) et produit un `LayoutResult` ; `SyncPages(document, layout)` crée ou supprime ensuite les
`PageNode` générés, sur le fil propriétaire.

1. **Géométrie** : format (A3, A4, A5, Letter, Legal, personnalisé), orientation et marges
   résolus par la cascade de chaque page. Une page pas encore créée est résolue comme membre du
   groupe de pages de sa section, puis par la section et le document. Une marge plus grande que
   la page est ramenée.
2. **Mesure** (parallèle, `jobs::ParallelFor`, une `PropertyCache` par tâche) :
   - paragraphe :
     - police de chaque segment : fichier installé de la famille demandée (`LayoutOptions::fonts`,
       style le plus proche en graisse et italique), sinon police standard de `foundation.pdf`
       (Helvetica, Times, Courier ; autres noms rapprochés de l'une d'elles) ;
     - coupure aux espaces, un mot pouvant traverser plusieurs segments ;
     - mots trop longs coupés au caractère ; sauts de ligne `\n` ;
     - retraits gauche, droit et de première ligne ;
     - alignements gauche, centré, droite et justifié (dernière ligne non étirée) ;
     - hauteur de ligne = 1,2 × corps × interligne ;
   - image : 96 ppp, proportions gardées, réduite à la colonne et à la page ;
   - forme : largeur de colonne par défaut ; 25 formes (`ShapeKind` : rectangle, ellipse, trait,
     rectangle arrondi, triangles, losange, parallélogramme, trapèze, pentagone, hexagone,
     octogone, étoiles à 5 et 6 branches, croix, flèches, double flèche, chevron, cœur, bulle,
     éclair, flèche-trait) ; contour inscrit dans la boîte donné par `ShapeOutline` ;
   - texte d'une forme : ses paragraphes, mesurés à la largeur intérieure (bordure et
     rembourrage déduits), alignés verticalement par `VerticalAlign` ; hauteur 0 = hauteur du
     texte. Ses lignes tournent avec la forme (`PlacedLine::rotation`, pivot) ; le texte d'une
     forme est un récit à part (navigation du curseur) ;
   - zone de dessin (`Drawing`) : bloc du flux (largeur de la colonne et 200 pt de haut par
     défaut, fond, bordure, rembourrage) dont les formes et images sont placées librement :
     ni réduites à la colonne, ni alignées, position dans le repère de sa zone intérieure
     (nœud et parent), rotation propre ; pas de découpe à ses bords ;
   - alignement horizontal `None` (« aucun ») : image, forme ou zone de dessin placée par sa seule
     position (place = bord de la colonne) ; pour le texte, comme `Left`. La boîte d'une forme
     garde le décalage d'alignement appliqué (`alignX`) pour passer à « aucun » sans bouger ;
   - `KeepAspectRatio` (images, formes, zones de dessin) : indication pour le redimensionnement
     à la souris (l'application la respecte), sans effet sur la mise en page ;
   - images et formes : position dans le repère choisi (`PositionReference` : nœud = décalage
     depuis sa place dans le flux, parent = coin du conteneur, page ; `Auto` = nœud dans le
     flux, page hors flux), rotation (`Rotation`, degrés, sens horaire) autour d'un centre
     (`RotationOriginX/Y` dans le repère `RotationOriginReference` : décalage depuis le centre
     de la forme, coin du parent ou page). La boîte garde le rectangle avant rotation, le
     centre en coordonnées de la page et les origines des repères (conversion des coordonnées
     quand on change de repère) ; `Contains` et `HitTest` tiennent compte de la rotation ;
   - tableau : colonnes de largeur explicite (première cellule qui en porte une), les autres à
     parts égales ; hauteur de ligne = cellule la plus haute ; alignement vertical des cellules.
3. **Cache incrémental** (`LayoutCache`) :
   - clé : nœud ; révision du nœud (descendants compris) ; signature de la lignée (identifiant
     et révision du style transmis de chaque ancêtre) ; révision du style partagé (groupes,
     nœud Document, politique et hiérarchie des groupes) ; largeur et hauteur disponibles ;
     identité de la bibliothèque de polices ;
   - la clé est vérifiée en entier : une collision de hachage ne rend jamais une mauvaise mesure ;
   - remesuré seulement :
     - le paragraphe dont on modifie le texte ou le style ;
     - le tableau dont une cellule change ;
     - les blocs du corps dont on change le style hérité ;
     - tout le document seulement pour un style du document ou d'un groupe ;
   - un test différentiel compare la mise en page incrémentale à une mise en page complète
     après 300 modifications aléatoires ;
   - les mesures inutilisées sont retirées à chaque mise en page.
4. **Pagination** (séquentielle), bloc après bloc :
   - saut de page explicite, `PageBreakBefore` (sans page vide en tête de page) ;
   - `KeepWithNext` : le bloc et le début du suivant, au moins `MinimumLines` lignes ;
   - `KeepTogether` ;
   - paragraphes coupés entre lignes, sans orpheline ni veuve (`MinimumLines`), avec au plus
     `MaximumLines` lignes par page ;
   - tableaux coupés entre lignes (une ligne ne se coupe jamais), lignes `RepeatHeader` de tête
     répétées sur chaque page ;
   - bloc trop grand pour une page vide : placé en débordement (`LayoutBox::overflow`), sans boucle ;
   - espace avant supprimé en haut de page ;
   - blocs `Positioning::Absolute` posés sur la page courante.
5. **En-têtes et pieds** : celui de la page s'il existe, sinon la variante de la section (première
   page de la section, paire, impaire, défaut). Leur hauteur repousse la zone de flux au-delà
   des marges si nécessaire. Ils sont mis en page après la pagination avec les vrais numéros :
   champs `PageNumber` / `PageCount`, numérotation continue sur tout le document.
6. **Contenu propre des pages** : formes et images posées sur un `PageNode` (`PositionX/Y`).
   Une page qui en porte n'est jamais supprimée.
7. **Résultat** :
   - pages : géométrie, zone de flux, boîtes positionnées dans l'ordre de dessin, lignes avec
     leurs segments en coordonnées absolues ;
   - `HitTest` : nœud le plus profond sous un point, texte d'abord ;
   - `PagesOf` : pages où un nœud apparaît.

   Le rendu et les exports consomment ce résultat tel quel.

### 2.4 Persistance (.docdb)

`SaveDocument(view, path)` / `LoadDocument(path)` (`persistence.hpp`).

Le fichier est une base `foundation.sql` sérialisée en instantané avec une somme CRC-32. Il est
écrit dans un fichier temporaire puis renommé : une sauvegarde interrompue laisse l'ancien
fichier intact et ne laisse pas de fichier temporaire. L'enregistrement lit une vue en lecture
seule ; il s'exécute donc sur un `Snapshot`, sur un fil de fond, pendant que l'édition continue.

| Table | Colonnes |
|---|---|
| `meta` | `key TEXT PK`, `value TEXT` : `format` (`apps-lib.docdb`), `version`, `next_id`, `next_resource`, `resolution` |
| `nodes` | `id INT PK`, `type TEXT`, `parent INT INDEX`, `position INT`, `generated INT` |
| `properties` | `id INT PK`, `node INT INDEX`, `name TEXT`, `kind INT`, `ival INT`, `rval REAL`, `tval TEXT` |
| `memberships` | `id INT PK`, `node INT INDEX`, `grp INT INDEX`, `position INT` (rang dans les groupes du nœud), `member_position INT` (rang dans les membres du groupe), `automatic INT` |
| `resources` | `id INT PK`, `kind TEXT` (`rgba8`), `width INT`, `height INT`, `data BLOB` (zlib) |

- **Identifiants stables** : types de nœuds et propriétés enregistrés par leur nom du
  catalogue, jamais par valeur d'énumération. Les couleurs sont stockées en RVBA 32 bits dans
  `ival`. Les colonnes sont lues par nom.
- **Intégrité au chargement**. `foundation.sql` n'a ni clés étrangères ni transactions, donc
  tout est vérifié et la moindre incohérence rend `ErrorCode::Corrupt` :
  - une seule racine Document ;
  - parents existants ;
  - enfants admis par leur parent, unicité des Body et Groups ;
  - aucun nœud hors de l'arborescence (cycle ou orphelin) ;
  - chaque section a un corps ;
  - propriétés connues, du bon type, dans leur domaine, applicables, non dupliquées ;
  - appartenances vers des groupes, non dupliquées ;
  - ressources de dimensions cohérentes avec leurs pixels ;
  - images vers des ressources présentes ;
  - groupes de pages valides.
- **Compteurs** : `next_id` n'est jamais inférieur au plus grand identifiant présent, donc un
  identifiant supprimé avant l'enregistrement n'est pas réutilisé après rechargement.
- **Versions** :
  - un format plus récent que le moteur est refusé (`Unsupported`) ;
  - un format plus ancien est migré par les fonctions `Migration{from, apply}` (version n → n+1,
    appliquées à la base avant la lecture), et la version est mise à jour ;
  - le moteur est en version 1 : aucune migration intégrée pour l'instant, le mécanisme est
    testé avec une migration d'essai.

### 2.5 Rendu et export

`RenderPage(view, layout, page, options)` produit une `DisplayList` en points (`render.hpp`).
`RenderDocument` rend toutes les pages, en parallèle sur le pool de `foundation.jobs`.

- **Commandes** : `FillRect`, `StrokeRect`, `FillEllipse`, `StrokeEllipse`, `Line`, `Text`
  (police standard ou fichier installé, corps, couleur, ligne de base, boîte du segment),
  `Image` (ressource, opacité) et `FillPolygon` / `StrokePolygon` (sommets `points` : formes
  autres que rectangle, ellipse et traits ; pointe de la flèche-trait, `ArrowHead`). Chaque
  commande porte son nœud d'origine, et la rotation et le centre de rotation de son image, de
  sa forme ou du texte de sa forme.
- **Ordre de dessin** : papier, fond et cadre de la page, puis les boîtes dans l'ordre de la
  mise en page (fonds et bordures des paragraphes, tableaux, lignes, cellules, en-têtes et
  pieds, zones de dessin ; images ; formes), enfin le texte (surlignage, glyphes,
  soulignement aux position et épaisseur AFM).
- **Propriétés** : opacité des images et des formes. Une image sans ressource est dessinée en
  cadre barré.
- **Pages virtuelles** : les propriétés d'une page pas encore créée sont résolues par
  `ResolvePageProperty` (groupe de pages, section, document).
- **Options** : papier, limites des blocs (Affichage), marqueurs de débordement.

La liste ne dépend d'aucune bibliothèque graphique. L'application la convertit vers son
canevas, l'export vers un `pdf::Canvas` : la même police (fichier installé ou métriques
standard) mesure le texte pour l'écran et pour le PDF, et l'interface place chaque segment à la
position calculée. Le PDF tourne les commandes par une matrice (`cm`), le HTML par
`transform: rotate()` et `transform-origin`.

### 2.6 Exports (`export.hpp`)

Les exports lisent une vue en lecture seule, donc un instantané sur un fil de fond. Les fichiers
sont écrits de façon atomique.

- **PDF** — `ExportPdf` / `SavePdf` :
  - les listes d'affichage (`RenderDocument`) sont converties vers `pdf::Writer` : le PDF montre
    exactement ce que montre l'écran ;
  - chaque ressource image n'est écrite qu'une fois, avec son opacité ;
  - métadonnées : titre (sinon le nom du nœud Document), auteur, sujet, créateur, date facultative ;
  - signets sur les blocs du flux qui portent un nom, à leur première position ;
  - sortie déterministe sans date ; une mise en page d'un autre document est refusée.
- **HTML** — `ExportHtml` / `SaveHtml` : document HTML5 autonome en flux continu.
  - **Structure** : `@page` au format et aux marges de la première page, en-tête et pied par
    défaut de la première section.
  - **Paragraphes** : style complet (espacements, retraits, alignement, interligne, fond,
    bordure, marge intérieure, règles de pagination en CSS d'impression : `break-before`,
    `break-inside`, `break-after`, `orphans`, `widows`) ; segments stylés seulement là où ils
    diffèrent du paragraphe.
  - **Tableaux** : lignes `RepeatHeader` en `<thead>`, que le navigateur répète à l'impression ;
    largeurs mesurées par le moteur.
  - **Images** en PNG embarqué (encodeur SDL_image du wrapper `sdl3`, Base64 de
    `foundation.dataio`) ; formes en `div` (rectangle, ellipse : `border-radius`), les autres et
    les traits en SVG intégré (`polygon`, `line`), texte des formes dans la `div` (alignement
    vertical en flexbox) ; zones de dessin en `div` relative, éléments en position absolue.
  - **Sauts de page** et texte échappé.
- **CSV** — `ExportCsv` / `SaveCsv` (tableurs) : un tableau par fichier, RFC 4180, écrit par
  `dataio::CsvDocument::EncodeRows`. La grille est brute : la première ligne peut répéter des
  intitulés. Les paragraphes d'une cellule sont séparés par un retour à la ligne, les tableaux
  imbriqués aplatis, les lignes masquées omises et les lignes courtes complétées.

### 2.7 Sélection et commandes (`selection.hpp`, `commands.hpp`)

- **Sélection** :
  - un nœud, plusieurs (ajout, bascule, retrait), un groupe (sous-groupes compris), un type,
    une zone de page (blocs entièrement contenus), inversion parmi les frères, identifiants ;
  - règles : structurels et `Selectable` faux refusés ; masqués refusés, sauf depuis
    l'arborescence ; `SelectViaGroup` sélectionne tout le groupe ;
  - `Prune` après modification ;
  - `SelectionContext` (texte, tableau englobant, image, forme, page, groupe, en-tête ou pied,
    verrouillé, masqué, mixte) pour l'interface.
- **Commandes** :
  - création, suppression, déplacement, duplication, propriétés (affectation, retour à
    l'héritage), groupes (création, ajout, retrait au rang d'origine, regroupement,
    dissociation), politique des groupes, composite atomique ;
  - références par identifiant : une suppression annulée restaure le sous-arbre à l'identique,
    une création rétablie réutilise son identifiant.
- **Historique** :
  - annuler, rétablir, saut à une position ;
  - fusion des frappes dans un même texte, branche abandonnée à la commande suivante ;
  - limite d'entrées ; état enregistré, y compris celui d'une sauvegarde de fond
    (`StateId` / `MarkCleanAt`) ;
  - transactions (une entrée, annulation en bloc ou abandon) ; notification `Changed`.

### 2.8 Polices installées (`fonts.hpp`)

`FontLibrary::Scan(dossiers)` inventorie les fichiers `.ttf`, `.otf`, `.ttc` et `.otc` (lecture
partielle des tables `name` et `OS/2` par `pdf::ScanFonts`) ; l'application l'exécute en
arrière-plan sur les dossiers de polices du système. `Families()` donne les trois familles
standard puis les familles installées, triées. `Resolve(famille, gras, italique)` choisit le
fichier le plus proche (graisse, puis italique), le charge une seule fois (partagé entre fils)
et rend un `pdf::FontRef` : métriques réelles pour la mesure, programme de police pour le PDF.
Une police illisible retombe sur la police standard (mesure, écran et PDF cohérents).

### 2.9 Édition du texte (`text.hpp`)

- **Positions** : `TextPosition` = segment de texte + décalage en octets (frontière UTF-8).
  `TextOrder` donne l'ordre de lecture et les récits (corps de section, en-tête, pied, page) :
  caractère et mot suivants ou précédents (convention de Word : Ctrl+→ va au début du mot
  suivant), mot et paragraphe autour d'une position, début et fin du récit. Les flèches ne
  quittent pas le récit.
- **Géométrie** (sur une vue et sa mise en page) : curseur (`Caret`), position sous un point,
  ligne voisine à abscisse constante (traverse les pages), début et fin de ligne, rectangles de
  surbrillance d'une plage.
- **Modifications** (annulables, via `History`) : insertion (« \n » coupe le paragraphe),
  saut de ligne dans le paragraphe (`InsertLineBreak` : « \n » gardé dans le segment ; le curseur
  qui le suit est au début de la ligne suivante, y compris une ligne vide finale),
  suppression d'une plage sur plusieurs paragraphes (fusion), coupure (Entrée), style d'une
  plage (segments coupés aux bornes, appartenances aux groupes copiées), collage d'un fragment
  enrichi, déplacement ou copie d'une plage (glisser-déposer). Une frappe dans un segment reste
  une commande simple fusionnée avec les suivantes ; le reste est une transaction.
- **Presse-papiers** :
  - `TextFragment` : paragraphes et segments avec leurs propriétés explicites, texte brut,
    construction depuis un texte brut ;
  - `NodeFragment` (`commands.hpp`) : `CopyNodes` copie des sous-arbres (ordre du document,
    descendants d'un nœud déjà copié ignorés, pages refusées, appartenances aux groupes
    conservées, nom non dupliqué) ; `PasteNodes` les recrée en une transaction, refusée en bloc
    si le parent n'accepte pas un des nœuds.

---

### 2.10 Structure du document (`outline.hpp`)

Calculée au début de chaque mise en page, avant la mesure ; utilisable seule (`BuildOutline`)
pour un panneau de navigation ou les signets.

- **Titres** : `OutlineLevel` 1…9 sur un paragraphe (en pratique par un groupe de style
  « Titre 1 »…). `HeadingNumbering` (document ou section) : aucun numéro, « 1.2 » ou « 1.2. » ;
  compteurs par niveau, remis à zéro sous un titre de niveau supérieur.
- **Légendes** : `CaptionKind` (Figure, Tableau, Listing), un compteur par type sur tout le
  document ; libellé par défaut « Figure », « Tableau », « Listing » ou `CaptionLabel` ; préfixe
  « Figure 3 : ».
- **Listes** : `ListStyle` (puces •, –, —, », numérotations 1. 1) a. A. i. I.), `ListLevel`
  0…8, `ListStart` (reprise à une valeur). Compteurs par conteneur (corps, cellule) : un
  paragraphe hors liste, un tableau ou un autre bloc coupent la liste ; un niveau inférieur
  repart de 1 sous un élément de niveau supérieur. Le marqueur est dans un retrait de
  18 pt par niveau.
- **Préfixes** : numéros et légendes en tête de la première ligne, marqueurs de liste dans le
  retrait : des segments générés (`GlyphRun::generated`) que l'édition du texte ignore (le
  curseur ne s'y place jamais).
- **Liens et renvois** : `Link` sur un segment (URL, ou « #id » vers un nœud). Champs
  `Reference` (numéro de la cible : « 2.3 », « Tableau 4 »), `PageReference` (sa page, remplie
  après la pagination) et `Chapter` (en-tête courant : premier titre de niveau 1 de la page,
  sinon le dernier des pages précédentes).
- **Tables des matières** (`TableOfContents`, bloc sans enfant) : titres jusqu'à `TocDepth`, ou
  légendes d'un type (`TocContent`), retrait par niveau, points de conduite, numéros de page
  alignés à droite. `Hyperlinks` (vrai par défaut) : entrées en liens « #id » (Ctrl+clic dans
  l'application, liens du PDF). Coupée entre pages comme un paragraphe.
- **Cache** : la contribution de chaque bloc du corps (titres, légendes, listes, cellules
  comprises) est gardée avec la même clé exacte que les mesures ; seule la numérotation est
  refaite. La clé de mesure d'un bloc contient ses préfixes : un titre renuméroté est
  remesuré, pas les autres. Les tables des matières et les paragraphes à renvois sont toujours
  remesurés.
- **Autres propriétés de texte et de bloc** : `Strike` (barré), `Script` (exposant, indice :
  corps × 0,65, ligne de base décalée), `BorderSides` (côtés de la bordure : filets,
  encadré à barre latérale), `PreserveSpaces` (espaces de début de ligne conservés : code).
- **Ordre d'empilement** : `ZIndex` (entier, 0 par défaut) des images et formes. Dessin par
  couche — les autres boîtes à la couche 0 —, ordre de la mise en page à couche égale (un
  document sans ZIndex se dessine exactement comme avant) ; le texte reste au-dessus.
  `HitTest` rend l'objet du dessus (couche, puis profondeur). HTML : `z-index`.

### 2.11 Tableaux (`tables.hpp`)

- `TableGrid` : lignes, cellules par rang, plage rectangulaire, colonne ; cellule d'un nœud.
- `table::Create` : un tableau de L × C en une transaction — première ligne construite, les
  autres dupliquées (une commande par ligne) ; 1 000 colonnes, 1 000 000 de cellules au plus.
- Lignes et colonnes : insertion (mise en forme de la voisine, sans texte ni largeur),
  suppression (toutes : le tableau disparaît), duplication, déplacement d'un bloc avant un
  rang ; une entrée d'historique par opération.
- Largeur de colonne (portée par la cellule de la première ligne), colonnes égales, hauteur
  minimale de ligne, même propriété sur plusieurs cellules, bordures prédéfinies (toutes,
  aucune, extérieures, filets horizontaux, intérieures), fonds alternés (portés par les
  cellules : un groupe de fond ne peint jamais une ligne entière).
- Cellules fusionnées : `ColumnSpan` / `RowSpan` de la cellule d'ancrage (coin haut gauche),
  `table::Spans` (fusions effectives, bornées, sans chevauchement), `MergeCells` (contenu des
  autres cellules réuni, fusions comprises absorbées, refus si une fusion serait coupée),
  `SplitCells`. Mise en page : largeur des colonnes couvertes, la dernière ligne couverte
  s'agrandit si le contenu dépasse, lignes liées jamais coupées entre deux pages, cases
  couvertes non mises en page ; HTML : `colspan` / `rowspan`. Limite : insérer, supprimer ou
  déplacer des lignes et colonnes ne corrige pas les étendues (bornées à la grille).
- **Grands tableaux** : les blocs des cellules ont leur propre entrée de cache (mesurés en
  parallèle quand ils manquent) ; un tableau repris du cache garde ceux de ses cellules.
  Modifier une cellule ne remesure qu'elle.

## 3. Performances

Mesures du test `Performance.LongDocument` (`cpp-maker test release document`, 8 cœurs) sur
un document de 32 060 nœuds (3 000 paragraphes, 40 tableaux de 50 × 4 cellules, images),
191 pages :

| Opération | Durée |
|---|---|
| Mise en page à froid (mesure parallèle) | 39 ms (156 ms sur un seul fil) |
| Mise en page sans modification | 7,3 ms (structure du document comprise : 4,6 ms avant elle) |
| Après frappe dans un paragraphe | 4,6 ms* |
| Après changement de style d'un paragraphe | 4,6 ms* |
| Après changement de style du document | 34 ms* |
| Rendu des 191 pages (parallèle) | 2 ms* |
| Instantané | 1,5 ms* |
| Enregistrement / chargement `.docdb` | 25 ms / 28 ms* |
| Export PDF | 31 ms (164 Kio)* |

\* Valeurs de la phase précédente : la machine était chargée lors des dernières mesures,
elles n'ont pas été reprises (seules les deux premières lignes l'ont été, au calme).

Test `Tables.CreateLargeTableInOneEntry` (tableau de 2 000 × 40, 240 001 nœuds, 45 pages) :
création 61 ms (une entrée d'historique), mise en page à froid 268 ms, après frappe dans une
cellule 198 ms (une seule cellule remesurée : le reste est la relecture des propriétés des
80 000 cellules, la structure et la pagination), sans modification 21 ms.

La mise en page, le rendu, l'enregistrement, le chargement et les exports s'exécutent sur des
instantanés, hors du fil de l'interface. Seul l'instantané (1,5 ms) est pris sur ce fil à
chaque modification.

## 4. Limites connues (version 1)

- Structure :
  - pas de sections en colonnes multiples (un tableau sans bordure en tient lieu) ni de
    cellules fusionnées ;
  - libellés des légendes en français par défaut (« Tableau ») : `CaptionLabel` pour un autre
    mot ; séparateur « : » fixe ;
  - les listes et les légendes des en-têtes et pieds de page ne sont pas numérotées.
- Polices :
  - un nom de famille non installé est rapproché de Helvetica, Times ou Courier ;
  - le texte reste encodé en WinAnsi, même avec une police installée (police simple, pas de
    police composite Identity-H) : un caractère hors WinAnsi est remplacé par « ? » ;
  - police incorporée entière (pas de sous-ensemble), pas de crénage ni de ligatures ;
  - une police dont la licence interdit l'incorporation (`fsType` restreint) est mesurée et
    affichée avec ses métriques, mais le PDF écrit le segment dans la police standard
    rapprochée : chaque segment commence à la bonne place, ses glyphes n'ont pas la même largeur ;
  - une police variable est utilisée dans son instance par défaut.
- Édition du texte : positions en octets dans un segment ; un champ de numérotation compte pour
  un caractère et ne s'édite pas. Pas de correcteur, pas de méthode de saisie avec
  pré-édition affichée (le texte composé arrive validé).
- Rotation : images et formes (et le texte d'une forme) seulement ; l'édition du texte d'une
  forme tournée retrouve le curseur sous le pointeur (rotation inverse), mais `RangeRects` rend
  les rectangles non tournés (l'application les tourne). Redimensionner une forme tournée dont le centre de
  rotation est le centre par défaut déplace ce centre (comme la forme), donc le coin opposé
  bouge à l'écran.
- Pas de césure, pas de texte bidirectionnel, pas de colonnes multiples ; coupure aux espaces
  seulement.
- Un paragraphe coupé entre deux pages de largeurs différentes garde la coupure de lignes de la
  première page.
- `KeepWithNext` regarde le bloc suivant uniquement : une chaîne de trois blocs peut se séparer
  après le deuxième.
- Pagination entièrement refaite à chaque mise en page ; seules les mesures des blocs et la
  structure (par bloc) sont incrémentales.
- Hauteur des en-têtes et pieds évaluée pendant la pagination avec des champs provisoires
  (« 00 ») : un numéro qui provoquerait un retour à la ligne supplémentaire déborde légèrement.
  Dans le flux, la coupure des lignes est faite avec la largeur provisoire ; le texte qui suit
  le champ sur sa ligne est ensuite recalé.
- Une ligne de tableau plus haute qu'une page déborde (jamais coupée).
- Marge intérieure des cellules : 0 par défaut (catalogue) ; l'application la fixe à l'insertion
  d'un tableau. De même, une forme n'a ni fond ni bordure par défaut (catalogue) : l'application
  lui donne des couleurs à l'insertion.
- Historique : les pages créées ou supprimées par `SyncPages` ne passent pas par l'historique ;
  les ressources images ajoutées restent dans le document (non référencées) si leur insertion
  est annulée.
- Rendu : les formes et images sont dessinées avant le texte, quel que soit leur ZIndex (le
  texte reste au premier plan, y compris le texte d'une forme recouverte par une autre) ; le
  ZIndex ne les ordonne qu'entre elles et par rapport aux fonds des blocs.
- Zone de dessin : ses éléments ne sont pas découpés à ses bords ; un paragraphe ou un tableau
  n'y entre pas directement (texte libre : zone de texte, c'est-à-dire une forme sans fond ni
  bordure portant des paragraphes). Pas de structure (titres, légendes, listes numérotées) dans
  le texte des formes.
- Alignement d'une forme placée librement (zone de dessin, position absolue) : le moteur ne
  l'applique pas (seule la position compte) ; l'application calcule la position alignée.
- HTML :
  - flux continu, non paginé ;
  - les champs de numérotation sont conservés vides (`<span class="field">`) ;
  - seuls l'en-tête et le pied par défaut de la première section sont repris ;
  - les blocs positionnés (`Positioning::Absolute`) sont placés dans le flux.
- Images : pixels RGBA fournis par l'application (le décodage PNG/JPEG relève de la
  plateforme, absente du layer `engine`).
- Sauvegarde : instantané complet à chaque sauvegarde (pas d'écriture incrémentale sur
  disque) ; l'atomicité est celle du fichier (renommage), `foundation.sql` n'offrant pas de
  transactions internes. Pas de `fsync` : une coupure de courant juste après l'enregistrement
  peut laisser l'ancienne version selon le système de fichiers.
