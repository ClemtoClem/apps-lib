#pragma once
/**
 * @file scheduler.hpp
 * @brief Ordonnanceur de la machine : horloge maîtresse, processeurs exécutés par tranches
 *        et temporisateurs.
 *
 * Le temps est compté en tops de l'horloge maîtresse (`Time`, entier 64 bits : aucune dérive).
 * Chaque `Executable` (processeur) a un diviseur : un cycle du processeur dure `diviseur` tops.
 * Comme dans MAME, l'ordonnanceur calcule une tranche jusqu'au prochain temporisateur (bornée
 * par le quantum d'entrelacement), y fait tourner chaque processeur, puis déclenche les
 * temporisateurs arrivés à échéance. Un temporisateur armé pendant l'exécution, avant la fin
 * de la tranche, écourte celle-ci (`AbortSlice`) : l'évènement tombe au bon cycle.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace emu {

class Scheduler;
class StateArchive;

using Time = uint64_t;
inline constexpr Time kNever = ~Time(0);

/// Interface d'exécution d'un périphérique (processeur) — `device_execute_interface` de MAME.
class Executable {
public:
	virtual ~Executable() = default;

	/// Exécute des instructions tant que `m_icount > 0` (le coût de chaque instruction en est
	/// soustrait). Un processeur arrêté (HALT) consomme tout : `m_icount = 0`.
	virtual void Execute() = 0;

	/// Nombre de tops de l'horloge maîtresse par cycle (≥ 1).
	void SetClockDivider(uint32_t divider) noexcept { m_divider = divider ? divider : 1; }
	[[nodiscard]] uint32_t ClockDivider() const noexcept { return m_divider; }
	/// Suspendu : n'exécute rien mais suit le temps de la machine.
	void SetSuspended(bool suspended) noexcept { m_suspended = suspended; }
	[[nodiscard]] bool Suspended() const noexcept { return m_suspended; }
	/// Cycles exécutés depuis le démarrage.
	[[nodiscard]] uint64_t TotalCycles() const noexcept { return m_totalCycles + uint64_t(m_sliceCycles - mIcount - m_stolen); }
	/// Écourte la tranche en cours : l'instruction en cours se termine, puis `Execute` rend la main.
	void AbortSlice() noexcept {
		if (mIcount > 0) {
			m_stolen += mIcount;
			mIcount = 0;
		}
	}

protected:
	int64_t mIcount = 0; ///< cycles restant dans la tranche

private:
	friend class Scheduler;
	uint32_t m_divider = 1;
	bool m_suspended = false;
	Time m_localTime = 0; ///< temps atteint par ce processeur (peut dépasser légèrement la tranche)
	uint64_t m_totalCycles = 0;
	int64_t m_sliceCycles = 0, m_stolen = 0;
};

using TimerId = uint32_t;

class Scheduler {
public:
	using Callback = std::function<void()>;

	/// Enregistre un processeur (ordre d'exécution = ordre d'enregistrement).
	void AddExecutable(Executable &executable);
	/// Crée un temporisateur désarmé. L'ordre de création doit être déterministe (états).
	[[nodiscard]] TimerId AllocTimer(std::string name, Callback callback);
	/// Arme : échéance dans `delay` tops, puis toutes les `period` tops (0 : une seule fois).
	void Adjust(TimerId timer, Time delay, Time period = 0);
	void Cancel(TimerId timer);
	[[nodiscard]] bool Armed(TimerId timer) const;
	/// Tops restants avant l'échéance (`kNever` si désarmé).
	[[nodiscard]] Time Remaining(TimerId timer) const;

	/// Temps courant ; pendant l'exécution d'un processeur, son temps exact (au cycle près).
	[[nodiscard]] Time Now() const noexcept;
	/// Durée maximale d'une tranche (entrelacement de plusieurs processeurs) ; 0 : illimitée.
	void SetQuantum(Time ticks) noexcept { m_quantum = ticks; }

	/// Avance jusqu'à `target` (ou jusqu'à `RequestExit`). Retourne le temps atteint.
	Time RunUntil(Time target);
	/// Termine `RunUntil` à la fin de la tranche en cours (fin d'image, par exemple).
	void RequestExit() noexcept;

	void Reset();
	void State(StateArchive &archive);

	[[nodiscard]] size_t TimerCount() const noexcept { return m_timers.size(); }
	[[nodiscard]] size_t ExecutableCount() const noexcept { return m_executables.size(); }
	/// Cycles exécutés par le premier processeur enregistré (0 s'il n'y en a pas).
	[[nodiscard]] uint64_t PrimaryCycles() const noexcept { return m_executables.empty() ? 0 : m_executables.front()->TotalCycles(); }

private:
	struct Timer {
		std::string name;
		Callback callback;
		Time expiry = kNever;
		Time period = 0;
	};
	[[nodiscard]] Time NextExpiry() const noexcept;
	void FireTimers();

	std::vector<Executable *> m_executables;
	std::vector<Timer> m_timers;
	Time m_now = 0;
	Time m_sliceEnd = 0;
	Time m_quantum = 0;
	Executable *m_current = nullptr;
	bool m_exit = false;
};

} // namespace emu
