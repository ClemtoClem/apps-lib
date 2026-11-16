#pragma once
/**
 * @file css.hpp
 * @brief Feuilles de style CSS (sous-ensemble) : sélecteurs, règles, cascade.
 *
 * Sélecteurs pris en charge : type (`button`), universel (`*`), `#id`,
 * `.classe`, `this`/`:root`, pseudo-classes d'état (`:hover`, `:active`,
 * `:checked`…), listes (`a, b`), combinateurs descendant (espace) et enfant
 * (`>`). Déclarations : les propriétés de style.hpp et leurs raccourcis.
 * Une règle invalide est ignorée (jamais d'exception).
 */
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <core/core.hpp>
#include "style.hpp"

namespace ui {

enum class Combinator : uint8_t { Descendant, Child };

struct CompoundSelector {
	std::string type; ///< vide = n'importe quel type
	bool isThis = false;
	bool isRoot = false;
	std::string id;
	std::vector<std::string> classes;
	State states = State::None;

	[[nodiscard]] bool IsPrivate() const noexcept { return isThis || !id.empty(); }
};

struct Selector {
	std::vector<CompoundSelector> parts;
	std::vector<Combinator> combinators;
	uint32_t specificity = 0;

	[[nodiscard]] const CompoundSelector &Subject() const noexcept { return parts.back(); }
	[[nodiscard]] bool IsPrivate() const noexcept { return !parts.empty() && parts.back().IsPrivate(); }
};

[[nodiscard]] Result<Selector, String> ParseSelector(std::string_view text);
[[nodiscard]] std::vector<Selector> ParseSelectorList(std::string_view text);

/// Ce qu'un sélecteur sait d'un élément.
struct ElementData {
	std::string_view type;
	std::string_view id;
	const std::vector<std::string> *classes = nullptr;
	State state = State::None;
	int index = 0;
	int siblingCount = 1;
	bool isRoot = false;
	bool isScopeOwner = false;
};

[[nodiscard]] bool MatchCompound(const CompoundSelector &c, const ElementData &e) noexcept;

struct StyleRule {
	std::vector<Selector> selectors;
	StyleProps props;
	size_t order = 0;
};

class StyleSheet {
public:
	[[nodiscard]] static std::shared_ptr<StyleSheet> Parse(std::string_view css, const std::string &name = std::string());
	[[nodiscard]] static Result<std::shared_ptr<StyleSheet>, String> Load(const std::string &path);

	[[nodiscard]] const std::vector<StyleRule> &Rules() const noexcept { return m_rules; }
	[[nodiscard]] const std::vector<std::string> &Warnings() const noexcept { return m_warnings; }
	[[nodiscard]] const std::string &Name() const noexcept { return m_name; }

	void AddRule(std::vector<Selector> selectors, StyleProps props);
	void Append(std::string_view css);

private:
	std::vector<StyleRule> m_rules;
	std::vector<std::string> m_warnings;
	std::string m_name;
};

using StyleSheetPtr = std::shared_ptr<const StyleSheet>;

} // namespace ui
