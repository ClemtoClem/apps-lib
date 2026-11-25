#include <cpp-maker/test.hpp>

#include <string>
#include <unordered_map>

#include <core/core.hpp>

TEST(RobinMap_InsertFindEraseRandomized) {
	core::RobinMap<uint64_t, int> map;
	std::unordered_map<uint64_t, int> ref;
	uint64_t rnd = 12345;
	for (int i = 0; i < 200000; ++i) {
		rnd = core::Mix64(rnd + uint64_t(i));
		const uint64_t key = rnd % 5000;
		if (rnd & 1) {
			map.InsertOrAssign(key, i);
			ref[key] = i;
		} else {
			CHECK_EQ(map.Erase(key), ref.erase(key) > 0);
		}
	}
	CHECK_EQ(map.Size(), ref.size());
	for (const auto &[k, v] : ref) {
		const int *found = map.Find(k);
		REQUIRE(found != nullptr);
		CHECK_EQ(*found, v);
	}
	size_t iterated = 0;
	for (const auto &e : map) {
		CHECK_EQ(ref.at(e.key), e.value);
		++iterated;
	}
	CHECK_EQ(iterated, ref.size());
	CHECK_EQ(map.EraseIf([](uint64_t k, int) { return k % 3 == 0; }) > 0, true);
	for (const auto &e : map)
		CHECK_NE(e.key % 3, 0u);
}

TEST(RobinMap_KeyAliasingMovedValue) {
	// Régression : la clé référence un membre de la valeur déplacée.
	struct Item {
		std::string id;
		int payload;
	};
	core::RobinMap<std::string, Item, core::Hasher<std::string>> map;
	Item item{"core:water", 7};
	map.InsertOrAssign(item.id, std::move(item));
	REQUIRE(map.Find(std::string_view("core:water")) != nullptr);
	CHECK_EQ(map.Find(std::string_view("core:water"))->payload, 7);
	CHECK(map.Find(std::string_view("")) == nullptr);
}

TEST(Morton_RoundTripAndLocality) {
	for (int x = -40; x <= 40; x += 7)
		for (int y = -3; y <= 9; ++y)
			for (int z = -40; z <= 40; z += 9) {
				const auto c = core::morton::Decode3(core::morton::Encode3(x, y, z));
				CHECK(c.x == x && c.y == y && c.z == z);
			}
	// Les 8 enfants d'une cellule alignée sont contigus en ordre Z.
	const uint64_t base = core::morton::Encode3(0, 0, 0);
	CHECK_EQ(core::morton::Encode3(1, 1, 1), base + 7);
}

TEST(OptionResultString) {
	Option<int> none;
	CHECK(none.IsNone());
	Option<int> some = Some(3);
	CHECK_EQ(some.Map([](int v) { return v * 2; }).Unwrap(), 6);
	Result<int, StringView> ok = Ok(5);
	Result<int, StringView> err = Err(StringView("échec"));
	CHECK(ok.IsOk());
	CHECK_EQ(err.Error().GetSize(), std::string_view("échec").size());
	CHECK_EQ(ok.UnwrapOr(0), 5);
	String s = "  a,b,,c  ";
	auto parts = s.Trim().Split(',');
	CHECK_EQ(parts.size(), 4u);
	CHECK_EQ(String::Join(parts, "|"), String("a|b||c"));
	CHECK_EQ(unicode::FromUtf16(unicode::ToUtf16("é€𝄞")), std::string("é€𝄞"));
}


TEST(String_ParseAppendTrimFromStream) {
	CHECK(String("42").TryParseInt().Unwrap() == 42);
	CHECK(String("-17").TryParseInt().Unwrap() == -17);
	CHECK(String("+8").TryParseInt().Unwrap() == 8);
	CHECK(String("ff").TryParseInt(16).Unwrap() == 255);
	CHECK(String("12a").TryParseInt().IsNone()); // tout le texte doit être consommé
	CHECK(String("").TryParseInt().IsNone());
	CHECK(String("3.5").TryParseDouble().Unwrap() == 3.5);
	CHECK(String("1e3").TryParseDouble().Unwrap() == 1000.0);
	CHECK(String("x").TryParseDouble().IsNone());
	CHECK(String::FromStream(0.1) == "0.1");	 // représentation la plus courte
	CHECK(String::FromStream(2.0) == "2");
	String s("a");
	s.Append("bc").Append('d').Append(std::string_view("ef")).Append(size_t(2), '-');
	s.Append("xyz", 1);
	CHECK(s == "abcdef--x");
	CHECK(String("  pad \t").TrimLeft() == "pad \t");
	CHECK(String("  pad \t").TrimRight() == "  pad");
	CHECK(String(" \n ").TrimRight().IsEmpty());
}
