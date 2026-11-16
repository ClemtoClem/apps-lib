#include "diagnostics/assert.hpp"

#include <cstdlib>

#include <core/log.hpp>

#include "diagnostics/trace.hpp"

namespace diagnostics {

void AssertFailed(const char *expr, const std::string &message, std::source_location where) {
	core::log::Error("assertion échouée : {} — {} ({}:{} dans {})", expr, message, where.file_name(), where.line(),
					 where.function_name());
	trace::Stop(); // conserve la capture en cours
	std::abort();
}

void VerifyFailed(const char *expr, const std::string &message, std::source_location where) {
	core::log::Warn("vérification échouée : {} — {} ({}:{})", expr, message, where.file_name(), where.line());
}

} // namespace diagnostics
