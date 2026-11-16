#pragma once
/**
 * @file sensor.hpp
 * @brief Capteurs (SDL_sensor.h) : accéléromètre, gyroscope — énumération et lecture.
 */
#include <SDL3/SDL.h>
#include <array>
#include <vector>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// SensorType / SensorID
// ============================================================================

/// Identifiant d'un capteur.
using SensorID = SDL_SensorID;

/// Type d'un capteur.
enum class SensorType {
	INVALID		= SDL_SENSOR_INVALID,	///< invalide
	UNKNOWN		= SDL_SENSOR_UNKNOWN,	///< inconnu
	ACCEL		= SDL_SENSOR_ACCEL,		///< accéléromètre
	GYRO		= SDL_SENSOR_GYRO,		///< gyroscope
	ACCEL_L		= SDL_SENSOR_ACCEL_L,	///< accéléromètre (main gauche)
	GYRO_L		= SDL_SENSOR_GYRO_L,	///< gyroscope (main gauche)
	ACCEL_R		= SDL_SENSOR_ACCEL_R,	///< accéléromètre (main droite)
	GYRO_R		= SDL_SENSOR_GYRO_R,	///< gyroscope (main droite)
};

namespace detail {
	constexpr SDL_SensorType ToSDL(SensorType e) {
		return static_cast<SDL_SensorType>(e);
	}
}

// ============================================================================
// Énumération des capteurs connectés
// ============================================================================

namespace sensor {

/// Identifiants des capteurs connectés.
[[nodiscard]] std::vector<SensorID> Enumerated();

/// Nom d'un capteur par son identifiant.
[[nodiscard]] inline const char *NameFor(SensorID id) noexcept { return SDL_GetSensorNameForID(id); }
/// Type d'un capteur par son identifiant.
[[nodiscard]] inline SensorType TypeFor(SensorID id) noexcept { return SensorType(SDL_GetSensorTypeForID(id)); }
/// Type non portable (propre à la plateforme) d'un capteur.
[[nodiscard]] inline int NonPortableTypeFor(SensorID id) noexcept { return SDL_GetSensorNonPortableTypeForID(id); }

/// Force la mise à jour de tous les capteurs ouverts (appelé automatiquement
/// par la pompe d'événements si SDL_INIT_SENSOR est actif).
inline void Update() noexcept { SDL_UpdateSensors(); }

} // namespace sensor

namespace detail {

// Requêtes communes à Sensor (possédant) et SensorView (emprunté) — évite la
// duplication entre les deux classes tout en gardant SDL_Sensor* privé aux .cpp/.hpp internes.
[[nodiscard]] inline const char *GetSensorName(SDL_Sensor *h) noexcept { return h ? SDL_GetSensorName(h) : ""; }
[[nodiscard]] SensorType GetSensorType(SDL_Sensor *h) noexcept;
[[nodiscard]] int SensorNonPortableType(SDL_Sensor *h) noexcept;
[[nodiscard]] inline SensorID SensorId(SDL_Sensor *h) noexcept { return h ? SDL_GetSensorID(h) : 0; }
[[nodiscard]] SDL_PropertiesID SensorProperties(SDL_Sensor *h) noexcept;

template <size_t N> [[nodiscard]] Option<std::array<float, N>> SensorData(SDL_Sensor *h) {
	std::array<float, N> out{};
	if (!h || !SDL_GetSensorData(h, out.data(), int(N)))
		return NONE;
	return Some(out);
}

} // namespace detail

// ============================================================================
// SensorView — vue non-possédante sur un capteur déjà ouvert (ex: retrouvé à
// partir d'un SDL_EVENT_SENSOR_UPDATE). Ne ferme jamais le capteur sous-jacent.
// ============================================================================

class SensorView : public Borrowed<SDL_Sensor> {
public:
	using Borrowed::Borrowed;

	/// Propriétés du capteur.
	[[nodiscard]] SDL_PropertiesID Properties() const noexcept { return detail::SensorProperties(mHandle); }
	/// Nom du capteur.
	[[nodiscard]] const char *Name() const noexcept { return detail::GetSensorName(mHandle); }
	/// Type du capteur.
	[[nodiscard]] SensorType Type() const noexcept { return detail::GetSensorType(mHandle); }
	/// Type non portable (propre à la plateforme).
	[[nodiscard]] int NonPortableType() const noexcept { return detail::SensorNonPortableType(mHandle); }
	/// Identifiant du capteur.
	[[nodiscard]] SensorID GetId() const noexcept { return detail::SensorId(mHandle); }

	/// Lit jusqu'à `N` valeurs float du capteur.
	template <size_t N = 3> [[nodiscard]] Option<std::array<float, N>> GetData() const {
		return detail::SensorData<N>(mHandle);
	}
};

// ============================================================================
// Sensor — RAII SDL_Sensor
// ============================================================================

class Sensor : public Wrapper<SDL_Sensor, SDL_CloseSensor> {
public:
	using Wrapper::Wrapper;

	/// Ouvre un capteur par identifiant.
	[[nodiscard]] static Result<Sensor, StringView> Open(SensorID id);

	/// Retrouve la vue d'un capteur déjà ouvert ailleurs, à partir de son id
	/// (utile en réponse à un SDL_EVENT_SENSOR_UPDATE).
	[[nodiscard]] static Option<SensorView> FromId(SensorID id) noexcept;

	/// Propriétés du capteur.
	[[nodiscard]] SDL_PropertiesID Properties() const noexcept { return detail::SensorProperties(mHandle); }
	/// Nom du capteur.
	[[nodiscard]] const char *Name() const noexcept { return detail::GetSensorName(mHandle); }
	/// Type du capteur.
	[[nodiscard]] SensorType Type() const noexcept { return detail::GetSensorType(mHandle); }
	/// Type non portable (propre à la plateforme).
	[[nodiscard]] int NonPortableType() const noexcept { return detail::SensorNonPortableType(mHandle); }
	/// Identifiant du capteur.
	[[nodiscard]] SensorID GetId() const noexcept { return detail::SensorId(mHandle); }

	/// Lit jusqu'à `N` valeurs float dans le capteur (le nombre de canaux
	/// dépend du type : 3 pour Accel/Gyro).
	template <size_t N = 3> [[nodiscard]] Option<std::array<float, N>> GetData() const {
		return detail::SensorData<N>(mHandle);
	}
};

} // namespace sdl3
