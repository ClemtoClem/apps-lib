#pragma once
/**
 * @file types.hpp
 * @brief Types de base du moteur documentaire : identifiants, erreurs, couleurs, unités
 *        physiques et formats de page.
 *
 * Unité interne : le **point typographique** (1/72 de pouce), indépendant de l'écran. Les
 * conversions vers les millimètres, pouces ou pixels passent par les fonctions ci-dessous.
 */
#include <cstdint>
#include <string>
#include <string_view>

#include <core/result.hpp>
#include <math/rect.hpp>
#include <sdl3/core/structs.hpp>

namespace doc {

/// Identifiant stable d'un nœud : jamais réutilisé dans un document, conservé entre les sessions.
using NodeId = uint64_t;
inline constexpr NodeId kNoNode = 0;
/// Identifiant d'une ressource (image) du document.
using ResourceId = uint64_t;
inline constexpr ResourceId kNoResource = 0;

enum class ErrorCode : uint8_t {
	NotFound,		///< nœud, groupe ou ressource inexistant
	InvalidParent,	///< type d'enfant non admis par le parent
	Cycle,			///< déplacement d'un nœud sous l'un de ses descendants
	Locked,			///< nœud verrouillé (modification refusée)
	Protected,		///< nœud structurel (racine, conteneur de groupes) non supprimable
	TypeMismatch,	///< valeur de propriété du mauvais type
	InvalidValue,	///< valeur hors domaine
	Io,				///< lecture / écriture de fichier
	Corrupt,		///< fichier .docdb incohérent
	Unsupported,	///< version de schéma ou fonctionnalité non prise en charge
	Nothing,		///< rien à faire (historique vide…)
};

struct Error {
	ErrorCode code = ErrorCode::NotFound;
	std::string message;
};

/// Réussite sans valeur (le `Result<void>` n'existe pas dans foundation.core).
struct Done {};
using Status = Result<Done, Error>;
template <typename T> using DocResult = Result<T, Error>;

[[nodiscard]] inline Status Success() { return Ok(Done{}); }
[[nodiscard]] inline Err<Error> Failure(ErrorCode code, std::string message) { return Err(Error{code, std::move(message)}); }
[[nodiscard]] std::string_view ErrorName(ErrorCode code) noexcept;

/// Couleur : celle du wrapper SDL3 (`sdl3::Color`, RVBA 8 bits, palette nommée
/// `Color::BLACK()`, `Color::WHITE()`…). Le moteur ne redéfinit pas de type couleur.
using Color = sdl3::Color;

// ── Unités ──────────────────────────────────────────────────────────────────

inline constexpr double kPointsPerInch = 72.0;
[[nodiscard]] constexpr double Millimeters(double mm) noexcept { return mm * kPointsPerInch / 25.4; }
[[nodiscard]] constexpr double Centimeters(double cm) noexcept { return Millimeters(cm * 10.0); }
[[nodiscard]] constexpr double Inches(double in) noexcept { return in * kPointsPerInch; }
[[nodiscard]] constexpr double ToMillimeters(double pt) noexcept { return pt * 25.4 / kPointsPerInch; }
/// Points → pixels pour une résolution `dpi` (96 à l'écran, 300 à l'impression).
[[nodiscard]] constexpr double ToPixels(double pt, double dpi) noexcept { return pt * dpi / kPointsPerInch; }

/// Rectangle en points (origine en haut à gauche de la page, y vers le bas) : `math::Rect`.
using Rect = math::Rect<double>;

// ── Pages ───────────────────────────────────────────────────────────────────

enum class PageFormat : uint8_t { A4, A3, A5, Letter, Legal, Custom };
enum class Orientation : uint8_t { Portrait, Landscape };

struct PageSize {
	double width = 0, height = 0; ///< points, en portrait
};

/// Dimensions d'un format standard en portrait (Custom : 0 × 0, voir PageWidth / PageHeight).
[[nodiscard]] PageSize StandardSize(PageFormat format) noexcept;
[[nodiscard]] std::string_view PageFormatName(PageFormat format) noexcept;

} // namespace doc
