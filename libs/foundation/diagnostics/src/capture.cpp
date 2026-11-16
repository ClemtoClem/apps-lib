#include "diagnostics/capture.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <dataio/binary.hpp>

namespace diagnostics::capture {

const std::string &Capture::Name(uint32_t id) const {
	static const std::string unknown = "?";
	return id < names.size() ? names[id] : unknown;
}

Result<Capture, std::string> Load(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return Err("impossible d'ouvrir " + path);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return Parse(bytes);
}

Result<Capture, std::string> Parse(const std::vector<uint8_t> &bytes) {
	if (bytes.size() < 20)
		return Err(std::string("capture tronquée"));
	const std::span<const uint8_t> body(bytes.data(), bytes.size() - 4);
	uint32_t crc;
	std::memcpy(&crc, bytes.data() + bytes.size() - 4, 4);
	if (crc != dataio::Crc32(body))
		return Err(std::string("CRC invalide : capture corrompue"));
	dataio::ByteReader r(body);
	if (r.Get<uint32_t>() != kMagic)
		return Err(std::string("ce n'est pas une capture .vcap"));
	if (r.Get<uint32_t>() != kVersion)
		return Err(std::string("version de capture non prise en charge"));
	Capture c;
	c.startNs = r.Get<int64_t>();
	const uint64_t nameCount = r.GetVarUint();
	for (uint64_t i = 0; i < nameCount && r.Ok(); ++i)
		c.names.push_back(r.GetString());
	const uint64_t threadCount = r.GetVarUint();
	int64_t last = 0;
	for (uint64_t ti = 0; ti < threadCount && r.Ok(); ++ti) {
		ThreadTrace t;
		t.id = r.GetVarUint();
		t.name = r.GetString();
		const uint64_t n = r.GetVarUint();
		t.events.reserve(n);
		int64_t abs = c.startNs;
		for (uint64_t i = 0; i < n && r.Ok(); ++i) {
			trace::Event e{};
			e.type = static_cast<trace::EventType>(r.GetU8());
			e.name = static_cast<uint32_t>(r.GetVarUint());
			abs += r.GetVarInt();
			e.timeNs = abs - c.startNs;
			if (e.type == trace::EventType::Counter)
				e.value = r.Get<double>();
			if (e.type == trace::EventType::Frame)
				c.frames.push_back(e.timeNs);
			last = std::max(last, e.timeNs);
			t.events.push_back(e);
		}
		c.threads.push_back(std::move(t));
	}
	if (!r.Ok())
		return Err(std::string("capture illisible"));
	std::ranges::sort(c.frames);
	c.durationNs = last;
	BuildZones(c);
	return Ok(std::move(c));
}

void BuildZones(Capture &c) {
	for (ThreadTrace &t : c.threads) {
		t.zones.clear();
		struct Open {
			uint32_t name;
			int64_t begin;
		};
		std::vector<Open> stack;
		for (const trace::Event &e : t.events) {
			if (e.type == trace::EventType::Begin) {
				stack.push_back({e.name, e.timeNs});
			} else if (e.type == trace::EventType::End && !stack.empty()) {
				const Open o = stack.back();
				stack.pop_back();
				t.zones.push_back({o.name, o.begin, e.timeNs, static_cast<uint16_t>(stack.size())});
			}
		}
		for (auto it = stack.rbegin(); it != stack.rend(); ++it) // zones non fermées : jusqu'à la fin
			t.zones.push_back({it->name, it->begin, c.durationNs, static_cast<uint16_t>(stack.size() - 1)});
		std::ranges::sort(t.zones, [](const ZoneSpan &a, const ZoneSpan &b) { return a.begin < b.begin || (a.begin == b.begin && a.depth < b.depth); });
	}
}

} // namespace diagnostics::capture
