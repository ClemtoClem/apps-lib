#pragma once
/**
 * @file memory.hpp
 * @brief Espace d'adressage paginé d'un bus (`address_space` de MAME) : mémoire directe,
 *        gestionnaires de lecture/écriture, miroirs, commutation de banques.
 *
 * L'espace est découpé en pages de 2^pageBits octets. Lecture et écriture ont chacune leur
 * table : une ROM se projette en lecture pendant qu'un gestionnaire (contrôleur de banques)
 * reçoit les écritures de la même plage.
 *
 *  - Une plage alignée sur les pages et projetée sur de la mémoire est un accès direct
 *    (un pointeur par page : aucun appel).
 *  - Une plage non alignée, ou servie par un gestionnaire, passe par une liste de plages de
 *    la page (la plus récente l'emporte).
 *  - Une mémoire plus petite que sa plage est répétée (miroir).
 *  - Commuter une banque = reprojeter la plage (`MapRom`/`MapRam`), quelques pointeurs.
 *
 * Les bornes sont inclusives, comme dans MAME : `MapRam(0xC000, 0xDFFF, ram)`.
 * Les accès 16/32 bits sont petit-boutistes et composés d'accès 8 bits.
 */
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace emu {

class AddressSpace {
public:
	using ReadHandler = std::function<uint8_t(uint32_t address)>;
	using WriteHandler = std::function<void(uint32_t address, uint8_t value)>;

	/// `addressBits` ≤ 32 ; `pageBits` < `addressBits` (nombre de pages ≤ 2^20).
	AddressSpace(std::string name, unsigned addressBits, unsigned pageBits = 8);

	[[nodiscard]] const std::string &Name() const noexcept { return m_name; }
	[[nodiscard]] uint32_t AddressMask() const noexcept { return m_addressMask; }

	/// Mémoire vive : lecture et écriture directes.
	void MapRam(uint32_t start, uint32_t end, std::span<uint8_t> memory);
	/// Mémoire morte : lecture directe ; les écritures ne sont pas touchées.
	void MapRom(uint32_t start, uint32_t end, std::span<const uint8_t> memory);
	void MapRead(uint32_t start, uint32_t end, ReadHandler handler);
	void MapWrite(uint32_t start, uint32_t end, WriteHandler handler);
	void MapHandlers(uint32_t start, uint32_t end, ReadHandler read, WriteHandler write) {
		MapRead(start, end, std::move(read));
		MapWrite(start, end, std::move(write));
	}
	/// Retire toute projection de la plage (lectures : valeur non projetée ; écritures ignorées).
	void Unmap(uint32_t start, uint32_t end);
	void UnmapWrite(uint32_t start, uint32_t end);
	/// Valeur lue sur une adresse non projetée (bus ouvert), 0xFF par défaut.
	void SetUnmappedValue(uint8_t value) noexcept { m_unmapped = value; }

	[[nodiscard]] uint8_t Read8(uint32_t address) const {
		address &= m_addressMask;
		const Page &page = m_read[address >> m_pageBits];
		if (page.direct) [[likely]]
			return page.direct[address & m_pageMask];
		return ReadSlow(page, address);
	}
	void Write8(uint32_t address, uint8_t value) {
		address &= m_addressMask;
		const Page &page = m_write[address >> m_pageBits];
		if (page.direct) [[likely]] {
			page.direct[address & m_pageMask] = value;
			return;
		}
		WriteSlow(page, address, value);
	}
	[[nodiscard]] uint16_t Read16(uint32_t address) const { return uint16_t(Read8(address) | (Read8(address + 1) << 8)); }
	[[nodiscard]] uint32_t Read32(uint32_t address) const { return uint32_t(Read16(address)) | (uint32_t(Read16(address + 2)) << 16); }
	void Write16(uint32_t address, uint16_t value) {
		Write8(address, uint8_t(value));
		Write8(address + 1, uint8_t(value >> 8));
	}
	void Write32(uint32_t address, uint32_t value) {
		Write16(address, uint16_t(value));
		Write16(address + 2, uint16_t(value >> 16));
	}

	/// Lectures et écritures tombées sur une adresse non projetée (diagnostic).
	[[nodiscard]] uint64_t UnmappedReads() const noexcept { return m_unmappedReads; }
	[[nodiscard]] uint64_t UnmappedWrites() const noexcept { return m_unmappedWrites; }

private:
	/// Plage d'une page servie hors accès direct.
	struct Range {
		uint32_t start = 0, end = 0;
		uint8_t *memory = nullptr; ///< mémoire (miroir de `size` octets à partir de `origin`)
		size_t size = 0;
		uint32_t origin = 0;
		ReadHandler read;
		WriteHandler write;
	};
	struct Page {
		uint8_t *direct = nullptr; ///< base telle que direct[adresse & masque] soit l'octet
		uint32_t slot = 0;		   ///< 1 + indice dans m_slots, 0 : aucune liste
	};
	using Table = std::vector<Page>;

	void MapRange(Table &table, Range range);
	[[nodiscard]] uint8_t ReadSlow(const Page &page, uint32_t address) const;
	void WriteSlow(const Page &page, uint32_t address, uint8_t value);
	[[nodiscard]] uint32_t NewSlot();

	std::string m_name;
	unsigned m_pageBits;
	uint32_t m_addressMask, m_pageMask;
	Table m_read, m_write;
	std::vector<std::vector<Range>> m_slots;
	std::vector<uint32_t> m_freeSlots;
	uint8_t m_unmapped = 0xFF;
	mutable uint64_t m_unmappedReads = 0;
	uint64_t m_unmappedWrites = 0;
};

} // namespace emu
