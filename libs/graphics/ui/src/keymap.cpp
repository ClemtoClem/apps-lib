#include "ui/keymap.hpp"

#include <cctype>

namespace ui {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return true;
}

std::string_view Trim(std::string_view s) noexcept {
	const size_t b = s.find_first_not_of(" \t");
	if (b == std::string_view::npos)
		return {};
	const size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

} // namespace

std::string KeyChord::ToString() const {
	if (!Valid())
		return {};
	std::string out;
	if (ctrl)
		out += "Ctrl+";
	if (shift)
		out += "Shift+";
	if (alt)
		out += "Alt+";
	if (altGr)
		out += "AltGr+";
	if (const char *name = sdl3::keyboard::KeyName(key))
		out += name;
	else
		out += '?';
	return out;
}

std::optional<KeyChord> KeyChord::FromString(std::string_view text) {
	KeyChord c;
	bool haveKey = false;
	size_t start = 0;
	for (;;) {
		const size_t plus = text.find('+', start);
		const std::string_view token = Trim(text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start));
		if (!token.empty()) {
			if (EqualsIgnoreCase(token, "ctrl"))
				c.ctrl = true;
			else if (EqualsIgnoreCase(token, "shift"))
				c.shift = true;
			else if (EqualsIgnoreCase(token, "alt"))
				c.alt = true;
			else if (EqualsIgnoreCase(token, "altgr"))
				c.altGr = true;
			else {
				c.key = sdl3::keyboard::KeyFromName(token);
				if (c.key == sdl3::keyboard::Key::UNKNOWN)
					return std::nullopt;
				haveKey = true;
			}
		}
		if (plus == std::string_view::npos)
			break;
		start = plus + 1;
	}
	return haveKey ? std::optional<KeyChord>(c) : std::nullopt;
}

void KeyMap::Add(std::string action, KeyChord chord) { bindings_.push_back({std::move(action), chord, chord}); }

const KeyMap::Binding *KeyMap::Find(std::string_view action) const noexcept {
	for (const Binding &b : bindings_)
		if (b.action == action)
			return &b;
	return nullptr;
}

KeyMap::Binding *KeyMap::Find(std::string_view action) noexcept {
	for (Binding &b : bindings_)
		if (b.action == action)
			return &b;
	return nullptr;
}

bool KeyMap::Set(std::string_view action, KeyChord chord) noexcept {
	if (Binding *b = Find(action)) {
		b->chord = chord;
		return true;
	}
	return false;
}

void KeyMap::Reset(std::string_view action) noexcept {
	if (Binding *b = Find(action))
		b->chord = b->defaultChord;
}

void KeyMap::ResetAll() noexcept {
	for (Binding &b : bindings_)
		b.chord = b.defaultChord;
}

std::string_view KeyMap::Conflict(KeyChord chord, std::string_view except) const noexcept {
	if (!chord.Valid())
		return {};
	for (const Binding &b : bindings_)
		if (b.chord == chord && b.action != except)
			return b.action;
	return {};
}

std::string_view KeyMap::Match(sdl3::keyboard::Key key, bool ctrl, bool shift, bool alt, bool altGr) const noexcept {
	for (const Binding &b : bindings_)
		if (b.chord.key == key && b.chord.ctrl == ctrl && b.chord.shift == shift && b.chord.alt == alt && b.chord.altGr == altGr)
			return b.action;
	return {};
}

std::string KeyMap::Serialize() const {
	std::string out;
	for (const Binding &b : bindings_)
		if (b.chord != b.defaultChord)
			out += b.action + " = " + b.chord.ToString() + "\n";
	return out;
}

void KeyMap::Deserialize(std::string_view text) {
	size_t pos = 0;
	while (pos <= text.size()) {
		const size_t nl = text.find('\n', pos);
		const std::string_view line = Trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
		if (!line.empty() && line.front() != '#') {
			const size_t eq = line.find('=');
			if (eq != std::string_view::npos)
				if (const auto chord = KeyChord::FromString(Trim(line.substr(eq + 1))))
					(void)Set(Trim(line.substr(0, eq)), *chord);
		}
		if (nl == std::string_view::npos)
			break;
		pos = nl + 1;
	}
}

} // namespace ui
