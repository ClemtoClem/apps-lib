// Définitions de ui/css.hpp — sélecteurs CSS et feuilles de style.
#include "ui/css.hpp"

#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

#include <core/log.hpp>
#include <dataio/documents/css.hpp>

namespace ui {

// ============================================================================
// Analyse des sélecteurs
// ============================================================================

namespace {

bool IsIdentChar(char c) {
	return std::isalnum(uint8_t(c)) || c == '-' || c == '_';
}

std::optional<State> StateFromPseudo(std::string_view p) {
	if (p == "hover") return State::Hover;
	if (p == "active") return State::Active;
	if (p == "focus") return State::Focus;
	if (p == "checked") return State::Checked;
	if (p == "disabled") return State::Disabled;
	if (p == "selected") return State::Selected;
	if (p == "open") return State::Open;
	return std::nullopt;
}

} // namespace

Result<Selector, String> ParseSelector(std::string_view text) {
	Selector sel;
	// Découpe par combinateur (espace = descendant, '>' = enfant).
	CompoundSelector cur;
	auto pushPart = [&]() {
		if (!cur.type.empty() || cur.isThis || cur.isRoot || !cur.id.empty() || !cur.classes.empty() ||
			cur.states != State::None) {
			sel.parts.push_back(cur);
			cur = CompoundSelector{};
		}
	};
	// Combinateur entre parts.
	std::vector<Combinator> combinators;
	Combinator pending = Combinator::Descendant;

	size_t i = 0;
	const size_t n = text.size();
	// Consommation d'un jeton.
	while (i < n) {
		char c = text[i];
		if (std::isspace(uint8_t(c))) {
			// séparateur de composé ; on enregistre un descendant si un composé suit.
			pending = Combinator::Descendant;
			while (i < n && std::isspace(uint8_t(text[i]))) ++i;
			// espace => pas encore de partie ; on verra au prochain token.
			continue;
		}
		if (c == '>') {
			pending = Combinator::Child;
			++i;
			while (i < n && std::isspace(uint8_t(text[i]))) ++i;
			continue;
		}
		if (c == ',') {
			// fin d'un sélecteur (géré par ParseSelectorList) ; on s'arrête.
			break;
		}
		// Un composé simple : type / * / this / :root / #id / .class / :pseudo.
		bool isPartStart = cur.type.empty() && cur.id.empty() && cur.classes.empty() && !cur.isThis && !cur.isRoot &&
						   cur.states == State::None;
		if (c == '*') {
			pushPart(); // l'universel n'ajoute rien
			++i;
			continue;
		}
		if (c == '#') {
			++i;
			size_t s = i;
			while (i < n && IsIdentChar(text[i])) ++i;
			cur.id = std::string(text.substr(s, i - s));
			continue;
		}
		if (c == '.') {
			++i;
			size_t s = i;
			while (i < n && IsIdentChar(text[i])) ++i;
			cur.classes.emplace_back(text.substr(s, i - s));
			continue;
		}
		if (c == ':') {
			++i;
			size_t s = i;
			while (i < n && (IsIdentChar(text[i]) || text[i] == '(' || text[i] == ')' || std::isdigit(uint8_t(text[i])))) ++i;
			std::string_view pseudo = text.substr(s, i - s);
			if (pseudo == "root")
				cur.isRoot = true;
			else if (auto st = StateFromPseudo(pseudo))
				cur.states = cur.states | *st;
			// pseudo-classes structurelles ignorées ici (:first-child, :nth-child…)
			continue;
		}
		// Type (ou `this`).
		size_t s = i;
		while (i < n && IsIdentChar(text[i])) ++i;
		std::string_view word = text.substr(s, i - s);
		if (word == "this")
			cur.isThis = true;
		else
			cur.type = std::string(word);
		if (!isPartStart) {
			// nouveau composé après un séparateur => enregistrer le précédent.
			pushPart();
			combinators.push_back(pending);
		}
	}
	pushPart();

	if (sel.parts.empty())
		return Err(String("sélecteur vide"));
	// Combinators : un de moins que les parts.
	if (combinators.size() < sel.parts.size())
		combinators.resize(sel.parts.size() - 1, Combinator::Descendant);
	sel.combinators = combinators;

	// Spécificité : ids*10000 + classes/états*100 + types.
	uint32_t spec = 0;
	for (const CompoundSelector &p : sel.parts) {
		if (!p.id.empty())
			spec += 10000;
		spec += uint32_t(p.classes.size()) * 100;
		if (p.states != State::None)
			spec += 100;
		if (!p.type.empty() || p.isThis || p.isRoot)
			spec += 1;
	}
	sel.specificity = spec;
	return Ok(std::move(sel));
}

std::vector<Selector> ParseSelectorList(std::string_view text) {
	std::vector<Selector> out;
	size_t start = 0;
	while (start <= text.size()) {
		size_t comma = text.find(',', start);
		const std::string_view one = text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
		if (auto sel = ParseSelector(one); sel.IsOk())
			out.push_back(std::move(sel.Value()));
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return out;
}

bool MatchCompound(const CompoundSelector &c, const ElementData &e) noexcept {
	if (c.isRoot && !e.isRoot)
		return false;
	if (c.isThis && !e.isScopeOwner)
		return false;
	if (!c.type.empty() && c.type != e.type)
		return false;
	if (!c.id.empty() && c.id != e.id)
		return false;
	for (const std::string &cls : c.classes) {
		bool found = false;
		if (e.classes)
			for (const std::string &have : *e.classes)
				if (have == cls) {
					found = true;
					break;
				}
		if (!found)
			return false;
	}
	if (c.states != State::None && !HasState(e.state, c.states))
		return false;
	return true;
}

// ============================================================================
// StyleSheet
// ============================================================================

std::shared_ptr<StyleSheet> StyleSheet::Parse(std::string_view css, const std::string &name) {
	auto sheet = std::make_shared<StyleSheet>();
	sheet->m_name = name;
	sheet->Append(css);
	return sheet;
}

Result<std::shared_ptr<StyleSheet>, String> StyleSheet::Load(const std::string &path) {
	std::ifstream in(path);
	if (!in)
		return Err(String("feuille introuvable : " + path));
	std::ostringstream ss;
	ss << in.rdbuf();
	auto sheet = Parse(ss.str(), path);
	return Ok(std::move(sheet));
}

void StyleSheet::AddRule(std::vector<Selector> selectors, StyleProps props) {
	StyleRule rule;
	rule.selectors = std::move(selectors);
	rule.props = std::move(props);
	rule.order = m_rules.size();
	m_rules.push_back(std::move(rule));
}

void StyleSheet::Append(std::string_view css) {
	// Le découpage de la feuille (règles, déclarations, commentaires, chaînes
	// citées, parenthèses, règles-@) est délégué au module `dataio`
	// (dataio::CssDocument) : il rend un arbre de nœuds où chaque règle de style
	// est un objet `{ "selector": "...", "declarations": {prop: val, ...} }` et
	// chaque règle-@ est `{ "at": ..., "params"/"body"/"declarations" }`. On
	// convertit ensuite cet arbre en `StyleRule` (sélecteurs via
	// ParseSelectorList, déclarations via SetDeclaration).
	dataio::CssDocument doc;
	if (auto err = doc.DecodeStr(String(css)); err.IsSome()) {
		m_warnings.emplace_back(err.Value().Format());
		return;
	}

	std::function<void(const dataio::NodePtr &)> walk = [&](const dataio::NodePtr &rule) {
		if (!rule || !rule->IsObject())
			return;
		if (rule->Has("selector")) {
			// Règle de style.
			const String selectorText = rule->Get("selector")->stringValue;
			std::vector<Selector> selectors = ParseSelectorList(selectorText.View());
			if (selectors.empty()) {
				m_warnings.emplace_back(String("sélecteur invalide : ") + selectorText);
				return;
			}
			StyleProps props;
			if (dataio::NodePtr decls = rule->Get("declarations"); decls && decls->IsObject()) {
				for (const String &key : decls->Keys()) {
					// Les valeurs sont auto-typées par dataio (bool/int/float/string) ;
					// on les reconvertit en texte pour SetDeclaration. Les propriétés
					// personnalisées --x sont ignorées (pas de var()).
					if (key.StartsWith("--"))
						continue;
					SetDeclaration(props, key.View(), dataio::scalar::ToString(decls->Get(key)).View());
				}
			}
			AddRule(std::move(selectors), std::move(props));
		} else if (rule->Has("body")) {
			// Règle-@ à corps imbriqué (@media, @supports, @keyframes…) : on descend.
			if (dataio::NodePtr body = rule->Get("body"); body && body->IsArray())
				for (size_t i = 0; i < body->GetSize(); ++i)
					walk(body->At(i));
		}
		// Règles-@ à déclarations (@font-face…) et instructions (@import…) : ignorées.
	};

	if (dataio::NodePtr root = doc.GetRoot(); root && root->IsArray())
		for (size_t i = 0; i < root->GetSize(); ++i)
			walk(root->At(i));
}

} // namespace ui
