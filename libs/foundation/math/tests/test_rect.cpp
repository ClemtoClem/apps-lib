#include <cpp-maker/test.hpp>

#include <math/range.hpp>
#include <math/rect.hpp>

TEST(Rect_AccessorsAndContains) {
	const math::FRect r{10.f, 20.f, 30.f, 40.f};
	CHECK_NEAR(r.Left(), 10.f, 1e-6f);
	CHECK_NEAR(r.Right(), 40.f, 1e-6f);
	CHECK_NEAR(r.Bottom(), 60.f, 1e-6f);
	CHECK(r.Contains(10.f, 20.f)); // coins compris
	CHECK(r.Contains(40.f, 60.f));
	CHECK_FALSE(r.Contains(41.f, 20.f));
	CHECK(r.Contains(math::FRect{10.f, 20.f, 5.f, 5.f}));
	CHECK_FALSE(r.Contains(math::FRect{35.f, 55.f, 10.f, 10.f}));
}

TEST(Rect_InflateTranslateClip) {
	const math::FRect r{0.f, 0.f, 10.f, 10.f};
	const math::FRect big = r.Inflate(2.f, 1.f);
	CHECK(big == math::FRect(-2.f, -1.f, 14.f, 12.f));
	CHECK(r.Translate(5.f, 5.f) == math::FRect(5.f, 5.f, 10.f, 10.f));
	CHECK(r.ClipBy(math::FRect(5.f, 5.f, 10.f, 10.f)) == math::FRect(5.f, 5.f, 5.f, 5.f));
	CHECK(r.ClipBy(math::FRect(20.f, 20.f, 5.f, 5.f)) == math::FRect());
	CHECK(r.Intersects(math::FRect(5.f, 5.f, 10.f, 10.f)));
	CHECK_FALSE(r.Intersects(math::FRect(10.f, 0.f, 1.f, 1.f))); // bords tangents
}

TEST(Rect_FromPointsAndInt) {
	const math::IRect r = math::IRect::FromPoints(10, 30, 2, 4);
	CHECK(r == math::IRect(2, 4, 8, 26));
	const math::IRect i{0, 0, 100, 50};
	CHECK(i.Contains(0, 0));
	CHECK(i.Contains(100, 50));
	CHECK_FALSE(i.Contains(101, 0));
}

TEST(Range_NormalizeClampSample) {
	const math::FRange r{10.f, 0.f}; // ordre inversé normalisé
	CHECK_NEAR(r.min, 0.f, 1e-6f);
	CHECK_NEAR(r.max, 10.f, 1e-6f);
	CHECK_NEAR(r.Clamp(-5.f), 0.f, 1e-6f);
	CHECK_NEAR(r.Clamp(42.f), 10.f, 1e-6f);
	CHECK_NEAR(r.Clamp(3.f), 3.f, 1e-6f);
	CHECK(r.Contains(5.f));
	CHECK_FALSE(r.Contains(11.f));
	CHECK_NEAR(r.Size(), 10.f, 1e-6f);
	CHECK_NEAR(r.Sample(0.25f), 2.5f, 1e-6f);
	const math::IRange ri{3, 9};
	CHECK_EQ(ri.Clamp(0), 3);
	CHECK_EQ(ri.Clamp(99), 9);
	CHECK_EQ(ri.Size(), 6);
	CHECK_EQ(ri.Sample(0.5f), 6); // (3 + (9-3)*0.5) = 6
}
