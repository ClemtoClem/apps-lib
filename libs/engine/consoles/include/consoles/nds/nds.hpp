#pragma once
/**
 * @file nds.hpp
 * @brief Machine Nintendo DS : carte ARM (`consoles::arm::Board`, mode NDS) sur le cœur
 *        `consoles/arm/core.hpp` — ARM946E-S et ARM7TDMI, deux écrans 256 × 192 empilés (écran émulé de
 *        256 × 384, celui du haut d'abord), écran tactile en bas, son stéréo 32 kHz.
 *
 * Démarrage direct sur la cartouche : les BIOS (`ndsBios9.bin`, `ndsBios7.bin`) et le
 * micrologiciel (`firmware.bin`) sont facultatifs (BIOS logiciel du cœur sinon) ; ils sont
 * cherchés dans `Media::biosDirectory`, puis `assets/bios-firmware`, directement ou dans un
 * sous-dossier `nintendo-ds`.
 */
#include <emu/system.hpp>

namespace nds {

inline constexpr uint64_t kMasterClock = 33'513'982;
inline constexpr uint64_t kFrameTicks = 560'190; ///< 263 lignes × 355 points × 6 cycles : 59,83 Hz

struct Header {
	bool valid = false;
	std::string error, title, gameCode;
	bool checksumOk = false; ///< CRC-16 de l'en-tête (0x15E)
};
[[nodiscard]] Header ParseHeader(std::span<const uint8_t> rom);

[[nodiscard]] emu::MachineResult CreateMachine(const emu::Media &media);
[[nodiscard]] emu::SystemInfo System();

} // namespace nds
