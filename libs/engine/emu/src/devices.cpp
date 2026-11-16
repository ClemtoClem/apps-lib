// Périphériques génériques : écran, sortie son, port d'entrée, mémoire non volatile,
// registre des systèmes.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>

#include "emu/input.hpp"
#include "emu/machine.hpp"
#include "emu/nvram.hpp"
#include "emu/screen.hpp"
#include "emu/sound.hpp"
#include "emu/state.hpp"
#include "emu/system.hpp"

#include <archive/archive.hpp>

namespace emu {

// ── Screen ──────────────────────────────────────────────────────────────────

Screen::Screen(Machine &machine, std::string tag, Device *owner, int width, int height, bool primary)
	: Device(machine, std::move(tag), owner), m_width(std::max(width, 1)), m_height(std::max(height, 1)), m_primary(primary),
	  m_back(size_t(m_width) * size_t(m_height), Rgb(0, 0, 0)), m_front(m_back) {}

void Screen::EndFrame() {
	m_front = m_back; // copie : le dessin suivant peut repartir de l'image précédente
	++m_frameNumber;
	if (m_primary)
		GetMachine().SignalFrame();
}

void Screen::Reset() {
	std::ranges::fill(m_back, Rgb(0, 0, 0));
	m_front = m_back;
	m_frameNumber = 0;
}

void Screen::State(StateArchive &archive) {
	archive.Io(m_frameNumber);
	archive.Io(std::span<uint32_t>(m_front));
	if (!archive.Saving())
		m_back = m_front;
}

// ── SoundStream ─────────────────────────────────────────────────────────────

SoundStream::SoundStream(Machine &machine, std::string tag, Device *owner, int sampleRate, size_t capacityFrames)
	: Device(machine, std::move(tag), owner), m_sampleRate(std::max(sampleRate, 1)), m_ring(std::max<size_t>(capacityFrames, 16) * 2) {}

void SoundStream::Push(int16_t left, int16_t right) {
	const std::scoped_lock lock(m_mutex);
	const size_t capacity = m_ring.size() / 2;
	if (m_count == capacity) { // file pleine : la plus ancienne trame est perdue
		m_head = (m_head + 1) % capacity;
		--m_count;
	}
	const size_t at = (m_head + m_count) % capacity;
	m_ring[at * 2] = left;
	m_ring[at * 2 + 1] = right;
	++m_count;
	++m_total;
}

size_t SoundStream::Pull(std::span<int16_t> out) {
	const std::scoped_lock lock(m_mutex);
	const size_t capacity = m_ring.size() / 2, wanted = out.size() / 2;
	const size_t frames = std::min(wanted, m_count);
	for (size_t i = 0; i < frames; ++i) {
		m_last[0] = out[i * 2] = m_ring[m_head * 2];
		m_last[1] = out[i * 2 + 1] = m_ring[m_head * 2 + 1];
		m_head = (m_head + 1) % capacity;
	}
	m_count -= frames;
	for (size_t i = frames; i < wanted; ++i) {
		out[i * 2] = m_last[0];
		out[i * 2 + 1] = m_last[1];
	}
	return frames;
}

size_t SoundStream::Available() const {
	const std::scoped_lock lock(m_mutex);
	return m_count;
}

void SoundStream::Clear() {
	const std::scoped_lock lock(m_mutex);
	m_head = m_count = 0;
	m_last[0] = m_last[1] = 0;
}

// ── InputPort ───────────────────────────────────────────────────────────────

InputPort::InputPort(Machine &machine, std::string tag, Device *owner) : Device(machine, std::move(tag), owner) {}

void InputPort::AddField(std::string id, uint32_t mask, std::string label) {
	if (label.empty())
		label = id;
	m_allMask |= mask;
	m_fields.push_back({std::move(id), std::move(label), mask});
}

const InputPort::Field *InputPort::FindField(std::string_view id) const noexcept {
	for (const Field &f : m_fields)
		if (f.id == id)
			return &f;
	return nullptr;
}

bool InputPort::SetPressed(std::string_view id, bool pressed) {
	const Field *field = FindField(id);
	if (!field)
		return false;
	SetMask(field->mask, pressed);
	return true;
}

void InputPort::SetMask(uint32_t mask, bool pressed) {
	const uint32_t before = m_state;
	m_state = pressed ? (m_state | (mask & m_allMask)) : (m_state & ~mask);
	if (m_state != before && m_onChange)
		m_onChange(before, m_state);
}

void InputPort::State(StateArchive &archive) { archive.Io(m_state); }

// ── TouchPanel ──────────────────────────────────────────────────────────────

TouchPanel::TouchPanel(Machine &machine, std::string tag, Device *owner, int x, int y, int width, int height)
	: Device(machine, std::move(tag), owner), m_x(x), m_y(y), m_width(std::max(width, 1)), m_height(std::max(height, 1)) {}

void TouchPanel::Set(int x, int y, bool pressed) {
	const int px = std::clamp(x, 0, m_width - 1), py = std::clamp(y, 0, m_height - 1);
	if (pressed == m_pressed && (!pressed || (px == m_px && py == m_py)))
		return;
	m_pressed = pressed;
	if (pressed) {
		m_px = px;
		m_py = py;
	}
	if (m_onChange)
		m_onChange(m_pressed, m_px, m_py);
}

void TouchPanel::Reset() {
	m_pressed = false;
	m_px = m_py = 0;
}

void TouchPanel::State(StateArchive &archive) {
	archive.Io(m_px);
	archive.Io(m_py);
	archive.Io(m_pressed);
}

// ── Nvram ───────────────────────────────────────────────────────────────────

Nvram::Nvram(Machine &machine, std::string tag, Device *owner, size_t size, uint8_t fill)
	: Device(machine, std::move(tag), owner), m_data(size, fill) {}

bool Nvram::SetPath(std::filesystem::path path) {
	m_path = std::move(path);
	std::vector<uint8_t> file = ReadFile(m_path);
	if (file.empty())
		return false;
	std::copy_n(file.begin(), std::min(file.size(), m_data.size()), m_data.begin());
	m_dirty = false;
	return true;
}

bool Nvram::Flush() {
	if (!m_dirty || m_path.empty() || m_data.empty())
		return true;
	if (!WriteFile(m_path, m_data))
		return false;
	m_dirty = false;
	return true;
}

void Nvram::State(StateArchive &archive) {
	archive.Io(std::span<uint8_t>(m_data));
	if (!archive.Saving())
		m_dirty = true; // l'état chargé remplace la sauvegarde sur pile
}

// ── Media / SystemRegistry ──────────────────────────────────────────────────

std::filesystem::path Media::SavePath(std::string_view extension) const {
	std::filesystem::path file = romPath.filename();
	file.replace_extension(extension);
	if (file.empty())
		return {};
	return (saveDirectory.empty() ? romPath.parent_path() : saveDirectory) / file;
}

std::string_view Media::Option(std::string_view key, std::string_view fallback) const {
	const auto it = options.find(key);
	return it == options.end() ? fallback : std::string_view(it->second);
}

bool SystemRegistry::Register(SystemInfo info) {
	if (info.name.empty() || Find(info.name))
		return false;
	m_systems.push_back(std::move(info));
	return true;
}

const SystemInfo *SystemRegistry::Find(std::string_view name) const noexcept {
	for (const SystemInfo &s : m_systems)
		if (s.name == name)
			return &s;
	return nullptr;
}

const SystemInfo *SystemRegistry::FindByExtension(std::string_view extension) const noexcept {
	std::string wanted(extension);
	if (!wanted.empty() && wanted.front() != '.')
		wanted.insert(wanted.begin(), '.');
	for (char &c : wanted)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	for (const SystemInfo &s : m_systems)
		if (std::ranges::find(s.extensions, wanted) != s.extensions.end())
			return &s;
	return nullptr;
}

namespace {
std::string LowerExtension(const std::filesystem::path &path) {
	std::string ext = path.extension().string();
	for (char &c : ext)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return ext;
}

/// Archive reconnue par le module `archive` (ZIP, 7z, RAR, TAR, GZIP…).
bool IsArchive(const std::filesystem::path &path) { return archive::HasArchiveExtension(String(path.string())); }

/// Indice de la première entrée acceptée par `accept` (métadonnées macOS ignorées).
template <typename Accept> std::optional<size_t> FindRomEntry(const archive::ArchiveReader &reader, Accept &&accept) {
	const std::vector<archive::EntryInfo> &entries = reader.Entries();
	for (size_t i = 0; i < entries.size(); ++i) {
		const archive::EntryInfo &entry = entries[i];
		if (entry.type == archive::EntryType::FILE && !entry.path.StartsWith("__MACOSX/") &&
			accept(LowerExtension(std::filesystem::path(std::string(entry.path)))))
			return i;
	}
	return std::nullopt;
}

using ReaderResult = Result<std::unique_ptr<archive::ArchiveReader>, std::string>;
ReaderResult OpenReader(const std::filesystem::path &path) {
	auto reader = archive::OpenArchiveFile(String(path.string()));
	if (reader.IsError())
		return Err(std::string("archive illisible (") + std::string(reader.Error().Describe()) + ") : " + path.filename().string());
	return Ok(std::move(reader.Value()));
}
} // namespace

bool SystemRegistry::IsRomFile(const std::filesystem::path &path) const {
	return IsArchive(path) || FindByExtension(LowerExtension(path)) != nullptr;
}

Result<const SystemInfo *, std::string> SystemRegistry::Identify(const std::filesystem::path &path, std::string_view system) const {
	const SystemInfo *forced = system.empty() ? nullptr : Find(system);
	if (!system.empty() && !forced)
		return Err(std::string("système inconnu : ") + std::string(system));
	if (!IsArchive(path)) {
		const SystemInfo *found = forced ? forced : FindByExtension(LowerExtension(path));
		if (!found)
			return Err(std::string("aucun système ne prend en charge « ") + path.extension().string() + " »");
		return Ok(found);
	}
	auto reader = OpenReader(path);
	if (reader.IsError())
		return Err(reader.Error());
	const auto index = FindRomEntry(*reader.Value(), [&](const std::string &ext) {
		const SystemInfo *s = FindByExtension(ext);
		return s && (!forced || s == forced);
	});
	if (!index)
		return Err(std::string("l'archive ne contient aucune ROM d'un système pris en charge : ") + path.filename().string());
	const std::string name(reader.Value()->Entries()[*index].path);
	return Ok(forced ? forced : FindByExtension(LowerExtension(std::filesystem::path(name))));
}

Result<LoadedRom, std::string> SystemRegistry::LoadRom(const std::filesystem::path &path, std::string_view system) const {
	auto identified = Identify(path, system);
	if (identified.IsError())
		return Err(identified.Error());
	LoadedRom rom;
	rom.system = identified.Value();
	if (IsArchive(path)) {
		rom.data = ReadRomImage(path, rom.system->extensions);
		auto reader = OpenReader(path);
		if (reader.IsOk())
			if (const auto index = FindRomEntry(*reader.Value(), [&](const std::string &ext) {
					return std::ranges::find(rom.system->extensions, ext) != rom.system->extensions.end();
				}))
				rom.name = std::filesystem::path(std::string(reader.Value()->Entries()[*index].path)).filename().string();
	} else {
		rom.data = ReadFile(path);
		rom.name = path.filename().string();
	}
	if (rom.data.empty())
		return Err(std::string("ROM introuvable, vide ou impossible à extraire : ") + path.string());
	return Ok(std::move(rom));
}

std::vector<uint8_t> ReadRomImage(const std::filesystem::path &path, std::span<const std::string> extensions) {
	if (!IsArchive(path))
		return ReadFile(path);
	auto reader = OpenReader(path);
	if (reader.IsError())
		return {};
	const auto index =
		FindRomEntry(*reader.Value(), [&](const std::string &ext) { return std::ranges::find(extensions, ext) != extensions.end(); });
	if (!index)
		return {};
	auto data = reader.Value()->Extract(*index);
	return data.IsOk() ? std::move(data.Value()) : std::vector<uint8_t>{};
}

std::vector<uint8_t> ReadFile(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in)
		return {};
	const std::streamoff size = in.tellg();
	if (size <= 0)
		return {};
	std::vector<uint8_t> data(static_cast<size_t>(size));
	in.seekg(0);
	in.read(reinterpret_cast<char *>(data.data()), size);
	if (!in)
		return {};
	return data;
}

bool WriteFile(const std::filesystem::path &path, std::span<const uint8_t> data) {
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
	return bool(out);
}

} // namespace emu
