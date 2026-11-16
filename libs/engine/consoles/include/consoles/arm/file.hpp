#pragma once
/**
 * @file file.hpp
 * @brief Fichier binaire à accès direct (RAII sur `std::FILE`) : ROM lues par sections,
 *        sauvegardes, micrologiciel, image de carte SD.
 */
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace consoles::arm {

class File {
public:
	File() = default;
	/// `mode` de `std::fopen` (« rb », « wb », « rb+ ») ; vide si l'ouverture échoue.
	[[nodiscard]] static std::optional<File> Open(const std::string &path, const char *mode) {
		if (path.empty())
			return std::nullopt;
		File file;
		file.m_file.reset(std::fopen(path.c_str(), mode));
		if (!file.m_file)
			return std::nullopt;
		return file;
	}
	[[nodiscard]] bool IsOpen() const noexcept { return m_file != nullptr; }
	[[nodiscard]] explicit operator bool() const noexcept { return IsOpen(); }
	/// Longueur du flux en octets (alias historique de GetSize).
	[[nodiscard]] size_t Length() { return GetSize(); }
	void Reset() noexcept { m_file.reset(); }
	[[nodiscard]] size_t GetSize() {
		if (!m_file)
			return 0;
		const long at = std::ftell(m_file.get());
		std::fseek(m_file.get(), 0, SEEK_END);
		const long size = std::ftell(m_file.get());
		std::fseek(m_file.get(), at, SEEK_SET);
		return size > 0 ? size_t(size) : 0;
	}
	bool Seek(int64_t offset) { return m_file && std::fseek(m_file.get(), long(offset), SEEK_SET) == 0; }
	size_t Read(void *data, size_t size) { return m_file ? std::fread(data, 1, size, m_file.get()) : 0; }
	size_t Write(const void *data, size_t size) { return m_file ? std::fwrite(data, 1, size, m_file.get()) : 0; }
	[[nodiscard]] std::vector<uint8_t> ReadAll() {
		std::vector<uint8_t> data(GetSize());
		Seek(0);
		data.resize(Read(data.data(), data.size()));
		return data;
	}

private:
	struct Closer {
		void operator()(std::FILE *f) const noexcept { std::fclose(f); }
	};
	std::unique_ptr<std::FILE, Closer> m_file;
};

} // namespace consoles::arm
