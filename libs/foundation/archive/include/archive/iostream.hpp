#pragma once
/**
 * @file iostream.hpp
 * @brief Flux binaire à accès direct du module `archive`, en C++ standard : fichier
 *        (`std::FILE`), mémoire fixe, mémoire en lecture seule, mémoire extensible, ou
 *        implémentation fournie (`IOStreamImpl`).
 *
 * Remplace `sdl3::IOStream` (même vocabulaire : `FromFile`, `ReadU32Le`, `Seek`, `Tell`,
 * `GetSize`…) pour que le module ne dépende que de la couche foundation. Les lectures
 * typées imposent l'ordre d'octets par leur nom (`Le`/`Be`), jamais celui de l'hôte.
 */
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <core/core.hpp>

namespace archive {

using Sint64 = std::int64_t;

enum class IOWhence : int { SeekSet = SEEK_SET, SEEK_CURRENT = SEEK_CUR, SeekEnd = SEEK_END };

enum class IOStatus : int {
	READY,	   ///< tout va bien
	ERROR,	   ///< erreur de lecture/écriture
	END,	   ///< fin des données
	NOT_READY, ///< pas de données pour l'instant
	READONLY,  ///< écriture sur un flux en lecture seule
	WRITEONLY  ///< lecture sur un flux en écriture seule
};

/// Implémentation d'un flux (fichier, mémoire, décodeur…).
class IOStreamImpl {
public:
	virtual ~IOStreamImpl() = default;
	/// Taille totale en octets, −1 si inconnue.
	[[nodiscard]] virtual Sint64 Size() { return -1; }
	/// Nouvelle position, −1 en cas d'échec.
	virtual Sint64 Seek(Sint64 offset, IOWhence whence);
	virtual size_t Read(void *buffer, size_t size, IOStatus &status);
	virtual size_t Write(const void *buffer, size_t size, IOStatus &status);
	virtual bool Flush(IOStatus &status);
	virtual bool Close() { return true; }
};

class IOStream {
public:
	IOStream() = default;
	explicit IOStream(std::unique_ptr<IOStreamImpl> impl) noexcept : m_impl(std::move(impl)) {}
	IOStream(IOStream &&) noexcept = default;
	IOStream &operator=(IOStream &&) noexcept = default;
	~IOStream();

	/// `mode` de `std::fopen` (« rb », « wb », « r+b »…).
	[[nodiscard]] static Result<IOStream, StringView> FromFile(const String &path, const char *mode = "rb");
	/// Mémoire fixe en lecture et écriture (non possédée).
	[[nodiscard]] static Result<IOStream, StringView> FromMemory(void *memory, size_t size);
	/// Mémoire en lecture seule (non possédée : elle doit survivre au flux).
	[[nodiscard]] static Result<IOStream, StringView> FromConstMemory(const void *memory, size_t size);
	/// Mémoire extensible possédée par le flux (`DynamicMemoryBytes` en rend une copie).
	[[nodiscard]] static Result<IOStream, StringView> FromDynamicMemory();
	[[nodiscard]] static Result<IOStream, StringView> FromImpl(std::unique_ptr<IOStreamImpl> impl);

	[[nodiscard]] IOStreamImpl *Get() const noexcept { return m_impl.get(); }
	[[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

	size_t Read(void *buffer, size_t size) noexcept;
	size_t Write(const void *buffer, size_t size) noexcept;
	template <typename T> size_t Read(std::span<T> buffer) noexcept { return Read(buffer.data(), buffer.size_bytes()) / sizeof(T); }
	template <typename T> size_t Write(std::span<const T> buffer) noexcept {
		return Write(buffer.data(), buffer.size_bytes()) / sizeof(T);
	}
	Sint64 Seek(Sint64 offset, IOWhence whence = IOWhence::SeekSet) noexcept;
	[[nodiscard]] Sint64 Tell() const noexcept;
	[[nodiscard]] Sint64 GetSize() const noexcept;
	[[nodiscard]] IOStatus Status() const noexcept { return m_status; }

	Option<uint8_t> ReadU8() noexcept;
	Option<uint16_t> ReadU16Le() noexcept;
	Option<uint32_t> ReadU32Le() noexcept;
	Option<uint64_t> ReadU64Le() noexcept;
	Option<uint16_t> ReadU16Be() noexcept;
	Option<uint32_t> ReadU32Be() noexcept;
	Option<uint64_t> ReadU64Be() noexcept;
	bool WriteU8(uint8_t v) noexcept;
	bool WriteU16Le(uint16_t v) noexcept;
	bool WriteU32Le(uint32_t v) noexcept;
	bool WriteU64Le(uint64_t v) noexcept;
	bool WriteU16Be(uint16_t v) noexcept;
	bool WriteU32Be(uint32_t v) noexcept;
	bool WriteU64Be(uint64_t v) noexcept;

	[[nodiscard]] bool ReadExact(void *buffer, size_t size) noexcept { return Read(buffer, size) == size; }
	[[nodiscard]] bool WriteExact(const void *buffer, size_t size) noexcept { return Write(buffer, size) == size; }
	bool Flush() noexcept;
	/// Ferme le flux (écrit ce qui reste) ; faux si la fermeture a échoué.
	bool Close() noexcept;
	/// Contenu d'un flux `FromDynamicMemory` (vide pour les autres).
	[[nodiscard]] std::vector<uint8_t> DynamicMemoryBytes() const;
	/// Le reste du flux depuis la position courante.
	[[nodiscard]] std::vector<uint8_t> ReadAll() noexcept;

private:
	template <typename T, bool Big> Option<T> ReadInt() noexcept;
	template <typename T, bool Big> bool WriteInt(T value) noexcept;

	std::unique_ptr<IOStreamImpl> m_impl;
	IOStatus m_status = IOStatus::READY;
};

} // namespace archive
