#include "consoles/gbc/apu.hpp"

#include <algorithm>

#include <emu/machine.hpp>
#include <emu/state.hpp>

namespace gbc {

namespace {
constexpr int kDutyHighSteps[4] = {1, 2, 4, 6}; // pas hauts sur 8 : 12,5 / 25 / 50 / 75 %
constexpr int kNoiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
constexpr emu::Time kTicksPerCycle = 2;			   // horloge maîtresse à 8 388 608 Hz
constexpr emu::Time kSequencerTicks = 8192 * kTicksPerCycle; // 512 Hz

int SquarePeriod(uint8_t low, uint8_t high) { return (2048 - (low | ((high & 7) << 8))) * 4; }
} // namespace

Apu::Apu(emu::Machine &machine, std::string tag, emu::Device *owner, emu::SoundStream &output)
	: Device(machine, std::move(tag), owner), m_output(output) {}

void Apu::Start() {
	emu::Scheduler &scheduler = GetMachine().GetScheduler();
	m_sequencerTimer = scheduler.AllocTimer("apu-sequencer", [this] { OnSequencer(); });
	m_sampleTimer = scheduler.AllocTimer("apu-sample", [this] { OnSample(); });
}

void Apu::Reset() {
	PowerOff();
	m_wave.fill(0);
	m_sequencerStep = 0;
	m_lfsr = 0x7FFF;
	m_lastSampleTime = 0;
	m_sampleRemainder = 0;
	m_capacitor[0] = m_capacitor[1] = 0.f;
	// État laissé par le programme de démarrage : son activé, volume maximal, voie 1 routée.
	m_nr52 = 0x80;
	m_nr50 = 0x77;
	m_nr51 = 0xF3;
	emu::Scheduler &scheduler = GetMachine().GetScheduler();
	scheduler.Adjust(m_sequencerTimer, kSequencerTicks, kSequencerTicks);
	scheduler.Adjust(m_sampleTimer, 1);
}

void Apu::PowerOff() {
	m_square1 = m_square2 = m_noise = Channel{};
	m_waveEnabled = m_waveLengthEnable = false;
	m_waveFreqTimer = m_waveLengthCounter = m_wavePosition = 0;
	m_nr10 = m_nr11 = m_nr12 = m_nr13 = m_nr14 = 0;
	m_nr21 = m_nr22 = m_nr23 = m_nr24 = 0;
	m_nr30 = m_nr31 = m_nr32 = m_nr33 = m_nr34 = 0;
	m_nr41 = m_nr42 = m_nr43 = m_nr44 = 0;
	m_nr50 = m_nr51 = 0;
}

void Apu::State(emu::StateArchive &archive) {
	for (Channel *c : {&m_square1, &m_square2, &m_noise})
		archive.Io(*c);
	archive.Io(m_waveEnabled);
	archive.Io(m_waveLengthEnable);
	archive.Io(m_waveFreqTimer);
	archive.Io(m_waveLengthCounter);
	archive.Io(m_wavePosition);
	archive.Io(m_lfsr);
	archive.Io(m_sequencerStep);
	for (uint8_t *r : {&m_nr10, &m_nr11, &m_nr12, &m_nr13, &m_nr14, &m_nr21, &m_nr22, &m_nr23, &m_nr24, &m_nr30, &m_nr31, &m_nr32,
					   &m_nr33, &m_nr34, &m_nr41, &m_nr42, &m_nr43, &m_nr44, &m_nr50, &m_nr51, &m_nr52})
		archive.Io(*r);
	archive.Io(m_wave);
	archive.Io(m_lastSampleTime);
	archive.Io(m_sampleRemainder);
	archive.Io(m_capacitor);
}

bool Apu::ChannelEnabled(int channel) const noexcept {
	switch (channel) {
	case 0: return m_square1.enabled;
	case 1: return m_square2.enabled;
	case 2: return m_waveEnabled;
	default: return m_noise.enabled;
	}
}

void Apu::TriggerSquare(Channel &channel, uint8_t lengthRegister, uint8_t envelopeRegister) {
	channel.enabled = true;
	if (channel.lengthCounter == 0)
		channel.lengthCounter = 64 - (lengthRegister & 0x3F);
	channel.volume = envelopeRegister >> 4;
	channel.envelopePeriod = envelopeRegister & 7;
	channel.envelopeIncrease = (envelopeRegister & 8) != 0;
	channel.envelopeTimer = channel.envelopePeriod;
	if ((envelopeRegister >> 3) == 0)
		channel.enabled = false; // convertisseur éteint (volume initial nul, enveloppe décroissante)
}

void Apu::OnSequencer() {
	const bool lengthTick = (m_sequencerStep % 2) == 0;
	const bool sweepTick = m_sequencerStep == 2 || m_sequencerStep == 6;
	const bool envelopeTick = m_sequencerStep == 7;
	auto tickLength = [](int &counter, bool enable, bool &enabled) {
		if (enable && counter > 0 && --counter == 0)
			enabled = false;
	};
	if (lengthTick) {
		tickLength(m_square1.lengthCounter, m_square1.lengthEnable, m_square1.enabled);
		tickLength(m_square2.lengthCounter, m_square2.lengthEnable, m_square2.enabled);
		tickLength(m_waveLengthCounter, m_waveLengthEnable, m_waveEnabled);
		tickLength(m_noise.lengthCounter, m_noise.lengthEnable, m_noise.enabled);
	}
	if (sweepTick && m_square1.enabled) {
		const int period = (m_nr10 >> 4) & 7, shift = m_nr10 & 7;
		if (period && shift) {
			const int frequency = m_nr13 | ((m_nr14 & 7) << 8);
			const int delta = frequency >> shift;
			const int next = (m_nr10 & 8) ? frequency - delta : frequency + delta;
			if (next > 2047) {
				m_square1.enabled = false; // dépassement : la voie se coupe
			} else if (next >= 0) {
				m_nr13 = uint8_t(next);
				m_nr14 = uint8_t((m_nr14 & 0xF8) | ((next >> 8) & 7));
			}
		}
	}
	if (envelopeTick) {
		for (Channel *c : {&m_square1, &m_square2, &m_noise}) {
			if (c->envelopePeriod == 0)
				continue;
			if (c->envelopeTimer > 0)
				--c->envelopeTimer;
			if (c->envelopeTimer == 0) {
				c->envelopeTimer = c->envelopePeriod;
				if (c->envelopeIncrease && c->volume < 15)
					++c->volume;
				else if (!c->envelopeIncrease && c->volume > 0)
					--c->volume;
			}
		}
	}
	m_sequencerStep = uint8_t((m_sequencerStep + 1) & 7);
}

void Apu::StepChannels(int cycles) {
	auto stepSquare = [cycles](Channel &channel, uint8_t low, uint8_t high) {
		channel.freqTimer -= cycles;
		const int period = SquarePeriod(low, high);
		while (channel.freqTimer <= 0) {
			channel.freqTimer += period;
			channel.dutyPosition = (channel.dutyPosition + 1) & 7;
		}
	};
	stepSquare(m_square1, m_nr13, m_nr14);
	stepSquare(m_square2, m_nr23, m_nr24);
	if (m_waveEnabled) {
		m_waveFreqTimer -= cycles;
		const int period = (2048 - (m_nr33 | ((m_nr34 & 7) << 8))) * 2;
		while (m_waveFreqTimer <= 0) {
			m_waveFreqTimer += period;
			m_wavePosition = (m_wavePosition + 1) & 31;
		}
	}
	m_noise.freqTimer -= cycles;
	const int period = kNoiseDivisors[m_nr43 & 7] << (m_nr43 >> 4);
	while (m_noise.freqTimer <= 0) {
		m_noise.freqTimer += period;
		const uint16_t bit = (m_lfsr ^ (m_lfsr >> 1)) & 1;
		m_lfsr = uint16_t((m_lfsr >> 1) | (bit << 14));
		if (m_nr43 & 8) // registre court (7 bits) : timbre métallique
			m_lfsr = uint16_t((m_lfsr & ~0x40) | (bit << 6));
	}
}

void Apu::OnSample() {
	emu::Machine &machine = GetMachine();
	emu::Scheduler &scheduler = machine.GetScheduler();
	// Prochain échantillon : horloge / fréquence, le reste de la division est reporté.
	const uint64_t clock = machine.MasterClock(), rate = uint64_t(m_output.SampleRate());
	emu::Time delay = clock / rate;
	m_sampleRemainder += uint32_t(clock % rate);
	if (m_sampleRemainder >= rate) {
		m_sampleRemainder -= uint32_t(rate);
		++delay;
	}
	scheduler.Adjust(m_sampleTimer, delay);

	const emu::Time now = scheduler.Now();
	const int cycles = int((now - m_lastSampleTime) / kTicksPerCycle);
	m_lastSampleTime += emu::Time(cycles) * kTicksPerCycle;
	if (cycles > 0)
		StepChannels(cycles);

	int left = 0, right = 0;
	if (m_nr52 & 0x80) {
		auto add = [&](int amplitude, int channel) {
			if (m_nr51 & (0x10 << channel))
				left += amplitude;
			if (m_nr51 & (0x01 << channel))
				right += amplitude;
		};
		if (m_square1.enabled)
			add(m_square1.dutyPosition < kDutyHighSteps[m_nr11 >> 6] ? m_square1.volume : 0, 0);
		if (m_square2.enabled)
			add(m_square2.dutyPosition < kDutyHighSteps[m_nr21 >> 6] ? m_square2.volume : 0, 1);
		if (m_waveEnabled && (m_nr30 & 0x80)) {
			const uint8_t byte = m_wave[size_t(m_wavePosition / 2)];
			const int sample = (m_wavePosition & 1) ? (byte & 0x0F) : (byte >> 4);
			const int shift = (m_nr32 >> 5) & 3;
			add(shift == 0 ? 0 : (sample >> (shift - 1)), 2);
		}
		if (m_noise.enabled)
			add((m_lfsr & 1) == 0 ? m_noise.volume : 0, 3);
		left *= ((m_nr50 >> 4) & 7) + 1;
		right *= (m_nr50 & 7) + 1;
	}
	// 4 voies × 15 × volume 8 = 480 au maximum ; passe-haut du condensateur de sortie.
	const float inputs[2] = {float(left) / 480.f, float(right) / 480.f};
	int16_t out[2];
	for (int i = 0; i < 2; ++i) {
		const float filtered = inputs[i] - m_capacitor[i];
		m_capacitor[i] = inputs[i] - filtered * 0.996f;
		out[i] = int16_t(std::clamp(filtered, -1.f, 1.f) * 30000.f);
	}
	m_output.Push(out[0], out[1]);
}

uint8_t Apu::Read(uint32_t port) const {
	switch (port) {
	case 0xFF10: return uint8_t(m_nr10 | 0x80);
	case 0xFF11: return uint8_t(m_nr11 | 0x3F);
	case 0xFF12: return m_nr12;
	case 0xFF14: return uint8_t(m_nr14 | 0xBF);
	case 0xFF16: return uint8_t(m_nr21 | 0x3F);
	case 0xFF17: return m_nr22;
	case 0xFF19: return uint8_t(m_nr24 | 0xBF);
	case 0xFF1A: return uint8_t(m_nr30 | 0x7F);
	case 0xFF1C: return uint8_t(m_nr32 | 0x9F);
	case 0xFF1E: return uint8_t(m_nr34 | 0xBF);
	case 0xFF21: return m_nr42;
	case 0xFF22: return m_nr43;
	case 0xFF23: return uint8_t(m_nr44 | 0xBF);
	case 0xFF24: return m_nr50;
	case 0xFF25: return m_nr51;
	case 0xFF26:
		return uint8_t((m_nr52 & 0x80) | 0x70 | (m_square1.enabled ? 1 : 0) | (m_square2.enabled ? 2 : 0) | (m_waveEnabled ? 4 : 0) |
					   (m_noise.enabled ? 8 : 0));
	default:
		if (port >= 0xFF30 && port <= 0xFF3F)
			return m_wave[port - 0xFF30];
		return 0xFF; // registres en écriture seule
	}
}

void Apu::Write(uint32_t port, uint8_t value) {
	if (port >= 0xFF30 && port <= 0xFF3F) {
		m_wave[port - 0xFF30] = value;
		return;
	}
	if (!(m_nr52 & 0x80) && port != 0xFF26)
		return; // son coupé : les registres ignorent les écritures
	switch (port) {
	case 0xFF10: m_nr10 = value; break;
	case 0xFF11:
		m_nr11 = value;
		m_square1.lengthCounter = 64 - (value & 0x3F);
		break;
	case 0xFF12:
		m_nr12 = value;
		if ((value >> 3) == 0)
			m_square1.enabled = false;
		break;
	case 0xFF13: m_nr13 = value; break;
	case 0xFF14:
		m_nr14 = value;
		m_square1.lengthEnable = (value & 0x40) != 0;
		if (value & 0x80)
			TriggerSquare(m_square1, m_nr11, m_nr12);
		break;
	case 0xFF16:
		m_nr21 = value;
		m_square2.lengthCounter = 64 - (value & 0x3F);
		break;
	case 0xFF17:
		m_nr22 = value;
		if ((value >> 3) == 0)
			m_square2.enabled = false;
		break;
	case 0xFF18: m_nr23 = value; break;
	case 0xFF19:
		m_nr24 = value;
		m_square2.lengthEnable = (value & 0x40) != 0;
		if (value & 0x80)
			TriggerSquare(m_square2, m_nr21, m_nr22);
		break;
	case 0xFF1A:
		m_nr30 = value;
		if (!(value & 0x80))
			m_waveEnabled = false;
		break;
	case 0xFF1B:
		m_nr31 = value;
		m_waveLengthCounter = 256 - value;
		break;
	case 0xFF1C: m_nr32 = value; break;
	case 0xFF1D: m_nr33 = value; break;
	case 0xFF1E:
		m_nr34 = value;
		m_waveLengthEnable = (value & 0x40) != 0;
		if (value & 0x80) {
			m_waveEnabled = (m_nr30 & 0x80) != 0;
			if (m_waveLengthCounter == 0)
				m_waveLengthCounter = 256;
			m_wavePosition = 0;
		}
		break;
	case 0xFF20:
		m_nr41 = value;
		m_noise.lengthCounter = 64 - (value & 0x3F);
		break;
	case 0xFF21:
		m_nr42 = value;
		if ((value >> 3) == 0)
			m_noise.enabled = false;
		break;
	case 0xFF22: m_nr43 = value; break;
	case 0xFF23:
		m_nr44 = value;
		m_noise.lengthEnable = (value & 0x40) != 0;
		if (value & 0x80) {
			TriggerSquare(m_noise, m_nr41, m_nr42);
			m_lfsr = 0x7FFF;
		}
		break;
	case 0xFF24: m_nr50 = value; break;
	case 0xFF25: m_nr51 = value; break;
	case 0xFF26:
		if (!(value & 0x80))
			PowerOff();
		m_nr52 = value & 0x80;
		break;
	default: break;
	}
}

} // namespace gbc
