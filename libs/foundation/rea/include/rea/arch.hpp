#pragma once
/**
 * @file arch.hpp
 * @brief Architecture cible du désassemblage (partagée entre les modules).
 *
 * Une énumération de vocabulaire et ses conversions nom ↔ valeur, pour que
 * l'interface (menu « Architecture ») et les désassembleurs multi-architectures
 * parlent la même langue.
 */
#include <cstdint>
#include <optional>
#include <string_view>

namespace rea {

/// Architecture cible du désassemblage.
enum class Arch : uint8_t { X86_64, Arm, Thumb, Mips32 };

/// Nom canonique (« x86_64 », « arm », « thumb », « mips32 »).
[[nodiscard]] std::string_view ArchName(Arch arch) noexcept;

/// Architecture depuis un nom (insensible à la casse) ; `std::nullopt` si inconnu.
[[nodiscard]] std::optional<Arch> ArchFromName(std::string_view name);

} // namespace rea
