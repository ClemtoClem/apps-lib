#pragma once
/**
 * @file iostream.hpp
 * @brief Flux d'entrée/sortie (SDL_iostream.h) : fichiers, mémoire, implémentations C++.
 */
#include <SDL3/SDL.h>
#include <memory>
#include <span>
#include <vector>

#include <core/core.hpp>
#include "sdl3/core/error.hpp"

namespace sdl3 {

/// Origine d'un déplacement (seek) dans un flux.
enum class IOWhence : int {
    SeekSet = SDL_IO_SEEK_SET,	  /**< depuis le début des données */
    SEEK_CURRENT = SDL_IO_SEEK_CUR, /**< relatif à la position courante */
    SeekEnd = SDL_IO_SEEK_END,	  /**< depuis la fin des données */
};

/// État d'un flux après une lecture ou une écriture (SDL_IOStatus).
enum class IOStatus : int {
    READY = SDL_IO_STATUS_READY,         ///< tout va bien
    ERROR = SDL_IO_STATUS_ERROR,         ///< erreur de lecture/écriture
    END = SDL_IO_STATUS_EOF,             ///< fin des données
    NOT_READY = SDL_IO_STATUS_NOT_READY, ///< pas de données pour l'instant (non bloquant)
    READONLY = SDL_IO_STATUS_READONLY,   ///< écriture sur un flux en lecture seule
    WRITEONLY = SDL_IO_STATUS_WRITEONLY, ///< lecture sur un flux en écriture seule
};

/// Flux implémenté en C++ : un décompresseur, une fenêtre sur un autre flux…
/// `IOStream::FromImpl` l'expose comme n'importe quel SDL_IOStream (SDL_OpenIO),
/// utilisable ensuite avec ReadU32Le, WriteU64Be, ReadExact, etc.
///
/// `Read` doit remplir tout le tampon demandé tant que des données existent :
/// SDL_ReadU32LE et consorts ne relancent pas une lecture partielle.
class IOStreamImpl {
public:
    virtual ~IOStreamImpl() = default;
    /// Taille totale, -1 si inconnue.
    [[nodiscard]] virtual Sint64 Size() { return -1; }
    /// Nouvelle position, -1 si le déplacement est impossible.
    virtual Sint64 Seek(Sint64 offset, IOWhence whence);
    /// Lit des données (doit remplir le tampon tant que possible).
    virtual size_t Read(void *buffer, size_t size, IOStatus &status);
    /// Écrit des données.
    virtual size_t Write(const void *buffer, size_t size, IOStatus &status);
    /// Vide les tampons d'écriture.
    virtual bool Flush(IOStatus &status);
    /// Appelé une fois, juste avant la destruction (SDL_CloseIO) : faux si
    /// les dernières données n'ont pas pu être écrites.
    virtual bool Close() { return true; }
};

// ============================================================================
// IOStream — RAII SDL_IOStream
// ============================================================================

/// Flux d'entrée/sortie (RAII sur SDL_IOStream).
class IOStream : public Wrapper<SDL_IOStream, SDL_CloseIO> {
public:
    using Wrapper::Wrapper;

    // ── Constructors ─────────────────────────────────────────────────────────

    /// Ouvre un fichier (mode « rb », « wb », …).
    [[nodiscard]] static Result<IOStream, StringView> FromFile(const String &path, const char *mode = "rb");

    /// Flux sur un tampon mémoire mutable.
    [[nodiscard]] static Result<IOStream, StringView> FromMemory(void *mem, size_t size);

    /// Flux en lecture seule sur un tampon mémoire constant.
    [[nodiscard]] static Result<IOStream, StringView> FromConstMemory(const void *mem, size_t size);

    /// Flux mémoire extensible en écriture (SDL_IOFromDynamicMem) : le tampon
    /// grandit à chaque écriture ; `DynamicMemoryBytes()` en rend une copie.
    /// Sert à produire un fichier binaire en mémoire avec les mêmes
    /// Write*Le/Be qu'un fichier disque.
    [[nodiscard]] static Result<IOStream, StringView> FromDynamicMemory();

    /// Flux dont le comportement est fourni par `impl` (possédé par le flux,
    /// détruit à sa fermeture).
    [[nodiscard]] static Result<IOStream, StringView> FromImpl(std::unique_ptr<IOStreamImpl> impl);

    // ── I/O ──────────────────────────────────────────────────────────────────

    /// Lit jusqu'à `size` octets (nombre d'octets lus).
    size_t Read(void *buf, size_t size) noexcept { return mHandle ? SDL_ReadIO(mHandle, buf, size) : 0; }
    /// Écrit jusqu'à `size` octets (nombre d'octets écrits).
    size_t Write(const void *buf, size_t size) noexcept { return mHandle ? SDL_WriteIO(mHandle, buf, size) : 0; }

    /// Lit un span d'éléments (nombre d'éléments lus).
    template <typename T> size_t Read(std::span<T> buf) noexcept {
        return Read(buf.data(), buf.size_bytes()) / sizeof(T);
    }
    /// Écrit un span d'éléments (nombre d'éléments écrits).
    template <typename T> size_t Write(std::span<const T> buf) noexcept {
        return Write(buf.data(), buf.size_bytes()) / sizeof(T);
    }

    // ── Seek / Tell ──────────────────────────────────────────────────────────

    /// Déplace la position de lecture/écriture.
    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept;
    Sint64 Seek(Sint64 offset, IOWhence whence) noexcept;
    /// Position courante.
    Sint64 Tell() const noexcept { return mHandle ? SDL_TellIO(mHandle) : -1; }
    /// Taille du flux.
    Sint64 GetSize() const noexcept { return mHandle ? SDL_GetIOSize(mHandle) : -1; }
    /// État laissé par la dernière opération (fin de données, erreur…).
    [[nodiscard]] IOStatus Status() const noexcept;

    // ── Typed reads (little-endian) ──────────────────────────────────────────

    /// Lit un entier non signé (little-endian).
    Option<uint8_t> ReadU8() noexcept;
    Option<uint16_t> ReadU16Le() noexcept;
    Option<uint32_t> ReadU32Le() noexcept;
    Option<uint64_t> ReadU64Le() noexcept;

    // ── Typed reads (big-endian, signés) ─────────────────────────────────────
    // Ordre d'octets IMPOSÉ par le nom de la méthode, jamais celui de l'hôte :
    // SDL convertit (SDL_Swap*) selon l'architecture.

    Option<uint16_t> ReadU16Be() noexcept;
    Option<uint32_t> ReadU32Be() noexcept;
    Option<uint64_t> ReadU64Be() noexcept;
    Option<int8_t> ReadS8() noexcept;
    Option<int16_t> ReadS16Le() noexcept;
    Option<int32_t> ReadS32Le() noexcept;
    Option<int64_t> ReadS64Le() noexcept;
    Option<int16_t> ReadS16Be() noexcept;
    Option<int32_t> ReadS32Be() noexcept;
    Option<int64_t> ReadS64Be() noexcept;

    /// Écrit un entier non signé (little/big-endian).
    bool WriteU8(uint8_t v) noexcept { return mHandle && SDL_WriteU8(mHandle, v); }
    bool WriteU16Le(uint16_t v) noexcept { return mHandle && SDL_WriteU16LE(mHandle, v); }
    bool WriteU32Le(uint32_t v) noexcept { return mHandle && SDL_WriteU32LE(mHandle, v); }
    bool WriteU64Le(uint64_t v) noexcept { return mHandle && SDL_WriteU64LE(mHandle, v); }
    bool WriteU16Be(uint16_t v) noexcept { return mHandle && SDL_WriteU16BE(mHandle, v); }
    bool WriteU32Be(uint32_t v) noexcept { return mHandle && SDL_WriteU32BE(mHandle, v); }
    bool WriteU64Be(uint64_t v) noexcept { return mHandle && SDL_WriteU64BE(mHandle, v); }
    /// Écrit un entier signé (little/big-endian).
    bool WriteS8(int8_t v) noexcept { return mHandle && SDL_WriteS8(mHandle, v); }
    bool WriteS16Le(int16_t v) noexcept { return mHandle && SDL_WriteS16LE(mHandle, v); }
    bool WriteS32Le(int32_t v) noexcept { return mHandle && SDL_WriteS32LE(mHandle, v); }
    bool WriteS64Le(int64_t v) noexcept { return mHandle && SDL_WriteS64LE(mHandle, v); }
    bool WriteS16Be(int16_t v) noexcept { return mHandle && SDL_WriteS16BE(mHandle, v); }
    bool WriteS32Be(int32_t v) noexcept { return mHandle && SDL_WriteS32BE(mHandle, v); }
    bool WriteS64Be(int64_t v) noexcept { return mHandle && SDL_WriteS64BE(mHandle, v); }

    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Lit EXACTEMENT `size` octets (faux si le flux se termine avant).
    /// Relance les lectures partielles (flux personnalisés, tubes) ; s'arrête
    /// à la fin des données ou sur une erreur.
    [[nodiscard]] bool ReadExact(void *buf, size_t size) noexcept;
    /// Écrit EXACTEMENT `size` octets (écritures partielles relancées).
    [[nodiscard]] bool WriteExact(const void *buf, size_t size) noexcept;
    /// Vide les tampons d'écriture.
    bool Flush() noexcept { return mHandle && SDL_FlushIO(mHandle); }
    /// Ferme le flux et rend le statut de SDL_CloseIO (écritures en attente
    /// comprises) ; le destructeur, lui, ne peut pas signaler d'échec.
    bool Close() noexcept;

    /// Copie du tampon d'un flux créé par FromDynamicMemory() (vide sinon).
    [[nodiscard]] std::vector<uint8_t> DynamicMemoryBytes() const;

    /// Lit le reste du flux en un vecteur (taille inconnue acceptée :
    /// lecture par morceaux jusqu'à la fin).
    [[nodiscard]] std::vector<uint8_t> ReadAll() noexcept;
};

// ============================================================================
// IOStreamView — vue non-possédante sur un SDL_IOStream prêté (ex: stdio d'un
// sous-processus). Ne ferme jamais le flux sous-jacent.
// ============================================================================

class IOStreamView : public Borrowed<SDL_IOStream> {
public:
    using Borrowed::Borrowed;

    /// Lit jusqu'à `size` octets.
    size_t Read(void *buf, size_t size) noexcept { return mHandle ? SDL_ReadIO(mHandle, buf, size) : 0; }
    /// Écrit jusqu'à `size` octets.
    size_t Write(const void *buf, size_t size) noexcept { return mHandle ? SDL_WriteIO(mHandle, buf, size) : 0; }

    /// Déplace la position.
    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept;
    /// Position courante.
    Sint64 Tell() const noexcept { return mHandle ? SDL_TellIO(mHandle) : -1; }
    /// Taille du flux.
    Sint64 GetSize() const noexcept { return mHandle ? SDL_GetIOSize(mHandle) : -1; }
};

// ============================================================================
// Fonctions libres de commodité
// ============================================================================

/// Lit tout un fichier en un vecteur d'octets.
[[nodiscard]] Result<std::vector<uint8_t>, StringView> ReadFile(const String &path);

/// Écrit un tampon brut dans un fichier.
[[nodiscard]] bool WriteFile(const String &path, const void *data, size_t size);

/// Charge tout un fichier via SDL_LoadFile (tampon alloué/libéré par SDL).
[[nodiscard]] Result<std::vector<uint8_t>, StringView> LoadFile(const String &path);

/// Écrit tout un fichier via SDL_SaveFile.
[[nodiscard]] bool SaveFile(const String &path, const void *data, size_t size);

} // namespace sdl3
