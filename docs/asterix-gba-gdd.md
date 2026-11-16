# Game Design Document — Astérix & Obélix XXL (GBA) → remake apps-lib

> Document de spécification produit par rétro-ingénierie du binaire GBA
> `Asterix & Obelix XXL (Europe) (En,Fr,De,Es,It,Nl).gba` (8 388 608 octets),
> à l'aide du module REA (`foundation.rea` + `engine.disasm`).
>
> Objet : comprendre le fonctionnement du jeu original pour le **réimplémenter**
> en C++23 avec les modules d'apps-lib (physique, 3D, audio), avec des
> graphismes, une physique et un gameplay améliorés.

## 1. Identification du binaire

| Champ | Valeur |
|---|---|
| Taille | 8 388 608 octets (64 Mbit) |
| Titre | `ASTERIX` |
| Code jeu | `BLXP` · fabricant `70` · version 0 |
| Plateforme | Game Boy Advance (ARM7TDMI @ 16,78 MHz) |
| Genre | Plateformer 2D à défilement horizontal |
| Langues | Anglais, Français, Allemand, Espagnol, Italien, Néerlandais |

## 2. Structure de la ROM (carte mémoire)

```
0x000000..0x0001FF   En-tête GBA (logo Nintendo, titre, code, checksum)
0x000200..0x050000   Données : tuiles, tilemaps, textes, graphismes/son compressés (LZ77)
0x050000..0x07xxxx   CODE (ARM + Thumb) — boot, boucles, logique de jeu
0x07xxxx..0x7FFFFF   Données : graphismes, cartes de niveaux, musique/sons
```

- **Point d'entrée** : `0x08050000` (branche `0xEA013FFE` en `0x00`).
- **Mémoire** : IWRAM 256 Ko (`0x02000000`), EWRAM 32 Ko (`0x03000000`), VRAM 96 Ko (`0x06000000`), ROM 8 Mo (`0x08000000`).

## 3. Séquence de boot (CRT0, désassemblée)

Le point d'entrée configure le processeur puis remet à zéro les mémoires :

```asm
MRS  R0,CPSR / ORR R0,#0x12 / MSR CPSR_cf,R0   ; mode IRQ
LDR  SP,[PC,#0xE8]                              ; pile IRQ
…  (idem SVC) …
MOV  R0,#0x2000000 ; boucle STR → remise à zéro IWRAM (256 Ko)
MOV  R0,#0x3000000 ; remise à zéro EWRAM (32 Ko)
MOV  R0,#0x6000000 ; remise à zéro VRAM (96 Ko)
```

Puis saut vers la logique d'initialisation du jeu.

## 4. Boucle de jeu (identifiée)

**Boucle principale** (`0x080501D0`, boucle `B 0x080501D0`) :
```asm
LDRB R1,[R0] / ADD R1,#1 / STRB R1,[R0]   ; compteur d'images +1
CMP  R1,#0xFF / BLEQ 0x0805A114            ; à 255 → callback
```

**Boucle de menu** (`0x0805029C`) : navigation dans un menu à 5 entrées :
```asm
TST  R0,#0x80  →  curseur +1 (Down), boucle sur 5
TST  R0,#0x40  →  curseur −1 (Up)
STRB R2,[R1]                                 ; mémorise l'index de menu
TST  R0,#0x01  →  bouton A → action (0x0805A114)
```

**Lecture des entrées** (`0x0806EB20`) — structure de données clé :
```asm
LDR  R0,=0x04000130   ; registre KEYINPUT
LDRH R1,[R0]          ; état brut des boutons (actif-bas)
MVN  R1,R1            ; inversion
MOV  R1,R1,LSL #22 / LSR #22 ; masque 10 boutons
LDRH R3,[R2,#2]       ; état précédent
EOR  R3,R3,R1 / AND R3,R3,R1   ; boutons « appuyés ce frame »
STRH R3,[R2,#0]       ; ← pressé
STRH R1,[R2,#2]       ; ← maintenu
```

## 5. Structure de données d'entrée (inférée)

```c
struct InputState {
    uint16_t pressed; // boutons appuyés ce frame (bit 0=A, 1=B, 2=Select, 3=Start,
                      //   4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L)
    uint16_t held;    // boutons maintenus (même masque)
};
```

Le jeu maintient un **état d'entrée global** `{pressed, held}`, mis à jour à
chaque image, consommé par la boucle de menu et la boucle de jeu (saut,
déplacement, attaque).

## 6. Contenu identifié

- **Personnages** : Astérix, Obélix, Idéfix, Panoramix.
- **Menus** : `START`, `OPTION`, `NOUVEAU`, `CONTINUE`, `QUITTER`, `SCORE`, `SOUND`, `MUSIC`, `LANGUAGE`.
- **Données de niveau** : tilemaps en clair (caractères → index de tuiles).
- **Ressources** : graphismes (tuiles 8×8, sprites OAM) et son **compressés LZ77**.

## 7. Spécification du remake (C++23 / apps-lib)

| Aspect | Original (GBA) | Remake (apps-lib) |
|---|---|---|
| Rendu | 2D 240×160, tuiles + sprites | **3D** — `graphics.render3d` (PBR, ombres, lumières, ciel) |
| Monde | tilemaps 2D | **map 3D** — meshes procéduraux ou `engine.voxel` (terrain, AO) |
| Physique | intégrée au code (inconnue) | **`engine.physics`** — rigid-bodies, contacts SAT, impulsions, raycasts, step-up |
| Audio | échantillons GBA | **`engine.audio`** — oscillateurs, filtres, metering |
| Logique / mods | code figé | **`foundation.script`** (langage de mods) + `foundation.ecs` |
| Interface | menus bitmap | **`graphics.ui`** (retained, CSS) |

### Mécaniques à réimplémenter (issues de l'analyse)
- **Déplacement latéral** (gauche/droite) + **saut** (bouton A), avec **gravité** et **collisions**.
- **Menu à curseur** (haut/bas, validation A) → transposé en menu 3D moderne.
- **Score**, **niveaux**, **power-ups** (à préciser par extraction des données de niveau).
- **Multilingue** (6 langues) → table de localisation.

### Améliorations visées
1. **Physique** : gravité/sauts/step-up paramétrables, détection continue, trampolines, plateformes mobiles (via `engine.physics`).
2. **Graphismes** : éclairage PBR, ombres portées, caméra 3D, skybox analytique.
3. **Gameplay** : niveaux étendus, ennemis/IA, power-ups, sauvegarde.

## 6 bis. Machine à états et chargement des données (identifiés)

**Action du bouton A** (`0x0805A114`) — déclenche une **transition** :
```asm
LDRB R1,[R0] / CMP R1,#0 / BNE fin
MOV  R1,#0xF0 / STRB R1,[R0]   ; démarre un compteur de transition (240 images)
```
Le menu valide → un **minuteur de fondu/transition** de 240 images est lancé,
puis la logique de jeu prend le relais.

**Désérialiseur de données tagué** (`0x0805A148`) — format de chargement des
niveaux/entités/sprites :
```asm
LDRB×4 → R2 = mot 32 bits
MOV  R3,R2,LSR #28   ; tag (4 bits de poids fort)
BIC  R2,R2,#0xF0000000 ; adresse absolue GBA (28 bits)
tag 0 → fin du flux
tag 1 → écrit 1 octet    à [R2], avance de 1
tag 2 → écrit 1 demi-mot à [R2], avance de 2
tag 3 → écrit 1 mot      à [R2], avance de 4
```
Le jeu stocke ses **données de niveau et d'entités** dans ce format
`{tag(4), adresse(28), valeur…}`, interprété à l'exécution — l'équivalent d'un
mini-sérialiseur qui initialise directement la mémoire (variables IWRAM,
attributs de sprites OAM, tuiles VRAM). C'est la clé de l'extraction des
données : les flux tagués de la ROM décrivent l'état initial du jeu.

**Initialisation** (`0x0805360C`) — enchaîne des appels via **table de
pointeurs** et `BX R0` : confirme un **code mixte ARM + Thumb** (les `BX`
vers une adresse impaire basculent en Thumb). Le boot/entrées/menus sont en
ARM ; la logique de jeu appelée par table est en Thumb.

## 8. Prochaines étapes du rétro-ingénierie

1. Extraire la **liste des niveaux** et le **format des tilemaps** (dimensions, encodage des tuiles).
2. Retrouver la **structure d'entité** (position, vitesse, état) et les **constantes de physique** (gravité, vitesse de saut) dans le code de jeu.
3. Identifier les **ennemis** et leurs **comportements** (IA).
4. Localiser la **musique/SFX** et les **tableaux de palettes** (pour reproduire l'ambiance).
