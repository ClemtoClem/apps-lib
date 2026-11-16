#pragma once
/**
 * @file machine.hpp
 * @brief Outils internes communs aux fabriques Game Boy Advance et Nintendo DS : recherche
 *        des BIOS, texte d'en-tête, réglages de base du cœur et construction de la machine
 *        (carte `consoles::arm::Board`).
 */
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <emu/system.hpp>

#include "consoles/arm/board.hpp"
#include "consoles/arm/settings.hpp"

namespace consoles::arm {

/// Premier fichier existant parmi `<dossier>[/<sous-dossier>]/<nom>`, les dossiers étant
/// `media.biosDirectory` puis `assets/bios-firmware` ; vide si aucun.
[[nodiscard]] std::string FindFirmware(const emu::Media &media, std::string_view subdirectory, std::string_view name);

/// Texte ASCII d'un champ d'en-tête (arrêt au premier octet nul, `?` pour les octets non imprimables).
[[nodiscard]] std::string HeaderText(std::span<const uint8_t> rom, size_t offset, size_t length);

/// Réglages communs : démarrage direct (option `direct-boot`), chemin des sauvegardes.
[[nodiscard]] Settings BaseSettings(const emu::Media &media);

/// Machine complète : cœur construit tout de suite (erreurs rapportées à l'appelant), puis
/// recréé à chaque réinitialisation (ROM relue depuis le disque, archive comprise).
[[nodiscard]] emu::MachineResult BuildMachine(const emu::Media &media, Mode mode, Settings settings);

} // namespace consoles::arm
