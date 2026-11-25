#pragma once
/**
 * @file screen.hpp
 * @brief Écran émulé (`screen_device` de MAME) : tampon d'image RGBA et fin d'image.
 *
 * Le périphérique vidéo dessine dans `Pixels()` (0xAABBGGRR en mémoire : octets R, G, B, A)
 * puis appelle `EndFrame()` : l'image visible est échangée et, pour l'écran principal,
 * `Machine::RunFrame` rend la main.
 */
#include <cstdint>
#include <span>
#include <vector>

#include "device.hpp"

namespace emu {

/// Couleur RGBA d'un pixel (octets R, G, B, A en mémoire sur une machine petit-boutiste).
[[nodiscard]] constexpr uint32_t Rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
	return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

class Screen : public Device {
public:
	/// `primary` : sa fin d'image termine `Machine::RunFrame`.
	Screen(Machine &machine, std::string tag, Device *owner, int width, int height, bool primary = true);

	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	/// Tampon de dessin de l'image en cours (largeur × hauteur).
	[[nodiscard]] std::span<uint32_t> Pixels() noexcept { return m_back; }
	/// Dernière image terminée.
	[[nodiscard]] std::span<const uint32_t> Frame() const noexcept { return m_front; }
	[[nodiscard]] uint64_t FrameNumber() const noexcept { return m_frameNumber; }
	/// Termine l'image : le tampon de dessin devient l'image visible.
	void EndFrame();

	void Reset() override;
	void State(StateArchive &archive) override;

private:
	int m_width, m_height;
	bool m_primary;
	std::vector<uint32_t> m_back, m_front;
	uint64_t m_frameNumber = 0;
};

} // namespace emu
