#pragma once
/**
 * @file capture.hpp
 * @brief Format de capture `.vcap` (binaire) : lecture et modèle en mémoire.
 *
 * Disposition : `VCAP` u32, version u32, horodatage de départ (ns, i64),
 * table des noms (varint n, chaînes), puis par thread : id, nom, nombre
 * d'événements, événements `(type u8, nom varint, Δt varint, [valeur f64])`,
 * enfin CRC-32 de tout ce qui précède.
 */
#include <cstdint>
#include <string>
#include <vector>

#include <core/result.hpp>

#include "trace.hpp"

namespace diagnostics::capture {

struct ZoneSpan {
	uint32_t name;
	int64_t begin, end; // ns relatifs au départ
	uint16_t depth;
};

struct ThreadTrace {
	uint64_t id = 0;
	std::string name;
	std::vector<trace::Event> events; // temps relatifs au départ
	std::vector<ZoneSpan> zones;	  // reconstruites (début/fin appariés)
};

struct Capture {
	int64_t startNs = 0;
	int64_t durationNs = 0;
	std::vector<std::string> names;
	std::vector<ThreadTrace> threads;
	std::vector<int64_t> frames; // instants de fin d'image (ns)

	[[nodiscard]] const std::string &Name(uint32_t id) const;
};

constexpr uint32_t kMagic = 0x50414356; // "VCAP"
constexpr uint32_t kVersion = 1;

[[nodiscard]] Result<Capture, std::string> Load(const std::string &path);
[[nodiscard]] Result<Capture, std::string> Parse(const std::vector<uint8_t> &bytes);
/// Reconstruit `zones` de chaque thread à partir des paires Begin/End.
void BuildZones(Capture &c);

} // namespace diagnostics::capture
