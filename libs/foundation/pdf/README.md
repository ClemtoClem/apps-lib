# foundation.pdf

Lecture et écriture de fichiers PDF en C++23 pur, sans dépendance externe. Espace de noms
`pdf`. Dépendances : `foundation.core` (UTF-8, `Result`, `RobinMap`), `foundation.math`
(`math::Color`), `foundation.archive` (flux zlib, empreinte XXH64), `foundation.dataio`.

Module indépendant : il ne connaît ni le moteur documentaire ni l'interface. `engine.document`
s'en sert pour mesurer le texte (mêmes métriques que le PDF) et pour exporter.

## Écrire

```cpp
#include <pdf/pdf.hpp>

pdf::Writer w;
pdf::DocumentInfo info;
info.title = "Rapport d'été";
w.SetInfo(info);

pdf::Canvas &page = w.AddPage(595.28, 841.89);            // A4, en points
page.FillRect(72, 72, 200, 40, math::Color(230, 240, 255)); // origine en haut à gauche
page.Text(80, 98, pdf::StandardFont::HelveticaBold, 14, "Été 2026", math::Color(0, 0, 0));
page.Link(72, 72, 200, 40, "https://example.org");
w.AddBookmark("Introduction", 0, 72);
auto saved = w.Save("rapport.pdf");                        // écriture atomique
```

| Fonction | Détail |
|---|---|
| Pages | tailles libres (bornées à 3–14 400 pt comme l'exige la norme), une `Canvas` par page |
| Tracés | `MoveTo`, `LineTo`, `CurveTo`, `Rectangle`, `Ellipse`, remplissage (non nul / pair-impair), trait, découpe, épaisseur, extrémités, jointures, pointillés |
| Couleurs | RVB 8 bits ; l'alpha devient une opacité (`ExtGState` /ca, /CA) |
| Texte | 12 polices standard latines (Helvetica, Times, Courier et variantes) ou police TrueType / OpenType incorporée (`FontRef`), encodage WinAnsi, tableau /Widths écrit pour chaque police |
| Rotation | `Rotate(degrés, cx, cy)` : matrice `cm` autour d'un point (sens horaire), entre `Save` et `Restore` |
| Images | RVBA 8 bits → RVB compressé + masque doux (/SMask) seulement si un pixel n'est pas opaque |
| Navigation | liens URI, liens internes vers une page et une ordonnée (`LinkToPage`, destination /XYZ), signets hiérarchiques (niveaux) |
| Métadonnées | titre, auteur, sujet, mots-clés, créateur, producteur, date (`DateString`) ; texte non ASCII en UTF-16BE |
| Sortie | PDF 1.4, flux de contenu FlateDecode (désactivable), table de références croisées exacte, /ID déterministe : deux exécutions donnent le même fichier |

## Polices TrueType / OpenType (`truetype.hpp`)

```cpp
auto fonts = pdf::ScanFonts(dirs);                        // inventaire : noms, style, graisse
auto face = pdf::TrueTypeFont::Load(fonts[0].path);       // police complète
pdf::FontRef ref{pdf::StandardFont::Helvetica, face.Value()}; // repli standard si face est nul
double w = ref.TextWidth("Été", 12);                       // largeurs réelles (hmtx)
page.Text(80, 120, ref, 12, "Été 2026", math::Color(0, 0, 0)); // incorporée dans le PDF
```

- **Inventaire** : `.ttf`, `.otf`, `.ttc`, `.otc` ; lecture partielle (tables `name`, `OS/2`,
  `head`) : famille et style typographiques, nom PostScript, graisse, italique, contours CFF.
  Chaque police d'une collection est une entrée.
- **Lecture** : `head`, `hhea`, `hmtx`, `maxp`, `OS/2`, `post`, `cmap` (formats 4 et 12) ;
  métriques en 1/1000 em (ascendante, descendante, hauteur des capitales, soulignement) ;
  largeurs WinAnsi ; droits d'incorporation (`fsType`).
- **Incorporation** : programme TrueType (`/FontFile2`, police extraite de sa collection) ou CFF
  (`/FontFile3 /Type1C`), descripteur, /Widths et `WinAnsiEncoding`. Une police dont la licence
  interdit l'incorporation est écrite dans sa police standard de repli.

## Lire

```cpp
auto file = pdf::Reader::Load("rapport.pdf");
if (file) {
	const pdf::PageInfo &p = file->Page(0);                // MediaBox, rotation, dictionnaire hérité
	auto text = file->ExtractText(0);                      // texte UTF-8
	auto title = file->InfoString("Title");
}
```

- **Références croisées** : tables classiques, flux de références (PDF 1.5) avec prédicteurs
  PNG, fichiers hybrides (/XRefStm), mises à jour incrémentales (/Prev : la plus récente l'emporte).
- **Flux d'objets** (/ObjStm).
- **Réparation** : si la table est absente ou fausse, les objets sont retrouvés par balayage
  (`n g obj`) et l'en-queue reconstituée (`trailer`, flux /XRef ou recherche du catalogue) ;
  `Repaired()` le signale.
- **Pages** : arbre parcouru avec héritage de /Resources, /MediaBox, /Rotate, protection contre
  les cycles et la profondeur excessive.
- **Filtres** : FlateDecode (avec prédicteurs PNG ; repli sur DEFLATE brut si l'en-tête zlib
  ou la somme Adler-32 est faux), ASCIIHexDecode, ASCII85Decode.
- **Texte** : opérateurs Tj, TJ, ', ", Td, TD, Tm, T*, TL, Tc, Tw ; espaces et retours à la
  ligne déduits des positions (largeurs /Widths du fichier, sinon métriques standard).

## Tests

`cpp-maker test foundation.pdf` — 18 tests :
- métriques (largeurs AFM, y compris les octets WinAnsi 0x80–0xFF) et encodage ;
- polices TrueType : noms, métriques et `cmap` comparés à `fc-query`, inventaire des dossiers
  et des collections, incorporation et rotation validées par `pdffonts` / `pdftotext` ;
- analyseur ;
- structure et références croisées du fichier écrit ; liens internes (page absente ignorée) ;
- relecture complète (texte, image et masque, liens, signets) ;
- PDF 1.5 à flux de références compressé avec prédicteur et flux d'objets, construit à la main ;
- mise à jour incrémentale ;
- réparation ;
- filtres ASCII85 et ASCIIHex ;
- écriture atomique ;
- validation par une implémentation indépendante (poppler : `pdfinfo`, `pdftotext`), test
  ignoré si poppler-utils est absent.

## Limites

- Texte encodé en WinAnsi, y compris avec une police incorporée (police simple, pas de police
  composite Identity-H ni de /ToUnicode écrit) : un caractère hors WinAnsi (Latin-1 et quelques
  signes typographiques) est remplacé par « ? ».
- Police incorporée entière (pas de sous-ensemble) ; pas de crénage, de ligatures ni de lecture
  des tables `GPOS` / `GSUB` ; police variable lue dans son instance par défaut.
- Largeurs des polices standard : AFM d'Adobe pour l'ASCII ; octets 0x80–0xFF relevés dans les
  AFM des polices URW aux mêmes métriques (Nimbus Sans, Nimbus Roman), dont les largeurs ASCII
  sont identiques à celles d'Adobe.
- Lecture :
  - pas de chiffrement ;
  - pas de /ToUnicode ni de polices composites (texte décodé en WinAnsi) ;
  - texte des formulaires XObject non extrait ;
  - prédicteur TIFF et filtres d'images (DCT, JBIG2, CCITT…) non décodés ;
  - une entrée « libre » d'une mise à jour ne masque pas l'objet d'une révision précédente.
- Un `Reader` met en cache les objets lus : il ne se partage pas entre fils.
