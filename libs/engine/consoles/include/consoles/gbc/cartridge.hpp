#pragma once
/**
 * @file cartridge.hpp
 * @brief Cartouche Game Boy : en-tête, ROM, RAM sur pile et contrôleurs de banques
 *        (sans contrôleur, MBC1, MBC2, MBC3 avec horloge, MBC5).
 *
 * La cartouche se projette elle-même sur le bus : ROM en lecture directe (0x0000-0x7FFF,
 * banque haute commutée par reprojection), registres du contrôleur en écriture sur la même
 * plage, RAM externe en 0xA000-0xBFFF (retirée du bus quand elle est désactivée). La RAM
 * sur pile est un périphérique `emu::Nvram` enfant (« cart:ram »).
 */
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <emu/device.hpp>
#include <emu/memory.hpp>
#include <emu/nvram.hpp>
#include <emu/scheduler.hpp>

namespace gbc {

enum class Mbc : uint8_t { None, Mbc1, Mbc2, Mbc3, Mbc5 };
[[nodiscard]] const char *MbcName(Mbc mbc) noexcept;

struct CartridgeHeader {
	bool valid = false;
	std::string error;	  ///< raison si `valid` est faux
	std::string title;
	bool cgb = false;	  ///< accepte le mode couleur
	bool cgbOnly = false; ///< exige le mode couleur
	uint8_t type = 0;	  ///< octet 0x147
	Mbc mbc = Mbc::None;
	bool battery = false, rtc = false, rumble = false;
	size_t romSize = 0; ///< octets déclarés par l'en-tête
	size_t ramSize = 0; ///< octets de RAM externe (512 pour le MBC2)
	bool checksumOk = false;
};

/// Analyse l'en-tête (0x100-0x14F) d'une image de cartouche.
[[nodiscard]] CartridgeHeader ParseHeader(std::span<const uint8_t> rom);

class Cartridge : public emu::Device {
public:
	Cartridge(emu::Machine &machine, std::string tag, emu::Device *owner, std::vector<uint8_t> rom, const CartridgeHeader &header,
			  emu::AddressSpace &bus);

	void Reset() override;
	void State(emu::StateArchive &archive) override;

	[[nodiscard]] const CartridgeHeader &Header() const noexcept { return m_header; }
	/// RAM sur pile (`nullptr` si la cartouche n'a pas de RAM).
	[[nodiscard]] emu::Nvram *Ram() noexcept { return m_ram; }
	[[nodiscard]] unsigned RomBank() const noexcept { return m_romBank; }
	[[nodiscard]] unsigned RamBank() const noexcept { return m_ramBank; }
	[[nodiscard]] bool RamEnabled() const noexcept { return m_ramEnabled; }

private:
	void WriteControl(uint32_t address, uint8_t value);
	void Remap();
	[[nodiscard]] uint8_t ReadRtc() const;
	void LatchRtc();

	emu::AddressSpace &m_bus;
	std::vector<uint8_t> m_rom; ///< complétée à une puissance de deux
	CartridgeHeader m_header;
	emu::Nvram *m_ram = nullptr;
	unsigned m_romBanks = 2;
	// Registres du contrôleur.
	bool m_ramEnabled = false;
	uint8_t m_bankLow = 1, m_bankHigh = 0, m_mode = 0;
	uint8_t m_ramSelect = 0;
	// Banques effectives.
	unsigned m_romBank0 = 0, m_romBank = 1, m_ramBank = 0;
	// Horloge du MBC3 : secondes comptées en temps émulé (déterministe), registres verrouillés.
	uint64_t m_rtcBaseSeconds = 0;
	emu::Time m_rtcBaseTime = 0;
	uint8_t m_rtcLatch[5] = {};
	uint8_t m_rtcLatchState = 0xFF;
	bool m_rtcHalted = false;
};

} // namespace gbc
