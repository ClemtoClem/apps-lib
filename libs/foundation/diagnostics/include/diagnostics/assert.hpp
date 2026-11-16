#pragma once
/**
 * @file assert.hpp
 * @brief Assertions : `DIAG_ASSERT(cond, fmt, args…)` (actives hors NDEBUG) et
 *        `DIAG_VERIFY(cond, …)` (toujours évaluée, journalise sans arrêter).
 */
#include <format>
#include <source_location>
#include <string>

namespace diagnostics {
[[noreturn]] void AssertFailed(const char *expr, const std::string &message, std::source_location where);
void VerifyFailed(const char *expr, const std::string &message, std::source_location where);
} // namespace diagnostics

#ifndef NDEBUG
#define DIAG_ASSERT(cond, ...)                                                                                          \
	do {                                                                                                                \
		if (!(cond)) [[unlikely]]                                                                                       \
			::diagnostics::AssertFailed(#cond, std::format(__VA_ARGS__), std::source_location::current());              \
	} while (0)
#else
#define DIAG_ASSERT(cond, ...) ((void)0)
#endif

#define DIAG_VERIFY(cond, ...)                                                                                          \
	((cond) ? true : (::diagnostics::VerifyFailed(#cond, std::format(__VA_ARGS__), std::source_location::current()), false))
