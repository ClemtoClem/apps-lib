#pragma once
/**
 * @file hidapi.hpp
 * @brief Accès HID bas niveau (SDL_hidapi.h) : énumération et communication USB/Bluetooth.
 */
#include <SDL3/SDL.h>
#include <span>
#include <vector>

#include <core/core.hpp>
#include "core/string_unicode.hpp"
#include "core/wrapper.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// HidBusType
// ============================================================================

/// Bus de connexion d'un périphérique HID.
enum class HidBusType : int {
	UNKNOWN	  = SDL_HID_API_BUS_UNKNOWN,	 ///< inconnu
	USB		  = SDL_HID_API_BUS_USB,		 ///< USB
	BLUETOOTH = SDL_HID_API_BUS_BLUETOOTH,	 ///< Bluetooth
	I2C		  = SDL_HID_API_BUS_I2C,		 ///< I2C
	SPI		  = SDL_HID_API_BUS_SPI,		 ///< SPI
};

namespace hid::detail {

// hidapi renvoie des chaînes wchar_t natives (UTF-16 sous Windows, UTF-32
// ailleurs) — on les convertit systématiquement en UTF-8 (`String`) pour ne
// jamais exposer wchar_t* dans l'API publique.
[[nodiscard]] String WideToString(const wchar_t *w);

[[nodiscard]] std::wstring StringToWide(const String &s);

} // namespace hid::detail

// ============================================================================
// HidDeviceInfo — wrap de SDL_hid_device_info (une entrée ; la liste chaînée
// C est aplatie en std::vector par hid::Enumerate())
// ============================================================================

struct HidDeviceInfo {
    String path;				   ///< chemin système du périphérique
    uint16_t vendorId = 0, productId = 0; ///< identifiants vendeur / produit
    String serialNumber;		   ///< numéro de série
    uint16_t releaseNumber = 0;	   ///< numéro de version
    String manufacturer, product;  ///< fabricant / produit (libellés)
    uint16_t usagePage = 0, usage = 0; ///< usage HID
    int interfaceNumber = -1;	   ///< numéro d'interface
    int interfaceClass = 0, interfaceSubclass = 0, interfaceProtocol = 0; ///< classe d'interface
    HidBusType busType = HidBusType::UNKNOWN; ///< bus de connexion

    HidDeviceInfo() = default;
    /// Construit depuis un `SDL_hid_device_info` brut (convertit les chaînes en UTF-8).
    explicit HidDeviceInfo(const SDL_hid_device_info &d)
        : path(d.path ? d.path : ""), vendorId(d.vendor_id), productId(d.product_id),
          serialNumber(hid::detail::WideToString(d.serial_number)), releaseNumber(d.release_number),
          manufacturer(hid::detail::WideToString(d.manufacturer_string)),
          product(hid::detail::WideToString(d.product_string)), usagePage(d.usage_page), usage(d.usage),
          interfaceNumber(d.interface_number), interfaceClass(d.interface_class),
          interfaceSubclass(d.interface_subclass), interfaceProtocol(d.interface_protocol),
          busType(HidBusType(d.bus_type)) {}
};

// ============================================================================
// HidContext — RAII SDL_hid_init / SDL_hid_exit
// ============================================================================

/// Contexte HID (RAII sur SDL_hid_init / SDL_hid_exit).
class HidContext {
    bool m_owns = false;

public:
    HidContext() = default;
    /// Quitte le sous-système HID.
    ~HidContext();

    HidContext(const HidContext &) = delete;
    HidContext &operator=(const HidContext &) = delete;
    HidContext(HidContext &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
    HidContext &operator=(HidContext &&o) noexcept {
        if (this != &o) {
            if (m_owns)
                SDL_hid_exit();
            m_owns = o.m_owns;
            o.m_owns = false;
        }
        return *this;
    }

    /// Vrai si le sous-système HID est initialisé.
    [[nodiscard]] explicit operator bool() const noexcept { return m_owns; }

    /// Initialise le sous-système HID.
    [[nodiscard]] static Result<HidContext, Error> Create();
};

namespace hid {

/// Compteur incrémenté à chaque changement de la liste de périphériques HID
/// connectés (permet d'éviter de ré-énumérer à chaque frame).
[[nodiscard]] inline uint32_t DeviceChangeCount() noexcept { return SDL_hid_device_change_count(); }

/// Énumère les périphériques HID connectés. vendorId/productId à 0 signifie « tous ».
[[nodiscard]] std::vector<HidDeviceInfo> Enumerate(uint16_t vendorId = 0, uint16_t productId = 0);

/// Démarre/arrête un scan Bluetooth LE actif (plateformes qui le supportent).
inline void BleScan(bool active) noexcept { SDL_hid_ble_scan(active); }

} // namespace hid

// ============================================================================
// HidDevice — RAII SDL_hid_device
// ============================================================================

/// Périphérique HID ouvert (RAII sur SDL_hid_device).
class HidDevice : public Wrapper<SDL_hid_device, SDL_hid_close> {
public:
    using Wrapper::Wrapper;

    /// Ouvre un périphérique par vendor/product. `serial` vide == n'importe quel numéro de série.
    [[nodiscard]] static Result<HidDevice, StringView> Open(uint16_t vendorId, uint16_t productId,
                                                            const String &serial = "");

    /// Ouvre un périphérique par chemin système.
    [[nodiscard]] static Result<HidDevice, StringView> OpenPath(const String &path);

    /// Propriétés du périphérique.
    [[nodiscard]] SDL_PropertiesID Properties() const noexcept;

    // ── I/O ──────────────────────────────────────────────────────────────────

    /// Écrit des données (nombre d'octets écrits, ou -1 en cas d'échec).
    int Write(const void *data, size_t len) noexcept;
    /// Écrit un tampon d'octets.
    int Write(std::span<const uint8_t> data) noexcept { return Write(data.data(), data.size()); }

    /// Lit avec un délai maximal en millisecondes (0 = immédiat).
    int ReadTimeout(void *data, size_t len, int milliseconds) noexcept;
    int ReadTimeout(std::span<uint8_t> data, int milliseconds) noexcept;

    /// Lit de façon bloquante.
    int Read(void *data, size_t len) noexcept;
    int Read(std::span<uint8_t> data) noexcept { return Read(data.data(), data.size()); }

    /// Active/désactive le mode non bloquant.
    bool SetNonBlocking(bool nonblock) noexcept;

    /// Envoie un rapport de fonctionnalité.
    int SendFeatureReport(std::span<const uint8_t> data) noexcept;
    /// Reçoit un rapport de fonctionnalité.
    int GetFeatureReport(std::span<uint8_t> data) noexcept;
    /// Reçoit un rapport d'entrée.
    int GetInputReport(std::span<uint8_t> data) noexcept;

    // ── Chaînes descriptives (converties en UTF-8) ───────────────────────────

    /// Chaîne « fabricant ».
    [[nodiscard]] Option<String> ManufacturerString() const;
    /// Chaîne « produit ».
    [[nodiscard]] Option<String> ProductString() const;
    /// Chaîne « numéro de série ».
    [[nodiscard]] Option<String> SerialNumberString() const;
    /// Chaîne indexée.
    [[nodiscard]] Option<String> IndexedString(int index) const;

    /// Informations descriptives du périphérique.
    [[nodiscard]] Option<HidDeviceInfo> DeviceInfo() const;

    /// Descripteur de rapport du périphérique.
    [[nodiscard]] Result<std::vector<uint8_t>, StringView> ReportDescriptor(size_t maxSize = 4096) const;
};

} // namespace sdl3
