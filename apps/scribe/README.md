# scribe

Editeur de documents

```sh
cpp-maker run release scribe # compiler et exécuter
cpp-maker build scribe       # compiler seulement
```

# Scribe — éditeur de documents

Application d'édition de documents à la manière de Microsoft Word ou LibreOffice Writer,
construite sur `graphics.app` / `graphics.ui` et sur le moteur `engine.document`. Elle ne
contient **aucune logique documentaire** : création et organisation des nœuds, résolution des
propriétés, mise en page, pagination, historique, persistance et export sont des services du
moteur ; l'application présente, interagit et synchronise.

---

## 1. Cahier des charges

### 1.1 Objectif

Éditer des documents `.docdb` (texte, tableaux, images, formes, en-têtes et pieds de page,
pages portrait et paysage), les enregistrer, les rouvrir et les exporter en PDF, HTML ou CSV,
sans jamais figer l'interface : la mise en page, le rendu, la sauvegarde, le chargement et
l'export tournent en arrière-plan (services asynchrones du moteur).

### 1.2 Accueil

L'application s'ouvre sur l'accueil (sauf document ou `--template=` en argument) :
- colonne de gauche : Nouveau, Ouvrir…, Retour au document (si un document est ouvert),
  Paramètres, Effacer l'historique (documents récents) ;
- **Brouillon non sauvegardé** (bandeau en tête, s'il y a un brouillon) : nom du document et
  bouton « Reprendre » (voir ci-dessous) ;
- **Nouveau document** : vignettes des modèles intégrés — document vierge, lettre
  administrative, compte rendu de réunion, rapport de projet (table des matières, titres
  numérotés), curriculum vitae, **Démo** (fiche technique de démonstration : toutes les fonctions
  de l'éditeur), article de blog, recette de cuisine, liste de tâches, note rapide (contenus rédigés
  en français) ;
- **Mes modèles** : modèles personnels (fichiers `.docdb` du dossier de données) ; « Enregistrer le
  document comme modèle… » (aussi dans l'onglet Fichier), au survol d'une vignette : renommer,
  supprimer (confirmation) ; un document créé depuis un modèle est « Sans titre » ;
- **Récents** : derniers documents ouverts ou enregistrés (fichier disparu : retiré de la liste) ;
- **Paramètres** : langue de l'interface et thème de couleur.

Le bouton « maison » de la barre de titre (ou l'onglet Fichier) ramène à l'accueil.

`--template=<modèle>` ouvre directement un nouveau document depuis un modèle intégré, par son
identifiant (`blank`, `letter`, `minutes`, `report`, `resume`, `demo`, `article`, `recipe`, `tasks`,
`note`), ou depuis un modèle personnel, par son nom ; modèle inconnu : message et accueil (export
sans fenêtre : code de sortie 1 et liste des identifiants). Il remplace l'ancien `--demo`
(`--template=demo`).

**Brouillon.** Tant que le document a des modifications non enregistrées, une copie est
enregistrée en arrière-plan (au plus toutes les 2 secondes) dans le dossier des données
(`draft.docdb`, avec `draft.ini` : nom affiché et chemin du document d'origine), ainsi qu'avant
de le remplacer (nouveau document, modèle, ouverture), au retour à l'accueil et à la fermeture de
l'application. « Reprendre » revient au document de l'éditeur s'il est encore ouvert, sinon
recharge le brouillon (session précédente, ou document remplacé depuis) : le document repris est
marqué modifié et rattaché à son fichier d'origine (« Enregistrer » l'écrase ; jamais enregistré :
« Enregistrer » demande où). Le brouillon est supprimé dès que son document est enregistré (ou
revenu à l'état enregistré par des annulations) ; celui d'un document fermé peut être abandonné
(croix du bandeau). Il n'y a qu'un brouillon : celui du dernier document modifié.

### 1.3 Langues et thèmes

- Boutons de la barre de titre : langue (clic : langue suivante — français, anglais, allemand,
  italien, chinois simplifié, japonais) et thème (clic : thème suivant — Nuit, Clair,
  Contraste élevé). Les deux se choisissent aussi dans les Paramètres de l'accueil.
- Au premier lancement, la langue est celle du système (wrapper `sdl3`, anglais si elle n'est pas
  proposée) ; ensuite le dernier choix, gardé avec le thème et les documents récents dans
  `settings.ini` du dossier de données (`sdl3::filesystem::AppDataDir("scribe")`).
- Traductions : `apps/scribe/i18n/<code>.json` (traducteur de `graphics.ui`, clés = textes
  français des sources, repli sur le français) ; les messages formatés gardent leurs « {} »
  (vérifié par les tests, une traduction incompatible retombe sur le français).
- Chinois et japonais : une police installée qui les contient (Noto Sans CJK, WenQuanYi, Droid
  Sans Fallback…) devient police de repli du canevas après l'inventaire des polices.
- Les thèmes colorent les widgets (`ui`), la palette de l'application (espace de travail,
  panneaux, accueil, sélection, guides) et la feuille de style du ruban ; l'onglet ouvert du
  ruban est souligné à la couleur d'accent.
- Vocabulaire : « marge » désigne toujours la marge de la page (*margin*), « rembourrage »
  l'espace intérieur d'une boîte (*padding*) ; les traductions gardent la distinction (japonais :
  余白 / パディング).

### 1.4 Organisation de la fenêtre

| Zone | Rôle |
|---|---|
| Ruban (onglets en tête) | commandes regroupées par onglet ; l'onglet actif suit le contexte de sélection ; onglets trop nombreux pour la largeur : défilants (flèches, molette) ; page trop large : barre de défilement horizontale (et molette) |
| Zone centrale | règles graduées (marges, bords du tableau courant), pages rendues depuis les `DisplayList` du moteur, zoom, défilement, sélection à la souris, outils des tableaux au survol |
| Panneau latéral | onglets **Plan**, **Propriétés**, **Historique**, **Arborescence** |
| Barre d'état | page courante / nombre de pages, nœuds sélectionnés, état de sauvegarde, travaux en cours |

### 1.5 Onglets du ruban

| Onglet | Commandes |
|---|---|
| Fichier | nouveau, ouvrir, enregistrer, enregistrer sous, exporter PDF / HTML / CSV |
| Accueil (texte) | couper, copier, coller ; gras, italique, souligné, barré, exposant, indice ; famille (polices standard et toutes les polices installées, saisie ou liste filtrée), taille (liste ou valeur tapée, ex. « 10,5 »), couleur du texte et couleur de surlignage (fenêtre des couleurs) ; alignement, interligne, listes à puces et numérotées, retraits ; styles de paragraphe (Normal, Titre, Sous-titre, Titre 1 à 4, Citation, Code, Encadré, Légende) |
| Insertion | paragraphe ; tableau de L × C (valeurs tapées : jusqu'à 1 000 colonnes et 1 000 000 de cellules) ; image, rectangle, ellipse, trait, galerie des 25 formes (aperçus dessinés avec les contours du moteur), zone de texte, zone de dessin, légende de figure ; encadré, citation, bloc de code, lien ; table des matières, saut de page, numéro de page |
| Références | table des matières, table des figures, liste des tableaux ; profondeur et liens interactifs de la table sélectionnée ; numérotation des titres (aucune, 1.2, 1.2.) ; légendes (figure, tableau, listing) ; renvoi au curseur (numéro ou page d'un titre ou d'une légende) ; lien, retrait du lien |
| Mise en page | format (A3, A4, A5, Letter, Legal), orientation, marges (préréglage 1,27 / 1,91 / 2,54 / 3,81 cm des quatre côtés, puis chaque côté au centième de centimètre — de 0 à ce qui laisse 36 pt de contenu), couleur de fond de page |
| Tableau (contextuel) | sélection (tableau, lignes, colonnes) ; insertion de N lignes / colonnes de chaque côté ; suppression, duplication (lignes ou colonnes sélectionnées, contiguës ou non), déplacement (contiguës) ; fusion des cellules sélectionnées, séparation ; fond des cellules (fenêtre des couleurs), bordures prédéfinies, bandes, en-tête répété ; largeur de colonne et hauteur de ligne (cm), colonnes égales |
| Disposition | grouper, dissocier, dupliquer, supprimer ; premier plan, avancer, reculer, dernier plan (images et formes de la page, Ctrl+Maj+Haut / Ctrl+Haut / Ctrl+Bas / Ctrl+Maj+Bas) ; forme : alignement horizontal (aucun, gauche, centré, droite), garder les proportions, texte de la forme ; verrouiller, masquer |
| Affichage | zoom avant / arrière, ajuster à la largeur, afficher les limites des blocs, ruban contextuel, règles ; grille (affichage et pas en cm) |

Une commande non applicable à la sélection est désactivée. Les boutons du ruban sont des icônes
(Material, ou SVG de `apps/scribe/icons` quand Material n'en a pas d'adaptée, teintées comme les
autres) avec une info-bulle ; seuls les listes, les champs et l'indicateur de zoom gardent du
texte.

**Formes et zones de dessin**
- *Zone de dessin* : bloc du flux où formes, images et zones de texte se placent librement, à
  n'importe quelle position et inclinaison ; une forme, une image ou une zone de texte insérée
  quand la zone (ou l'un de ses éléments) est sélectionnée y entre, décalée de la précédente.
  Un clic sur son fond la sélectionne ; ses poignées la redimensionnent (elle se déplace comme un
  bloc, par la poignée de la marge).
- *Zone de texte* : forme sans fond ni bordure, hauteur automatique, curseur dans son texte.
  « Texte de la forme » ajoute (ou rejoint) le texte d'une forme, centré ; le texte tourne avec
  la forme, s'édite au clavier et à la souris comme le reste du document.
- *Alignement horizontal* (onglet Disposition ou panneau Propriétés, « Aucun » en plus de
  gauche, centré, droite) : choisir un alignement replace la forme — dans la colonne (la
  position revient à zéro), dans la zone de dessin, ou dans la zone de contenu de la page
  (position absolue) ; la déplacer ou la redimensionner à la souris, ou saisir sa position X,
  la passe en « aucun » à l'endroit choisi.
- *Grille* (onglet Affichage) : lignes au premier plan sur chaque page, couleur des limites des
  blocs à 50 % d'opacité, pas réglable de 0,1 à 20 cm (espacé à l'écran si les lignes seraient
  trop serrées) ; formes, images et zones de dessin s'y aimantent quand on les déplace ou les
  redimensionne. Affichage et pas sont gardés dans les réglages.

### 1.6 Panneau latéral

**Plan** — arbre des titres (par niveau) et des légendes de figures, de tableaux et de listings,
rangées sous le titre qui les précède ; filtres par type ; branches repliables ; l'entrée où se
trouve le curseur est mise en évidence ; un clic amène le curseur au début de l'élément et fait
défiler la vue. Pour un long document, c'est le moyen de s'y déplacer rapidement.

**Propriétés** — propriétés du nœud, des nœuds ou du groupe sélectionnés, par catégorie
(fonctionnelles, structurelles, visuelles). Pour une image ou une forme : repère de la position
(Auto, nœud, parent, page), rotation, centre de rotation et son repère (forme, parent, page) ;
changer de repère convertit les coordonnées, le nœud ne bouge pas. Chaque valeur indique sa source : explicite,
héritée d'un groupe (nommé), héritée du parent, du document, ou valeur par défaut. Une valeur
différente entre nœuds sélectionnés s'affiche « mixte ». Modifier une valeur passe par une
commande du moteur (annulable) ; une valeur explicite peut être effacée (retour à l'héritage).
Pour toute propriété de couleur (texte, fond, bordure, remplissage…), la pastille ouvre la
fenêtre des couleurs ; la valeur se tape aussi en `#rrggbb[aa]` ou par son nom.

**Fenêtre des couleurs** (texte, surlignage, cellules, pages, remplissage et contour des formes,
propriétés du panneau) : sélecteur RGBA (teinte, saturation, valeur, opacité, code) et liste
filtrable de toutes les couleurs nommées de sdl3 (`sdl3::Color::Names` / `FromName`) ;
« Aucun » : transparent (sauf la couleur du texte).

**Historique** — liste ordonnée des commandes (transactions signalées) ; cliquer une entrée
annule ou rétablit jusqu'à elle ; synchronisée avec le ruban et les raccourcis.

**Arborescence** — nœuds du document en hiérarchie, ou regroupés par groupe ; type, identifiant
et icône de chaque nœud ; développer / replier ; la sélection est partagée avec la zone
centrale et les propriétés ; double-clic : aller à la page du nœud. L'arbre est reconstruit
depuis le moteur à chaque notification de modification : il ne duplique pas l'état.

### 1.7 Interactions

- **Texte** : clic = curseur d'insertion (clignotant) ; glisser ou Maj + clic = plage en
  surbrillance (sur plusieurs paragraphes) ; double-clic = mot ; triple-clic = paragraphe.
  Glisser une plage la déplace, Ctrl en la relâchant la copie (repère orange au point de dépose).
- **Blocs** : survoler la marge gauche fait apparaître une poignée devant le paragraphe, le
  tableau, l'image, la forme ou la zone de dessin du corps ; un clic la sélectionne, la glisser
  déplace le bloc (trait d'insertion avant ou après le bloc visé, y compris dans une cellule de
  tableau ; jamais dans le texte d'une forme ni à l'intérieur d'une zone de dessin).
- **Formes et images** : clic = sélection, cadre à 8 poignées (coins et milieux des côtés) ;
  glisser une poignée redimensionne (dans les axes de la forme, même tournée ; Ctrl, ou la
  propriété `KeepAspectRatio` cochée : proportions gardées — coin opposé fixe, ou l'autre
  dimension centrée pour un bord), glisser l'intérieur ou le bord déplace (Maj : sur un seul
  axe, celui du plus grand déplacement). Grille affichée : le coin haut gauche déplacé, ou la
  poignée glissée (forme non tournée), est aimanté à la grille. Double-clic sur le cadre : mode rotation, cadre rouge et
  point central (centre de rotation) ; glisser le cadre tourne la forme (Maj : pas de 15°),
  glisser le point déplace le centre. Un nouveau double-clic revient au redimensionnement.
- **Cotes pendant le glisser** : position (coin supérieur gauche, repère de la page) d'une
  forme ou image déplacée, largeur × hauteur pendant un redimensionnement, angle pendant une
  rotation, position du centre de rotation déplacé — comme pour les bords des tableaux.
- **Rembourrage** (marge intérieure, *padding*) : au survol du bord intérieur d'un paragraphe
  (encadré, code…), d'une cellule ou d'une table des matières dont le rembourrage est non nul, la
  zone de contenu et le bord visé s'affichent ; glisser le bord règle le rembourrage (cote affichée, au demi-millimètre,
  Maj : au dixième). Si le nœud survolé fait partie de la sélection, tous les nœuds sélectionnés
  de son type reçoivent la valeur (une entrée d'historique).
- **Liens** : Ctrl + clic sur un lien, un renvoi ou une entrée de table des matières interactive
  mène à la cible (titre, légende, nœud) ; une adresse externe s'ouvre dans le navigateur
  (wrapper `sdl3`). Au survol avec Ctrl, le pointeur devient une main.
- **Tableaux** :
  - au survol, au-dessus du tableau : bouton « + » sur chaque bord vertical (insère une
    colonne à ce rang), poignée de sélection d'une colonne ailleurs ; à gauche : « + » sur
    chaque bord horizontal (insère une ligne), poignée de sélection d'une ligne ; coin
    supérieur gauche : tableau entier. Glisser une poignée étend la sélection ;
  - sélection multiple : Maj+clic (poignée de ligne ou de colonne, ou cellule) sélectionne la
    plage contiguë depuis l'ancre (dernière ligne, colonne ou cellule cliquée) ; Ctrl+clic ajoute
    ou retire une ligne, une colonne ou une cellule (sélection non contiguë) ; Ctrl+Maj+clic
    ajoute la plage. Une cellule fusionnée se sélectionne toujours entière ;
  - cellules fusionnées : la cellule du coin haut gauche couvre le rectangle (contenu des autres
    réuni dans l'ordre de lecture) ; ses lignes ne sont jamais coupées entre deux pages ;
  - glisser un bord intérieur (ou le bord droit / bas) règle la largeur des colonnes voisines
    ou la hauteur de la ligne, cote affichée, au demi-millimètre (Maj : au dixième) ;
  - glisser le texte d'une cellule à une autre sélectionne une plage rectangulaire de cellules ;
  - Tab passe à la cellule suivante (une ligne est ajoutée après la dernière), Maj+Tab revient.
- **Règles** : les repères triangulaires règlent les marges de la page (celle du curseur), les
  barres les bords des colonnes et des lignes du tableau courant, les petits repères verts de la
  règle verticale l'espace avant et l'espace après du paragraphe du curseur ; cote affichée pendant le
  glisser, au demi-millimètre (Maj : au dixième de millimètre).
- **Nœuds** : Ctrl + clic sélectionne le nœud sous le curseur (Maj : ajout) ; glisser dans la
  marge = sélection par zone ; Échap pendant l'édition sélectionne le paragraphe du curseur.
- Toute modification passe par une commande du moteur ; le moteur notifie, l'interface
  redemande une mise en page asynchrone et rafraîchit ses vues.
- **Onglet actif contextuel** : décidé par une fonction pure et testable (`SuggestTab`) à
  partir du contexte de sélection fourni par le moteur. Le changement automatique n'a lieu
  qu'à une *nouvelle* sélection faite dans le document ou l'arborescence, jamais pendant que
  l'utilisateur interagit avec le ruban ; il peut être désactivé (Affichage → Ruban
  contextuel).

### 1.8 Raccourcis et accessibilité

Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Maj+S, Ctrl+Z, Ctrl+Y (ou Ctrl+Maj+Z), Ctrl+B, Ctrl+I, Ctrl+U,
Ctrl+C / Ctrl+X / Ctrl+V (texte enrichi ou nœuds ; le texte brut passe aussi par le
presse-papiers du système), Ctrl+K (lien), Ctrl+molette (zoom).

- En édition de texte : flèches (Ctrl : mot par mot), Début / Fin (Ctrl : début / fin du récit),
  Maj étend la plage, Retour arrière / Suppr (Ctrl : un mot), Entrée (saut de ligne dans le
  paragraphe), Maj+Entrée (nouveau paragraphe du même style en dessous, la suite du texte y
  passe), Ctrl+Entrée (saut de page, puis paragraphe du même style sur la nouvelle page — corps
  du document seulement ; refusé dans un tableau, une forme, un en-tête), Tab
  (cellule suivante dans un tableau, niveau de liste suivant dans une liste, sinon tabulation),
  Ctrl+A (tout le récit), Échap (quitte le texte).
- En sélection de nœuds : Ctrl+A (tout sélectionner), Suppr (supprimer), Ctrl+D (dupliquer),
  Ctrl+G (grouper), Ctrl+Maj+G (dissocier), Entrée (insérer un paragraphe), Échap
  (désélectionner). Toutes les
commandes du ruban ont une infobulle ; le thème sombre du projet est respecté.

### 1.9 Limites de la première version

- **Saisie** : pas d'affichage de la pré-édition d'une méthode de saisie (le texte composé
  arrive validé) ; pas de correcteur orthographique. Le curseur est dessiné d'après la
  dernière mise en page reçue : juste après une frappe, il peut rester une image en retard.
- **Glisser-déposer** : interne au document (pas de dépôt depuis une autre application).
  Glisser une forme hors de sa page la décale dans le repère de sa page de départ.
- **Images** : fichiers décodés par SDL3_image (wrapper `sdl3`).
- **Polices à l'écran** :
  - l'inventaire des polices installées (dossiers du système) se fait en arrière-plan au
    démarrage ; tant qu'il n'est pas fini, seules les polices standard sont proposées ;
  - une police installée est dessinée avec son propre fichier, celui qui a servi à la mesure ;
  - une police standard est dessinée avec une police installée aux mêmes métriques (Nimbus
    Sans / Roman / Mono PS, sinon Liberation, Arimo / Tinos / Cousine, Arial / Times New Roman /
    Courier New) ; sans aucune d'elles, avec la police du canevas, réduite si elle est plus
    large que la mesure ;
  - le canevas accepte au plus 250 fichiers de police supplémentaires par session.

  Le PDF exporté utilise les vraies polices standard ou incorpore la police installée.
- **Rotation** : les formes et images seulement (voir les limites du moteur).
- **Langues** : les messages d'erreur du moteur (`engine.document`) restent en français, comme le
  contenu des modèles intégrés et de la démonstration ; les noms des propriétés du panneau sont les
  identifiants du moteur (anglais). Les fichiers de traduction sont cherchés dans
  `apps/scribe/i18n` depuis le dossier courant (lancement depuis la racine du dépôt).
- **Documents longs** : pas de colonnes multiples (la démonstration utilise un tableau sans
  bordure), pas de recherche / remplacement ; le signe moins
  typographique (U+2212) et les caractères hors WinAnsi deviennent « ? » dans le PDF.
- **Grands tableaux** : la mise en page tourne en arrière-plan ; pour 80 000 cellules, une frappe
  dans une cellule demande de l'ordre de 0,2 s (en version optimisée) avant que l'affichage ne
  suive ; l'interface reste fluide pendant ce temps.
- **Fermeture** : un document modifié demande une seconde fermeture dans les 5 secondes (pas
  encore de boîte de dialogue « Enregistrer ? ») ; ses modifications restent dans le brouillon.
- **Brouillon unique** : modifier un autre document remplace le brouillon précédent (par exemple
  celui d'une session précédente qu'on n'a pas repris) ; ouvrir un document ne demande toujours pas
  s'il faut enregistrer le document modifié, mais celui-ci reste récupérable par « Reprendre »
  tant qu'aucun autre document n'est modifié.

## 2. Architecture

| Fichier | Rôle |
|---|---|
| `include/scribe/editor.hpp` | `Editor` : contrôleur sans fenêtre (document, historique, sélection, curseur et plage de texte, saisie, presse-papiers, glisser-déposer, formes, polices installées, styles de paragraphe, listes, légendes, liens, renvois, tables des matières, sélection et opérations des tableaux — `editor_structure.cpp`, `editor_tables.cpp` —, mise en page et rendu de fond par `jobs::AsyncValue`, ouverture / enregistrement / exports de fond, actions du ruban). Testé sans interface. |
| `include/scribe/ribbon.hpp` | onglets et `SuggestTab` (fonction pure) |
| `include/scribe/table_tools.hpp` | géométrie des tableaux à l'écran (bords des colonnes et des lignes d'un fragment de page) et élément sous le pointeur (bord à glisser, « + », poignées de sélection) |
| `include/scribe/rulers.hpp` | graduations (cm), aimantation, cotes, dessin d'une règle et de ses repères |
| `include/scribe/navigator.hpp` | lignes du panneau Plan : filtres, profondeur, branches repliées |
| `include/scribe/display.hpp` | disposition des pages à l'écran (zoom, défilement, écran ↔ page) et dessin des `DisplayList` sur le canevas SDL3 (textures des images et polices du canevas en cache, rotation par `Canvas::PushRotation`, polygones par `Canvas::FillPolygon`) ; géométrie du cadre de transformation (poignées, test, redimensionnement dans les axes de la forme — proportions gardées —, rotation glissée) ; grille (aimantation, dessin) ; catalogue et aperçus des formes |
| `icons/*.svg` | icônes du ruban absentes de Material (tables de figures, légendes, lignes et colonnes, zone de dessin…), blanches, teintées à l'affichage |
| `include/scribe/demo.hpp` | document de démonstration (modèle « Démo », `--template=demo`) construit avec les actions de l'éditeur |
| `include/scribe/templates.hpp` | modèles intégrés (constructeurs) et modèles personnels (`TemplateStore` : créer, renommer, supprimer) |
| `include/scribe/settings.hpp` | réglages persistants (langue, thème, récents, grille) |
| `include/scribe/draft.hpp` | brouillon du dernier document modifié (`DraftStore` : enregistrement atomique, lecture, suppression) ; reprise par `Editor::OpenDraft` |
| `include/scribe/i18n.hpp` | langues : chargement, `Tr` / `TrF` / `TrArg`, langue du système |
| `include/scribe/theme.hpp` | thèmes : `ui::Theme` et palette de l'application, feuille de style |
| `src/app.cpp` | interface construite par la fabrique de `graphics.ui` : ruban (`TabWidget` ; pages `Scroll` de `ToolGroup` ; commandes `ToolButton` dont l'état — actif, enfoncé, icône, info-bulle, couleur — suit le document par des liaisons mises à jour à chaque image ; listes modifiables restées immédiates dans des feuilles `Custom`), espace ancrable `ui::DockLayout` (document au centre ; Propriétés, Historique, Arborescence à droite, déplaçables — contenus immédiats), barre d'état, calque des fenêtres surgissantes (couleurs, lien, formes, dialogues) placé en dernier, sur les fenêtres ouvertes ; accueil (feuille immédiate), raccourcis, boîtes de dialogue |

Mise en page : chaque modification relance une mise en page de fond sur un instantané (cache
incrémental partagé, protégé par un verrou) ; le résultat est affiché dès qu'il arrive et les
pages du document sont synchronisées sur le fil de l'interface. Un thread qui attend la pré-mesure
parallèle exécute des tâches en file : s'il y prend une autre mise en page du même éditeur alors
qu'il détient déjà le verrou, celle-ci utilise un cache temporaire au lieu de s'interbloquer. La
destruction de l'éditeur attend la fin de ses tâches de fond.

```sh
cpp-maker run scribe                  # accueil
cpp-maker run scribe -- rapport.docdb
cpp-maker run scribe -- --template=demo --page=4        # nouveau document depuis le modèle « Démo », page 4
cpp-maker run scribe -- --template="Mon modèle"         # modèle personnel (par son nom)
cpp-maker run scribe -- --lang=ja --theme=light          # langue et thème de la session (non enregistrés)
scribe --template=demo --export=demo.pdf   # export sans fenêtre (.pdf, .html, .csv : premier tableau)
cpp-maker test scribe                 # 30 tests (contrôleur, affichage, outils, langues, modèles, brouillon, démonstration)
```
