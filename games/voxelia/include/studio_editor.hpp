#pragma once
// Voxelia Studio — éditeur de code (phase 4) : scripts des mods et JSON.
//
//  - Sélection (souris, Maj + flèches, double-clic : mot, Ctrl+A), presse-papiers (Ctrl+C/X/V),
//    annuler / rétablir (Ctrl+Z / Ctrl+Y, frappe regroupée par mot).
//  - Recherche et remplacement (Ctrl+F, Ctrl+H), aller à la ligne (Ctrl+G), commenter
//    (Ctrl+/), dupliquer la ligne (Ctrl+D), indentation automatique et Tab sur sélection.
//  - Coloration syntaxique, diagnostic en direct (analyse 0,4 s après la dernière frappe :
//    ligne soulignée, marqueur dans la gouttière, message), complétion (Ctrl+Espace ou
//    automatique après « . » / 2 lettres : fonctions et modules de l'interpréteur).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ui/ui.hpp>

#include "app.hpp"

namespace voxelia::studio {

[[nodiscard]] size_t ColumnToByte(const std::string &line, size_t col);
[[nodiscard]] size_t ByteToColumn(const std::string &line, size_t byte);

/// Fichier ouvert dans l'éditeur.
struct CodeDocument {
	fs::path path;
	std::string mod;
	std::vector<std::string> lines{""};
	size_t cx = 0, cy = 0;			  // curseur : octet dans la ligne, ligne
	size_t ax = 0, ay = 0;			  // ancre de la sélection
	bool selecting = false;			  // sélection active (ancre ≠ curseur)
	float scrollX = 0, scrollY = 0;	  // défilement (pixels)
	bool dirty = false;
	int errorLine = -1;				  // diagnostic (ligne 0-based, -1 : aucun)
	int errorColumn = 0;
	std::string errorMessage;
	// Annulation.
	struct Snapshot {
		std::vector<std::string> lines;
		size_t cx, cy;
	};
	std::vector<Snapshot> undo, redo;
	int lastEdit = 0; // 1 : frappe (regroupée), 2 : effacement, 0 : autre
	uint64_t lastEditTicks = 0;
	uint64_t version = 0, checkedVersion = 0; // diagnostic en direct

	[[nodiscard]] std::string Text() const;
	void SetText(std::string_view text);
	[[nodiscard]] bool IsScript() const { return path.extension() == ".script"; }
};

/// Réglages et services fournis par le Studio.
struct EditorEnv {
	bool tabMode = false; // tabulation réelle, sinon espaces
	int tabSize = 4;
	/// Noms complétables pour un préfixe (« voxel.o » → membres du module voxel).
	std::function<std::vector<std::string>(std::string_view prefix)> complete;
};

class CodeEditor {
public:
	/// Affiche et édite `doc` dans `r` (`focus` : le clavier va à l'éditeur).
	void Draw(ui::Context &u, const sdl3::FRect &r, CodeDocument &doc, const EditorEnv &env, bool &focus);
	/// Place le curseur (lien de la console, diagnostic) ; ligne et colonne 1-based.
	void Goto(CodeDocument &doc, int line, int column = 1);
	/// Vérifie le document (script : analyse ; JSON : lecture) ; met à jour l'erreur.
	static void Check(CodeDocument &doc);

private:
	void Edit(CodeDocument &doc, int kind);
	void Undo(CodeDocument &doc);
	void Redo(CodeDocument &doc);
	void DeleteSelection(CodeDocument &doc);
	void Insert(CodeDocument &doc, std::string_view text);
	[[nodiscard]] std::string SelectedText(const CodeDocument &doc) const;
	void FindNext(CodeDocument &doc, bool backwards);

	bool m_findOpen = false, m_replaceOpen = false, m_gotoOpen = false;
	std::string m_find, m_replace, m_goto;
	bool m_focusField = false;
	// Complétion.
	bool m_completing = false;
	std::vector<std::string> m_items;
	int m_item = 0;
	std::string m_prefix;
	// Souris.
	bool m_dragging = false;
	uint64_t m_lastClick = 0;
	float m_charW = 8.f;
	size_t m_lastCx = SIZE_MAX, m_lastCy = SIZE_MAX;
};

} // namespace voxelia::studio
