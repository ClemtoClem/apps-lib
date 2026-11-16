
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"
#include "consoles/arm/file.hpp"

namespace consoles::arm {

class Core;
enum NdsCmdMode {
	CMD_NONE = 0,
	CMD_HEADER,
	CMD_CHIP,
	CMD_SECURE,
	CMD_DATA
};

class Cartridge {
public:
	Cartridge(Core *core_) : core(core_) {}
	
	virtual ~Cartridge();

	virtual bool LoadRom(const std::string& path);

	void WriteSave();

	void ResizeSave(int newSize, bool dirty = true);

	/**
	 * @brief Sérialise/désérialise l'état commun à toutes les cartouches.
	 *
	 * Le contenu de la ROM elle-même n'est pas sauvegardé : seul le chemin
	 * (romName) est conservé, et la ROM est rechargée depuis le disque au
	 * chargement (via LoadRom(), qui appelle la version virtuelle dérivée).
	 * Le contenu de la sauvegarde (save) est en revanche sérialisé.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void IoState(StateArchive &archive);

	int GetRomSize() const {
		return romSize;
	}

	int GetSaveSize() const {
		return saveSize;
	}

protected:
	Core *core;
	File romFile;
	std::vector<uint8_t> romData, saveData; ///< mémoire possédée ; `rom` et `save` en sont des vues
	uint8_t *rom = nullptr, *save = nullptr;
	int romSize = 0, saveSize = 0;
	bool saveDirty = false;
	std::mutex mutex;
	uint32_t romMask = 0;
	/// false si la section n'a pas pu être allouée (ROM laissée inchangée).
	bool LoadRomSection(size_t offset, size_t size);

private:
	std::string m_romName, m_saveName;
};


class CartridgeNds : public Cartridge {
public:
	CartridgeNds(Core *core);

	bool LoadRom(const std::string& path);

	void DirectBoot();

	uint16_t ReadAuxSpiCnt(bool cpu) const {
		return m_auxSpiCnt[cpu];
	}

	uint8_t ReadAuxSpiData(bool cpu) const {
		return m_auxSpiData[cpu];
	}

	uint32_t ReadRomCtrl(bool cpu) const {
		return m_romCtrl[cpu];
	}

	uint32_t ReadRomDataIn(bool cpu);
	void WriteAuxSpiCnt(bool cpu, uint16_t mask, uint16_t value);
	void WriteAuxSpiData(bool cpu, uint8_t value);
	void WriteRomCtrl(bool cpu, uint32_t mask, uint32_t value);
	void WriteRomCmdOutL(bool cpu, uint32_t mask, uint32_t value);
	void WriteRomCmdOutH(bool cpu, uint32_t mask, uint32_t value);

	/**
	 * @brief Sérialise/désérialise l'état spécifique aux cartouches NDS.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void IoState(StateArchive &archive);

private:
	uint32_t m_romCode = 0;
	bool m_romEncrypted = false;
	NdsCmdMode m_cmdMode = CMD_NONE;
	uint32_t m_encTable[0x412] = {};
	uint32_t m_encCode[3] = {};
	uint32_t m_romAddrReal[2] = {}, m_romAddrVirt[2] = {};
	uint16_t m_blockSize[2] = {}, m_readCount[2] = {};
	uint32_t m_wordCycles[2] = {};
	bool m_encrypted[2] = {};
	uint8_t m_auxCommand[2] = {};
	uint32_t m_auxAddress[2] = {};
	int m_auxWriteCount[2] = {};
	uint16_t m_auxSpiCnt[2] = {};
	uint8_t m_auxSpiData[2] = {};
	uint32_t m_romCtrl[2] = {};
	uint64_t m_romCmdOut[2] = {};
	std::function<void()> m_wordReadyTasks[2];
	uint64_t Encrypt64(uint64_t value);
	uint64_t Decrypt64(uint64_t value);
	void InitKeycode(int level);
	void ApplyKeycode();
	void WordReady(bool cpu);
};

class CartridgeGba : public Cartridge {
public:
	CartridgeGba(Core *core_) : Cartridge(core_) {}
	bool LoadRom(const std::string& path);
	uint8_t *GetRom(uint32_t address);
	bool IsEeprom(uint32_t address);
	uint8_t EepromRead();
	void EepromWrite(uint8_t value);
	uint8_t SramRead(uint32_t address);
	void SramWrite(uint32_t address, uint8_t value);

	/**
	 * @brief Sérialise/désérialise l'état spécifique aux cartouches GBA.
	 * @param archive Archive de sauvegarde/chargement.
	 */
	void IoState(StateArchive &archive);

private:
	int m_eepromCount = 0;
	uint16_t m_eepromCmd = 0;
	uint64_t m_eepromData = 0;
	bool m_eepromDone = false;
	uint8_t m_flashCmd = 0;
	bool m_bankSwap = false;
	bool m_flashErase = false;
	bool FindString(std::string string);
};

FORCE_INLINE uint8_t *CartridgeGba::GetRom(uint32_t address) {
	return ((address &= romMask) < uint32_t(romSize)) ? &rom[address] : nullptr;
}

FORCE_INLINE bool CartridgeGba::IsEeprom(uint32_t address) {
	return (saveSize == -1 || saveSize == 0x200 || saveSize == 0x2000) &&
				 (romSize <= 0x1000000 || address >= (uint32_t)0x0DFFFF00);
}

} // namespace consoles::arm

