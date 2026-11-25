# apps-lib

Bibliothèque C++23 pour applications et jeux 2D/3D, gérée avec [`cpp-maker`](https://github.com/ClemtoClem/tui/tree/main/apps/cpp-maker).


```sh
cpp-maker build release                 # tout le projet
cpp-maker run release voxelia           # le jeu
cpp-maker run release profiler          # le visualiseur de captures
cpp-maker test                          # 10 suites de tests (dont les 32 tests du wrapper SDL3)
tools/compile_shaders.sh                # régénère le SPIR-V embarqué (glslc, uniquement si un .vert/.frag change)
```

## Couches et modules

Un module ne dépend que de sa couche ou d'une couche inférieure :
`foundation → engine → platform → graphics → ui → applications`.

| Couche | Module | Rôle |
|---|---|---|
| foundation | `core` | `Option`/`Result` (sur `std::optional`/`std::expected`), `Ref`, `Wrapper` RAII, `String`/`StringView`, UTF-8/16/32, hachage 64 bits, **clés de Morton** 2D/3D, **`RobinMap`** (Robin Hood, suppression par décalage arrière), journal, horloges |
| foundation | `math` | Vecteurs, matrices (profondeur [0,1]), quaternions, AABB, frustum, rayons, bruit simplex/fBm/ridged, RNG xoshiro |
| foundation | `jobs` | Pool `std::jthread` à 3 priorités, `ParallelFor` à distribution dynamique, `WaitGroup` (`std::atomic::wait`), canaux MPSC |
| foundation | `diagnostics` | Assertions, **traceur** par fil (zones, images, compteurs) et format de capture `.vcap` |
| foundation | `dataio` | JSON tolérant (commentaires, virgules finales, fusion profonde), flux binaires varint/zigzag, CRC-32, RLE |
| foundation | `ecs` | ECS à archétypes : colonnes SoA alignées, cache de requêtes incrémental, `ParEach`, création en masse, tampons de commandes, ressources, ordonnanceur |
| foundation | `events` | `Signal` à priorités, `EventBus` typé à double tampon, événements nommés des mods |
| foundation | `sql` | Base embarquée : tables en colonnes, index de hachage et **B+arbre**, SQL (requêtes préparées, planificateur), instantané CRC, **`RegionStore`** partitionné par région de Morton, découpage d'une boîte en intervalles Z |
| foundation | `script` | Langage des mods : lexeur, analyseur, interpréteur à symboles interneés, liaisons hôte |
| engine | `voxel` | Monde voxel : `RobinMap` des chunks actifs, streaming asynchrone sur `sql`, grille spatiale d'entités par chunk, mailleur glouton avec AO, terrain, physique, mods |
| platform | `sdl3` | Wrapper C++ de SDL3 (inclut le rendu 3D sur SDL_GPU, dépend de `core` et `math`) |
| graphics | `gfx` | Rendu SDL_GPU des chunks (SPIR-V embarqué), canvas 2D par lots sur `sdl3::Renderer` (atlas de glyphes `sdl3::Font`), composition d'images |
| ui | `ui` | Interface en mode immédiat sur le canvas |

Modules supprimés : `graphics.vulkan`, `graphics.shader` (shaderc), `graphics.render2d`,
`graphics.render3d`, `foundation.image` (stb/nanosvg), `foundation.physics` (intégré à `voxel`),
l'ancien `graphics.ui` et l'application `example-ui`. Une archive de l'état précédent est dans
`.backups/apps-lib_before_rewrite_2026-11-10.tar.xz`.

## Performances mesurées (release, 8 cœurs)

| Mesure | Résultat |
|---|---|
| ECS : 200 000 entités `Position += Velocity` | 0,38 ms (`Each`), **0,16 ms** (`ParEach`) ; création en masse 4 ms |
| Grille spatiale : 100 000 entités déplacées, 20 700 franchissements de chunk | **1,6 ms** (`UpdateEntityGrid`) |
| Voisinage (rayon 24 m, 100 000 entités) | 0,5 ms |
| SQL : 50 000 `INSERT` préparés / 1 000 `SELECT` indexés | 22 ms / 0,9 ms |
| Voxelia, distance 10, 2 219 chunks, 100 000 créatures simulées | **151 images/s** (vsync coupée) |

## Voxelia

```sh
voxelia                                   # menu principal
voxelia --world essai --new --seed 7      # nouveau monde, directement en jeu
voxelia --exec "/spawn core:sheep 100000" # commande après chargement
voxelia --frames 300 --screenshot shot.png
```

| Touche | Action |
|---|---|
| ZQSD/WASD, Espace, Maj | se déplacer, sauter / monter, descendre (vol) ou courir |
| Souris, clic gauche / droit, E | regarder, casser / poser, utiliser (`block_use`) |
| 1–9, molette | barre d'outils |
| F | vol | 
| T, Entrée, `/` | console (`/help`) |
| F2, F3, F5, F11, Échap | capture, débogage, sauvegarde, plein écran, pause |

Console : `/help`, `/tp x y z`, `/time t`, `/save`, `/stats`, `/spawn type n`, `/near rayon type`,
`/chunks rayon` (recherche par index de Morton dans la base, sans charger), `/events n`,
`/sql REQUÊTE` (base du monde), les commandes des mods, ou du code Script.

Voir [docs/guide.md](docs/guide.md) (guide complet : modules, jeu, Studio, mods) et
[docs/voxelia.md](docs/voxelia.md) (flux de streaming, grille d'entités, base de données).

## Profiler

```sh
profiler record --duration 10 -- ./build/host/release/bin/voxelia --world essai
profiler report                     # capture la plus récente : images, zones, fils, compteurs, diagnostic
profiler compare avant.vcap apres.vcap
profiler export --chrome trace.json # pour ui.perfetto.dev
profiler                            # visualiseur : molette = zoom, clic droit glissé = défilement, Maj + glisser = plage
```

Tout programme instrumenté avec `DIAG_ZONE` / `DIAG_FRAME` / `DIAG_COUNTER` est capturable :
le profiler positionne `DIAG_TRACE_FILE` (et `DIAG_TRACE_DURATION`) dans l'environnement du programme.
Voir [docs/profiler.md](docs/profiler.md).

## Prérequis

- GCC 15 ou Clang 21 (C++23), `cpp-maker`
- SDL3 ≥ 3.4 avec SDL3_image, SDL3_ttf, SDL3_mixer, SDL3_net (dépendances du wrapper)
- Un pilote Vulkan (SDL_GPU, shaders SPIR-V embarqués)
- `glslc` uniquement pour régénérer les shaders

Note : lancé depuis le terminal de VSCode installé en snap, un programme SDL peut charger
la glibc du snap et planter au démarrage (`__libc_pthread_init`). Lancez-le depuis un
terminal système, ou avec un environnement nettoyé (`env -i HOME=$HOME DISPLAY=$DISPLAY
WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR ./build/host/release/bin/voxelia`).
