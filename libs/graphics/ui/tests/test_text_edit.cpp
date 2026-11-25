// Tests de ui::TextEdit (édition de texte commune aux widgets) : insertion, sélection,
// déplacements par caractère, mot, ligne et page, effacements, positionnement à la souris.
// La mesure est monospace (10 px par caractère) : aucune police nécessaire.
#include <cpp-maker/test.hpp>

#include <string>

#include <ui/code_editor.hpp>
#include <ui/text_edit.hpp>

namespace {

float Mono(std::string_view s) {
	size_t n = 0;
	for (char c : s)
		n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1u : 0u;
	return float(n) * 10.f;
}

} // namespace

TEST(TextEdit_InsertReplacesSelection) {
	ui::TextEdit ed;
	std::string text = "Bonjour";
	ed.SetCursor(text.size(), false);
	CHECK(ed.Insert(text, " monde"));
	CHECK(text == "Bonjour monde");
	ed.anchor = 0;
	ed.cursor = 7; // « Bonjour »
	CHECK(ed.Selected(text) == "Bonjour");
	CHECK(ed.Insert(text, "Salut"));
	CHECK(text == "Salut monde");
	CHECK_EQ(ed.cursor, 5u);
	CHECK(!ed.HasSelection());
	// Mono-ligne : les sauts de ligne collés deviennent des espaces, les contrôles disparaissent.
	CHECK(ed.Insert(text, "\na\x01" "b"));
	CHECK(text == "Salut ab monde");
	ui::TextEdit ro;
	ro.readOnly = true;
	CHECK(!ro.Insert(text, "x"));
}

TEST(TextEdit_MotionsAndSelection) {
	ui::TextEdit ed;
	std::string text = "un deux, trois";
	ed.SetCursor(0, false);
	ed.Move(text, ui::TextEdit::Motion::WordRight, false);
	CHECK_EQ(ed.cursor, 3u); // après « un » et l'espace
	ed.Move(text, ui::TextEdit::Motion::WordRight, true);
	CHECK(ed.Selected(text) == "deux, ");
	ed.Move(text, ui::TextEdit::Motion::Left, false); // sans Maj : la sélection se referme à gauche
	CHECK_EQ(ed.cursor, 3u);
	CHECK(!ed.HasSelection());
	ed.Move(text, ui::TextEdit::Motion::End, false);
	ed.Move(text, ui::TextEdit::Motion::WordLeft, true);
	CHECK(ed.Selected(text) == "trois");
	ed.Move(text, ui::TextEdit::Motion::LineStart, true);
	CHECK(ed.Selected(text) == "un deux, trois");
	ed.SelectWord(text, 5);
	CHECK(ed.Selected(text) == "deux");
	ed.SelectAll(text);
	CHECK(ed.Selected(text) == text);
	// UTF-8 : jamais au milieu d'un caractère.
	std::string accents = "été";
	ed.SetCursor(accents.size(), false);
	ed.Move(accents, ui::TextEdit::Motion::Left, false);
	CHECK_EQ(ed.cursor, 3u); // « ét » = 3 octets
	CHECK(ed.Backspace(accents, false)); // efface le « t »
	CHECK(accents == "éé");
}

TEST(TextEdit_MultilineLinesAndPages) {
	ui::TextEdit ed;
	ed.multiline = true;
	std::string text = "ligne 1\nl2\nligne trois\nquatre\ncinq";
	ed.SetCursor(5, false); // « ligne| 1 »
	ed.Move(text, ui::TextEdit::Motion::Down, false, Mono);
	CHECK_EQ(ed.cursor, 10u); // « l2| » : colonne bornée à la fin de la ligne courte
	ed.Move(text, ui::TextEdit::Motion::Down, false, Mono);
	CHECK_EQ(ed.cursor, 16u); // colonne visée conservée : « ligne| trois »
	ed.Move(text, ui::TextEdit::Motion::LineEnd, false, Mono);
	CHECK_EQ(ed.cursor, 22u);
	ed.Move(text, ui::TextEdit::Motion::LineStart, true, Mono);
	CHECK(ed.Selected(text) == "ligne trois");
	ed.SetCursor(0, false);
	ed.Move(text, ui::TextEdit::Motion::PageDown, false, Mono, 3);
	CHECK_EQ(ui::TextEdit::LineStart(text, ed.cursor), 23u); // trois lignes plus bas : « quatre »
	ed.Move(text, ui::TextEdit::Motion::PageDown, false, Mono, 3);
	CHECK_EQ(ui::TextEdit::LineStart(text, ed.cursor), 30u); // moins d'une page : dernière ligne, même colonne
	ed.Move(text, ui::TextEdit::Motion::PageDown, false, Mono, 3);
	CHECK_EQ(ed.cursor, text.size()); // déjà sur la dernière ligne : bout du texte
	ed.Move(text, ui::TextEdit::Motion::PageUp, true, Mono, 10);
	CHECK_EQ(ed.cursor, 0u);
	CHECK(ed.Insert(text, "\n")); // Entrée en multi-ligne : le texte sélectionné est remplacé par une ligne
	CHECK(text == "\n");
}

TEST(TextEdit_DeleteWordsAndHitTest) {
	ui::TextEdit ed;
	std::string text = "alpha beta gamma";
	ed.SetCursor(text.size(), false);
	CHECK(ed.Backspace(text, true));
	CHECK(text == "alpha beta ");
	ed.SetCursor(0, false);
	CHECK(ed.Delete(text, true));
	CHECK(text == "beta ");
	// Souris : caractère le plus proche, ligne sous le point.
	const std::string lines = "abcd\nefgh";
	CHECK_EQ(ui::TextEdit::HitTest(lines, 24.f, 2.f, 16.f, Mono), 2u);	 // entre « b » et « c »
	CHECK_EQ(ui::TextEdit::HitTest(lines, 26.f, 2.f, 16.f, Mono), 3u);	 // plus près de « c|d »
	CHECK_EQ(ui::TextEdit::HitTest(lines, 999.f, 20.f, 16.f, Mono), 9u); // fin de la seconde ligne
	CHECK_EQ(ui::TextEdit::HitTest(lines, -5.f, 20.f, 16.f, Mono), 5u);
	ed.MousePress(lines, 6, false, true); // double-clic : le mot
	CHECK(ed.Selected(lines) == "efgh");
	ed.MousePress(lines, 1, false, false);
	ed.MouseDrag(3);
	CHECK(ed.Selected(lines) == "bc");
	ed.MousePress(lines, 8, true, false); // Maj+clic : étend depuis l'ancre
	CHECK(ed.Selected(lines) == "bcd\nefg");
}

TEST(TextEdit_CodeIndentation) {
	ui::TextEdit e;
	e.multiline = e.code = true;
	std::string text = "fn f() {";
	e.SetCursor(text.size(), false);
	CHECK(e.NewLine(text)); // un cran de plus après « { »
	CHECK(text == "fn f() {\n\t");
	e.Insert(text, "x");
	CHECK(e.NewLine(text)); // même indentation
	CHECK(text == "fn f() {\n\tx\n\t");
	// Tab sur deux lignes sélectionnées, puis Maj+Tab.
	std::string block = "a\nb";
	ui::TextEdit b;
	b.multiline = b.code = true;
	b.SelectAll(block);
	CHECK(b.IndentLines(block, false));
	CHECK(block == "\ta\n\tb");
	CHECK(b.Selected(block) == "\ta\n\tb");
	CHECK(b.IndentLines(block, true));
	CHECK(block == "a\nb");
	CHECK(!b.IndentLines(block, true)); // plus rien à retirer
}

TEST(CodeEditor_Tokenize) {
	ui::SyntaxRules rules;
	rules.keywords = {"let", "fn"};
	rules.builtins = {"math"};
	std::vector<ui::Token> tokens;
	bool inBlock = false;
	const std::string_view line = "let x = math.sqrt(2.5) # fin";
	ui::Tokenize(line, rules, inBlock, tokens);
	const auto kindOf = [&](std::string_view word) {
		for (const ui::Token &t : tokens)
			if (line.substr(t.start, t.length) == word)
				return t.kind;
		return ui::TokenKind::Text;
	};
	CHECK(kindOf("let") == ui::TokenKind::Keyword);
	CHECK(kindOf("math") == ui::TokenKind::Builtin);
	CHECK(kindOf("sqrt") == ui::TokenKind::Function);
	CHECK(kindOf("2.5") == ui::TokenKind::Number);
	CHECK(kindOf("# fin") == ui::TokenKind::Comment);
	CHECK(kindOf("=") == ui::TokenKind::Operator);
	// Chaîne avec guillemet échappé, accents UTF-8 dans un identifiant.
	const std::string_view s = R"(é = "a\"b")";
	ui::Tokenize(s, rules, inBlock, tokens);
	REQUIRE(tokens.size() == 3);
	CHECK(s.substr(tokens[0].start, tokens[0].length) == "é");
	CHECK(tokens[2].kind == ui::TokenKind::String);
	CHECK(tokens[2].start + tokens[2].length == s.size());
	// Commentaire bloc sur plusieurs lignes.
	ui::Tokenize("x /* début", rules, inBlock, tokens);
	CHECK(inBlock);
	ui::Tokenize("suite */ fn", rules, inBlock, tokens);
	CHECK(!inBlock);
	REQUIRE(tokens.size() == 2);
	CHECK(tokens[0].kind == ui::TokenKind::Comment);
	CHECK(tokens[1].kind == ui::TokenKind::Keyword);
}

TEST(TextEdit_ToggleComment) {
	ui::TextEdit e;
	e.multiline = e.code = true;
	std::string text = "fn f() {\n\tx += 1\n\n\ty()\n}";
	// Lignes 2 à 4 (vide comprise) : commentées à l'indentation commune.
	e.anchor = text.find("\tx");
	e.cursor = text.find("y()") + 1;
	CHECK(e.ToggleComment(text, "#"));
	CHECK(text == "fn f() {\n\t# x += 1\n\n\t# y()\n}");
	CHECK(e.ToggleComment(text, "#")); // toutes commentées : décommente
	CHECK(text == "fn f() {\n\tx += 1\n\n\ty()\n}");
	// Une ligne sans sélection : le curseur suit le texte.
	ui::TextEdit one;
	one.multiline = true;
	std::string line = "abc";
	one.SetCursor(1, false);
	CHECK(one.ToggleComment(line, "//"));
	CHECK(line == "// abc");
	CHECK_EQ(one.cursor, 4u);
	std::string blank = "\n";
	ui::TextEdit b;
	CHECK(!b.ToggleComment(blank, "#")); // rien à commenter
}
