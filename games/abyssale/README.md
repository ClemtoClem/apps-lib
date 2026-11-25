# abyssale

**Abyssale** — jardin abyssal sous-marin procédural (fond marin ondulé, rochers,
forêt de kelp, coraux, bancs de poissons, raie manta, bulles).

```sh
cpp-maker run abyssale          # compiler et exécuter
cpp-maker build abyssale        # compiler seulement
```

## Commandes

| Touche | Action |
|---|---|
| ZQSD / flèches | nager |
| E / Espace | monter |
| Q | descendre |
| Maj | nager vite |
| souris | regarder |
| Échap | menu |
| 1–3 (menu) | qualité Élevée / Moyenne / Basse |

## Architecture

Le jeu **ne redéfinit aucune classe** de rendu ni de bruit : il compose les
modules existants.

- `math::SimplexNoise` / `math::Rng` — génération procédurale du fond marin ;
- `sdl3::Renderer3D` — rendu (ombre du soleil, brouillard sous-marin, PBR) ;
- `sdl3::MeshData` — géométries (sphère déformée, cylindres, plan, poisson) ;
- `graphics.app` — cycle de vie, fenêtre, GPU ;
- `graphics.ui` — menu (interface immédiate).

## Assets

- `assets/textures/` — textures physiques (sable, roche, caustiques), générées
  procéduralement puis chargées au démarrage par `sdl3::ImgLoad`.
- `assets/shaders/` — sources GLSL de référence de l'éclairage sous-marin
  (`scene.frag` : brouillard + soleil ; `water.frag` : caustiques de la
  surface). Le rendu effectif passe par les shaders SPIR-V embarqués de
  `sdl3::Renderer3D` (`libs/platform/sdl3/shaders/`) ; ces sources documentent
  le même pipeline et peuvent être compilées avec `tools/compile_shaders.sh`.
