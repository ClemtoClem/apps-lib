# Architecture

## Principes

- **Une seule dépendance externe** : SDL3 et ses bibliothèques compagnes, via le wrapper
  `libs/platform/sdl3`. Le reste n'utilise que la bibliothèque standard C++23.
- **Aucune exception** (`-fno-exceptions`) : erreurs par `Result<T, E>` (sur `std::expected`)
  ou `std::optional`.
- **Pas d'état partagé caché** : les tâches parallèles lisent des copies (volume rembourré
  pour le maillage, `EntityRecord` pour les entités) et rendent leurs résultats par des
  canaux vidés sur le thread principal.
- **Noms en PascalCase** pour les types, énumérations, méthodes et fonctions ; espaces de
  noms en minuscules. Exceptions imposées par le langage : `begin`/`end`, spécialisations
  `std::hash`/`std::formatter`.

## Compatibilité avec le wrapper SDL3

Le wrapper attend le vocabulaire de `SDL3_Wrapper/libs/foundation/core` et `math`. Le
nouveau `core` fournit la même interface, réimplémentée sur la bibliothèque standard :

| Type attendu | Implémentation |
|---|---|
| `Option<T>`, `Some`, `NONE` | `std::optional<T>` |
| `Result<T, E>`, `Ok`, `Err` | `std::expected<T, E>` |
| `String`, `StringView` | dérivés de `std::string` / `std::string_view` |
| `Wrapper<T, Deleter>`, `DeviceWrapper`, `Borrowed`, `Ref`, `RefMut` | RAII sur pointeur brut, déplacements par `std::exchange` |
| `unicode::ToUtf16/32`, `FromUtf16/32` | conversions UTF maison |
| `math::FVector2`, `Corners`, `Sides` | `foundation.math` |

## ECS (`foundation.ecs`)

- **Archétype** = ensemble exact de composants ; une colonne contiguë alignée sur 64 octets
  par composant ; relogement par `memcpy` pour les types trivialement copiables.
- **Graphe d'arêtes** : `Add<T>` / `Remove<T>` retrouvent l'archétype cible en O(1) après
  la première transition.
- **Requêtes** : `MakeQuery<A, const B>().Without<C>()` garde la liste des archétypes
  correspondants et n'examine que les nouveaux à chaque appel.
- **`ParEach`** : lots de 8 192 lignes répartis par `jobs::ParallelFor` (le thread appelant
  participe, attente par compteur atomique).
- **`SpawnBatch(n, composants…)`** : une réservation puis une boucle de construction par colonne.
- **Changements structurels** pendant une itération : `CommandBuffer` (un par thread) puis
  `World::Apply`, ou `World::Defer` (verrou).

## Base de données (`foundation.sql`)

- `Table` : une colonne typée (`int64`, `double`, `string`, `Blob`) + masque de NULL par
  colonne, identifiants de lignes stables (emplacements libres réutilisés).
- Index : clé primaire en `RobinMap` (entier ou texte), index secondaires entiers en
  B+arbre (nœuds de 64 entrées, feuilles chaînées, doublons via (clé, ligne)).
- SQL : `CREATE TABLE/INDEX`, `DROP`, `INSERT [OR REPLACE]` multi-lignes, `SELECT … WHERE …
  ORDER BY … LIMIT … OFFSET`, `COUNT(*)`, `UPDATE`, `DELETE` ; expressions avec `BETWEEN`,
  `IN`, `LIKE`, `IS NULL`, paramètres `?`. Requêtes préparées.
- Planificateur : égalité sur la clé primaire → hachage ; égalité/plage sur colonne indexée
  → B+arbre ; `ORDER BY` sur colonne indexée → lecture ordonnée avec arrêt anticipé.
  Le plan choisi est renvoyé dans `ResultSet::plan`.
- Concurrence : `std::shared_mutex` (lectures partagées, écritures exclusives).
- `RegionStore` : partitionnement par région de Morton, journal en ajout seul par région,
  index mémoire reconstruit à l'ouverture, compaction triée par clé (chunks voisins
  contigus sur disque), API asynchrone sur le pool.
- `MortonBoxRanges` : découpe une boîte de coordonnées en intervalles de clés Z contigus
  (parcours d'octree implicite), pour interroger le B+arbre par plages.

## Rendu (`graphics.gfx`)

- `app::Context` crée la fenêtre, un `sdl3::GpuDevice` (SPIR-V) et un `sdl3::Renderer` **sur
  ce même device**. La scène 3D est rendue dans une texture couleur + profondeur, affichée
  sans copie par le renderer (`Texture::FromGpuTexture`), puis l'interface est dessinée
  par-dessus. Les deux flux sont soumis dans l'ordre sur la même file.
- `VoxelRenderer` : sommets de 12 octets, index de quads partagé, frustum culling, tri
  avant→arrière (opaques) / arrière→avant (translucides), texture-tableau 16×16 avec mipmaps,
  boîtes instanciées pour les entités.
- Shaders GLSL dans `libs/graphics/gfx/shaders/`, compilés hors ligne par
  `tools/compile_shaders.sh` en `src/shaders_spirv.inc` : aucune compilation à l'exécution.
- `Canvas` : atlas de glyphes par taille (SDL_ttf), texel blanc commun à tous les atlas,
  donc une seule `RenderGeometry` pour les rectangles et le texte d'une même taille.

## Traceur (`foundation.diagnostics`)

- Un tampon par fil (blocs de 65 536 événements, verrou propre au fil, jamais disputé
  hors de l'écriture finale), noms interneés une fois par site d'appel.
- Activation par `DIAG_TRACE_FILE`, arrêt par `DIAG_TRACE_DURATION`, `SIGUSR2` ou sortie
  du processus.
- Format `.vcap` : noms, puis par fil des événements à horodatage delta-varint, CRC-32 final.
