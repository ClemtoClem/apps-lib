#include <chrono>
// Définitions de data/archive/archive_platform.hpp
#include "archive/archive_platform.hpp"

namespace archive::platform {

namespace detail {

std::filesystem::path ToPath(const String& path) {
	return std::filesystem::path(
		std::u8string(reinterpret_cast<const char8_t*>(path.CStr()), path.GetSize()));
}

String FromPath(const std::filesystem::path& path) {
	const std::u8string text = path.u8string();
	return String(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace detail

bool IsSymlink(const String& path) {
	std::error_code error;
	return std::filesystem::is_symlink(detail::ToPath(path), error) && !error;
}

Option<String> ReadSymlink(const String& path) {
	std::error_code error;
	std::filesystem::path target = std::filesystem::read_symlink(detail::ToPath(path), error);
	if (error)
		return NONE;
	// Séparateurs d'archive : « / » partout.
	String text = detail::FromPath(target);
	return Some(text.Replace('\\', '/'));
}

Result<bool, String> CreateSymlink(const String& target, const String& linkPath, bool directoryHint) {
	std::error_code error;
	const std::filesystem::path link = detail::ToPath(linkPath);
	std::filesystem::remove(link,
							error); // un lien ou fichier existant est remplacé
	error.clear();
	if (directoryHint)
		std::filesystem::create_directory_symlink(detail::ToPath(target), link, error);
	else
		std::filesystem::create_symlink(detail::ToPath(target), link, error);
	if (error)
		return Err(String::Format("lien {} -> {} impossible : {}", linkPath.CStr(), target.CStr(),
								  error.message().c_str()));
	return Ok(true);
}

Option<PathInfo> GetPathInfo(const String &path) {
	std::error_code error;
	const std::filesystem::path p = detail::ToPath(path);
	const std::filesystem::file_status status = std::filesystem::status(p, error);
	if (error || !std::filesystem::exists(status))
		return NONE;
	PathInfo info;
	if (std::filesystem::is_regular_file(status)) {
		info.type = PathType::FILE;
		info.size = std::filesystem::file_size(p, error);
	} else if (std::filesystem::is_directory(status)) {
		info.type = PathType::DIRECTORY;
	} else {
		info.type = PathType::OTHER;
	}
	const auto time = std::filesystem::last_write_time(p, error);
	if (!error) {
		const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
		info.modifyTime = std::chrono::duration_cast<std::chrono::nanoseconds>(system.time_since_epoch()).count();
	}
	return Option<PathInfo>(info);
}

bool Remove(const String &path) {
	std::error_code error;
	return std::filesystem::remove(detail::ToPath(path), error) && !error;
}

bool Rename(const String &from, const String &to) {
	std::error_code error;
	std::filesystem::rename(detail::ToPath(from), detail::ToPath(to), error);
	return !error;
}

bool CreateDirectory(const String &path) {
	std::error_code error;
	const std::filesystem::path p = detail::ToPath(path);
	std::filesystem::create_directories(p, error);
	return std::filesystem::is_directory(p, error);
}

bool EnumerateDirectory(const String &path, const std::function<bool(const char *, const char *)> &visit) {
	std::error_code error;
	std::filesystem::directory_iterator it(detail::ToPath(path), error);
	if (error)
		return false;
	for (const auto &entry : it) {
		const String name = detail::FromPath(entry.path().filename());
		if (!visit(path.CStr(), name.CStr()))
			break;
	}
	return true;
}

int64_t CurrentTimeNs() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace archive::platform
