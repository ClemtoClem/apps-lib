#pragma once
/**
 * @file gbc.hpp
 * @brief Machine Game Boy / Game Boy Color bâtie sur le cadre `emu`.
 *
 * Arbre des périphériques :
 * @code
 * gbc
 * └─ board      carte mère : bus 16 bits, RAM de travail, registres d'E/S, DMA/HDMA, vitesse
 *    ├─ cpu     Sharp LR35902
 *    ├─ cart    cartouche (contrôleur de banques) ── ram : RAM sur pile (emu::Nvram)
 *    ├─ screen  écran 160 × 144 (emu::Screen)
 *    ├─ sound   sortie stéréo 48 kHz (emu::SoundStream)
 *    ├─ ppu     unité vidéo
 *    ├─ apu     unité sonore
 *    ├─ timer   diviseur et compteur programmable (DIV/TIMA)
 *    └─ pad     manette (emu::InputPort : right, left, up, down, a, b, select, start)
 * @endcode
 *
 * L'horloge maîtresse est à 8 388 608 Hz : le processeur tourne à un cycle pour deux tops
 * (4,19 MHz) ou, en vitesse double de la Game Boy Color, à un cycle par top. La machine
 * démarre directement sur la cartouche (registres laissés par le programme de démarrage) :
 * aucun BIOS n'est nécessaire.
 */
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <emu/input.hpp>
#include <emu/memory.hpp>
#include <emu/system.hpp>

#include "apu.hpp"
#include "cartridge.hpp"
#include "cpu.hpp"
#include "ppu.hpp"

namespace gbc {

inline constexpr uint64_t kMasterClock = 8'388'608;

/// Diviseur (DIV) et compteur programmable (TIMA/TMA/TAC), cadencés par le processeur.
class Timer : public emu::Device {
public:
	Timer(emu::Machine &machine, std::string tag, emu::Device *owner, Lr35902 &cpu);

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;

	[[nodiscard]] uint8_t Read(uint32_t port) const;
	void Write(uint32_t port, uint8_t value);
	/// Tops par cycle du processeur (2 en vitesse normale, 1 en vitesse double).
	void SetTicksPerCycle(uint32_t ticks);

private:
	[[nodiscard]] uint16_t Counter() const;
	void Sync();
	void Rearm();
	void OnOverflowTick();

	Lr35902 &m_cpu;
	emu::TimerId m_timer = 0;
	uint32_t m_ticksPerCycle = 2;
	uint16_t m_counterBase = 0; ///< compteur système 16 bits (DIV = octet haut) à `m_baseTime`
	emu::Time m_baseTime = 0;
	uint8_t m_tima = 0, m_tma = 0, m_tac = 0;
};

/// Carte mère : possède le bus et tous les périphériques de la console.
class Board : public emu::Device {
public:
	Board(emu::Machine &machine, std::string tag, emu::Device *owner, std::vector<uint8_t> rom, const CartridgeHeader &header, bool color);

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;

	[[nodiscard]] emu::AddressSpace &Bus() noexcept { return m_bus; }
	[[nodiscard]] Lr35902 &Cpu() noexcept { return *m_cpu; }
	[[nodiscard]] Ppu &Video() noexcept { return *m_ppu; }
	[[nodiscard]] Apu &Audio() noexcept { return *m_apu; }
	[[nodiscard]] Cartridge &Cart() noexcept { return *m_cart; }
	[[nodiscard]] emu::InputPort &Pad() noexcept { return *m_pad; }
	[[nodiscard]] bool Color() const noexcept { return m_color; }
	[[nodiscard]] bool DoubleSpeed() const noexcept { return m_doubleSpeed; }
	/// Octets envoyés sur la liaison série (les ROM de test y écrivent leur verdict).
	[[nodiscard]] const std::string &SerialOutput() const noexcept { return m_serialOutput; }

private:
	[[nodiscard]] uint8_t ReadIo(uint32_t address);
	void WriteIo(uint32_t address, uint8_t value);
	void MapWorkRam();
	void ToggleSpeed();
	void StartHdma(uint8_t value);
	void HdmaBlock();
	void OamDma(uint8_t page);

	emu::AddressSpace m_bus;
	bool m_color;
	Lr35902 *m_cpu = nullptr;
	Cartridge *m_cart = nullptr;
	emu::Screen *m_screen = nullptr;
	emu::SoundStream *m_sound = nullptr;
	Ppu *m_ppu = nullptr;
	Apu *m_apu = nullptr;
	Timer *m_timer = nullptr;
	emu::InputPort *m_pad = nullptr;
	emu::TimerId m_serialTimer = 0;

	std::vector<uint8_t> m_wram; // 8 banques de 4 Ko
	std::array<uint8_t, 0x7F> m_hram{};
	uint8_t m_svbk = 1;
	uint8_t m_joypadSelect = 0x30;
	uint8_t m_serialData = 0, m_serialControl = 0;
	uint8_t m_dma = 0xFF;
	bool m_doubleSpeed = false, m_speedArmed = false;
	uint8_t m_hdmaSource[2] = {0xFF, 0xFF}, m_hdmaTarget[2] = {0xFF, 0xFF};
	uint8_t m_hdmaRemaining = 0xFF;
	bool m_hdmaActive = false;
	uint16_t m_hdmaNextSource = 0, m_hdmaNextTarget = 0;
	uint32_t m_framesSinceFlush = 0;
	std::string m_serialOutput;
};

/// Construit la machine pour une image de cartouche. Options de `media` :
/// `model` = `auto` (défaut : couleur si la cartouche l'accepte), `dmg` ou `cgb`.
[[nodiscard]] emu::MachineResult CreateMachine(const emu::Media &media);
/// Description du système pour le registre.
[[nodiscard]] emu::SystemInfo System();

} // namespace gbc
