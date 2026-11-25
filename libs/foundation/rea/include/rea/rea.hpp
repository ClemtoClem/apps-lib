#pragma once
/// rea:: — rétro-ingénierie de binaires x86-64.
///   elf.hpp       analyseur ELF64 (sections, symboles, segments)
///   disasm.hpp    désassembleur x86-64 (Zydis vendu) — décodage + balayage linéaire
///   lifter.hpp    lifting « au mieux » d'une fonction désassemblée vers du C
///   analysis.hpp  chaînes, désassemblage complet, graphe d'appels et identité
///   arch.hpp      architecture cible (x86-64, ARM, Thumb, MIPS32)
///   project.hpp   projet sérialisable (binaire, architecture, annotations)
///   signature.hpp recherche de motifs (signatures d'algorithmes)
#include "analysis.hpp"
#include "arch.hpp"
#include "disasm.hpp"
#include "elf.hpp"
#include "lifter.hpp"
#include "project.hpp"
#include "signature.hpp"
