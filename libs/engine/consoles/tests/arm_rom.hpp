#pragma once
/**
 * @file arm_rom.hpp
 * @brief Outils de test : petit assembleur ARM / Thumb, images de cartouches GBA et NDS
 *        synthétiques (vérifier le cœur ARM sans jeu du commerce) et aides pour les machines.
 *
 * Seules les instructions utiles aux tests sont encodées (ARMv4T, plus CLZ de l'ARMv5).
 */
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <emu/emu.hpp>

namespace armtest {

/// Conditions ARM (bits 31-28).
enum class Cond : uint32_t { Eq = 0x0, Ne = 0x1, Al = 0xE };

/// Programme assemblé à partir de l'adresse `base` (adresse d'exécution).
class Program {
public:
	explicit Program(uint32_t base) : m_base(base) {}

	[[nodiscard]] uint32_t Here() const noexcept { return m_base + uint32_t(m_bytes.size()); }
	[[nodiscard]] std::span<const uint8_t> Bytes() const noexcept { return m_bytes; }

	void Word(uint32_t value) {
		for (int i = 0; i < 4; ++i)
			m_bytes.push_back(uint8_t(value >> (8 * i)));
	}
	void Half(uint16_t value) {
		m_bytes.push_back(uint8_t(value));
		m_bytes.push_back(uint8_t(value >> 8));
	}
	/// Complète jusqu'à l'adresse `address` (zéros).
	void PadTo(uint32_t address) {
		while (Here() < address)
			m_bytes.push_back(0);
	}

	// ── ARM ────────────────────────────────────────────────────────────────
	/// Charge une constante quelconque : MOV puis jusqu'à trois ORR (un octet chacun).
	void MovConst(uint32_t rd, uint32_t value) {
		bool first = true;
		for (uint32_t k = 0; k < 4; ++k) {
			const uint32_t byte = (value >> (8 * k)) & 0xFF;
			if (byte == 0)
				continue;
			const uint32_t imm12 = (((16 - 4 * k) & 0xF) << 8) | byte; // octet tourné à sa place
			Word((first ? 0xE3A00000u : (0xE3800000u | (rd << 16))) | (rd << 12) | imm12);
			first = false;
		}
		if (first)
			Word(0xE3A00000u | (rd << 12)); // MOV rd,#0
	}
	void MovImm(uint32_t rd, uint32_t imm8, Cond cond = Cond::Al) { Word((uint32_t(cond) << 28) | 0x03A00000u | (rd << 12) | imm8); }
	void Add(uint32_t rd, uint32_t rn, uint32_t rm) { Word(0xE0800000u | (rn << 16) | (rd << 12) | rm); }
	void AddImm(uint32_t rd, uint32_t rn, uint32_t imm8) { Word(0xE2800000u | (rn << 16) | (rd << 12) | imm8); }
	void Sub(uint32_t rd, uint32_t rn, uint32_t rm) { Word(0xE0400000u | (rn << 16) | (rd << 12) | rm); }
	void Mul(uint32_t rd, uint32_t rm, uint32_t rs) { Word(0xE0000090u | (rd << 16) | (rs << 8) | rm); }
	void Clz(uint32_t rd, uint32_t rm) { Word(0xE16F0F10u | (rd << 12) | rm); }
	void CmpImm(uint32_t rn, uint32_t imm8) { Word(0xE3500000u | (rn << 16) | imm8); }
	void Str(uint32_t rd, uint32_t rn, uint32_t offset = 0) { Word(0xE5800000u | (rn << 16) | (rd << 12) | offset); }
	void Ldr(uint32_t rd, uint32_t rn, uint32_t offset = 0) { Word(0xE5900000u | (rn << 16) | (rd << 12) | offset); }
	void Strb(uint32_t rd, uint32_t rn, uint32_t offset = 0) { Word(0xE5C00000u | (rn << 16) | (rd << 12) | offset); }
	void Strh(uint32_t rd, uint32_t rn, uint32_t offset = 0) {
		Word(0xE1C000B0u | (rn << 16) | (rd << 12) | ((offset >> 4) << 8) | (offset & 0xF));
	}
	void Ldrh(uint32_t rd, uint32_t rn, uint32_t offset = 0) {
		Word(0xE1D000B0u | (rn << 16) | (rd << 12) | ((offset >> 4) << 8) | (offset & 0xF));
	}
	void B(uint32_t target, Cond cond = Cond::Al) { Word((uint32_t(cond) << 28) | 0x0A000000u | (((target - (Here() + 8)) >> 2) & 0xFFFFFF)); }
	void Bl(uint32_t target) { Word(0xEB000000u | (((target - (Here() + 8)) >> 2) & 0xFFFFFF)); }
	void Bx(uint32_t rm) { Word(0xE12FFF10u | rm); }

	// ── Thumb ──────────────────────────────────────────────────────────────
	void TMov(uint32_t rd, uint32_t imm8) { Half(uint16_t(0x2000u | (rd << 8) | imm8)); }
	void TAdd(uint32_t rd, uint32_t rn, uint32_t rm) { Half(uint16_t(0x1800u | (rm << 6) | (rn << 3) | rd)); }
	void TAddImm(uint32_t rd, uint32_t imm8) { Half(uint16_t(0x3000u | (rd << 8) | imm8)); }
	void TLsl(uint32_t rd, uint32_t rm, uint32_t shift) { Half(uint16_t((shift << 6) | (rm << 3) | rd)); }
	void TMul(uint32_t rd, uint32_t rm) { Half(uint16_t(0x4340u | (rm << 3) | rd)); }
	void TStr(uint32_t rd, uint32_t rn, uint32_t offset) { Half(uint16_t(0x6000u | ((offset / 4) << 6) | (rn << 3) | rd)); }
	void TLdr(uint32_t rd, uint32_t rn, uint32_t offset) { Half(uint16_t(0x6800u | ((offset / 4) << 6) | (rn << 3) | rd)); }
	void TB(uint32_t target) { Half(uint16_t(0xE000u | (((target - (Here() + 4)) >> 1) & 0x7FF))); }

private:
	uint32_t m_base;
	std::vector<uint8_t> m_bytes;
};

inline void Put32(std::vector<uint8_t> &image, size_t offset, uint32_t value) {
	for (int i = 0; i < 4; ++i)
		image[offset + size_t(i)] = uint8_t(value >> (8 * i));
}

inline void PutText(std::vector<uint8_t> &image, size_t offset, std::string_view text) {
	for (size_t i = 0; i < text.size(); ++i)
		image[offset + i] = uint8_t(text[i]);
}

/// Cartouche GBA : `code` commence à 0x08000000 (sa première instruction saute par-dessus
/// l'en-tête), titre et code du jeu renseignés, somme de contrôle de l'en-tête correcte.
inline std::vector<uint8_t> GbaRom(const Program &code) {
	std::vector<uint8_t> image(code.Bytes().begin(), code.Bytes().end());
	image.resize(std::max<size_t>((image.size() + 3) & ~size_t(3), 0x400), 0);
	PutText(image, 0xA0, "SYNTHETIC");
	PutText(image, 0xAC, "ZTST");
	image[0xB2] = 0x96;
	uint8_t sum = 0;
	for (size_t i = 0xA0; i <= 0xBC; ++i)
		sum = uint8_t(sum - image[i]);
	image[0xBD] = uint8_t(sum - 0x19);
	return image;
}

/// CRC-16 (polynôme 0xA001) de l'en-tête d'une cartouche NDS.
inline uint16_t NdsCrc16(std::span<const uint8_t> data) {
	uint32_t crc = 0xFFFF;
	for (const uint8_t byte : data) {
		crc ^= byte;
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
	}
	return uint16_t(crc);
}

inline constexpr uint32_t kNdsArm9Base = 0x02000000, kNdsArm7Base = 0x02380000;

/// Cartouche NDS : programme ARM9 chargé en 0x02000000, ARM7 en 0x02380000 (démarrage
/// direct), en-tête complet avec son CRC-16.
inline std::vector<uint8_t> NdsRom(const Program &arm9, const Program &arm7) {
	constexpr size_t kOffset9 = 0x200, kOffset7 = 0x1000;
	std::vector<uint8_t> image(0x2000, 0);
	PutText(image, 0x00, "SYNTHETIC");
	PutText(image, 0x0C, "ZTST");
	const auto place = [&](const Program &program, size_t offset, size_t header, uint32_t base) {
		const auto bytes = program.Bytes();
		std::copy(bytes.begin(), bytes.end(), image.begin() + std::ptrdiff_t(offset));
		Put32(image, header + 0x0, uint32_t(offset));
		Put32(image, header + 0x4, base);
		Put32(image, header + 0x8, base);
		Put32(image, header + 0xC, uint32_t(bytes.size() + 3) & ~3u);
	};
	place(arm9, kOffset9, 0x20, kNdsArm9Base);
	place(arm7, kOffset7, 0x30, kNdsArm7Base);
	Put32(image, 0x80, uint32_t(image.size()));
	const uint16_t crc = NdsCrc16(std::span(image).subspan(0, 0x15E));
	image[0x15E] = uint8_t(crc);
	image[0x15F] = uint8_t(crc >> 8);
	return image;
}

/// Composantes d'un pixel d'`emu::Screen` (0xAABBGGRR).
struct Color {
	uint8_t r, g, b;
};
[[nodiscard]] inline Color Split(uint32_t pixel) { return {uint8_t(pixel), uint8_t(pixel >> 8), uint8_t(pixel >> 16)}; }

} // namespace armtest

namespace armtest {

// ── Machines ────────────────────────────────────────────────────────────────

inline bool FileExists(const std::filesystem::path &path) {
	std::error_code ec;
	return std::filesystem::is_regular_file(path, ec);
}

inline const std::filesystem::path kGbaBios = "assets/bios-firmware/nintendo-game-boy-advance/gbaBios.bin";

/// Machine à partir d'une image en mémoire (aucune sauvegarde sur disque).
inline emu::Media MediaFromImage(std::vector<uint8_t> image, bool directBoot = true) {
	emu::Media media;
	media.rom = std::move(image);
	media.options["direct-boot"] = directBoot ? "1" : "0";
	return media;
}

/// Machine à partir d'un fichier (sauvegardes dans un dossier temporaire).
inline emu::Media MediaFromFile(const std::filesystem::path &rom) {
	emu::Media media;
	media.romPath = rom;
	media.saveDirectory = std::filesystem::temp_directory_path() / "consoles_test_saves";
	return media;
}

inline uint64_t FrameHash(const emu::Screen &screen) {
	uint64_t h = 1469598103934665603ull;
	for (const uint32_t pixel : screen.Frame())
		h = (h ^ pixel) * 1099511628211ull;
	return h;
}

inline bool HasContent(std::span<const uint32_t> pixels) {
	return std::ranges::any_of(pixels, [&](uint32_t p) { return p != pixels.front(); });
}

inline uint32_t Pixel(const emu::Screen &screen, int x, int y) { return screen.Frame()[size_t(y * screen.Width() + x)]; }

inline void RunFrames(emu::Machine &machine, int count) {
	for (int i = 0; i < count; ++i)
		machine.RunFrame();
}

} // namespace armtest
