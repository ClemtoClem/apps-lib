// Définitions de sdl3/tray.hpp
#include "sdl3/io/tray.hpp"

namespace sdl3 {

// ── TrayEntry ────────────────────────────────────────────────────────────────

void TrayEntry::SetLabel(const String &label) noexcept {
	if (mHandle)
		SDL_SetTrayEntryLabel(mHandle, label.c_str());
}

void TrayEntry::SetChecked(bool checked) noexcept {
	if (mHandle)
		SDL_SetTrayEntryChecked(mHandle, checked);
}

void TrayEntry::SetEnabled(bool enabled) noexcept {
	if (mHandle)
		SDL_SetTrayEntryEnabled(mHandle, enabled);
}

void TrayEntry::Click() noexcept {
	if (mHandle)
		SDL_ClickTrayEntry(mHandle);
}

void TrayEntry::Remove() noexcept {
	if (mHandle) {
		SDL_RemoveTrayEntry(mHandle);
		mHandle = nullptr;
	}
}

void TrayEntry::SetCallback(Callback cb) {
	if (!mHandle)
		return;
	auto *ctx = new Callback(std::move(cb));
	SDL_SetTrayEntryCallback(
		mHandle,
		[](void *userdata, SDL_TrayEntry *entry) { (*static_cast<Callback *>(userdata))(TrayEntry(entry)); }, ctx);
}

// ── TrayMenu ─────────────────────────────────────────────────────────────────

std::vector<TrayEntry> TrayMenu::Entries() const {
	if (!mHandle)
		return {};
	int count = 0;
	const SDL_TrayEntry **raw = SDL_GetTrayEntries(mHandle, &count);
	std::vector<TrayEntry> out;
	out.reserve(size_t(count));
	for (int i = 0; i < count; ++i)
		out.emplace_back(const_cast<SDL_TrayEntry *>(raw[i]));
	return out;
}

TrayEntry TrayMenu::InsertAt(int pos, const String &label, TrayEntryFlags flags) {
	return TrayEntry(mHandle ? SDL_InsertTrayEntryAt(mHandle, pos, label.CStr(), detail::ToSDL(flags)) : nullptr);
}

Option<TrayEntry> TrayMenu::ParentEntry() const {
	if (!mHandle)
		return NONE;
	auto *e = SDL_GetTrayMenuParentEntry(mHandle);
	if (!e)
		return NONE;
	return Some(TrayEntry(e));
}

Option<TrayMenu> TrayEntry::Submenu() const {
	if (!mHandle)
		return NONE;
	auto *m = SDL_GetTraySubmenu(mHandle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

Option<TrayMenu> TrayEntry::Parent() const {
	if (!mHandle)
		return NONE;
	auto *m = SDL_GetTrayEntryParent(mHandle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

// ── Tray ─────────────────────────────────────────────────────────────────────

Result<Tray, Error> Tray::Create(const Surface &icon, const String &tooltip) {
	auto *t = SDL_CreateTray(icon.Get(), tooltip.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Tray(t));
}

Result<Tray, StringView> Tray::CreateWithoutIcon(const String &tooltip) {
	auto *t = SDL_CreateTray(nullptr, tooltip.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Tray(t));
}

void Tray::SetIcon(const Surface &icon) noexcept {
	if (mHandle)
		SDL_SetTrayIcon(mHandle, icon.Get());
}

void Tray::SetTooltip(const String &tooltip) noexcept {
	if (mHandle)
		SDL_SetTrayTooltip(mHandle, tooltip.c_str());
}

Option<TrayMenu> Tray::Menu() const {
	if (!mHandle)
		return NONE;
	auto *m = SDL_GetTrayMenu(mHandle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

} // namespace sdl3
