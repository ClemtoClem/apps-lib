#pragma once
/**
 * @file state.hpp
 * @brief Archive binaire d'état : un seul chemin de code pour sauvegarder et charger.
 *
 * Chaque périphérique décrit son état dans `Device::State(StateArchive &)` ; les mêmes
 * appels écrivent (sauvegarde) ou lisent (chargement) les champs dans le même ordre. Une
 * lecture au-delà de la fin met l'archive en échec (`Failed()`) et rend des zéros : aucun
 * accès hors bornes, aucune exception.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace emu {

class StateArchive {
public:
	/// Archive de sauvegarde (vide).
	StateArchive() = default;
	/// Archive de chargement sur des octets produits par une sauvegarde.
	explicit StateArchive(std::span<const uint8_t> data) : m_buffer(data.begin(), data.end()), m_saving(false) {}

	[[nodiscard]] bool Saving() const noexcept { return m_saving; }
	[[nodiscard]] bool Failed() const noexcept { return m_failed; }
	/// Marque l'état comme incompatible (structure différente de celle attendue).
	void Fail() noexcept { m_failed = true; }
	[[nodiscard]] const std::vector<uint8_t> &Data() const noexcept { return m_buffer; }
	[[nodiscard]] std::vector<uint8_t> TakeData() noexcept { return std::move(m_buffer); }
	/// Octets restant à lire (chargement).
	[[nodiscard]] size_t Remaining() const noexcept { return m_buffer.size() - m_cursor; }

	/// Bloc d'octets bruts.
	void Bytes(void *data, size_t size);

	/// Valeur trivialement copiable (registre, structure sans pointeur).
	template <typename T>
		requires std::is_trivially_copyable_v<T>
	void Io(T &value) {
		Bytes(&value, sizeof(T));
	}
	template <typename T, size_t N>
		requires std::is_trivially_copyable_v<T>
	void Io(T (&values)[N]) {
		Bytes(values, sizeof(T) * N);
	}
	template <typename T, size_t N>
		requires std::is_trivially_copyable_v<T>
	void Io(std::array<T, N> &values) {
		Bytes(values.data(), sizeof(T) * N);
	}
	/// Tableau de taille fixe (mémoire d'un périphérique) : la taille n'est pas enregistrée.
	template <typename T>
		requires std::is_trivially_copyable_v<T>
	void Io(std::span<T> values) {
		Bytes(values.data(), values.size_bytes());
	}
	/// Vecteur de taille variable : longueur puis contenu (bornée par `maxCount` au chargement).
	template <typename T>
		requires std::is_trivially_copyable_v<T>
	void IoVector(std::vector<T> &values, size_t maxCount = size_t(1) << 28) {
		uint64_t count = values.size();
		Io(count);
		if (!m_saving) {
			if (m_failed || count > maxCount || count * sizeof(T) > Remaining()) {
				m_failed = true;
				values.clear();
				return;
			}
			values.resize(size_t(count));
		}
		Bytes(values.data(), values.size() * sizeof(T));
	}
	void IoString(std::string &text, size_t maxLength = 1 << 16);

	/// Marqueur de cohérence : écrit `tag` à la sauvegarde, le vérifie au chargement
	/// (une divergence d'ordre ou de version met l'archive en échec).
	void Marker(uint32_t tag);

private:
	std::vector<uint8_t> m_buffer;
	size_t m_cursor = 0;
	bool m_saving = true;
	bool m_failed = false;
};

} // namespace emu
