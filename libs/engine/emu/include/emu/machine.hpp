#pragma once
/**
 * @file machine.hpp
 * @brief Machine émulée : racine de l'arbre de périphériques, ordonnanceur, exécution par
 *        image, réinitialisation et états sauvegardés (`running_machine` de MAME).
 *
 * @code
 * emu::Machine machine("gbc", 4'194'304);
 * auto &cpu = machine.Root().Add<Lr35902>("cpu", bus);
 * auto &screen = machine.Root().Add<emu::Screen>("screen", 160, 144);
 * machine.Start();                       // Start() puis Reset() de chaque périphérique
 * machine.RunFrame();                    // jusqu'à la fin d'image signalée par l'écran
 * std::vector<uint8_t> state = machine.SaveState();
 * @endcode
 *
 * Un état sauvegardé contient, pour chaque périphérique, son chemin et la taille de son
 * bloc : un état d'une autre machine, d'une autre version ou tronqué est refusé avant toute
 * modification ; si un bloc se révèle incohérent pendant l'application, la machine est
 * réinitialisée (jamais d'état à moitié chargé).
 */
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "device.hpp"
#include "scheduler.hpp"

namespace emu {

class Screen;
class SoundStream;
class InputPort;
class TouchPanel;
class Debuggable;

class Machine {
public:
	/// `masterClock` : fréquence de l'horloge maîtresse en Hz (unité de `Time`).
	Machine(std::string name, uint64_t masterClock);
	virtual ~Machine();
	Machine(const Machine &) = delete;
	Machine &operator=(const Machine &) = delete;

	[[nodiscard]] const std::string &Name() const noexcept { return m_name; }
	[[nodiscard]] uint64_t MasterClock() const noexcept { return m_masterClock; }
	[[nodiscard]] Device &Root() noexcept { return *m_root; }
	[[nodiscard]] Scheduler &GetScheduler() noexcept { return m_scheduler; }
	[[nodiscard]] const Scheduler &GetScheduler() const noexcept { return m_scheduler; }
	/// Périphérique par chemin sans la racine (« cpu », « cart:ram »).
	template <typename T> [[nodiscard]] T *Find(std::string_view path) { return m_root->Find<T>(path); }

	/// Démarre tous les périphériques puis les réinitialise. À appeler une fois l'arbre construit.
	void Start();
	[[nodiscard]] bool Started() const noexcept { return m_started; }
	/// Remise sous tension : ordonnanceur à zéro, `Reset()` de chaque périphérique.
	void Reset();

	/// Exécute jusqu'à la fin de l'image (`SignalFrame`) ; bornée à deux durées d'image.
	void RunFrame();
	/// Exécute `ticks` tops de l'horloge maîtresse.
	void Run(Time ticks);
	/// Fin d'image (appelée par l'écran principal) : `RunFrame` rend la main.
	void SignalFrame();
	[[nodiscard]] uint64_t FrameCount() const noexcept { return m_frames; }
	/// Durée nominale d'une image en tops (défaut : 1/60 s) — borne de `RunFrame`.
	void SetFrameTicks(Time ticks) noexcept { m_frameTicks = ticks ? ticks : 1; }
	[[nodiscard]] Time FrameTicks() const noexcept { return m_frameTicks; }
	/// Images par seconde nominales de la machine.
	[[nodiscard]] double FrameRate() const noexcept { return double(m_masterClock) / double(m_frameTicks); }

	/// Écrans, sorties son et ports d'entrée de l'arbre (ordre de construction), après `Start`.
	[[nodiscard]] std::span<Screen *const> Screens() const noexcept { return m_screens; }
	[[nodiscard]] std::span<SoundStream *const> Sounds() const noexcept { return m_sounds; }
	[[nodiscard]] std::span<InputPort *const> Inputs() const noexcept { return m_inputs; }
	[[nodiscard]] std::span<TouchPanel *const> TouchPanels() const noexcept { return m_touch; }
	/// Processeur principal débogable (le premier de l'arbre), `nullptr` s'il n'y en a pas.
	[[nodiscard]] Debuggable *Debugger() const noexcept { return m_debugger; }
	/// Exécute une instruction du processeur principal (pas à pas) : la machine avance top
	/// par top jusqu'à ce qu'il ait exécuté des cycles (il peut être en avance d'une
	/// instruction sur l'horloge, ou arrêté en attente d'une interruption).
	void StepInstruction();

	[[nodiscard]] std::vector<uint8_t> SaveState();
	/// Faux si l'état est illisible ou ne correspond pas à cette machine.
	[[nodiscard]] bool LoadState(std::span<const uint8_t> data);
	/// Version du format des états de la machine (à incrémenter quand un `State()` change).
	void SetStateVersion(uint32_t version) noexcept { m_stateVersion = version; }

	/// Parcours en profondeur (parents avant enfants).
	void ForEachDevice(const std::function<void(Device &)> &visit);

private:
	std::string m_name;
	uint64_t m_masterClock;
	Scheduler m_scheduler;
	std::unique_ptr<Device> m_root;
	std::vector<Screen *> m_screens;
	std::vector<SoundStream *> m_sounds;
	std::vector<InputPort *> m_inputs;
	std::vector<TouchPanel *> m_touch;
	Debuggable *m_debugger = nullptr;
	Time m_frameTicks;
	uint64_t m_frames = 0;
	uint32_t m_stateVersion = 1;
	bool m_started = false;
	bool m_frameDone = false;
};

} // namespace emu
