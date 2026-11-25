#include "scribe/settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>

#include <sdl3/io/filesystem.hpp>

namespace scribe {

void Settings::AddRecent(const std::filesystem::path &path) {
	RemoveRecent(path);
	recent.insert(recent.begin(), path.string());
	if (recent.size() > kMaxRecent)
		recent.resize(kMaxRecent);
}

void Settings::RemoveRecent(const std::filesystem::path &path) { std::erase(recent, path.string()); }

Settings Settings::Load(const std::filesystem::path &file) {
	Settings s;
	std::ifstream in(file);
	for (std::string line; std::getline(in, line);) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
		if (key == "language")
			s.language = value;
		else if (key == "theme")
			s.theme = value;
		else if (key == "recent" && !value.empty() && s.recent.size() < kMaxRecent)
			s.recent.push_back(value);
		else if (key == "grid")
			s.grid = value == "1";
		else if (key == "gridStep") {
			char *end = nullptr;
			if (const double v = std::strtod(value.c_str(), &end); end != value.c_str() && v >= kMinGridStep && v <= 1000)
				s.gridStep = v;
		}
	}
	return s;
}

bool Settings::Save(const std::filesystem::path &file) const {
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	const std::filesystem::path tmp = file.string() + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		if (!out)
			return false;
		out << "language=" << language << "\ntheme=" << theme << "\ngrid=" << (grid ? 1 : 0) << "\ngridStep=" << std::format("{}", gridStep) << "\n";
		for (const std::string &r : recent)
			out << "recent=" << r << "\n";
		if (!out)
			return false;
	}
	std::filesystem::rename(tmp, file, ec);
	if (ec) {
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}

std::filesystem::path DataDirectory() { return std::filesystem::path(sdl3::filesystem::AppDataDir("scribe").CStr()); }

} // namespace scribe
