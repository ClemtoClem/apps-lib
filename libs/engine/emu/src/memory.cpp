#include "emu/memory.hpp"

#include <algorithm>

namespace emu {

AddressSpace::AddressSpace(std::string name, unsigned addressBits, unsigned pageBits) : m_name(std::move(name)) {
	addressBits = std::clamp(addressBits, 1u, 32u);
	pageBits = std::clamp(pageBits, 1u, addressBits);
	if (addressBits - pageBits > 20)
		pageBits = addressBits - 20;
	m_pageBits = pageBits;
	m_addressMask = addressBits == 32 ? 0xFFFFFFFFu : ((1u << addressBits) - 1u);
	m_pageMask = (1u << pageBits) - 1u;
	const size_t pages = size_t(1) << (addressBits - pageBits);
	m_read.resize(pages);
	m_write.resize(pages);
}

uint32_t AddressSpace::NewSlot() {
	if (!m_freeSlots.empty()) {
		const uint32_t slot = m_freeSlots.back();
		m_freeSlots.pop_back();
		return slot;
	}
	m_slots.emplace_back();
	return uint32_t(m_slots.size());
}

void AddressSpace::MapRange(Table &table, Range range) {
	range.start &= m_addressMask;
	range.end &= m_addressMask;
	if (range.end < range.start)
		return;
	range.origin = range.start;
	const bool memory = range.memory != nullptr && range.size > 0;
	const bool mapped = memory || range.read || range.write;
	const uint32_t pageSize = m_pageMask + 1;
	for (uint64_t p = range.start >> m_pageBits; p <= (range.end >> m_pageBits); ++p) {
		Page &page = table[size_t(p)];
		const uint32_t pageStart = uint32_t(p << m_pageBits), pageEnd = pageStart + m_pageMask;
		const bool whole = range.start <= pageStart && range.end >= pageEnd;
		const size_t offset = memory ? size_t(pageStart - range.start) % range.size : 0;
		if (whole && (!mapped || (memory && offset + pageSize <= range.size))) {
			// Page entière : accès direct (ou page vidée).
			if (page.slot) {
				m_slots[page.slot - 1].clear();
				m_freeSlots.push_back(page.slot);
				page.slot = 0;
			}
			page.direct = mapped ? range.memory + offset : nullptr;
			continue;
		}
		// Page partielle, miroir non contigu ou gestionnaire : liste de plages de la page.
		if (!page.slot) {
			page.slot = NewSlot();
			if (page.direct) { // l'accès direct existant devient une plage couvrant la page
				Range wholePage;
				wholePage.start = wholePage.origin = pageStart;
				wholePage.end = pageEnd;
				wholePage.memory = page.direct;
				wholePage.size = pageSize;
				m_slots[page.slot - 1].push_back(std::move(wholePage));
				page.direct = nullptr;
			}
		}
		std::vector<Range> &ranges = m_slots[page.slot - 1];
		if (whole)
			ranges.clear();
		Range clipped = range;
		clipped.start = std::max(range.start, pageStart);
		clipped.end = std::min(range.end, pageEnd);
		ranges.insert(ranges.begin(), std::move(clipped)); // la plus récente en tête
	}
}

void AddressSpace::MapRam(uint32_t start, uint32_t end, std::span<uint8_t> memory) {
	Range r;
	r.start = start;
	r.end = end;
	r.memory = memory.data();
	r.size = memory.size();
	MapRange(m_read, r);
	MapRange(m_write, r);
}

void AddressSpace::MapRom(uint32_t start, uint32_t end, std::span<const uint8_t> memory) {
	Range r;
	r.start = start;
	r.end = end;
	// La table de lecture ne modifie jamais la mémoire : le const_cast reste interne.
	r.memory = const_cast<uint8_t *>(memory.data());
	r.size = memory.size();
	MapRange(m_read, r);
}

void AddressSpace::MapRead(uint32_t start, uint32_t end, ReadHandler handler) {
	Range r;
	r.start = start;
	r.end = end;
	r.read = std::move(handler);
	MapRange(m_read, std::move(r));
}

void AddressSpace::MapWrite(uint32_t start, uint32_t end, WriteHandler handler) {
	Range r;
	r.start = start;
	r.end = end;
	r.write = std::move(handler);
	MapRange(m_write, std::move(r));
}

void AddressSpace::Unmap(uint32_t start, uint32_t end) {
	Range r;
	r.start = start;
	r.end = end;
	MapRange(m_read, r);
	MapRange(m_write, r);
}

void AddressSpace::UnmapWrite(uint32_t start, uint32_t end) {
	Range r;
	r.start = start;
	r.end = end;
	MapRange(m_write, r);
}

uint8_t AddressSpace::ReadSlow(const Page &page, uint32_t address) const {
	if (page.slot)
		for (const Range &r : m_slots[page.slot - 1])
			if (address >= r.start && address <= r.end) {
				if (r.read)
					return r.read(address);
				if (r.memory)
					return r.memory[size_t(address - r.origin) % r.size];
				break; // plage retirée : non projetée
			}
	++m_unmappedReads;
	return m_unmapped;
}

void AddressSpace::WriteSlow(const Page &page, uint32_t address, uint8_t value) {
	if (page.slot)
		for (const Range &r : m_slots[page.slot - 1])
			if (address >= r.start && address <= r.end) {
				if (r.write)
					r.write(address, value);
				else if (r.memory)
					r.memory[size_t(address - r.origin) % r.size] = value;
				else
					break;
				return;
			}
	++m_unmappedWrites;
}

} // namespace emu
