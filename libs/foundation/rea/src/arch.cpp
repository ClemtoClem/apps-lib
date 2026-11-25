#include "rea/arch.hpp"

#include <cctype>
#include <string>

namespace rea {

std::string_view ArchName(Arch arch) noexcept {
	switch (arch) {
	case Arch::X86_64: return "x86_64";
	case Arch::Arm: return "arm";
	case Arch::Thumb: return "thumb";
	case Arch::Mips32: return "mips32";
	}
	return "?";
}

std::optional<Arch> ArchFromName(std::string_view name) {
	std::string lower;
	lower.reserve(name.size());
	for (char c : name)
		lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (lower == "x86_64" || lower == "x64" || lower == "x86-64" || lower == "amd64")
		return Arch::X86_64;
	if (lower == "arm" || lower == "arm32" || lower == "a32" || lower == "armv5te")
		return Arch::Arm;
	if (lower == "thumb" || lower == "thumb16" || lower == "t32")
		return Arch::Thumb;
	if (lower == "mips" || lower == "mips32" || lower == "mips1" || lower == "mips32el")
		return Arch::Mips32;
	return std::nullopt;
}

} // namespace rea
