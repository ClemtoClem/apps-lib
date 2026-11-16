#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <functional>
#include <vector>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PropertyType
// ============================================================================

/// Type d'une propriété (groupe SDL_PropertiesID).
enum class PropertyType {
    INVALID = SDL_PROPERTY_TYPE_INVALID,
    POINTER = SDL_PROPERTY_TYPE_POINTER,
    STRING = SDL_PROPERTY_TYPE_STRING,
    NUMBER = SDL_PROPERTY_TYPE_NUMBER,
    FLOAT = SDL_PROPERTY_TYPE_FLOAT,
    BOOLEAN = SDL_PROPERTY_TYPE_BOOLEAN,
};

// ============================================================================
// PropertyProxy — returned by Properties::operator[]
// Allows type-deduced get and set through a single name lookup.
// ============================================================================

class PropertyProxy {
private:
    SDL_PropertiesID m_id;
    const char *m_name;

public:
    constexpr PropertyProxy(SDL_PropertiesID id, const char *name) noexcept : m_id(id), m_name(name) {}

    // ── Setters ──────────────────────────────────────────────────────────────

    PropertyProxy &operator=(const char *v) {
        SDL_SetStringProperty(m_id, m_name, v);
        return *this;
    }
    PropertyProxy &operator=(const String &v) {
        SDL_SetStringProperty(m_id, m_name, v.CStr());
        return *this;
    }
    PropertyProxy &operator=(Sint64 v) {
        SDL_SetNumberProperty(m_id, m_name, v);
        return *this;
    }
    PropertyProxy &operator=(int v) {
        SDL_SetNumberProperty(m_id, m_name, Sint64(v));
        return *this;
    }
    PropertyProxy &operator=(float v) {
        SDL_SetFloatProperty(m_id, m_name, v);
        return *this;
    }
    PropertyProxy &operator=(double v) {
        SDL_SetFloatProperty(m_id, m_name, float(v));
        return *this;
    }
    PropertyProxy &operator=(bool v) {
        SDL_SetBooleanProperty(m_id, m_name, v);
        return *this;
    }
    PropertyProxy &operator=(void *v) {
        SDL_SetPointerProperty(m_id, m_name, v);
        return *this;
    }

    /// Remove the property.
    void Clear() { SDL_ClearProperty(m_id, m_name); }

    // ── Type query ────────────────────────────────────────────────────────────

    [[nodiscard]] PropertyType Type() const noexcept { return PropertyType(SDL_GetPropertyType(m_id, m_name)); }
    [[nodiscard]] bool Exists() const noexcept { return SDL_HasProperty(m_id, m_name); }

    // ── Typed getters ─────────────────────────────────────────────────────────

    template <typename T> [[nodiscard]] T Get(T def = T{}) const;

    // Explicit conversion operators for ergonomic use
    [[nodiscard]] explicit operator const char *() const { return SDL_GetStringProperty(m_id, m_name, ""); }
    [[nodiscard]] explicit operator String() const { return String(SDL_GetStringProperty(m_id, m_name, "")); }
    [[nodiscard]] explicit operator Sint64() const { return SDL_GetNumberProperty(m_id, m_name, 0); }
    [[nodiscard]] explicit operator int() const { return int(SDL_GetNumberProperty(m_id, m_name, 0)); }
    [[nodiscard]] explicit operator float() const { return SDL_GetFloatProperty(m_id, m_name, 0.f); }
    [[nodiscard]] explicit operator bool() const { return SDL_GetBooleanProperty(m_id, m_name, false); }
    [[nodiscard]] explicit operator void *() const { return SDL_GetPointerProperty(m_id, m_name, nullptr); }
};

// Template specialisations for PropertyProxy::get<T>
template <> inline const char *PropertyProxy::Get(const char *def) const {
    return SDL_GetStringProperty(m_id, m_name, def);
}
template <> inline String PropertyProxy::Get(String def) const {
    return String(SDL_GetStringProperty(m_id, m_name, def.CStr()));
}
template <> inline Sint64 PropertyProxy::Get(Sint64 def) const { return SDL_GetNumberProperty(m_id, m_name, def); }
template <> inline int PropertyProxy::Get(int def) const {
    return int(SDL_GetNumberProperty(m_id, m_name, Sint64(def)));
}
template <> inline float PropertyProxy::Get(float def) const { return SDL_GetFloatProperty(m_id, m_name, def); }
template <> inline bool PropertyProxy::Get(bool def) const { return SDL_GetBooleanProperty(m_id, m_name, def); }
template <> inline void *PropertyProxy::Get(void *def) const { return SDL_GetPointerProperty(m_id, m_name, def); }

// ============================================================================
// PropertiesLock — RAII lock for thread-safe multi-property operations
// ============================================================================

class PropertiesLock {
private:
    SDL_PropertiesID m_id = 0;

public:
    explicit PropertiesLock(SDL_PropertiesID id);
    ~PropertiesLock();

    PropertiesLock(const PropertiesLock &) = delete;
    PropertiesLock &operator=(const PropertiesLock &) = delete;
    PropertiesLock(PropertiesLock &&o) noexcept : m_id(o.m_id) { o.m_id = 0; }
};

// ============================================================================
// PropertiesBase — accesseurs partagés entre Properties (possédant) et
// PropertiesRef (emprunté), inspiré de SDL3pp_properties.h (talesm/SDL3pp).
// ============================================================================

class PropertiesRef;

class PropertiesBase {
protected:
    SDL_PropertiesID mId = 0;

public:
    constexpr PropertiesBase() noexcept = default;
    explicit constexpr PropertiesBase(SDL_PropertiesID id) noexcept : mId(id) {}

    [[nodiscard]] SDL_PropertiesID GetId() const noexcept { return mId; }
    [[nodiscard]] explicit operator bool() const noexcept { return mId != 0; }

    // ── Operator[] — ergonomic typed access ──────────────────────────────────

    [[nodiscard]] PropertyProxy operator[](const char *name) const noexcept { return {mId, name}; }
    [[nodiscard]] PropertyProxy operator[](const String &name) const noexcept { return {mId, name.CStr()}; }

    // ── Explicit setters ─────────────────────────────────────────────────────

    bool SetString(const char *name, const char *val) { return SDL_SetStringProperty(mId, name, val); }
    bool SetNumber(const char *name, Sint64 val) { return SDL_SetNumberProperty(mId, name, val); }
    bool SetFloat(const char *name, float val) { return SDL_SetFloatProperty(mId, name, val); }
    bool SetBool(const char *name, bool val) { return SDL_SetBooleanProperty(mId, name, val); }
    bool SetPointer(const char *name, void *val) { return SDL_SetPointerProperty(mId, name, val); }

    bool SetPointerWithCleanup(const char *name, void *val, SDL_CleanupPropertyCallback cleanup, void *userdata = nullptr);

    /// Cleanup via std::function — captures et gère la durée de vie.
    bool SetPointerWithCleanup(const char *name, void *val, std::function<void(void *)> cleanup);

    // ── Explicit getters ─────────────────────────────────────────────────────

    [[nodiscard]] const char *GetString(const char *name, const char *def = "") const;
    [[nodiscard]] Sint64 GetNumber(const char *name, Sint64 def = 0) const;
    [[nodiscard]] float GetFloat(const char *name, float def = 0.f) const;
    [[nodiscard]] bool GetBool(const char *name, bool def = false) const {
        return SDL_GetBooleanProperty(mId, name, def);
    }
    [[nodiscard]] void *GetPointer(const char *name, void *def = nullptr) const;

    template <typename T> [[nodiscard]] T *GetTypedPointer(const char *name) const noexcept {
        return static_cast<T *>(SDL_GetPointerProperty(mId, name, nullptr));
    }

    // ── Inspection ────────────────────────────────────────────────────────────

    [[nodiscard]] bool HasProperty(const char *name) const noexcept { return SDL_HasProperty(mId, name); }
    [[nodiscard]] sdl3::PropertyType PropertyType(const char *name) const noexcept;
    bool ClearProperty(const char *name) { return SDL_ClearProperty(mId, name); }

    // ── Enumeration ──────────────────────────────────────────────────────────

    /// Call `fn(name)` for every property. Thread-safe if you hold the lock.
    void Enumerate(std::function<void(const char *name)> fn) const;

    /// Nombre de propriétés (implémenté via enumerate() — pas O(1)).
    [[nodiscard]] uint64_t Count() const;

    /// Nombre de propriétés du groupe (O(1), via SDL_GetNumProperties).
    [[nodiscard]] inline int NumProperties() const noexcept { return SDL_GetNumProperties(mId); }

    // ── Thread safety ─────────────────────────────────────────────────────────

    /// Acquire un verrou scopé ; relâché automatiquement à la destruction du retour.
    [[nodiscard]] PropertiesLock Lock() const { return PropertiesLock(mId); }

    // ── Copy ─────────────────────────────────────────────────────────────────

    /// Copie toutes les propriétés vers `dst`.
    bool CopyTo(const PropertiesBase &dst) const { return SDL_CopyProperties(mId, dst.mId); }
};

// ============================================================================
// PropertiesRef — vue non-possédante sur un groupe de propriétés existant
// (ex: sdl3::globalProperties(), ou pour passer un Properties sans transférer
// la propriété à une fonction qui n'a pas besoin de le détruire).
// ============================================================================

class PropertiesRef : public PropertiesBase {
public:
    using PropertiesBase::PropertiesBase;
    constexpr PropertiesRef(const PropertiesBase &other) noexcept : PropertiesBase(other.GetId()) {}
};

// ============================================================================
// Properties — RAII SDL_PropertiesID
// ============================================================================

class Properties : public PropertiesBase {
public:
    using PropertiesBase::PropertiesBase;

    ~Properties();

    Properties(const Properties &) = delete;
    Properties &operator=(const Properties &) = delete;
    Properties(Properties &&o) noexcept : PropertiesBase(o.mId) { o.mId = 0; }
    Properties &operator=(Properties &&o) noexcept {
        if (this != &o) {
            if (mId)
                SDL_DestroyProperties(mId);
            mId = o.mId;
            o.mId = 0;
        }
        return *this;
    }

    // ── Factory ──────────────────────────────────────────────────────────────

    [[nodiscard]] static Properties Create() { return Properties(SDL_CreateProperties()); }

    /// Vue non-possédante sur ce groupe de propriétés.
    [[nodiscard]] PropertiesRef Ref() const noexcept { return PropertiesRef(mId); }

    /// Retourne un nouveau Properties contenant une copie de toutes les entrées
    /// (les propriétés "pointer" avec cleanup ne sont PAS copiées — cf. doc SDL).
    [[nodiscard]] Properties Clone() const;
};

// ============================================================================
// Global properties (process-lifetime, no init required)
// ============================================================================

/// Retourne le groupe de propriétés global (vue non-possédante, valide pour
/// toute la durée de vie du processus).
[[nodiscard]] inline PropertiesRef GlobalProperties() noexcept { return PropertiesRef(SDL_GetGlobalProperties()); }

#ifdef SDL_PROP_NAME_STRING
/// Nom générique conventionnel pour associer un libellé humain à un objet
/// (ex: nom d'une Texture, d'un AudioStream...). SDL ne définit cette
/// propriété sur aucun objet par défaut ; c'est une convention à disposition
/// des apps/libs.
inline constexpr const char *PROP_NAME_STRING = SDL_PROP_NAME_STRING;
#endif

// ============================================================================
// MessageBox helpers
// ============================================================================

/// Type (icône) d'une boîte de message.
enum class MsgBoxKind : uint32_t {
    INFO = SDL_MESSAGEBOX_INFORMATION,
    WARNING = SDL_MESSAGEBOX_WARNING,
    ERROR = SDL_MESSAGEBOX_ERROR,
};

bool ShowMessage(MsgBoxKind kind, const String &title, const String &msg, SDL_Window *parent = nullptr);
bool ShowInfo(const String &title, const String &msg, SDL_Window *parent = nullptr);
bool ShowWarning(const String &title, const String &msg, SDL_Window *parent = nullptr);
bool ShowError(const String &title, const String &msg, SDL_Window *parent = nullptr);

// ============================================================================
// System info
// ============================================================================

namespace system {

[[nodiscard]] inline int CpuCount() noexcept { return SDL_GetNumLogicalCPUCores(); }
[[nodiscard]] inline int SystemRam() noexcept { return SDL_GetSystemRAM(); }
[[nodiscard]] inline const char *Platform() noexcept { return SDL_GetPlatform(); }

} // namespace system

// ============================================================================
// CPUInfo — détection de caractéristiques CPU/SIMD (SDL_cpuinfo.h)
// ============================================================================

namespace cpuinfo {

inline constexpr int CACHELINE_SIZE = SDL_CACHELINE_SIZE;

[[nodiscard]] inline int LogicalCpuCount() noexcept { return SDL_GetNumLogicalCPUCores(); }
[[nodiscard]] inline int CacheLineSize() noexcept { return SDL_GetCPUCacheLineSize(); }
[[nodiscard]] inline size_t SIMDAlignment() noexcept { return SDL_GetSIMDAlignment(); }
[[nodiscard]] inline int SystemPageSize() noexcept { return SDL_GetSystemPageSize(); }
[[nodiscard]] inline int SystemRAM() noexcept { return SDL_GetSystemRAM(); }

[[nodiscard]] inline bool HasMMX() noexcept { return SDL_HasMMX(); }
[[nodiscard]] inline bool HasSSE() noexcept { return SDL_HasSSE(); }
[[nodiscard]] inline bool HasSSE2() noexcept { return SDL_HasSSE2(); }
[[nodiscard]] inline bool HasSSE3() noexcept { return SDL_HasSSE3(); }
[[nodiscard]] inline bool HasSSE41() noexcept { return SDL_HasSSE41(); }
[[nodiscard]] inline bool HasSSE42() noexcept { return SDL_HasSSE42(); }
[[nodiscard]] inline bool HasAVX() noexcept { return SDL_HasAVX(); }
[[nodiscard]] inline bool HasAVX2() noexcept { return SDL_HasAVX2(); }
[[nodiscard]] inline bool HasAVX512F() noexcept { return SDL_HasAVX512F(); }
[[nodiscard]] inline bool HasARMSIMD() noexcept { return SDL_HasARMSIMD(); }
[[nodiscard]] inline bool HasNEON() noexcept { return SDL_HasNEON(); }
[[nodiscard]] inline bool HasSVE2() noexcept { return SDL_HasSVE2(); }
[[nodiscard]] inline bool HasLSX() noexcept { return SDL_HasLSX(); }
[[nodiscard]] inline bool HasLASX() noexcept { return SDL_HasLASX(); }
[[nodiscard]] inline bool HasAltiVec() noexcept { return SDL_HasAltiVec(); }

} // namespace cpuinfo

// ============================================================================
// MessageBox — boîte de dialogue native paramétrable (SDL_messagebox.h)
// ============================================================================

/// Drapeaux d'une boîte de message.
using MessageBoxFlags = SDL_MessageBoxFlags;
/// Drapeaux d'un bouton de boîte de message.
using MessageBoxButtonFlags = SDL_MessageBoxButtonFlags;

namespace messageboxFlags {
inline constexpr MessageBoxFlags ERROR                 = SDL_MESSAGEBOX_ERROR;
inline constexpr MessageBoxFlags WARNING               = SDL_MESSAGEBOX_WARNING;
inline constexpr MessageBoxFlags INFORMATION           = SDL_MESSAGEBOX_INFORMATION;
inline constexpr MessageBoxFlags BUTTONS_LEFT_TO_RIGHT = SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
inline constexpr MessageBoxFlags BUTTONS_RIGHT_TO_LEFT = SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT;
} // namespace messagebox_flags

namespace messageboxButtonFlags {
inline constexpr MessageBoxButtonFlags RETURNKEY_DEFAULT = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
inline constexpr MessageBoxButtonFlags ESCAPEKEY_DEFAULT = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
} // namespace messagebox_button_flags

/// Élément colorable d'une boîte de message.
enum class MessageBoxColorType {
	BACKGROUND       = SDL_MESSAGEBOX_COLOR_BACKGROUND,
	TEXT             = SDL_MESSAGEBOX_COLOR_TEXT,
	BUTTON_BORDER    = SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
	BUTTON_BACKGROUND = SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
	BUTTON_SELECTED  = SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
	COUNT            = SDL_MESSAGEBOX_COLOR_COUNT,
};

/// Couleur d'un élément de boîte de message (RGBA 8 bits).
struct MessageBoxColor {
	Uint8 r = 0, g = 0, b = 0;
	[[nodiscard]] constexpr operator SDL_MessageBoxColor() const { return {r, g, b}; }
};

/// Schéma de couleurs d'une boîte de message.
struct MessageBoxColorScheme {
	std::array<MessageBoxColor, SDL_MESSAGEBOX_COLOR_COUNT> colors{};

	[[nodiscard]] SDL_MessageBoxColorScheme ToSDL() const {
		SDL_MessageBoxColorScheme out{};
		for (size_t i = 0; i < SDL_MESSAGEBOX_COLOR_COUNT; ++i)
			out.colors[i] = colors[i];
		return out;
	}
};

/// Bouton d'une boîte de message.
struct MessageBoxButton {
	MessageBoxButtonFlags flags = 0;
	int id = 0;
	String text;
};

/// Boîte de dialogue native avec boutons et schéma de couleurs optionnel.
/// Boîte de dialogue native paramétrable (boutons, couleurs).
struct MessageBox {
	MessageBoxFlags flags = 0;
	SDL_Window *window = nullptr;
	String title;
	String message;
	std::vector<MessageBoxButton> buttons;
	Option<MessageBoxColorScheme> colorScheme;

	/// Affiche la boîte ; retourne l'ID du bouton cliqué (-1 si fermée).
	[[nodiscard]] Result<int, Error> Show() const;
};

/// Version simplifiée (un seul bouton « OK »), déjà disponible via
/// ShowMessage/ShowInfo/ShowWarning/ShowError ci-dessus.
inline bool ShowSimpleMessageBox(MessageBoxFlags flags, const String &title, const String &message, SDL_Window *window = nullptr) {
	return SDL_ShowSimpleMessageBox(flags, title.CStr(), message.CStr(), window);
}

// ============================================================================
// Clipboard — presse-papiers par type MIME et sélection primaire
// ============================================================================

namespace clipboard {

[[nodiscard]] String GetText();
inline bool SetText(const String &text) { return SDL_SetClipboardText(text.CStr()); }
[[nodiscard]] inline bool HasText() noexcept { return SDL_HasClipboardText(); }

[[nodiscard]] String GetPrimarySelectionText();
inline bool SetPrimarySelectionText(const String &text) { return SDL_SetPrimarySelectionText(text.CStr()); }
[[nodiscard]] inline bool HasPrimarySelectionText() noexcept { return SDL_HasPrimarySelectionText(); }

[[nodiscard]] inline bool HasData(const char *mimeType) noexcept { return SDL_HasClipboardData(mimeType); }

/// Lit le presse-papiers pour un type MIME donné (tampon null-terminé recopié).
[[nodiscard]] Option<std::vector<uint8_t>> GetData(const char *mimeType);

/// Liste des types MIME disponibles dans le presse-papiers.
[[nodiscard]] std::vector<String> GetMimeTypes();

inline bool ClearData() { return SDL_ClearClipboardData(); }

/// Offre des données au presse-papiers via des callbacks (fourniture à la demande).
bool SetData(SDL_ClipboardDataCallback callback, SDL_ClipboardCleanupCallback cleanup,
			 void *userdata, const std::vector<String> &mimeTypes);

} // namespace clipboard

inline bool OpenUrl(const String &url) { return SDL_OpenURL(url.CStr()); }

/// Encode une chaîne pour l'utiliser dans une URL (SDL_EncodeURL, alloue via SDL).
/// `noEncodeChars` liste les caractères à ne PAS encoder (peut être nul).
[[nodiscard]] String EncodeUrl(const String &url, const char *noEncodeChars = nullptr);

} // namespace sdl3
