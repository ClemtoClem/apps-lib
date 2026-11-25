# Exemples et cadre d'application

Migration des exemples de SDL3_Wrapper vers apps-lib : chaque exemple repose sur
`graphics.app` (cycle de vie) et `graphics.ui` (interfaces), accepte `--frames=N` et
`--screenshot=F.png` pour les essais automatiques et se lance depuis la racine du projet
(polices, feuilles de style, traductions dans `assets/`).

| Exemple | Origine (SDL3_Wrapper) | Contenu |
|---|---|---|
| `example-platform` | `platform_app` | Simulation à pas fixe réglable (10 à 240 Hz), rendu interpolé (I pour comparer), barre de titre avec boutons, mesures en direct |
| `example-renderer` | `renderer` | API brute de `sdl3::Renderer` dans une texture cible (rectangles, lignes, cercles, géométrie colorée par sommet), clavier, souris, molette, fichiers déposés, journal lisible |
| `example-ui-minimal` | `ui_minimal` | La plus petite interface retenue : carte centrée (position absolue), curseur lié, bouton, bascule |
| `example-ui-aero` | `ui_aero_basics`, `ui_aero_lists`, `ui_aero_windows` | Thème verre fumé : contrôles et sélecteur de couleur, listes / arbre / tableau / graphique, bureau avec barre de menus, menu contextuel, calendrier, fenêtres internes, modale (`--tab=N`) |
| `example-ui` | `ui_showcase` | Catalogue complet du module ui, thèmes et langues (voir plus bas) |
| `example-ui-plot` | `ui_plot_demo` | Galerie `ui::PlotView` : courbes, aires, barres d'erreur, barres et tiges, nuage sur deux axes, chandeliers OHLC, direct, carte de chaleur |
| `example-ui-node-graph` | `ui_node_graph_demo` | Calculatrice visuelle sur `ui::NodeGraph` : évaluation en ordre topologique, menus contextuels, copier-coller, sauvegarde JSON |
| `example-audio-generator` | `audio_signal_generator` | Quatre oscillateurs PolyBLEP, choix du périphérique, oscilloscope, spectre, vumètres |
| `example-audio-spectrum` | `audio_spectrum_analyzer` | Capture (ou signal de test), FFT et fenêtre réglables, maintien des crêtes, fréquence dominante et note |
| `example-audio-patchbay` | `audio_patchbay` | Graphe audio : oscillateur, bruit coloré, clavier polyphonique, lecteur OGG/MP3 (SDL3_mixer), entrée et sortie au choix du périphérique, filtre, gain, mélangeur, retard, écrêtage, réverbération, limiteur, visualisations |
| `example-net-chat` | `adhoc_chat_demo` | Messagerie de réseau local : découverte UDP, messages et fichiers TCP avec progression (`--peer`, `--say`) |
| `example-gpu-basics` | `vk_triangle`, `vk_cube` | API SDL_GPU brute par le wrapper : shaders SPIR-V, pipelines plein / filaire, tampons, texture avec mipmaps, trois échantillonneurs, profondeur (`--cube`) |
| `example-render3d` | `render3d_cube`, `render3d_showcase`, `render3d_shadows_ibl_showcase` | Trois scènes `sdl3` : cube ; géométries, PBR et lumières ; ombres, ciel, verre, instances, particules — réglages en direct (`--scene=N`) |
| `example-ui-viewport3d` | `ui_viewport3d_demo` | Même scène dans une vue perspective large et une vue de dessus étroite, sélection à la souris, déplacement au sol, propriétés |
| `example-gpu-instancing` | `gfx_scene` | Jusqu'à 100 000 objets statiques et 20 000 dynamiques : élimination par cellules, un appel instancié par matériau, mesures CPU (`--statics=N`) |
| `example-ui-shader-effects` | `ui_shader_effects_demo` | Six shaders de fragments (`sdl3::GpuRenderState`) appliqués à des cartes de widgets restés interactifs |

Application `sandbox` : modeleur à la SketchUp (voir `apps/sandbox/README.md`) ; son mode Jeu
(physique, scripts, 2D, sons) reste à migrer. La messagerie gère la cellule Wi-Fi ad hoc
(`net::wifi`, colonne Wi-Fi de `example-net-chat`). Ne sont pas repris, des exemples 3D d'origine : l'IBL chargée depuis un `.hdr`
(remplacée par un environnement analytique), les ombres des lumières ponctuelles et des
spots, les maillages animés par squelette, LOD et sprites ; de `gfx_scene`, le culling sur
GPU (compute + appels indirects), l'occlusion Hi-Z et l'enregistrement multi-fil.

Modules ajoutés pour ces exemples : `engine.audio` (traitement du signal sans entrée-sortie :
oscillateurs PolyBLEP, FFT, fenêtres, niveaux, biquad, retard, tampon circulaire) et
`platform.net` (messagerie sur SDL_net : trames, balises, transferts avec CRC-32). Le rendu
3D vit dans `platform.sdl3` (intégré au wrapper). Tout l'affichage et l'audio
passent par le wrapper sdl3 (`sdl3::Renderer`, `sdl3::GpuDevice`, `sdl3::AudioStream`,
`sdl3::AudioDecoder`) ; les shaders sont compilés en SPIR-V et embarqués par
`tools/compile_shaders.sh`.

## `graphics.app`

```cpp
class Demo final : public app::Application {
	bool OnInit() override;                                    // après la fenêtre et le canvas
	void OnFixedUpdate(double step) override;                  // 0..n fois par image, pas constant
	void OnUpdate(double frame) override;                      // une fois par image (entrées lues)
	void OnOffscreen() override;                               // textures cibles, passes GPU
	void OnRender(const sdl3::FRect &area, double alpha) override; // canvas ouvert, zone sous la barre de titre
	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &right) override;
	void OnTitleButton(std::string_view id) override;
};
```

`AppConfig` : titre, taille, `customFrame` (barre de titre dessinée), icône, thème, audio,
`gpu` (device SDL_GPU partagé avec le renderer 2D : textures 3D affichées sans copie,
shaders 2D personnalisés), horloge (`FixedStepClock::Config`). `Application` fournit
`Events()`, `Window()`, `Renderer()`, `Gpu()`, `Canvas()`, `Ui()` (interface immédiate),
`Fps()`, `SetFixedStep()`, `SetTitle()`, `SaveScreenshot()`.

Canvas : `SetTarget(texture)` dessine dans une texture cible (mêmes coordonnées que la
fenêtre), `SetRenderState(state)` applique un shader de fragments aux dessins suivants.

## Thèmes

Un thème = une feuille CSS appliquée par-dessus `assets/ui.css` (dossier `assets/themes/` :
`midnight.css`, `light.css`, `contrast.css`) + un `ui::Theme`. Les couleurs que le CSS ne
décrit pas (pistes, sélections, menus et popups, en-têtes de tableau, infobulles) viennent
du `ui::Theme` donné à l'interface retenue :

```cpp
const ui::Theme theme = LightTheme();
Ui().Style() = theme;                 // interface immédiate et barre de titre
ui.SetTheme(theme);                   // interface retenue
ui.ClearSheets();
ui.Sheet(ui::StyleSheet::Load("assets/ui.css").Value());
ui.Sheet(ui::StyleSheet::Load("assets/themes/light.css").Value());
```

## Traductions

Un fichier JSON par langue associe chaque identifiant de widget à une phrase
(`assets/i18n/example-ui/en.json`) :

```json
{ "locale": "en", "language": "English",
  "strings": { "title": "Complete catalogue", "line.placeholder": "Single-line input…",
               "open-modal.tooltip": "Opens a modal dialog", "tabs.0": "Controls", "menu.0.3": "Quit" } }
```

```cpp
ui::I18n().LoadFile("assets/i18n/example-ui/en.json");
ui.SetTranslator(&ui::I18n());        // une fois : réappliqué à chaque changement
```

Clés : `id` (texte, titre), `id.placeholder`, `id.tooltip`, `id.N` (onglets, options,
en-têtes de tableau, titres de menus, items d'un menu contextuel), `id.N.M` (items de
menus). Une clé absente rétablit le texte des sources : le fichier de la langue des sources
peut rester vide. Les textes dessinés par le code passent par `ui::Tr` ou
`Translator::Find` ; les sélecteurs de couleur et de date lisent `ui.color.*`,
`ui.month.N`, `ui.weekday.N`, `ui.calendar.*`.

`example-ui` : L change la langue, T le thème (aussi par la barre de titre et le menu
Affichage) ; `--tab=N --lang=en --theme=light` au lancement.

## Améliorations de `graphics.ui` faites pendant la migration

- Mise en page retenue : tailles intrinsèques des conteneurs (plus de rangées de hauteur
  nulle), `position: absolute` (ancre, `left/top/right/bottom`, étirement), `flex-shrink`
  avec planchers (taille explicite, libellé jamais coupé), grilles, sections dépliantes,
  fenêtres flottantes et zones défilantes mesurées, titre des panneaux, orientation des
  séparateurs, splitter centré par défaut.
- Widgets : menu contextuel (`.ContextMenu(items)`), sélecteurs immédiats `ColorPicker` et
  `DatePicker`, feuilles `Custom(Context&)` placées dans leur boîte, liste déroulante des
  `Choice` (premier plan, reste ouverte jusqu'au choix), infobulles multi-lignes,
  visualisations `Scope` / `SpectrumView` / `LevelMeter`, graphique interactif `PlotView`,
  éditeur de graphes de nœuds `NodeGraph` / `NodeGraphEditor`.
- `Context::MenuBar(rect, menus)` : barre de menus immédiate (raccourcis affichés, coches,
  entrées désactivées, bascule au survol), utilisée par le sandbox.
- `Context::MouseOver(rect)` : survol d'une vue personnalisée hors liste déroulante ouverte
  (graphiques, scènes 3D).
- Rendu : le texte et le contenu d'un widget sont rognés à sa boîte, les enfants à celle du
  parent (les fenêtres flottantes y échappent) ; `TextCentered` ne déborde plus.
- Thèmes et traductions comme ci-dessus ; styles de base des champs, panneaux, bascules et
  fenêtres internes dans `assets/ui.css`.
- Tests : `tests/test_layout.cpp` met réellement en page (renderer logiciel) et simule la
  souris (sélecteurs, menu contextuel, traductions).

## Ajouts aux modules existants (sandbox, messagerie)

Plutôt que de nouveaux modules, les fonctions nécessaires ont été ajoutées aux classes en place :

- `sdl3::Process::Create(ProcessOptions)` et `Process::Run` : environnement modifié,
  redirections, sortie complète (utilisés par `net::wifi` pour lancer `nmcli` en `LC_ALL=C`).
- `sdl3::Renderer::SetGpuRenderState`, `sdl3::BlendMode::BLEND_PREMULTIPLIED`.
- `math` : `polygon.hpp` (aire signée, point dans un polygone, triangulation avec trous),
  `FMatrix4::Rotation`, `Inverse`, `ProjectPoint`, `TransformDirection`, distances, `Min`/`Max`.
- `sdl3` : `Renderer3D::Replace` / `Release` (géométrie éditée en direct), `DashedLine`,
  caméra orthographique (`Camera3D::orthographic`).
- `sdl3::Canvas::SetTarget` / `SetRenderState`.
