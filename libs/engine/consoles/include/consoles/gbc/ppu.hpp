#pragma once
/**
 * @file ppu.hpp
 * @brief Unité vidéo de la Game Boy / Game Boy Color : automate des modes 0 à 3 piloté par
 *        un temporisateur de l'ordonnanceur, registres LCDC/STAT/LY, palettes, et rendu
 *        ligne par ligne du fond, de la fenêtre et des objets.
 *
 * Une ligne dure 456 points : balayage de l'OAM (mode 2, 80), transfert (mode 3, 172),
 * retour horizontal (mode 0, 204) ; les lignes 144 à 153 forment le retour vertical
 * (mode 1). La ligne est dessinée à la fin du mode 3. L'interruption STAT suit la ligne
 * logique « OU » de ses sources (front montant), comme sur le matériel.
 */
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include <emu/device.hpp>
#include <emu/memory.hpp>
#include <emu/scheduler.hpp>
#include <emu/screen.hpp>

#include "cpu.hpp"

namespace gbc {

inline constexpr int kScreenWidth = 160, kScreenHeight = 144;
/// Tops de l'horloge maîtresse (8 388 608 Hz) par point de l'écran.
inline constexpr emu::Time kTicksPerDot = 2;
inline constexpr int kFrameDots = 70224;

class Ppu : public emu::Device {
public:
	Ppu(emu::Machine &machine, std::string tag, emu::Device *owner, emu::AddressSpace &bus, Lr35902 &cpu, emu::Screen &screen, bool color);

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;

	/// Registres 0xFF40-0xFF4B, 0xFF4F (banque de VRAM), 0xFF68-0xFF6B (palettes couleur).
	[[nodiscard]] uint8_t Read(uint32_t port) const;
	void Write(uint32_t port, uint8_t value);

	[[nodiscard]] uint8_t Mode() const noexcept { return m_stat & 3; }
	[[nodiscard]] uint8_t Line() const noexcept { return m_ly; }
	[[nodiscard]] bool Color() const noexcept { return m_color; }
	[[nodiscard]] std::span<uint8_t> Oam() noexcept { return m_oam; }
	[[nodiscard]] std::span<uint8_t> Vram(int bank) noexcept { return std::span<uint8_t>(m_vram).subspan(size_t(bank & 1) * 0x2000, 0x2000); }
	/// Appelé à chaque entrée dans le retour horizontal d'une ligne visible (transfert HDMA).
	void OnHBlank(std::function<void()> callback) { m_onHBlank = std::move(callback); }
	/// Appelé à chaque fin d'image (y compris écran éteint).
	void OnFrame(std::function<void()> callback) { m_onFrame = std::move(callback); }

private:
	void OnTimer();
	void SetMode(uint8_t mode);
	void UpdateStatLine();
	void MapVram();
	void RenderLine(int y);
	[[nodiscard]] uint32_t Shade(uint8_t palette, uint8_t index) const noexcept;
	[[nodiscard]] static uint32_t ColorOf(const std::array<uint8_t, 0x40> &data, unsigned palette, unsigned index) noexcept;

	emu::AddressSpace &m_bus;
	Lr35902 &m_cpu;
	emu::Screen &m_screen;
	bool m_color;
	emu::TimerId m_timer = 0;
	std::vector<uint8_t> m_vram; // 2 banques de 8 Ko
	std::array<uint8_t, 0xA0> m_oam{};
	uint8_t m_lcdc = 0x91, m_stat = 0, m_scy = 0, m_scx = 0, m_ly = 0, m_lyc = 0;
	uint8_t m_bgp = 0xFC, m_obp0 = 0xFF, m_obp1 = 0xFF, m_wy = 0, m_wx = 0;
	uint8_t m_vbk = 0, m_bgpi = 0, m_obpi = 0;
	std::array<uint8_t, 0x40> m_bgpd{}, m_obpd{};
	uint8_t m_windowLine = 0; ///< ligne interne de la fenêtre (n'avance que quand elle est dessinée)
	bool m_statLine = false;
	std::function<void()> m_onHBlank, m_onFrame;
};

} // namespace gbc
