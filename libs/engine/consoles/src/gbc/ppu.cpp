#include "consoles/gbc/ppu.hpp"

#include <algorithm>

#include <emu/machine.hpp>
#include <emu/state.hpp>

namespace gbc {

namespace {
constexpr emu::Time kOamDots = 80, kTransferDots = 172, kHBlankDots = 204, kLineDots = 456;

enum Lcdc : uint8_t {
	kLcdcBg = 0x01,
	kLcdcObj = 0x02,
	kLcdcObjTall = 0x04,
	kLcdcBgMap = 0x08,
	kLcdcTileData = 0x10,
	kLcdcWindow = 0x20,
	kLcdcWindowMap = 0x40,
	kLcdcOn = 0x80
};
} // namespace

Ppu::Ppu(emu::Machine &machine, std::string tag, emu::Device *owner, emu::AddressSpace &bus, Lr35902 &cpu, emu::Screen &screen, bool color)
	: Device(machine, std::move(tag), owner), m_bus(bus), m_cpu(cpu), m_screen(screen), m_color(color), m_vram(0x4000, 0) {}

void Ppu::Start() {
	m_timer = GetMachine().GetScheduler().AllocTimer("ppu", [this] { OnTimer(); });
}

void Ppu::Reset() {
	std::ranges::fill(m_vram, uint8_t(0));
	m_oam.fill(0);
	m_lcdc = 0x91;
	m_stat = 0x02; // mode 2 de la ligne 0
	m_scy = m_scx = m_ly = m_lyc = 0;
	m_bgp = 0xFC;
	m_obp0 = m_obp1 = 0xFF;
	m_wy = m_wx = 0;
	m_vbk = m_bgpi = m_obpi = 0;
	m_bgpd.fill(0xFF); // palettes blanches à la mise sous tension
	m_obpd.fill(0xFF);
	m_windowLine = 0;
	m_statLine = false;
	m_bus.MapRam(0xFE00, 0xFE9F, m_oam);
	MapVram();
	GetMachine().GetScheduler().Adjust(m_timer, kOamDots * kTicksPerDot);
}

void Ppu::State(emu::StateArchive &archive) {
	archive.Io(std::span<uint8_t>(m_vram));
	archive.Io(m_oam);
	archive.Io(m_lcdc);
	archive.Io(m_stat);
	archive.Io(m_scy);
	archive.Io(m_scx);
	archive.Io(m_ly);
	archive.Io(m_lyc);
	archive.Io(m_bgp);
	archive.Io(m_obp0);
	archive.Io(m_obp1);
	archive.Io(m_wy);
	archive.Io(m_wx);
	archive.Io(m_vbk);
	archive.Io(m_bgpi);
	archive.Io(m_obpi);
	archive.Io(m_bgpd);
	archive.Io(m_obpd);
	archive.Io(m_windowLine);
	archive.Io(m_statLine);
	if (!archive.Saving())
		MapVram();
}

void Ppu::MapVram() { m_bus.MapRam(0x8000, 0x9FFF, Vram(m_vbk)); }

void Ppu::SetMode(uint8_t mode) {
	m_stat = uint8_t((m_stat & 0xFC) | mode);
	UpdateStatLine();
}

void Ppu::UpdateStatLine() {
	const bool match = m_ly == m_lyc;
	m_stat = uint8_t((m_stat & ~0x04) | (match ? 0x04 : 0));
	const uint8_t mode = m_stat & 3;
	const bool line = (m_lcdc & kLcdcOn) && ((match && (m_stat & 0x40)) || (mode == 0 && (m_stat & 0x08)) || (mode == 1 && (m_stat & 0x10)) ||
											 (mode == 2 && (m_stat & 0x20)));
	if (line && !m_statLine)
		m_cpu.RequestInterrupt(kIntStat);
	m_statLine = line;
}

void Ppu::OnTimer() {
	emu::Scheduler &scheduler = GetMachine().GetScheduler();
	if (!(m_lcdc & kLcdcOn)) { // écran éteint : image blanche, cadence d'image conservée
		std::ranges::fill(m_screen.Pixels(), Shade(0, 0));
		scheduler.Adjust(m_timer, emu::Time(kFrameDots) * kTicksPerDot);
		if (m_onFrame)
			m_onFrame();
		m_screen.EndFrame();
		return;
	}
	switch (m_stat & 3) {
	case 2: // balayage de l'OAM → transfert
		SetMode(3);
		scheduler.Adjust(m_timer, kTransferDots * kTicksPerDot);
		break;
	case 3: // transfert terminé : la ligne est dessinée, retour horizontal
		RenderLine(m_ly);
		SetMode(0);
		scheduler.Adjust(m_timer, kHBlankDots * kTicksPerDot);
		if (m_onHBlank)
			m_onHBlank();
		break;
	case 0: // fin de ligne visible
		++m_ly;
		if (m_ly == kScreenHeight) {
			SetMode(1);
			m_cpu.RequestInterrupt(kIntVBlank);
			scheduler.Adjust(m_timer, kLineDots * kTicksPerDot);
			if (m_onFrame)
				m_onFrame();
			m_screen.EndFrame();
		} else {
			SetMode(2);
			scheduler.Adjust(m_timer, kOamDots * kTicksPerDot);
		}
		break;
	default: // retour vertical : lignes 144 à 153
		if (m_ly == 153) {
			m_ly = 0;
			m_windowLine = 0;
			SetMode(2);
			scheduler.Adjust(m_timer, kOamDots * kTicksPerDot);
		} else {
			++m_ly;
			UpdateStatLine();
			scheduler.Adjust(m_timer, kLineDots * kTicksPerDot);
		}
		break;
	}
}

uint8_t Ppu::Read(uint32_t port) const {
	switch (port) {
	case 0xFF40: return m_lcdc;
	case 0xFF41: return uint8_t(m_stat | 0x80);
	case 0xFF42: return m_scy;
	case 0xFF43: return m_scx;
	case 0xFF44: return m_ly;
	case 0xFF45: return m_lyc;
	case 0xFF47: return m_bgp;
	case 0xFF48: return m_obp0;
	case 0xFF49: return m_obp1;
	case 0xFF4A: return m_wy;
	case 0xFF4B: return m_wx;
	case 0xFF4F: return m_color ? uint8_t(m_vbk | 0xFE) : 0xFF;
	case 0xFF68: return m_color ? uint8_t(m_bgpi | 0x40) : 0xFF;
	case 0xFF69: return m_color ? m_bgpd[m_bgpi & 0x3F] : 0xFF;
	case 0xFF6A: return m_color ? uint8_t(m_obpi | 0x40) : 0xFF;
	case 0xFF6B: return m_color ? m_obpd[m_obpi & 0x3F] : 0xFF;
	default: return 0xFF;
	}
}

void Ppu::Write(uint32_t port, uint8_t value) {
	switch (port) {
	case 0xFF40: {
		const bool wasOn = (m_lcdc & kLcdcOn) != 0, on = (value & kLcdcOn) != 0;
		m_lcdc = value;
		emu::Scheduler &scheduler = GetMachine().GetScheduler();
		if (wasOn && !on) { // extinction : LY revient à 0, mode 0, plus d'interruption
			m_ly = 0;
			m_windowLine = 0;
			m_stat = uint8_t(m_stat & 0xFC);
			m_statLine = false;
			scheduler.Adjust(m_timer, emu::Time(kFrameDots) * kTicksPerDot);
		} else if (!wasOn && on) { // allumage : la ligne 0 repart du balayage de l'OAM
			m_ly = 0;
			m_windowLine = 0;
			SetMode(2);
			scheduler.Adjust(m_timer, kOamDots * kTicksPerDot);
		}
		break;
	}
	case 0xFF41:
		m_stat = uint8_t((value & 0x78) | (m_stat & 0x07));
		UpdateStatLine();
		break;
	case 0xFF42: m_scy = value; break;
	case 0xFF43: m_scx = value; break;
	case 0xFF44: break; // LY en lecture seule
	case 0xFF45:
		m_lyc = value;
		UpdateStatLine();
		break;
	case 0xFF47: m_bgp = value; break;
	case 0xFF48: m_obp0 = value; break;
	case 0xFF49: m_obp1 = value; break;
	case 0xFF4A: m_wy = value; break;
	case 0xFF4B: m_wx = value; break;
	case 0xFF4F:
		if (m_color) {
			m_vbk = value & 1;
			MapVram();
		}
		break;
	case 0xFF68:
		if (m_color)
			m_bgpi = value & 0xBF;
		break;
	case 0xFF69:
		if (m_color) {
			m_bgpd[m_bgpi & 0x3F] = value;
			if (m_bgpi & 0x80)
				m_bgpi = uint8_t(0x80 | ((m_bgpi + 1) & 0x3F));
		}
		break;
	case 0xFF6A:
		if (m_color)
			m_obpi = value & 0xBF;
		break;
	case 0xFF6B:
		if (m_color) {
			m_obpd[m_obpi & 0x3F] = value;
			if (m_obpi & 0x80)
				m_obpi = uint8_t(0x80 | ((m_obpi + 1) & 0x3F));
		}
		break;
	default: break;
	}
}

uint32_t Ppu::Shade(uint8_t palette, uint8_t index) const noexcept {
	// Palette verte classique de la Game Boy d'origine, de la plus claire à la plus foncée.
	static constexpr uint32_t kShades[4] = {emu::Rgb(0x9B, 0xBC, 0x0F), emu::Rgb(0x8B, 0xAC, 0x0F), emu::Rgb(0x30, 0x62, 0x30),
											emu::Rgb(0x0F, 0x38, 0x0F)};
	return kShades[(palette >> (index * 2)) & 3];
}

uint32_t Ppu::ColorOf(const std::array<uint8_t, 0x40> &data, unsigned palette, unsigned index) noexcept {
	const size_t at = palette * 8 + index * 2;
	const unsigned raw = unsigned(data[at] | (data[at + 1] << 8));
	const unsigned r = raw & 0x1F, g = (raw >> 5) & 0x1F, b = (raw >> 10) & 0x1F;
	return emu::Rgb(uint8_t(r * 255 / 31), uint8_t(g * 255 / 31), uint8_t(b * 255 / 31));
}

void Ppu::RenderLine(int y) {
	if (y < 0 || y >= kScreenHeight)
		return;
	uint32_t *line = m_screen.Pixels().data() + size_t(y) * kScreenWidth;
	const uint8_t *bank0 = m_vram.data(), *bank1 = m_vram.data() + 0x2000;
	// Indice brut (0 à 3) et priorité du fond par pixel : les objets s'y comparent.
	std::array<uint8_t, kScreenWidth> bgIndex{};
	std::array<bool, kScreenWidth> bgPriority{};

	// Pixel d'une tuile du fond ou de la fenêtre aux coordonnées (px, py) de sa carte.
	auto tilePixel = [&](unsigned mapBase, unsigned px, unsigned py, uint8_t &attr) -> uint8_t {
		const unsigned mapIndex = mapBase + (py / 8) * 32 + (px / 8);
		const uint8_t tile = bank0[mapIndex];
		attr = m_color ? bank1[mapIndex] : 0;
		unsigned tx = px & 7, ty = py & 7;
		if (attr & 0x20)
			tx = 7 - tx;
		if (attr & 0x40)
			ty = 7 - ty;
		// LCDC.4 : tuiles en 0x8000 (indice non signé) ou en 0x9000 (indice signé).
		const unsigned base = (m_lcdc & kLcdcTileData) ? unsigned(tile) * 16 : unsigned(0x1000 + int8_t(tile) * 16);
		const uint8_t *data = ((attr & 0x08) ? bank1 : bank0) + base + ty * 2;
		const unsigned shift = 7 - tx;
		return uint8_t(((data[0] >> shift) & 1) | (((data[1] >> shift) & 1) << 1));
	};

	// En couleur, LCDC.0 ne masque pas le fond : il retire sa priorité sur les objets.
	const bool bgVisible = m_color || (m_lcdc & kLcdcBg);
	if (bgVisible) {
		const unsigned mapBase = (m_lcdc & kLcdcBgMap) ? 0x1C00 : 0x1800;
		for (int x = 0; x < kScreenWidth; ++x) {
			uint8_t attr = 0;
			const uint8_t index = tilePixel(mapBase, unsigned(x + m_scx) & 0xFF, unsigned(y + m_scy) & 0xFF, attr);
			bgIndex[size_t(x)] = index;
			bgPriority[size_t(x)] = (attr & 0x80) != 0;
			line[x] = m_color ? ColorOf(m_bgpd, attr & 7, index) : Shade(m_bgp, index);
		}
	} else {
		std::fill_n(line, kScreenWidth, Shade(0, 0));
	}

	// Fenêtre : elle a sa propre ligne interne, qui n'avance que sur les lignes où elle apparaît.
	if ((m_lcdc & kLcdcWindow) && bgVisible && y >= m_wy && m_wx <= 166) {
		const unsigned mapBase = (m_lcdc & kLcdcWindowMap) ? 0x1C00 : 0x1800;
		const int start = int(m_wx) - 7;
		for (int x = std::max(start, 0); x < kScreenWidth; ++x) {
			uint8_t attr = 0;
			const uint8_t index = tilePixel(mapBase, unsigned(x - start), m_windowLine, attr);
			bgIndex[size_t(x)] = index;
			bgPriority[size_t(x)] = (attr & 0x80) != 0;
			line[x] = m_color ? ColorOf(m_bgpd, attr & 7, index) : Shade(m_bgp, index);
		}
		++m_windowLine;
	}

	if (!(m_lcdc & kLcdcObj))
		return;
	// Objets : les 10 premiers de l'OAM présents sur la ligne. Priorité : ordre de l'OAM en
	// couleur ; abscisse puis ordre de l'OAM sur la Game Boy d'origine.
	struct Sprite {
		int y, x;
		uint8_t tile, flags;
	};
	const int height = (m_lcdc & kLcdcObjTall) ? 16 : 8;
	std::array<Sprite, 10> sprites{};
	size_t count = 0;
	for (size_t i = 0; i < 40 && count < sprites.size(); ++i) {
		const int top = int(m_oam[i * 4]) - 16;
		if (y >= top && y < top + height)
			sprites[count++] = {top, int(m_oam[i * 4 + 1]) - 8, m_oam[i * 4 + 2], m_oam[i * 4 + 3]};
	}
	if (!m_color)
		std::stable_sort(sprites.begin(), sprites.begin() + std::ptrdiff_t(count), [](const Sprite &a, const Sprite &b) { return a.x < b.x; });
	std::array<bool, kScreenWidth> taken{};
	for (size_t i = 0; i < count; ++i) {
		const Sprite &s = sprites[i];
		int row = y - s.y;
		if (s.flags & 0x40)
			row = height - 1 - row;
		const unsigned tile = height == 16 ? unsigned(s.tile & 0xFE) : s.tile;
		const uint8_t *data = ((m_color && (s.flags & 0x08)) ? bank1 : bank0) + tile * 16 + unsigned(row) * 2;
		for (int px = 0; px < 8; ++px) {
			const int x = s.x + px;
			if (x < 0 || x >= kScreenWidth || taken[size_t(x)])
				continue;
			const unsigned shift = (s.flags & 0x20) ? unsigned(px) : unsigned(7 - px);
			const uint8_t index = uint8_t(((data[0] >> shift) & 1) | (((data[1] >> shift) & 1) << 1));
			if (index == 0)
				continue;
			taken[size_t(x)] = true; // l'objet prioritaire occupe le pixel, même caché par le fond
			const bool bgOpaque = bgIndex[size_t(x)] != 0;
			const bool behind = m_color ? ((m_lcdc & kLcdcBg) && bgOpaque && (bgPriority[size_t(x)] || (s.flags & 0x80)))
										: ((s.flags & 0x80) && bgOpaque && (m_lcdc & kLcdcBg));
			if (behind)
				continue;
			line[x] = m_color ? ColorOf(m_obpd, s.flags & 7, index) : Shade((s.flags & 0x10) ? m_obp1 : m_obp0, index);
		}
	}
}

} // namespace gbc
