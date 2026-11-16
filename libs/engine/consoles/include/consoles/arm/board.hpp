#pragma once
/**
 * @file board.hpp
 * @brief Carte d'un système à processeurs ARM (Game Boy Advance, Nintendo DS) : place le
 *        cœur ARM du module (`consoles/arm/core.hpp` : interpréteurs ARM7TDMI / ARM946E-S, vidéo 2D/3D, son, cartouches)
 *        dans l'arbre de périphériques `emu`.
 *
 * Le cœur garde son propre ordonnanceur interne (au cycle près entre ses processeurs et
 * ses périphériques) ; vu d'`emu`, la carte est un processeur dont une « instruction » est
 * une image complète : l'ordonnanceur de la machine cadence ainsi l'émulation image par
 * image. Le pas à pas du débogueur avance donc d'une image sur ces systèmes.
 *
 * La carte publie l'image (`emu::Screen`), le son (`emu::SoundStream`), la manette
 * (`emu::InputPort` : a, b, select, start, right, left, up, down, r, l, x, y) et, sur la
 * Nintendo DS, l'écran tactile (`emu::TouchPanel`, écran du bas).
 */
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <core/result.hpp>
#include <emu/debug.hpp>
#include <emu/device.hpp>
#include <emu/input.hpp>
#include <emu/memory.hpp>
#include <emu/scheduler.hpp>
#include <emu/screen.hpp>
#include <emu/sound.hpp>

namespace consoles::arm {

class Core;
class CPU;

enum class Mode : uint8_t { Gba, Nds };

/// Recrée un cœur neuf (réinitialisation de la machine) ; l'erreur décrit ce qui manque.
using CoreFactory = std::function<Result<std::unique_ptr<Core>, std::string>()>;

class Board : public emu::Device, public emu::Executable, public emu::Debuggable {
public:
	/// `frameTicks` : durée d'une image en tops de l'horloge maîtresse de la machine.
	Board(emu::Machine &machine, std::string tag, emu::Device *owner, Mode mode, std::unique_ptr<Core> core, CoreFactory factory,
		  emu::Time frameTicks);
	~Board() override;

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;
	void Execute() override;

	[[nodiscard]] Mode GetMode() const noexcept { return m_mode; }
	/// Cœur courant (`nullptr` si la dernière réinitialisation a échoué).
	[[nodiscard]] Core *GetCore() noexcept { return m_core.get(); }
	[[nodiscard]] emu::InputPort &Pad() noexcept { return *m_pad; }
	[[nodiscard]] emu::TouchPanel *Touch() noexcept { return m_touch; }
	/// Message de la dernière réinitialisation échouée (vide si tout va bien).
	[[nodiscard]] const std::string &LastError() const noexcept { return m_error; }

	// emu::Debuggable — processeur principal : ARM7TDMI (GBA) ou ARM946E-S (NDS).
	[[nodiscard]] std::vector<emu::RegisterValue> DebugRegisters() const override;
	[[nodiscard]] uint32_t ProgramCounter() const override;
	[[nodiscard]] emu::DisassembledLine Disassemble(uint32_t address) const override;
	[[nodiscard]] const emu::AddressSpace &ProgramSpace() const override { return m_debugSpace; }
	[[nodiscard]] std::string FlagsText() const override;
	[[nodiscard]] std::vector<std::pair<std::string, uint32_t>> MemoryRegions() const override;

private:
	void Attach();
	void PublishFrame();
	[[nodiscard]] CPU *MainCpu() const noexcept;
	[[nodiscard]] uint8_t DebugRead(uint32_t address) const;

	Mode m_mode;
	std::unique_ptr<Core> m_core;
	CoreFactory m_factory;
	emu::Time m_frameTicks;
	bool m_fresh = true; ///< le cœur vient d'être construit : le premier Reset() le garde
	std::string m_error;
	emu::Screen *m_screen = nullptr;
	emu::SoundStream *m_sound = nullptr;
	emu::InputPort *m_pad = nullptr;
	emu::TouchPanel *m_touch = nullptr;
	emu::AddressSpace m_debugSpace; ///< lecture seule, sans effet sur les registres d'E/S
};

} // namespace consoles::arm
