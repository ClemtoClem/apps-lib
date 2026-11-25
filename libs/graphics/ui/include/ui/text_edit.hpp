#pragma once
/**
 * @file text_edit.hpp
 * @brief Édition de texte commune à tous les widgets : curseur, sélection, presse-papiers,
 *        déplacements au clavier et à la souris. Utilisé par les champs de l'interface
 *        immédiate (`Context::TextInputAt`, `Context::TextArea`, `DragNumber`), les champs de
 *        l'interface retenue (`LineEdit`, `TextArea`) et les textes statiques sélectionnables.
 *
 * Clavier (`HandleKeys`) :
 *  - saisie (texte de SDL, chiffres du pavé numérique compris), Entrée (nouvelle ligne en
 *    multi-ligne, sinon validation), Échap (abandon) ;
 *  - ← → (Ctrl : par mot), Origine / Fin (Ctrl : début / fin du texte), ↑ ↓ et Page préc. /
 *    Page suiv. en multi-ligne ; Maj étend la sélection ; pavé numérique sans Verr. num. idem ;
 *  - Retour arrière / Suppr (Ctrl : par mot), Ctrl+A tout sélectionner, Ctrl+C / Ctrl+Insert
 *    copier, Ctrl+X / Maj+Suppr couper, Ctrl+V / Maj+Insert coller (presse-papiers de SDL).
 * En lecture seule, seules la sélection et la copie restent actives.
 *
 * Positions en octets UTF-8, toujours sur une limite de caractère. Les lignes sont séparées
 * par '\n' (pas de retour automatique).
 */
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdl3/render/canvas.hpp>

namespace sdl3 {
class EventsManager;
}

namespace ui {

/// Largeur logique d'un texte d'une ligne (police et taille du widget).
using MeasureFn = std::function<float(std::string_view)>;

struct TextEditResult {
	bool changed = false;	///< le texte a été modifié
	bool submitted = false; ///< Entrée (mono-ligne)
	bool cancelled = false; ///< Échap
	bool moved = false;		///< curseur ou sélection déplacés
};

class TextEdit {
public:
	enum class Motion : uint8_t { Left, Right, WordLeft, WordRight, LineStart, LineEnd, Up, Down, PageUp, PageDown, Start, End };

	size_t cursor = 0; ///< position du curseur (octets)
	size_t anchor = 0; ///< autre bout de la sélection (= cursor : aucune)
	bool multiline = false;
	bool readOnly = false;
	bool secret = false; ///< mot de passe : ni copie ni coupe
	/// Éditeur de code : Entrée reprend l'indentation (un cran de plus après « { »), « } » en
	/// début de ligne la retire, Tab / Maj+Tab indentent ou désindentent les lignes sélectionnées.
	bool code = false;
	std::string indentUnit = "\t";
	// État de vue gardé d'une image à l'autre par le widget.
	float scrollX = 0.f, scrollY = 0.f;
	// ── Cadence de répétition (maintien d'une touche) ────────────────────────
	/// La répétition du système (trop rapide pour un champ de saisie) est remplacée
	/// par une cadence propre à l'application : première action immédiate, puis une
	/// action toutes les `repeatInterval` secondes après `repeatDelay`, tant que la
	/// touche reste enfoncée. Ajustable par widget selon le contexte.
	float repeatDelay = 0.45f;    ///< secondes avant la première répétition
	float repeatInterval = 0.06f; ///< secondes entre deux répétitions

	[[nodiscard]] bool HasSelection() const noexcept { return cursor != anchor; }
	/// Bornes ordonnées de la sélection.
	[[nodiscard]] std::pair<size_t, size_t> Range() const noexcept { return cursor < anchor ? std::pair{cursor, anchor} : std::pair{anchor, cursor}; }
	[[nodiscard]] std::string_view Selected(std::string_view text) const noexcept;
	/// Ramène curseur et ancre dans le texte (texte changé par l'application).
	void Clamp(std::string_view text) noexcept;
	void SelectAll(std::string_view text) noexcept;
	/// Mot (ou suite de séparateurs) contenant `at`.
	void SelectWord(std::string_view text, size_t at) noexcept;
	void Collapse() noexcept { anchor = cursor; }
	void SetCursor(size_t at, bool extend) noexcept;

	// ── Édition ──────────────────────────────────────────────────────────
	/// Remplace la sélection par `s` (sauts de ligne retirés en mono-ligne, caractères de
	/// contrôle ignorés sauf '\n' et '\t'). Faux si rien n'a changé.
	bool Insert(std::string &text, std::string_view s);
	bool DeleteSelection(std::string &text);
	bool Backspace(std::string &text, bool word);
	bool Delete(std::string &text, bool word);
	/// Indente (ou désindente) les lignes touchées par la sélection ; les sélectionne entières.
	bool IndentLines(std::string &text, bool outdent);
	/// Commente les lignes touchées par la sélection (`prefix` + espace, à l'indentation commune)
	/// ou les décommente si toutes les lignes non vides le sont déjà.
	bool ToggleComment(std::string &text, std::string_view prefix);
	/// Nouvelle ligne avec l'indentation de la ligne courante (mode code).
	bool NewLine(std::string &text);
	void Move(std::string_view text, Motion motion, bool extend, const MeasureFn &measure = {}, int pageLines = 10);

	// ── Presse-papiers (sdl3::clipboard) ─────────────────────────────────
	bool Copy(std::string_view text) const;
	bool Cut(std::string &text);
	bool Paste(std::string &text);

	/// Touches et saisie de l'image (`EventsManager`). `dt` pilote la cadence de
	/// répétition (`<= 0` : la répétition du système est appliquée telle quelle).
	TextEditResult HandleKeys(std::string &text, const sdl3::EventsManager &events, float dt, const MeasureFn &measure = {}, int pageLines = 10);

	// ── Souris ───────────────────────────────────────────────────────────
	/// Position la plus proche du point (`x`, `y`) relatif à l'origine du texte.
	[[nodiscard]] static size_t HitTest(std::string_view text, float x, float y, float lineHeight, const MeasureFn &measure);
	/// Appui : place le curseur (Maj : étend) ; double-clic : sélectionne le mot.
	void MousePress(std::string_view text, size_t at, bool extend, bool doubleClick) noexcept;
	/// Glissé, bouton enfoncé : étend la sélection jusqu'à `at`.
	void MouseDrag(size_t at) noexcept { cursor = at; }

	// ── Dessin ───────────────────────────────────────────────────────────
	/// Fond de la sélection, texte (une ligne par '\n') et curseur, à partir de (x, y).
	void Draw(sdl3::Canvas &canvas, std::string_view text, float x, float y, float size, sdl3::CanvasFont font, sdl3::Color textColor, sdl3::Color selectionColor,
			  sdl3::Color caretColor, bool showCaret) const;
	/// Position (relative à l'origine du texte) du curseur : x, y du haut de sa ligne.
	[[nodiscard]] std::pair<float, float> CaretPosition(std::string_view text, float lineHeight, const MeasureFn &measure) const;

	// ── Utilitaires de texte ─────────────────────────────────────────────
	[[nodiscard]] static size_t PrevChar(std::string_view text, size_t p) noexcept;
	[[nodiscard]] static size_t NextChar(std::string_view text, size_t p) noexcept;
	[[nodiscard]] static size_t LineStart(std::string_view text, size_t p) noexcept;
	[[nodiscard]] static size_t LineEnd(std::string_view text, size_t p) noexcept;
	/// Nombre de caractères avant `p` (texte masqué d'un mot de passe).
	[[nodiscard]] static size_t CharCount(std::string_view text, size_t p) noexcept;
	/// Octet du `n`-ième caractère.
	[[nodiscard]] static size_t ByteOfChar(std::string_view text, size_t n) noexcept;

private:
	/// Débuts des lignes touchées par la sélection (ou de la ligne du curseur).
	[[nodiscard]] std::vector<size_t> SelectedLineStarts(std::string_view text) const;

	float m_goalX = -1.f; ///< colonne visée par ↑ ↓ (gardée pendant les déplacements verticaux)

	// État de cadence (répétition pilotée par l'application).
	int m_repeatKey = 0;        ///< dernière touche répétée (SDL_Keycode) ; 0 = aucune
	float m_repeatTimer = 0.f;  ///< temps restant avant la prochaine répétition
	std::string m_lastTyped;    ///< texte saisi à l'image précédente (répétition de saisie)
};

} // namespace ui
