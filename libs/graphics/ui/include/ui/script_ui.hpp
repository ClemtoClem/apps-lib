#pragma once
/**
 * @file script_ui.hpp
 * @brief Liaison des scripts (`foundation.script`) à l'interface retenue (`ui::Ui`) : le module
 *        `ui` d'un interpréteur construit des widgets, les met à jour et reçoit leurs évènements.
 *
 *   let panneau = ui.column({anchor: "bottom_right", offset: [-40, -44], gap: 10, pad: 24, bg: "#120e0ce0", radius: 10})
 *   ui.label("Graine", {parent: panneau, bold: true, font_size: 16, color: "#e8c27a"})
 *   let champ = ui.input("nombre", fn(w, texte) { … }, {parent: panneau, w: 160})
 *   ui.button("Entrer", fn(b) { … }, {parent: panneau, w: 300})
 *   let vie = ui.progress(30, 100, {parent: panneau})
 *   vie.value = 25          # champs synchronisés à chaque image : text, value, max, checked, visible
 *   champ.text              # saisie relue après l'image
 *
 * Widgets : `column`, `row` (conteneurs), `label`, `button`, `input`, `textarea`, `image`,
 * `progress`, `separator`, `checkbox`, `toggle` (interrupteur), `slider`, `choice`, `selectable`,
 * `heading`, `badge`, `richtext` ; `ui.remove(w)`, `ui.clear()`. Chaque widget est une instance
 * de la classe native `Widget` (`set_bg`, `set_color`, `set_text`, `set_visible`, `set_enabled`,
 * `set_value`, `set_checked`, `set_index`, `get_text`, `get_value`, `get_checked`, `get_visible`,
 * `remove`). Options : `parent`, `id`, `anchor` (top_left, top, top_right, left, center, right,
 * bottom_left, bottom, bottom_right), `offset` [x, y], `w`, `h`, `gap`, `pad`, `grow`, `visible`,
 * `disabled`, `tooltip`, `icon` (point de code), `bg`, `radius`, `color`, `font_size`, `bold`,
 * `align` (start, center, end). Les rappels sont appelés après le dessin de l'image.
 */
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <script/script.hpp>

#include "retained.hpp"

namespace ui {

class ScriptUi {
public:
	/// Module `module` de `interp` ; ses widgets vivent dans `ui` (racine plein écran partagée par
	/// tous les `ScriptUi` d'une même interface).
	ScriptUi(script::Interpreter &interp, Ui &ui, std::string_view module = "ui");
	~ScriptUi();
	ScriptUi(const ScriptUi &) = delete;
	ScriptUi &operator=(const ScriptUi &) = delete;

	/// Avant `Ui::Present` : champs des widgets → nœuds.
	void Sync();
	/// Après `Ui::Present` : saisies → champs, puis rappels (clic, validation, changement).
	void Collect();
	/// Retire tous les widgets de ce script.
	void Clear();
	[[nodiscard]] size_t Count() const noexcept { return m_items.size(); }
	/// Erreurs des rappels (le script continue) ; vidées par l'appelant.
	[[nodiscard]] std::vector<std::string> &Errors() noexcept { return m_errors; }

	struct Item;

private:
	friend struct ScriptUiAccess;
	[[nodiscard]] Node *RootNode();
	script::Value Add(std::string kind, WidgetBuilder builder, const script::Value &options, script::Value onEvent);
	void Remove(size_t index);

	script::Interpreter &m_interp;
	Ui &m_ui;
	std::shared_ptr<script::ClassDef> m_widgetClass;
	std::vector<std::unique_ptr<Item>> m_items;
	std::vector<std::pair<script::Value, std::vector<script::Value>>> m_pending; ///< rappels à appeler
	std::vector<std::string> m_errors;
};

} // namespace ui
