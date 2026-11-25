#include <cpp-maker/test.hpp>

#include <cmath>

#include <math/color.hpp>

TEST(Color_RgbaAndInteger) {
	const math::Color c{255, 128, 0, 255};
	CHECK_EQ(c.r, uint8_t(255));
	CHECK_EQ(c.g, uint8_t(128));
	CHECK_EQ(c.b, uint8_t(0));
	CHECK_EQ(c.a, uint8_t(255));
	CHECK_EQ(uint32_t(c), 0xFF0080FFu); // 0xAABBGGRR : alpha=FF, bleu=00, vert=80, rouge=FF
	// Aller-retour entier.
	const math::Color back{uint32_t(c)};
	CHECK(back == c);
	CHECK(back.WithAlpha(0).Opaque() == math::Color(255, 128, 0, 255));
}

TEST(Color_HexAndNames) {
	const auto red = math::Color::FromHex("#ff0000");
	REQUIRE(red.has_value());
	CHECK(*red == math::Color(255, 0, 0, 255));
	const auto withAlpha = math::Color::FromHex("11223344");
	REQUIRE(withAlpha.has_value());
	CHECK(*withAlpha == math::Color(0x11, 0x22, 0x33, 0x44));
	CHECK_FALSE(math::Color::FromHex("#12").has_value());
	CHECK_FALSE(math::Color::FromHex("#gg0000").has_value());
	const auto named = math::Color::FromName("Tomato");
	REQUIRE(named.has_value());
	CHECK(*named == math::Color(255, 99, 71));
	CHECK_FALSE(math::Color::FromName("pas-une-couleur").has_value());
}

TEST(Color_HsvRoundTrip) {
	const math::Color src{30, 144, 255}; // dodgerblue
	const math::Hsv hsv = math::ColorToHsv(src);
	const math::Color back = math::HsvToColor(hsv);
	CHECK(std::abs(int(back.r) - int(src.r)) <= 1);
	CHECK(std::abs(int(back.g) - int(src.g)) <= 1);
	CHECK(std::abs(int(back.b) - int(src.b)) <= 1);
	// Rouge pur.
	CHECK(math::HsvToColor({0.f, 100.f, 100.f}) == math::Color(255, 0, 0, 255));
}

TEST(Color_HslRoundTrip) {
	const math::Color src{46, 139, 87}; // seagreen
	const math::Hsl hsl = math::ColorToHsl(src);
	const math::Color back = math::HslToColor(hsl);
	CHECK(std::abs(int(back.r) - int(src.r)) <= 1);
	CHECK(std::abs(int(back.g) - int(src.g)) <= 1);
	CHECK(std::abs(int(back.b) - int(src.b)) <= 1);
	// Blanc : luminosité 1 quelle que soit la saturation.
	CHECK(math::HslToColor({0.f, 0.f, 1.f}) == math::Color(255, 255, 255, 255));
}

TEST(Color_SrgbLinearRoundTrip) {
	for (int i = 0; i <= 255; i += 17) {
		const float v = float(i) / 255.f;
		const float back = math::LinearToSrgb(math::SrgbToLinear(v));
		CHECK_NEAR(back, v, 1e-4f);
	}
}

TEST(ColorGradient_SampleAndClamp) {
	math::ColorGradient g;
	CHECK(g.Sample(0.5f) == math::Color(255, 255, 255, 255)); // vide → blanc
	g.AddPoint({0.f, {255, 0, 0}});
	CHECK(g.Sample(0.5f) == math::Color(255, 0, 0)); // un seul point
	g.AddPoint({1.f, {0, 0, 255}});
	const math::Color mid = g.Sample(0.5f);
	CHECK(std::abs(int(mid.r) - 128) <= 1);
	CHECK(std::abs(int(mid.b) - 127) <= 1);
	CHECK(g.Sample(-1.f) == math::Color(255, 0, 0)); // borné à gauche
	CHECK(g.Sample(2.f) == math::Color(0, 0, 255));  // borné à droite
	// Insertion dans le désordre : les points restent triés.
	math::ColorGradient g2;
	g2.AddPoint({1.f, {0, 0, 255}});
	g2.AddPoint({0.f, {255, 0, 0}});
	g2.AddPoint({0.5f, {0, 255, 0}});
	CHECK(g2.Sample(0.25f) == math::Color(128, 128, 0));
	CHECK(g2.Sample(0.75f) == math::Color(0, 128, 128));
}
