#pragma once
/**
 * @file input.hpp
 * @brief Port d'entrée (`ioport` de MAME) : champs nommés (boutons) regroupés en un mot.
 *
 * Le pilote déclare les champs (`AddField("a", 0x01, "A")`) ; l'application presse et
 * relâche par nom ou par masque ; le matériel lit `Read()` (bits à 1 = pressé) ou
 * `ReadActiveLow()`.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "device.hpp"

namespace emu {

class InputPort : public Device {
public:
	struct Field {
		std::string id;	   ///< identifiant stable (« a », « start », « up »)
		std::string label; ///< nom affiché
		uint32_t mask = 0;
	};

	InputPort(Machine &machine, std::string tag, Device *owner);

	void AddField(std::string id, uint32_t mask, std::string label = {});
	[[nodiscard]] const std::vector<Field> &Fields() const noexcept { return m_fields; }
	[[nodiscard]] const Field *FindField(std::string_view id) const noexcept;

	/// Faux si le champ n'existe pas.
	bool SetPressed(std::string_view id, bool pressed);
	void SetMask(uint32_t mask, bool pressed);
	[[nodiscard]] uint32_t Read() const noexcept { return m_state; }
	[[nodiscard]] uint32_t ReadActiveLow() const noexcept { return ~m_state & m_allMask; }
	/// Appelé quand l'état change (interruption de manette, par exemple) avec l'ancien état.
	void OnChange(std::function<void(uint32_t before, uint32_t after)> callback) { m_onChange = std::move(callback); }

	void Reset() override { m_state = 0; }
	void State(StateArchive &archive) override;

private:
	std::vector<Field> m_fields;
	uint32_t m_state = 0, m_allMask = 0;
	std::function<void(uint32_t, uint32_t)> m_onChange;
};

/// Écran tactile (stylet) couvrant une zone d'un écran : coordonnées en pixels de l'écran
/// émulé, relatives au coin de la zone.
class TouchPanel : public Device {
public:
	/// Zone tactile dans les pixels de l'écran `screenTag` (le pilote la décrit, l'application
	/// y projette la souris ou le doigt).
	TouchPanel(Machine &machine, std::string tag, Device *owner, int x, int y, int width, int height);

	[[nodiscard]] int AreaX() const noexcept { return m_x; }
	[[nodiscard]] int AreaY() const noexcept { return m_y; }
	[[nodiscard]] int AreaWidth() const noexcept { return m_width; }
	[[nodiscard]] int AreaHeight() const noexcept { return m_height; }
	/// Coordonnées relatives à la zone (bornées) ; `pressed` faux : stylet levé.
	void Set(int x, int y, bool pressed);
	[[nodiscard]] bool Pressed() const noexcept { return m_pressed; }
	[[nodiscard]] int X() const noexcept { return m_px; }
	[[nodiscard]] int Y() const noexcept { return m_py; }
	void OnChange(std::function<void(bool pressed, int x, int y)> callback) { m_onChange = std::move(callback); }

	void Reset() override;
	void State(StateArchive &archive) override;

private:
	int m_x, m_y, m_width, m_height;
	int m_px = 0, m_py = 0;
	bool m_pressed = false;
	std::function<void(bool, int, int)> m_onChange;
};

} // namespace emu
