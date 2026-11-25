#include "emu/scheduler.hpp"

#include <algorithm>

#include "emu/state.hpp"

namespace emu {

void Scheduler::AddExecutable(Executable &executable) {
	executable.m_localTime = m_now;
	m_executables.push_back(&executable);
}

TimerId Scheduler::AllocTimer(std::string name, Callback callback) {
	m_timers.push_back({std::move(name), std::move(callback), kNever, 0});
	return TimerId(m_timers.size() - 1);
}

void Scheduler::Adjust(TimerId timer, Time delay, Time period) {
	if (timer >= m_timers.size())
		return;
	Timer &t = m_timers[timer];
	t.expiry = Now() + delay;
	t.period = period;
	// Échéance avant la fin de la tranche en cours : on l'écourte pour tomber au bon cycle.
	if (t.expiry < m_sliceEnd) {
		m_sliceEnd = t.expiry;
		if (m_current)
			m_current->AbortSlice();
	}
}

void Scheduler::Cancel(TimerId timer) {
	if (timer < m_timers.size())
		m_timers[timer].expiry = kNever;
}

bool Scheduler::Armed(TimerId timer) const { return timer < m_timers.size() && m_timers[timer].expiry != kNever; }

Time Scheduler::Remaining(TimerId timer) const {
	if (!Armed(timer))
		return kNever;
	const Time now = Now();
	return m_timers[timer].expiry > now ? m_timers[timer].expiry - now : 0;
}

Time Scheduler::Now() const noexcept {
	if (!m_current)
		return m_now;
	const Executable &e = *m_current;
	return e.m_localTime + Time(e.m_sliceCycles - e.mIcount - e.m_stolen) * e.m_divider;
}

Time Scheduler::NextExpiry() const noexcept {
	Time next = kNever;
	for (const Timer &t : m_timers)
		next = std::min(next, t.expiry);
	return next;
}

void Scheduler::FireTimers() {
	// Par ordre d'échéance (puis de création) ; un rappel peut réarmer n'importe quel temporisateur.
	for (;;) {
		size_t best = m_timers.size();
		for (size_t i = 0; i < m_timers.size(); ++i)
			if (m_timers[i].expiry <= m_now && (best == m_timers.size() || m_timers[i].expiry < m_timers[best].expiry))
				best = i;
		if (best == m_timers.size())
			return;
		Timer &t = m_timers[best];
		t.expiry = t.period ? t.expiry + t.period : kNever;
		if (t.callback)
			t.callback(); // `t` peut être invalidé (AllocTimer dans un rappel) : ne plus l'utiliser
	}
}

void Scheduler::RequestExit() noexcept {
	m_exit = true;
	// La tranche s'arrête là où en est le processeur courant : les suivants n'iront pas au-delà.
	m_sliceEnd = std::min(m_sliceEnd, std::max(Now(), m_now));
	if (m_current)
		m_current->AbortSlice();
}

Time Scheduler::RunUntil(Time target) {
	m_exit = false;
	while (m_now < target && !m_exit) {
		m_sliceEnd = std::min(target, NextExpiry());
		if (m_quantum)
			m_sliceEnd = std::min(m_sliceEnd, m_now + m_quantum);
		if (m_sliceEnd < m_now)
			m_sliceEnd = m_now;
		for (Executable *e : m_executables) {
			if (e->m_suspended) {
				e->m_localTime = std::max(e->m_localTime, m_sliceEnd);
				continue;
			}
			// La fin de tranche peut reculer pendant l'exécution (temporisateur armé, sortie).
			while (e->m_localTime < m_sliceEnd) {
				const int64_t cycles = int64_t((m_sliceEnd - e->m_localTime + e->m_divider - 1) / e->m_divider);
				e->m_sliceCycles = e->mIcount = cycles;
				e->m_stolen = 0;
				m_current = e;
				e->Execute();
				m_current = nullptr;
				int64_t ran = cycles - e->mIcount - e->m_stolen;
				if (ran <= 0 && e->m_stolen == 0)
					ran = cycles; // processeur qui n'a rien consommé : il suit le temps
				e->m_sliceCycles = e->mIcount = e->m_stolen = 0;
				e->m_totalCycles += uint64_t(ran);
				e->m_localTime += Time(ran) * e->m_divider;
				if (ran == 0)
					break; // tranche écourtée avant toute instruction
			}
		}
		m_now = m_sliceEnd;
		FireTimers();
	}
	m_sliceEnd = m_now;
	return m_now;
}

void Scheduler::Reset() {
	m_now = 0;
	m_sliceEnd = 0;
	m_exit = false;
	for (Executable *e : m_executables) {
		e->m_localTime = 0;
		e->m_totalCycles = 0;
		e->mIcount = e->m_sliceCycles = e->m_stolen = 0;
	}
	for (Timer &t : m_timers) {
		t.expiry = kNever;
		t.period = 0;
	}
}

void Scheduler::State(StateArchive &archive) {
	archive.Marker(0x53434844); // « SCHD »
	archive.Io(m_now);
	uint32_t executables = uint32_t(m_executables.size()), timers = uint32_t(m_timers.size());
	archive.Io(executables);
	archive.Io(timers);
	if (!archive.Saving() && (executables != m_executables.size() || timers != m_timers.size())) {
		archive.Fail(); // structure différente : état incompatible
		return;
	}
	for (Executable *e : m_executables) {
		archive.Io(e->m_localTime);
		archive.Io(e->m_totalCycles);
		archive.Io(e->m_divider);
		archive.Io(e->m_suspended);
	}
	for (Timer &t : m_timers) {
		archive.Io(t.expiry);
		archive.Io(t.period);
	}
	m_sliceEnd = m_now;
}

} // namespace emu
