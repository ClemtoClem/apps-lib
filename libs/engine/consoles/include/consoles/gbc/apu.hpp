#pragma once
/**
 * @file apu.hpp
 * @brief Unité sonore de la Game Boy : deux voies carrées (la première avec balayage de
 *        fréquence), une voie d'onde programmable, une voie de bruit, séquenceur de trame à
 *        512 Hz (durées, balayage, enveloppes) et mixage stéréo vers `emu::SoundStream`.
 *
 * Deux temporisateurs de l'ordonnanceur cadencent l'unité : le séquenceur (tous les 8192
 * cycles) et l'échantillonnage à la fréquence de la sortie ; les générateurs avancent du
 * nombre de cycles écoulés à chaque échantillon. Un filtre passe-haut retire la composante
 * continue, comme le condensateur de sortie de la console.
 */
#include <array>
#include <cstdint>

#include <emu/device.hpp>
#include <emu/scheduler.hpp>
#include <emu/sound.hpp>

namespace gbc {

class Apu : public emu::Device {
public:
	Apu(emu::Machine &machine, std::string tag, emu::Device *owner, emu::SoundStream &output);

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;

	/// Registres 0xFF10-0xFF26 et table d'onde 0xFF30-0xFF3F.
	[[nodiscard]] uint8_t Read(uint32_t port) const;
	void Write(uint32_t port, uint8_t value);

	/// Voie active (0 à 3) — bits 0 à 3 de NR52.
	[[nodiscard]] bool ChannelEnabled(int channel) const noexcept;

private:
	/// Voie à enveloppe de volume (carrées et bruit).
	struct Channel {
		bool enabled = false, lengthEnable = false, envelopeIncrease = false;
		int freqTimer = 0, lengthCounter = 0, volume = 0, envelopeTimer = 0, envelopePeriod = 0, dutyPosition = 0;
	};
	void OnSequencer();
	void OnSample();
	void StepChannels(int cycles);
	void TriggerSquare(Channel &channel, uint8_t lengthRegister, uint8_t envelopeRegister);
	void PowerOff();

	emu::SoundStream &m_output;
	emu::TimerId m_sequencerTimer = 0, m_sampleTimer = 0;
	Channel m_square1, m_square2, m_noise;
	bool m_waveEnabled = false, m_waveLengthEnable = false;
	int m_waveFreqTimer = 0, m_waveLengthCounter = 0, m_wavePosition = 0;
	uint16_t m_lfsr = 0x7FFF;
	uint8_t m_sequencerStep = 0;
	uint8_t m_nr10 = 0, m_nr11 = 0, m_nr12 = 0, m_nr13 = 0, m_nr14 = 0;
	uint8_t m_nr21 = 0, m_nr22 = 0, m_nr23 = 0, m_nr24 = 0;
	uint8_t m_nr30 = 0, m_nr31 = 0, m_nr32 = 0, m_nr33 = 0, m_nr34 = 0;
	uint8_t m_nr41 = 0, m_nr42 = 0, m_nr43 = 0, m_nr44 = 0;
	uint8_t m_nr50 = 0, m_nr51 = 0, m_nr52 = 0;
	std::array<uint8_t, 16> m_wave{};
	// Échantillonnage : répartition exacte des tops entre échantillons (reste accumulé).
	emu::Time m_lastSampleTime = 0;
	uint32_t m_sampleRemainder = 0;
	float m_capacitor[2] = {0.f, 0.f};
};

} // namespace gbc
