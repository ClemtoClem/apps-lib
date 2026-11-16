// Définitions de sdl3/storage.hpp
#include "sdl3/io/storage.hpp"

namespace sdl3 {

// ── Storage ──────────────────────────────────────────────────────────────────

Result<Storage, StringView> Storage::OpenTitle(const String &overridePath, SDL_PropertiesID props) {
	auto *s = SDL_OpenTitleStorage(overridePath.IsEmpty() ? nullptr : overridePath.c_str(), props);
	if (!s)
		return Err(GetError());
	return Ok(Storage(s));
}

Result<Storage, StringView> Storage::OpenUser(const String &org, const String &app, SDL_PropertiesID props) {
	auto *s = SDL_OpenUserStorage(org.c_str(), app.c_str(), props);
	if (!s)
		return Err(GetError());
	return Ok(Storage(s));
}

Result<Storage, StringView> Storage::OpenFile(const String &path) {
	auto *s = SDL_OpenFileStorage(path.c_str());
	if (!s)
		return Err(GetError());
	return Ok(Storage(s));
}

Result<Storage, StringView> Storage::OpenCustom(const SDL_StorageInterface *iface, void *userdata) {
	auto *s = SDL_OpenStorage(iface, userdata);
	if (!s)
		return Err(GetError());
	return Ok(Storage(s));
}

Result<uint64_t, StringView> Storage::FileSize(const String &path) const {
	uint64_t len = 0;
	if (!mHandle || !SDL_GetStorageFileSize(mHandle, path.c_str(), &len))
		return Err(GetError());
	return Ok(len);
}

Result<std::vector<uint8_t>, StringView> Storage::ReadFile(const String &path) const {
	auto sizeRes = FileSize(path);
	if (sizeRes.IsError())
		return Err(sizeRes.UnwrapError());

	std::vector<uint8_t> buf(size_t(sizeRes.Unwrap()));
	if (!buf.empty() && !SDL_ReadStorageFile(mHandle, path.c_str(), buf.data(), uint64_t(buf.size())))
		return Err(GetError());
	return Ok(std::move(buf));
}

bool Storage::WriteFile(const String &path, const void *data, size_t len) {
	return mHandle && SDL_WriteStorageFile(mHandle, path.c_str(), data, uint64_t(len));
}

bool Storage::WriteFile(const String &path, const std::vector<uint8_t> &data) {
	return WriteFile(path, data.data(), data.size());
}

bool Storage::CreateDirectory(const String &path) {
	return mHandle && SDL_CreateStorageDirectory(mHandle, path.c_str());
}

bool Storage::EnumerateDirectory(const String &path, std::function<bool(const char *, const char *)> fn) const {
	if (!mHandle)
		return false;
	struct Ctx {
		std::function<bool(const char *, const char *)> fn;
	};
	Ctx ctx{std::move(fn)};
	auto cb = [](void *ud, const char *dirname, const char *fname) -> SDL_EnumerationResult {
		auto *c = static_cast<Ctx *>(ud);
		return c->fn(dirname, fname) ? SDL_ENUM_CONTINUE : SDL_ENUM_SUCCESS;
	};
	return SDL_EnumerateStorageDirectory(mHandle, path.c_str(), cb, &ctx);
}

bool Storage::Rename(const String &oldPath, const String &newPath) {
	return mHandle && SDL_RenameStoragePath(mHandle, oldPath.c_str(), newPath.c_str());
}

bool Storage::Copy(const String &oldPath, const String &newPath) {
	return mHandle && SDL_CopyStorageFile(mHandle, oldPath.c_str(), newPath.c_str());
}

Option<sdl3::PathInfo> Storage::PathInfo(const String &path) const {
	SDL_PathInfo info{};
	if (!mHandle || !SDL_GetStoragePathInfo(mHandle, path.c_str(), &info))
		return NONE;
	return Some(sdl3::PathInfo(info));
}

uint64_t Storage::SpaceRemaining() const noexcept {
	return mHandle ? SDL_GetStorageSpaceRemaining(mHandle) : 0;
}

std::vector<String> Storage::Glob(const String &path, const String &pattern, bool caseInsensitive) const {
	if (!mHandle)
		return {};
	int count = 0;
	char **items = SDL_GlobStorageDirectory(mHandle, path.IsEmpty() ? nullptr : path.c_str(),
											pattern.IsEmpty() ? nullptr : pattern.c_str(),
											caseInsensitive ? SDL_GLOB_CASEINSENSITIVE : 0, &count);
	if (!items)
		return {};
	std::vector<String> out;
	out.reserve(size_t(count));
	for (int i = 0; i < count; ++i)
		out.emplace_back(items[i]);
	SDL_free(items);
	return out;
}

} // namespace sdl3
