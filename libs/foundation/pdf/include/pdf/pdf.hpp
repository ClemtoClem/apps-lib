#pragma once
/**
 * @file pdf.hpp
 * @brief foundation.pdf : écriture et lecture de fichiers PDF, polices standard.
 *
 *  - `fonts.hpp`  : métriques des 12 polices latines standard, encodage WinAnsi, mesure de texte ;
 *  - `writer.hpp` : `Writer` / `Canvas` (pages, tracés, texte, images, liens, signets, métadonnées) ;
 *  - `object.hpp` : objets PDF et analyseur syntaxique ;
 *  - `reader.hpp` : `Reader` (références croisées, flux d'objets, réparation, pages, texte) ;
 *  - `truetype.hpp` : polices TrueType / OpenType (métriques, incorporation, inventaire des dossiers).
 */
#include "pdf/fonts.hpp"
#include "pdf/object.hpp"
#include "pdf/reader.hpp"
#include "pdf/truetype.hpp"
#include "pdf/writer.hpp"
