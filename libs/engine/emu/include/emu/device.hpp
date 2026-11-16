#pragma once
/**
 * @file device.hpp
 * @brief Périphérique émulé : nœud d'un arbre identifié par une étiquette (`device_t` de MAME).
 *
 * Une machine est un arbre de périphériques (« gbc », « gbc:cpu », « gbc:ppu »…). Chaque
 * périphérique possède ses enfants (`std::unique_ptr`) ; les liens entre périphériques sont
 * des références résolues à la construction ou dans `Start()`.
 *
 * Cycle de vie, piloté par `Machine` : construction de tout l'arbre → `Start()` (parents
 * avant enfants : allocation des temporisateurs, résolution des liens) → `Reset()` (état de
 * mise sous tension, rejouable) → exécution → `Stop()` (enfants avant parents). `State()`
 * sérialise l'état propre du périphérique (jamais celui de ses enfants).
 */
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emu {

class Machine;
class StateArchive;

class Device {
public:
	Device(Machine &machine, std::string tag, Device *owner = nullptr);
	virtual ~Device();
	Device(const Device &) = delete;
	Device &operator=(const Device &) = delete;

	[[nodiscard]] const std::string &Tag() const noexcept { return m_tag; }
	/// Chemin depuis la racine : « gbc:cart:ram ».
	[[nodiscard]] std::string FullTag() const;
	[[nodiscard]] Device *Owner() const noexcept { return m_owner; }
	[[nodiscard]] Machine &GetMachine() const noexcept { return m_machine; }
	[[nodiscard]] std::span<const std::unique_ptr<Device>> Children() const noexcept { return m_children; }

	/// Crée un enfant possédé par ce périphérique : `Add<Lr35902>("cpu", args…)`.
	template <typename T, typename... Args> T &Add(std::string tag, Args &&...args) {
		auto child = std::make_unique<T>(m_machine, std::move(tag), this, std::forward<Args>(args)...);
		T &ref = *child;
		m_children.push_back(std::move(child));
		return ref;
	}
	/// Descendant par chemin relatif (« cart:ram ») ; `nullptr` s'il n'existe pas.
	[[nodiscard]] Device *Find(std::string_view path) const;
	template <typename T> [[nodiscard]] T *Find(std::string_view path) const { return dynamic_cast<T *>(Find(path)); }

	virtual void Start() {}
	virtual void Reset() {}
	virtual void Stop() {}
	virtual void State(StateArchive &archive) { (void)archive; }

private:
	Machine &m_machine;
	std::string m_tag;
	Device *m_owner;
	std::vector<std::unique_ptr<Device>> m_children;
};

} // namespace emu
