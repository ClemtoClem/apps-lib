#pragma once
/**
 * @file code_editor.hpp
 * @brief Éditeur de code : coloration syntaxique (règles par langage), marge des numéros de
 *        ligne, ligne courante et ligne d'erreur en surbrillance, crochets appariés, police à
 *        chasse fixe, UTF-8. L'édition (sélection, presse-papiers, indentation automatique,
 *        Tab / Maj+Tab sur plusieurs lignes) est celle de `TextEdit` en mode `code`.
 *
 * Utilisé par `Context::CodeEditor` (interface immédiate) et par la fabrique `ui::CodeEditor()`
 * de l'interface retenue ; les deux partagent `Tokenize`, `DrawCode` et `CodeHit`.
 */
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sdl3/render/canvas.hpp>

#include "text_edit.hpp"

namespace ui {

/// Règles de coloration d'un langage.
struct SyntaxRules {
	std::vector<std::string> keywords;			///< mots réservés
	std::vector<std::string> builtins;			///< modules et fonctions connus
	std::vector<std::string> lineComments{"#", "//"};
	std::string blockOpen = "/*", blockClose = "*/";
	std::string quotes = "\"'";
};

enum class TokenKind : uint8_t { Text, Keyword, Builtin, Number, String, Comment, Function, Operator };

struct Token {
	size_t start = 0, length = 0;
	TokenKind kind = TokenKind::Text;
};

/// Découpe une ligne en jetons. `inBlock` : commentaire bloc ouvert en entrée (mis à jour en sortie).
void Tokenize(std::string_view line, const SyntaxRules &rules, bool &inBlock, std::vector<Token> &out);

/// Couleurs de l'éditeur (dérivées du thème par `Context`).
struct CodePalette {
	sdl3::Color background = sdl3::Rgba(15, 17, 22), text = sdl3::Rgba(220, 224, 232), keyword = sdl3::Rgba(198, 120, 221),
			   builtin = sdl3::Rgba(97, 175, 239), number = sdl3::Rgba(209, 154, 102), string = sdl3::Rgba(152, 195, 121),
			   comment = sdl3::Rgba(110, 118, 134), function = sdl3::Rgba(229, 192, 123), operatorColor = sdl3::Rgba(86, 182, 194),
			   gutter = sdl3::Rgba(20, 23, 30), gutterText = sdl3::Rgba(90, 98, 112), gutterCurrent = sdl3::Rgba(200, 205, 215),
			   currentLine = sdl3::Rgba(255, 255, 255, 10), selection = sdl3::Rgba(76, 140, 236, 90), caret = sdl3::Rgba(240, 240, 240),
			   error = sdl3::Rgba(222, 76, 82), bracket = sdl3::Rgba(255, 255, 255, 40);
	[[nodiscard]] sdl3::Color Of(TokenKind kind) const noexcept;
};

struct CodeEditorOptions {
	const SyntaxRules *syntax = nullptr; ///< nullptr : texte sans coloration
	bool readOnly = false;
	bool lineNumbers = true;
	int errorLine = 0;			  ///< ligne en erreur (à partir de 1), 0 : aucune
	float fontSize = 0.f;		  ///< 0 : taille du thème − 1
	std::string_view placeholder; ///< texte grisé d'un éditeur vide
};

/// Géométrie de l'éditeur dans sa boîte : marge des numéros, origine du texte (défilement compris).
struct CodeLayout {
	sdl3::FRect box;
	float gutter = 0.f, lineHeight = 16.f, size = 14.f;
	float originX = 0.f, originY = 0.f;
	[[nodiscard]] sdl3::FRect TextArea() const noexcept { return {box.x + gutter, box.y, box.w - gutter, box.h}; }
};

[[nodiscard]] CodeLayout LayoutCode(sdl3::Canvas &canvas, const sdl3::FRect &box, std::string_view text, const TextEdit &edit, const CodeEditorOptions &options,
									float size);
/// Garde le curseur visible (défilement de `edit`).
void ScrollCodeToCaret(sdl3::Canvas &canvas, const CodeLayout &layout, std::string_view text, TextEdit &edit);
void DrawCode(sdl3::Canvas &canvas, const CodeLayout &layout, std::string_view text, const TextEdit &edit, const CodeEditorOptions &options,
			  const CodePalette &palette, bool focused, bool caretOn);
/// Position dans le texte du point (x, y) de l'écran.
[[nodiscard]] size_t CodeHit(sdl3::Canvas &canvas, const CodeLayout &layout, std::string_view text, float x, float y);

} // namespace ui
