#pragma once
/**
 * @file nvram.hpp
 * @brief Mémoire non volatile (`nvram_device` de MAME) : RAM de cartouche sur pile, EEPROM,
 *        flash — chargée d'un fichier et réécrite quand elle a changé.
 */
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "device.hpp"

namespace emu {

class Nvram : public Device {
public:
	/// `fill` : valeur d'une mémoire vierge (0xFF pour une flash/EEPROM, 0x00 pour une SRAM).
	Nvram(Machine &machine, std::string tag, Device *owner, size_t size, uint8_t fill = 0xFF);

	[[nodiscard]] std::span<uint8_t> Data() noexcept { return m_data; }
	[[nodiscard]] std::span<const uint8_t> Data() const noexcept { return m_data; }
	[[nodiscard]] size_t Size() const noexcept { return m_data.size(); }
	/// À appeler après toute écriture : la mémoire sera enregistrée au prochain `Flush`.
	void MarkDirty() noexcept { m_dirty = true; }
	[[nodiscard]] bool Dirty() const noexcept { return m_dirty; }

	/// Fichier de sauvegarde ; le charge s'il existe (taille tronquée/complétée). Faux si absent.
	bool SetPath(std::filesystem::path path);
	[[nodiscard]] const std::filesystem::path &Path() const noexcept { return m_path; }
	/// Écrit le fichier si la mémoire a changé. Faux en cas d'échec d'écriture.
	bool Flush();

	void Stop() override { (void)Flush(); }
	void State(StateArchive &archive) override;

private:
	std::vector<uint8_t> m_data;
	std::filesystem::path m_path;
	bool m_dirty = false;
};

} // namespace emu
