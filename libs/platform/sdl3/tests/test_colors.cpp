#include "test.hpp"
// Couleurs nommées : palette complète (LIST_COLORS, noms étendus et palette ui::), recherche par
// nom tolérante (casse, espaces, tirets, soulignés), nom d'une couleur, équivalents FColor.
#include <sdl3/core/structs.hpp>

#include <string>

using namespace sdl3;

TEST(Colors, PaletteTableMatchesFactories) {
	CHECK(sdl3::detail::kNamedColorCount >= 400);
	CHECK(sdl3::detail::kNamedColors.front().name == "TRANSPARENT");
	CHECK(sdl3::detail::kNamedColors.back().name == "UI_WHITE_STRONG");
	CHECK(sdl3::detail::kNamedColors.back().color == Color::UI_WHITE_STRONG());
	// Entrées de la palette étendue dont les octets ressemblent à des exposants (2E, 8E…).
	CHECK(Color::SEA_GREEN() == Color(0x2E, 0x8B, 0x57, 0xFF));
	CHECK(Color::OLIVE_DRAB() == Color(0x6B, 0x8E, 0x23, 0xFF));
}

TEST(Colors, FromNameIgnoresCaseAndSeparators) {
	const auto exact = Color::FromName("SEA_GREEN");
	REQUIRE(exact.has_value());
	CHECK(*exact == Color::SEA_GREEN());
	for (const char *alias : {"sea_green", "sea green", "SeaGreen", "sea-green", "seagreen"}) {
		const auto c = Color::FromName(alias);
		CHECK(c.has_value() && *c == Color::SEA_GREEN());
	}
	CHECK(Color::FromName("ui accent blue").value_or(Color::BLACK()) == Color::UI_ACCENT_BLUE());
	CHECK(!Color::FromName("pas une couleur").has_value());
	CHECK(!Color::FromName("").has_value());
	// Toutes les couleurs de la table se retrouvent par leur nom.
	for (const sdl3::detail::NamedColor &n : sdl3::detail::kNamedColors) {
		const auto c = Color::FromName(n.name);
		CHECK(c.has_value() && *c == n.color);
	}
}

TEST(Colors, NameOfColor) {
	CHECK(Color::RED().Name() == std::optional<std::string_view>("RED"));
	CHECK(Color::UI_OVERLAY_DARK().Name() == std::optional<std::string_view>("UI_OVERLAY_DARK"));
	CHECK(!Color(1, 2, 3, 4).Name().has_value());
	// Une valeur partagée par plusieurs noms rend le premier de la liste.
	const auto name = Color::AQUA().Name();
	REQUIRE(name.has_value());
	CHECK(Color::FromName(*name).value_or(Color::BLACK()) == Color::AQUA());
}

TEST(Colors, FColorLookupRoundsToBytes) {
	const auto f = FColor::FromName("tomato");
	REQUIRE(f.has_value());
	CHECK_NEAR(f->r, 1.f, 1e-6f);
	CHECK_NEAR(f->g, float(0x63) / 255.f, 1e-6f);
	CHECK(f->Name() == std::optional<std::string_view>("TOMATO"));
	// 0.3882 × 255 = 98.99 : arrondi à 99 (0x63) — une troncature donnerait 98 et aucun nom.
	CHECK(FColor(1.f, 0.3882f, float(0x47) / 255.f, 1.f).Name() == std::optional<std::string_view>("TOMATO"));
	CHECK(!FColor::FromName("inconnue").has_value());
}

TEST(Colors, NamesListAllNamedColors) {
	const auto names = Color::Names();
	CHECK(names.size() == sdl3::detail::kNamedColorCount);
	CHECK(names.front() == "TRANSPARENT");
	for (std::string_view name : names) { // chaque nom se retrouve par FromName
		const auto c = Color::FromName(name);
		CHECK(c.has_value());
	}
	CHECK(Color::FromName(names[1]).has_value());
}
