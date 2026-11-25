#include "archive/iostream.hpp"

#include <algorithm>
#include <cstring>

namespace archive {

// ── IOStreamImpl : comportements par défaut ─────────────────────────────────

Sint64 IOStreamImpl::Seek(Sint64, IOWhence) { return -1; }

size_t IOStreamImpl::Read(void *, size_t, IOStatus &status) {
	status = IOStatus::WRITEONLY;
	return 0;
}

size_t IOStreamImpl::Write(const void *, size_t, IOStatus &status) {
	status = IOStatus::READONLY;
	return 0;
}

bool IOStreamImpl::Flush(IOStatus &) { return true; }

namespace {

/// Fichier sur disque.
class FileImpl final : public IOStreamImpl {
public:
	explicit FileImpl(std::FILE *file) noexcept : m_file(file) {}
	~FileImpl() override { (void)Close(); }
	Sint64 Size() override {
		if (!m_file)
			return -1;
		const long at = std::ftell(m_file);
		std::fseek(m_file, 0, SEEK_END);
		const long size = std::ftell(m_file);
		std::fseek(m_file, at, SEEK_SET);
		return size;
	}
	Sint64 Seek(Sint64 offset, IOWhence whence) override {
		if (!m_file || std::fseek(m_file, long(offset), int(whence)) != 0)
			return -1;
		return std::ftell(m_file);
	}
	size_t Read(void *buffer, size_t size, IOStatus &status) override {
		const size_t read = m_file ? std::fread(buffer, 1, size, m_file) : 0;
		if (read < size)
			status = (m_file && std::ferror(m_file)) ? IOStatus::ERROR : IOStatus::END;
		return read;
	}
	size_t Write(const void *buffer, size_t size, IOStatus &status) override {
		const size_t written = m_file ? std::fwrite(buffer, 1, size, m_file) : 0;
		if (written < size)
			status = IOStatus::ERROR;
		return written;
	}
	bool Flush(IOStatus &status) override {
		if (m_file && std::fflush(m_file) == 0)
			return true;
		status = IOStatus::ERROR;
		return false;
	}
	bool Close() override {
		if (!m_file)
			return true;
		const bool ok = std::fclose(m_file) == 0;
		m_file = nullptr;
		return ok;
	}

private:
	std::FILE *m_file; // possédé
};

/// Mémoire fixe (non possédée), éventuellement en lecture seule.
class MemoryImpl final : public IOStreamImpl {
public:
	MemoryImpl(uint8_t *data, const uint8_t *readOnly, size_t size) noexcept : m_data(data), m_readOnly(readOnly), m_size(size) {}
	Sint64 Size() override { return Sint64(m_size); }
	Sint64 Seek(Sint64 offset, IOWhence whence) override {
		const Sint64 base = whence == IOWhence::SeekSet ? 0 : (whence == IOWhence::SEEK_CURRENT ? Sint64(m_position) : Sint64(m_size));
		const Sint64 target = base + offset;
		if (target < 0 || target > Sint64(m_size))
			return -1;
		m_position = size_t(target);
		return target;
	}
	size_t Read(void *buffer, size_t size, IOStatus &status) override {
		const size_t count = std::min(size, m_size - m_position);
		std::memcpy(buffer, Bytes() + m_position, count);
		m_position += count;
		if (count < size)
			status = IOStatus::END;
		return count;
	}
	size_t Write(const void *buffer, size_t size, IOStatus &status) override {
		if (!m_data) {
			status = IOStatus::READONLY;
			return 0;
		}
		const size_t count = std::min(size, m_size - m_position);
		std::memcpy(m_data + m_position, buffer, count);
		m_position += count;
		if (count < size)
			status = IOStatus::ERROR;
		return count;
	}

private:
	[[nodiscard]] const uint8_t *Bytes() const noexcept { return m_data ? m_data : m_readOnly; }
	uint8_t *m_data;
	const uint8_t *m_readOnly;
	size_t m_size, m_position = 0;
};

/// Mémoire extensible possédée.
class DynamicImpl final : public IOStreamImpl {
public:
	Sint64 Size() override { return Sint64(m_bytes.size()); }
	Sint64 Seek(Sint64 offset, IOWhence whence) override {
		const Sint64 base = whence == IOWhence::SeekSet ? 0 : (whence == IOWhence::SEEK_CURRENT ? Sint64(m_position) : Sint64(m_bytes.size()));
		const Sint64 target = base + offset;
		if (target < 0)
			return -1;
		m_position = size_t(target);
		return target;
	}
	size_t Read(void *buffer, size_t size, IOStatus &status) override {
		const size_t count = m_position < m_bytes.size() ? std::min(size, m_bytes.size() - m_position) : 0;
		if (count)
			std::memcpy(buffer, m_bytes.data() + m_position, count);
		m_position += count;
		if (count < size)
			status = IOStatus::END;
		return count;
	}
	size_t Write(const void *buffer, size_t size, IOStatus &) override {
		if (m_position + size > m_bytes.size())
			m_bytes.resize(m_position + size); // une écriture après la fin complète par des zéros
		if (size)
			std::memcpy(m_bytes.data() + m_position, buffer, size);
		m_position += size;
		return size;
	}
	[[nodiscard]] const std::vector<uint8_t> &Bytes() const noexcept { return m_bytes; }

private:
	std::vector<uint8_t> m_bytes;
	size_t m_position = 0;
};

} // namespace

// ── IOStream ────────────────────────────────────────────────────────────────

IOStream::~IOStream() { (void)Close(); }

Result<IOStream, StringView> IOStream::FromFile(const String &path, const char *mode) {
	std::FILE *file = path.IsEmpty() ? nullptr : std::fopen(path.CStr(), mode);
	if (!file)
		return Err(StringView("ouverture du fichier impossible"));
	return Ok(IOStream(std::make_unique<FileImpl>(file)));
}

Result<IOStream, StringView> IOStream::FromMemory(void *memory, size_t size) {
	if (!memory && size)
		return Err(StringView("mémoire nulle"));
	return Ok(IOStream(std::make_unique<MemoryImpl>(static_cast<uint8_t *>(memory), nullptr, size)));
}

Result<IOStream, StringView> IOStream::FromConstMemory(const void *memory, size_t size) {
	if (!memory && size)
		return Err(StringView("mémoire nulle"));
	return Ok(IOStream(std::make_unique<MemoryImpl>(nullptr, static_cast<const uint8_t *>(memory), size)));
}

Result<IOStream, StringView> IOStream::FromDynamicMemory() { return Ok(IOStream(std::make_unique<DynamicImpl>())); }

Result<IOStream, StringView> IOStream::FromImpl(std::unique_ptr<IOStreamImpl> impl) {
	if (!impl)
		return Err(StringView("implémentation nulle"));
	return Ok(IOStream(std::move(impl)));
}

size_t IOStream::Read(void *buffer, size_t size) noexcept {
	if (!m_impl || size == 0)
		return 0;
	m_status = IOStatus::READY;
	return m_impl->Read(buffer, size, m_status);
}

size_t IOStream::Write(const void *buffer, size_t size) noexcept {
	if (!m_impl || size == 0)
		return 0;
	m_status = IOStatus::READY;
	return m_impl->Write(buffer, size, m_status);
}

Sint64 IOStream::Seek(Sint64 offset, IOWhence whence) noexcept { return m_impl ? m_impl->Seek(offset, whence) : -1; }

Sint64 IOStream::Tell() const noexcept { return m_impl ? m_impl->Seek(0, IOWhence::SEEK_CURRENT) : -1; }

Sint64 IOStream::GetSize() const noexcept { return m_impl ? m_impl->Size() : -1; }

bool IOStream::Flush() noexcept { return m_impl && m_impl->Flush(m_status); }

bool IOStream::Close() noexcept {
	if (!m_impl)
		return true;
	const bool ok = m_impl->Close();
	m_impl.reset();
	return ok;
}

std::vector<uint8_t> IOStream::DynamicMemoryBytes() const {
	if (const auto *dynamic = dynamic_cast<const DynamicImpl *>(m_impl.get()))
		return dynamic->Bytes();
	return {};
}

std::vector<uint8_t> IOStream::ReadAll() noexcept {
	std::vector<uint8_t> out;
	uint8_t chunk[65536];
	for (;;) {
		const size_t read = Read(chunk, sizeof(chunk));
		out.insert(out.end(), chunk, chunk + read);
		if (read < sizeof(chunk))
			break;
	}
	return out;
}

template <typename T, bool Big> Option<T> IOStream::ReadInt() noexcept {
	uint8_t bytes[sizeof(T)];
	if (!ReadExact(bytes, sizeof(T)))
		return NONE;
	T value = 0;
	for (size_t i = 0; i < sizeof(T); ++i)
		value = T(value | (T(bytes[Big ? i : sizeof(T) - 1 - i]) << (8 * (sizeof(T) - 1 - i))));
	return Option<T>(value);
}

template <typename T, bool Big> bool IOStream::WriteInt(T value) noexcept {
	uint8_t bytes[sizeof(T)];
	for (size_t i = 0; i < sizeof(T); ++i) {
		const uint8_t byte = uint8_t(value >> (8 * i));
		bytes[Big ? sizeof(T) - 1 - i : i] = byte;
	}
	return WriteExact(bytes, sizeof(T));
}

Option<uint8_t> IOStream::ReadU8() noexcept { return ReadInt<uint8_t, false>(); }
Option<uint16_t> IOStream::ReadU16Le() noexcept { return ReadInt<uint16_t, false>(); }
Option<uint32_t> IOStream::ReadU32Le() noexcept { return ReadInt<uint32_t, false>(); }
Option<uint64_t> IOStream::ReadU64Le() noexcept { return ReadInt<uint64_t, false>(); }
Option<uint16_t> IOStream::ReadU16Be() noexcept { return ReadInt<uint16_t, true>(); }
Option<uint32_t> IOStream::ReadU32Be() noexcept { return ReadInt<uint32_t, true>(); }
Option<uint64_t> IOStream::ReadU64Be() noexcept { return ReadInt<uint64_t, true>(); }
bool IOStream::WriteU8(uint8_t v) noexcept { return WriteInt<uint8_t, false>(v); }
bool IOStream::WriteU16Le(uint16_t v) noexcept { return WriteInt<uint16_t, false>(v); }
bool IOStream::WriteU32Le(uint32_t v) noexcept { return WriteInt<uint32_t, false>(v); }
bool IOStream::WriteU64Le(uint64_t v) noexcept { return WriteInt<uint64_t, false>(v); }
bool IOStream::WriteU16Be(uint16_t v) noexcept { return WriteInt<uint16_t, true>(v); }
bool IOStream::WriteU32Be(uint32_t v) noexcept { return WriteInt<uint32_t, true>(v); }
bool IOStream::WriteU64Be(uint64_t v) noexcept { return WriteInt<uint64_t, true>(v); }

} // namespace archive
