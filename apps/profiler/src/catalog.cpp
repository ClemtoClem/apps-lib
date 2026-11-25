#include "catalog.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <diagnostics/session.hpp>
#include <sdl3/io/filesystem.hpp>

namespace profiler {

namespace fs = std::filesystem;

std::vector<fs::path> CaptureDirectories() {
	return {fs::path(sdl3::filesystem::AppDataDir("profiler").c_str()) / "captures", fs::path(diagnostics::session::DefaultDirectory())};
}

std::vector<fs::path> FindCaptures() {
	std::vector<fs::path> out;
	for (const fs::path &dir : CaptureDirectories()) {
		std::error_code ec;
		for (const auto &e : fs::directory_iterator(dir, ec))
			if (e.path().extension() == ".vcap")
				out.push_back(e.path());
	}
	std::ranges::sort(out, [](const fs::path &a, const fs::path &b) {
		std::error_code e1, e2;
		return fs::last_write_time(a, e1) > fs::last_write_time(b, e2);
	});
	return out;
}

Companions CompanionsOf(const fs::path &capture) {
	Companions c;
	fs::path base = capture;
	base.replace_extension();
	std::error_code ec;
	if (const fs::path p = base.string() + ".crash.txt"; fs::exists(p, ec))
		c.incident = p;
	if (const fs::path p = base.string() + ".log"; fs::exists(p, ec))
		c.log = p;
	return c;
}

std::string ReadText(const fs::path &path, size_t maxBytes) {
	if (path.empty())
		return {};
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f)
		return {};
	const auto size = size_t(f.tellg());
	const size_t start = size > maxBytes ? size - maxBytes : 0;
	f.seekg(std::streamoff(start));
	std::string text(size - start, '\0');
	f.read(text.data(), std::streamsize(text.size()));
	return text;
}

} // namespace profiler
