# Émulation : modules `engine.emu`, `engine.consoles` et application `retro`

## `engine.emu` — cadre d'émulation matérielle

Inspiré de l'architecture de MAME, en C++23 standard (aucune dépendance hors `foundation.core`).

| MAME | `emu` | Rôle |
|------|-------|------|
| `device_t` | `emu::Device` | Nœud de l'arbre de périphériques : étiquette, enfants possédés (`std::unique_ptr`), cycle `Start` → `Reset` → `Stop`, `State()` |
| `running_machine` | `emu::Machine` | Racine, ordonnanceur, `RunFrame`, `Reset`, `SaveState` / `LoadState`, `StepInstruction` |
| `device_scheduler`, `emu_timer` | `emu::Scheduler` | Temps en tops de l'horloge maîtresse (entier 64 bits), tranches jusqu'au prochain temporisateur, quantum d'entrelacement |
| `device_execute_interface` | `emu::Executable` | Processeur : `Execute()` consomme `m_icount`, diviseur d'horloge, suspension |
| `address_space` | `emu::AddressSpace` | Bus paginé : mémoire directe, gestionnaires, miroirs, banques ; lecture et écriture séparées |
| `save_item` | `emu::StateArchive` | Un seul chemin de code pour sauver et charger ; lecture hors bornes impossible |
| `screen_device`, `sound_stream`, `ioport`, `nvram_device` | `emu::Screen`, `SoundStream`, `InputPort`, `Nvram` | Périphériques génériques |
| `device_state/disasm_interface` | `emu::Debuggable` | Registres nommés, compteur ordinal, désassemblage, bus du programme |
| liste `GAME()` | `emu::SystemRegistry` | Systèmes disponibles : nom, extensions, fabrique de machine |

Points de conception :

- **Temps exact** : un temporisateur armé pendant l'exécution d'un processeur écourte la tranche
  en cours (`AbortSlice`) ; l'évènement tombe au cycle demandé.
- **États robustes** : chaque périphérique a son bloc préfixé de son chemin et de sa taille. Un
  état d'une autre machine, d'une autre version ou tronqué est refusé avant toute modification ;
  si un bloc se révèle incohérent pendant l'application, la machine est réinitialisée.
- **Bus** : une plage alignée sur les pages est un pointeur par page (aucun appel) ; les plages
  partielles ou servies par un gestionnaire passent par une liste de plages de la page.

## `engine.consoles` — systèmes

### Game Boy / Game Boy Color (`consoles/gbc/`)

```
gbc
└─ board      bus 16 bits, RAM de travail (8 banques), E/S, DMA de l'OAM, HDMA, vitesse double
   ├─ cpu     Sharp LR35902 (durées d'instruction exactes, désassembleur)
   ├─ cart    cartouche : ROM seule, MBC1, MBC2, MBC3 (horloge), MBC5 ── ram (emu::Nvram)
   ├─ screen  160 × 144
   ├─ sound   stéréo 48 kHz
   ├─ ppu     modes 0-3 par temporisateur, fond, fenêtre, objets, palettes couleur
   ├─ apu     2 voies carrées, onde, bruit, séquenceur 512 Hz, passe-haut
   ├─ timer   DIV / TIMA
   └─ pad     right, left, up, down, a, b, select, start
```

Horloge maîtresse à 8 388 608 Hz : un cycle du processeur vaut deux tops (un seul en vitesse
double). Démarrage direct sur la cartouche, sans BIOS. Option `model` : `auto`, `dmg`, `cgb`.

Validation : tests unitaires (processeur, contrôleurs de banques, vidéo, minuterie, manette,
liaison série, états, RAM sur pile) et ROM de référence de Blargg `cpu_instrs` et
`instr_timing`, exécutées si la variable `GBC_TEST_ROMS` désigne leur dossier.

Limites connues : pas de programme de démarrage (BIOS), pas de câble de liaison, rendu par ligne
(les effets en milieu de ligne ne sont pas reproduits), horloge du MBC3 en temps émulé.

### Game Boy Advance et Nintendo DS (`consoles/gba/`, `consoles/nds/`, `consoles/arm/`)

Les deux consoles partagent le cœur ARM du module, dans `consoles/arm/` (espace de noms
`consoles::arm` : interpréteurs ARM7TDMI et ARM946E-S, vidéo 2D et 3D, son, cartouches, BIOS
logiciel ; repris de l'émulateur de SDL3_Wrapper, lignée NooDS), placé dans l'arbre `emu` par
la carte `consoles::arm::Board`. Les fabriques sont dans `src/gba/gba.cpp` et `src/nds/nds.cpp`,
leur partie commune dans `src/arm/machine.cpp`.

```
gba | nds
└─ board      carte ARM (processeur principal vu d'emu ; une « instruction » = une image)
   ├─ screen  240 × 160 (GBA) ou 256 × 384 : les deux écrans DS empilés, celui du haut d'abord
   ├─ sound   stéréo 32 kHz
   ├─ pad     a, b, select, start, right, left, up, down, r, l (+ x, y sur DS)
   └─ touch   écran tactile DS (emu::TouchPanel, moitié basse de l'écran)
```

- Le cœur garde son ordonnanceur interne (au cycle près entre ses processeurs) ; `emu` le
  cadence image par image. Le pas à pas du débogueur avance donc d'une image sur ces systèmes.
- BIOS : `gbaBios.bin` est **requis** pour la GBA. Par défaut, son introduction (logo) est jouée ;
  l'option `direct-boot=1` la saute et lance la cartouche dans l'état laissé par le BIOS (piles,
  mode système, SOUNDBIAS, matrices affines identité). La DS démarre directement sur la
  cartouche (BIOS et micrologiciel facultatifs, inutiles pour une cartouche GBA). Recherche dans `Media::biosDirectory` puis
  `assets/bios-firmware` (sous-dossiers `nintendo-game-boy-advance`, `nintendo-ds`).
- Débogueur : registres R0-R15 et CPSR, drapeaux et mode, désassembleur ARM/Thumb
  (`consoles/arm/disasm.hpp`), lecture de la mémoire sans toucher aux registres d'E/S, régions
  nommées (BIOS, EWRAM, IWRAM, VRAM…).
- Sauvegardes sur pile : `<dossier des sauvegardes>/<rom>.sav` ; états via `Machine::SaveState`.
- Réinitialisation : un cœur neuf, la ROM étant relue depuis le disque (archive comprise).

Tests : `tests/test_gba.cpp` et `tests/test_nds.cpp` assemblent de petites cartouches
synthétiques (`tests/arm_rom.hpp` : encodeur ARM / Thumb, en-têtes GBA et NDS avec leurs sommes
de contrôle) pour vérifier sans jeu du commerce les calculs ARM et Thumb, les appels, CLZ,
les deux processeurs de la DS, la vidéo (mode 3, affichage de la VRAM, inversion des écrans),
les touches, le stylet, les états (exécution identique après rechargement) et la
réinitialisation ; `tests/test_arm.cpp` couvre le désassembleur et les erreurs de construction
du cœur. Les jeux du dépôt (`assets/roms`) sont essayés en plus quand ils sont présents.

Vérifié : Metroid Fusion, Wario Land 4, Minish Cap, Pokémon Rubis, Super Mario Advance 4 (GBA) ;
Mario Kart DS, Super Mario 64 DS (tactile compris), Pokémon Platine, Bomberman (3D), checknds.

### Chargement des ROM

`emu::SystemRegistry::Identify` / `LoadRom` acceptent un fichier simple ou une archive lue par
`foundation.archive` (ZIP, 7z, RAR, TAR…) : la première entrée dont l'extension correspond à
un système est retenue (les archives ne contenant que des correctifs IPS/UPS sont écartées).
Les fabriques GBA et NDS lisent elles-mêmes la ROM depuis le chemin (`readsRomFromPath`), sans
copie intermédiaire.

## `foundation.archive` et `foundation.dataio`

- `archive` (repris de SDL3_Wrapper) : lecture et écriture ZIP, 7z, RAR v4/v5 (lecture),
  TAR, GZIP, BZIP2, XZ/LZMA, Zstandard, ISO 9660, chiffrement AES, arborescence virtuelle.
  Il ne dépend plus que de `foundation.core` : flux `archive::IOStream` en C++ standard
  (fichier, mémoire fixe, mémoire extensible) à la place de `sdl3::IOStream`, opérations de
  fichiers par `std::filesystem` (`archive::platform`).
- `dataio` : arbre `dataio::Json` (commentaires, écriture, modifications en place), flux
  binaires, et documents indépendants du format (JSON, XML, YAML, TOML, INI, CSV, CSS, HTML,
  glTF) sur un arbre commun `dataio::Node`, dont les enfants sont désormais possédés par des
  `std::shared_ptr` dans une table standard (plus de `sdl3::Properties` ni de `new`/`delete`).

## `graphics.ui` — encadrement de fenêtre (`ui/window_frame.hpp`)

Barre de titre dessinée par l'application pour une fenêtre SDL sans décoration : icône, titre,
boutons personnalisés à gauche et à droite (`ui::FrameButton` : icône, texte, infobulle, état
actif, séparateur), boutons réduire / agrandir-restaurer / fermer.

- `ComputeFrameLayout`, `HitTestFrame` : géométrie et zones (déplacement, bords et coins de
  redimensionnement), sans dessin ni SDL ;
- `WindowFrame(u, fenêtre, options)` : dessin et action demandée (`FrameAction`) ;
- `WindowChrome` : zones gérées par le système (`SDL_SetWindowHitTest`), réduire, agrandir,
  restaurer.

## Application `retro`

```
retro [ROM] [--roms DOSSIER]… [--model dmg|cgb] [--data-dir D] [--mute] [--debug] [--paused]
retro ROM --headless [--frames N] [--screenshot F.png] [--press BOUTON@IMAGE[+DURÉE]]…
          [--touch X,Y@IMAGE[+DURÉE]]… [--save-state N] [--load-state N] [--require-video]
retro --list-systems
```

- **Vue « Jeu »** : bibliothèque (recherche, favoris, récents, vignette), écran de la machine
  (lecture, pause, ×0,5 / ×1 / ×2, volume, plein écran), états sauvegardés (vignette, date,
  renommer, supprimer ; F5 : nouvel état, F9 : charger le plus récent).
- **Vue « Débogage »** (F12) : registres et drapeaux, désassemblage avec points d'arrêt (clic),
  tuiles de la mémoire vidéo et état de l'unité vidéo, mémoire (octets modifiés en rouge),
  console, performances. Pas à pas : F11 ; pas principal : F10.
- Clavier : flèches, X (A), W ou Z (B), Entrée (Start), Maj droite (Select), A/S (L/R),
  Q/D (X/Y de la DS) ; manette SDL ; souris : stylet sur l'écran tactile de la DS.
- Écrans de la DS : bouton en bas du lecteur (ou F3) pour les placer l'un au-dessus de l'autre
  ou côte à côte (écran du haut à gauche) ; le choix est mémorisé dans `library.txt`.
- La bibliothèque est analysée en arrière-plan (archives comprises).
- Données dans `data/retro/` : `states/`, `saves/`, `screenshots/`, `library.txt`.
