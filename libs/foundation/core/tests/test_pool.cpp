#include <cpp-maker/test.hpp>

#include <string>

#include <core/core.hpp>

TEST(Pool_InsertGetErase) {
	core::Pool<std::string> p;
	const core::Handle a = p.Emplace("hello");
	REQUIRE(a);
	REQUIRE(p.Get(a) != nullptr);
	CHECK_EQ(*p.Get(a), "hello");
	CHECK(p.Contains(a));
	CHECK_EQ(p.Size(), size_t(1));
	CHECK(p.Erase(a));
	CHECK_FALSE(p.Contains(a));
	CHECK(p.Get(a) == nullptr);
	CHECK_EQ(p.Size(), size_t(0));
	CHECK_FALSE(p.Erase(a)); // déjà mort
}

TEST(Pool_ReuseBumpsGeneration) {
	core::Pool<int> p;
	const core::Handle h1 = p.Insert(1);
	p.Erase(h1);
	const core::Handle h2 = p.Insert(2); // réutilise l'emplacement
	CHECK_EQ(h2.index, h1.index);
	CHECK_NE(h2.generation, h1.generation);
	CHECK_FALSE(p.Contains(h1)); // l'ancien handle est mort
	CHECK(p.Contains(h2));
	CHECK_EQ(*p.Get(h2), 2);
}

TEST(Pool_NullHandleNeverValid) {
	core::Pool<int> p;
	const core::Handle null{};
	CHECK_FALSE(null);
	CHECK_FALSE(p.Contains(null));
	CHECK(p.Get(null) == nullptr);
}

TEST(Pool_ForEachAndClear) {
	core::Pool<int> p;
	p.Insert(1);
	p.Insert(2);
	p.Insert(3);
	size_t count = 0, sum = 0;
	p.ForEach([&](core::Handle, int v) { ++count; sum += size_t(v); });
	CHECK_EQ(count, size_t(3));
	CHECK_EQ(sum, size_t(6));
	// Effacement pendant l'itération du milieu.
	p.ForEach([&](core::Handle h, int v) {
		if (v == 2)
			CHECK(p.Erase(h));
	});
	CHECK_EQ(p.Size(), size_t(2));
	p.Clear();
	CHECK(p.Empty());
}
