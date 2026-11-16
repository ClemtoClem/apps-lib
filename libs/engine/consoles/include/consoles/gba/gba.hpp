#pragma once
/**
 * @file gba.hpp
 * @brief Machine Game Boy Advance : carte ARM (`consoles::arm::Board`, mode GBA) sur le
 *        cœur `consoles/arm/core.hpp` — ARM7TDMI à 16,78 MHz, écran 240 × 160, son stéréo 32 kHz.
 *
 * Le BIOS de la console (`gbaBios.bin`, 16 Ko) est requis : le cœur n'en a pas
 * d'équivalent logiciel. Il est cherché dans `Media::biosDirectory`, puis dans
 * `assets/bios-firmware`, directement ou dans un sous-dossier `nintendo-game-boy-advance`.
 *
 * Options (`emu::Media::options`) : `direct-boot` = `1` saute l'introduction du BIOS (logo)
 * et démarre la cartouche dans l'état où le BIOS la lance (défaut `0`).
 */
#include <emu/system.hpp>

namespace gba {

inline constexpr uint64_t kMasterClock = 16'777'216;
inline constexpr uint64_t kFrameTicks = 280'896; ///< 228 lignes × 1232 cycles : 59,73 Hz

/// Informations de l'en-tête d'une cartouche (titre, code du jeu, validité).
struct Header {
	bool valid = false;
	std::string error, title, gameCode;
	bool checksumOk = false;
};
[[nodiscard]] Header ParseHeader(std::span<const uint8_t> rom);

[[nodiscard]] emu::MachineResult CreateMachine(const emu::Media &media);
[[nodiscard]] emu::SystemInfo System();

} // namespace gba
