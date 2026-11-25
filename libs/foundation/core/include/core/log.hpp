#pragma once
/**
 * @file log.hpp
 * @brief Journal minimaliste et thread-safe : niveaux, formatage `std::format`,
 *        sortie stderr par défaut, puits remplaçable (console de jeu, fichier…).
 */
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace core::log {

enum class Level : unsigned char { Trace, Debug, Info, Warn, Error };

using Sink = std::function<void(Level, std::string_view message)>;

/// Remplace le puits (nullptr : retour à stderr). Retourne l'ancien.
Sink SetSink(Sink sink);
void SetMinLevel(Level level) noexcept;
[[nodiscard]] Level MinLevel() noexcept;
void Write(Level level, std::string_view message);
[[nodiscard]] std::string_view LevelName(Level level) noexcept;

template <typename... Args> void Print(Level level, std::format_string<Args...> fmt, Args &&...args) {
	if (level < MinLevel())
		return;
	Write(level, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args> void Trace(std::format_string<Args...> f, Args &&...a) { Print(Level::Trace, f, std::forward<Args>(a)...); }
template <typename... Args> void Debug(std::format_string<Args...> f, Args &&...a) { Print(Level::Debug, f, std::forward<Args>(a)...); }
template <typename... Args> void Info(std::format_string<Args...> f, Args &&...a) { Print(Level::Info, f, std::forward<Args>(a)...); }
template <typename... Args> void Warn(std::format_string<Args...> f, Args &&...a) { Print(Level::Warn, f, std::forward<Args>(a)...); }
template <typename... Args> void Error(std::format_string<Args...> f, Args &&...a) { Print(Level::Error, f, std::forward<Args>(a)...); }

} // namespace core::log
