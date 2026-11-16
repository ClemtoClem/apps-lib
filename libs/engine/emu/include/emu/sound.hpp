#pragma once
/**
 * @file sound.hpp
 * @brief Sortie son d'une machine (`sound_stream` de MAME) : file stéréo 16 bits.
 *
 * Le périphérique audio pousse ses échantillons à la fréquence de la sortie (`Push`), sur
 * le fil d'émulation ; le fil audio de l'application les retire (`Pull`). La file est
 * protégée par un verrou et bornée : si personne ne lit, les plus anciens sont perdus.
 */
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

#include "device.hpp"

namespace emu {

class SoundStream : public Device {
public:
	SoundStream(Machine &machine, std::string tag, Device *owner, int sampleRate, size_t capacityFrames = 16384);

	[[nodiscard]] int SampleRate() const noexcept { return m_sampleRate; }
	/// Ajoute une trame stéréo.
	void Push(int16_t left, int16_t right);
	/// Retire jusqu'à `out.size() / 2` trames (entrelacées G, D) ; le reste est complété par
	/// la dernière valeur (pas de claquement). Retourne le nombre de trames réellement lues.
	size_t Pull(std::span<int16_t> out);
	/// Trames en attente.
	[[nodiscard]] size_t Available() const;
	/// Trames produites depuis le démarrage (tests, synchronisation).
	[[nodiscard]] uint64_t TotalFrames() const noexcept { return m_total; }
	void Clear();

	void Reset() override { Clear(); }

private:
	int m_sampleRate;
	std::vector<int16_t> m_ring; // capacité × 2
	size_t m_head = 0, m_count = 0;
	int16_t m_last[2] = {0, 0};
	uint64_t m_total = 0;
	mutable std::mutex m_mutex;
};

} // namespace emu
