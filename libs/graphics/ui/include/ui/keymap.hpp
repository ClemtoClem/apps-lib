#pragma once
/**
 * @file keymap.hpp
 * @brief Registre des raccourcis clavier : une combinaison (`KeyChord`) associe une touche
 *        (`sdl3::keyboard::Key`) et des modificateurs (Ctrl/Maj/Alt/AltGr) ; une `KeyMap` lie des
 *        actions nommées à leur combinaison, avec défauts, réinitialisation, détection de
 *        conflit et sérialisation texte. Source de vérité unique du dispatch ET des libellés.
 *
 *   ui::KeyMap map;
 *   map.Add("nouveau", {sdl3::keyboard::Key::N, true});
 *   const std::string_view action = map.Match(sdl3::keyboard::Key::N, true, false, false, false);
 *   map.Serialize();   // "nouveau = Ctrl+N\n"
 */
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdl3/input/input.hpp>

namespace ui {

/// Combinaison de touches : une touche et des modificateurs Ctrl / Maj / Alt / AltGr.
struct KeyChord {
	sdl3::keyboard::Key key = sdl3::keyboard::Key::UNKNOWN;
	bool ctrl = false, shift = false, alt = false, altGr = false;

	constexpr KeyChord() = default;
	constexpr KeyChord(sdl3::keyboard::Key k, bool c = false, bool s = false, bool a = false, bool ag = false) : key(k), ctrl(c), shift(s), alt(a), altGr(ag) {}

	/// Combinaison vide (aucune touche) : jamais déclenchée.
	[[nodiscard]] constexpr bool Valid() const noexcept { return key != sdl3::keyboard::Key::UNKNOWN; }
	constexpr bool operator==(const KeyChord &) const = default;
	constexpr bool operator!=(const KeyChord &) const = default;

	/// Libellé affichable : « Ctrl+Shift+Z », « F5 », « Space »…
	[[nodiscard]] std::string ToString() const;
	/// Analyse un libellé (« ctrl+shift+z », « F5 »…) ; std::nullopt si illisible.
	[[nodiscard]] static std::optional<KeyChord> FromString(std::string_view text);
};

/// Registre des raccourcis clavier (ordre d'enregistrement conservé pour l'affichage).
class KeyMap {
public:
	struct Binding {
		std::string action;
		KeyChord chord;		  ///< combinaison courante
		KeyChord defaultChord; ///< défaut (réinitialisation)
	};

	/// Déclare une action avec son raccourci par défaut.
	void Add(std::string action, KeyChord chord);
	[[nodiscard]] const Binding *Find(std::string_view action) const noexcept;
	[[nodiscard]] Binding *Find(std::string_view action) noexcept;
	/// Change la combinaison d'une action connue ; faux si l'action n'existe pas.
	bool Set(std::string_view action, KeyChord chord) noexcept;
	/// Rétablit le défaut d'une action.
	void Reset(std::string_view action) noexcept;
	void ResetAll() noexcept;
	/// Action déjà liée à `chord` (hors `except`) ; vide sinon (détection de conflit).
	[[nodiscard]] std::string_view Conflict(KeyChord chord, std::string_view except = {}) const noexcept;
	/// Action déclenchée par (key, ctrl, maj, alt, altGr) ; vide sinon.
	[[nodiscard]] std::string_view Match(sdl3::keyboard::Key key, bool ctrl = false, bool shift = false, bool alt = false, bool altGr = false) const noexcept;
	[[nodiscard]] const std::vector<Binding> &Bindings() const noexcept { return bindings_; }

	/// Sérialisation texte (« action = Ctrl+Z » par ligne, uniquement les combinaisons modifiées).
	[[nodiscard]] std::string Serialize() const;
	/// Applique une sérialisation ; les actions inconnues ou illisibles sont ignorées.
	void Deserialize(std::string_view text);

private:
	std::vector<Binding> bindings_;
};

} // namespace ui
