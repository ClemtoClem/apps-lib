#include <cpp-maker/test.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>

#include <core/morton.hpp>
#include <sql/sql.hpp>

TEST(Sql_CreateInsertSelect) {
	sql::Database db;
	REQUIRE(db.ExecuteScript(R"(
		CREATE TABLE player (id INT PRIMARY KEY, name TEXT, level INT INDEX, score REAL);
		INSERT INTO player VALUES (1, 'Ada', 12, 1520.5), (2, 'Bob', 7, 830.0), (3, 'Cyd', 15, 2210.0);
	)").IsOk());
	auto r = db.Execute("SELECT name, score FROM player WHERE level >= 10 AND score > 1000 ORDER BY score DESC");
	REQUIRE(r.IsOk());
	REQUIRE_EQ(r.Value().rows.size(), 2u);
	CHECK_EQ(r.Value().At(0, 0).AsText(), std::string_view("Cyd"));
	CHECK_EQ(r.Value().plan, std::string("btree range(level)"));
	auto k = db.Execute("SELECT * FROM player WHERE id = ?", std::vector<sql::Value>{sql::Value(2)});
	REQUIRE(k.IsOk());
	CHECK_EQ(k.Value().plan, std::string("hash pk"));
	CHECK_EQ(k.Value().At(0, 1).AsText(), std::string_view("Bob"));
	CHECK(db.Execute("INSERT INTO player VALUES (1, 'Dup', 1, 0.0)").IsError());
	CHECK(db.Execute("SELECT age FROM player").IsError());
}

TEST(Sql_UpdateDeleteLikeInBetween) {
	sql::Database db;
	REQUIRE(db.ExecuteScript("CREATE TABLE cfg (key TEXT PRIMARY KEY, value TEXT, mod TEXT);"
							 "INSERT INTO cfg VALUES ('player.speed','4.3','core'),('player.jump','8.4','core'),('world.seed','7','core'),('x','1','edit');").IsOk());
	auto like = db.Execute("SELECT COUNT(*) FROM cfg WHERE key LIKE 'player.%'");
	CHECK_EQ(like.Value().At(0, 0).AsInt(), 2);
	auto up = db.Execute("UPDATE cfg SET value = '5' WHERE key = 'player.speed'");
	CHECK_EQ(up.Value().affected, 1u);
	CHECK_EQ(db.Execute("SELECT value FROM cfg WHERE key = 'player.speed'").Value().At(0, 0).AsText(), std::string_view("5"));
	CHECK_EQ(db.Execute("DELETE FROM cfg WHERE mod IN ('edit')").Value().affected, 1u);
	CHECK_EQ(db.Execute("SELECT COUNT(*) FROM cfg").Value().At(0, 0).AsInt(), 3);
	CHECK_EQ(db.Execute("INSERT OR REPLACE INTO cfg VALUES ('world.seed','9','core')").Value().affected, 1u);
	CHECK_EQ(db.Execute("SELECT COUNT(*) FROM cfg").Value().At(0, 0).AsInt(), 3);
}

TEST(Sql_PreparedAndSnapshot) {
	sql::Database db;
	REQUIRE(db.Execute("CREATE TABLE ev (id INT PRIMARY KEY, tick INT INDEX, type TEXT, payload BLOB)").IsOk());
	auto ins = db.Prepare("INSERT INTO ev VALUES (?, ?, ?, ?)");
	REQUIRE(ins.IsOk());
	for (int i = 0; i < 1000; ++i) {
		std::vector<sql::Value> p{sql::Value(i), sql::Value(i / 10), sql::Value(i % 2 ? "hit" : "spawn"), sql::Value(sql::Blob{uint8_t(i)})};
		REQUIRE(db.Execute(ins.Value(), p).IsOk());
	}
	auto q = sql::QueryBuilder("ev").Select({"id"}).Where("tick", "=", sql::Value(42)).OrderBy("id").Limit(3);
	auto r = q.Execute(db);
	REQUIRE(r.IsOk());
	CHECK_EQ(r.Value().rows.size(), 3u);
	CHECK_EQ(r.Value().At(0, 0).AsInt(), 420);
	const auto path = (std::filesystem::temp_directory_path() / "apps_lib_sql_test.vqdb").string();
	REQUIRE(db.SaveSnapshot(path));
	sql::Database copy;
	REQUIRE(copy.LoadSnapshot(path).IsOk());
	CHECK_EQ(copy.Execute("SELECT COUNT(*) FROM ev WHERE tick BETWEEN 10 AND 19").Value().At(0, 0).AsInt(), 100);
	std::filesystem::remove(path);
}

TEST(Sql_BTreeLarge) {
	sql::BTreeIndex idx;
	for (uint32_t i = 0; i < 100000; ++i)
		idx.Insert(int64_t((i * 7919u) % 100000u), i);
	CHECK_EQ(idx.Size(), 100000u);
	int64_t prev = -1;
	size_t n = 0;
	idx.Range(1000, 1999, [&](int64_t k, uint32_t) {
		CHECK_GE(k, prev);
		prev = k;
		++n;
		return true;
	});
	CHECK_EQ(n, 1000u);
	for (uint32_t i = 0; i < 100000; i += 2)
		CHECK(idx.Erase(int64_t((i * 7919u) % 100000u), i));
	CHECK_EQ(idx.Size(), 50000u);
}

TEST(Sql_MortonBoxRangesCoverExactly) {
	std::vector<std::pair<uint64_t, uint64_t>> ranges;
	sql::MortonBoxRanges(-3, 0, 5, 4, 2, 9, ranges, 4096);
	size_t covered = 0;
	for (auto [a, b] : ranges)
		for (uint64_t k = a; k <= b; ++k) {
			auto c = core::morton::Decode3(k);
			CHECK(c.x >= -3 && c.x <= 4 && c.y >= 0 && c.y <= 2 && c.z >= 5 && c.z <= 9);
			++covered;
		}
	CHECK_EQ(covered, size_t(8 * 3 * 5));
}

TEST(Sql_RegionStoreAsync) {
	const auto dir = std::filesystem::temp_directory_path() / "apps_lib_region_test";
	std::filesystem::remove_all(dir);
	{
		sql::RegionStore store(dir, 2);
		for (int x = -8; x < 8; ++x)
			for (int z = -8; z < 8; ++z) {
				std::vector<uint8_t> blob(64, uint8_t(x * 16 + z));
				store.SaveAsync(core::morton::Encode3(x, 0, z), std::move(blob));
			}
		store.Flush();
		CHECK(store.Erase(core::morton::Encode3(0, 0, 0)));
		std::atomic<int> loaded{0};
		for (int x = -8; x < 8; ++x)
			store.LoadAsync(core::morton::Encode3(x, 0, 3), [&](uint64_t, std::optional<std::vector<uint8_t>> d) {
				if (d && d->size() == 64)
					++loaded;
			});
		store.Flush();
		CHECK_EQ(loaded.load(), 16);
		CHECK_GT(store.GetStats().regionsOpen, 1u);
	}
	sql::RegionStore reopened(dir, 2);
	auto d = reopened.Load(core::morton::Encode3(-5, 0, 7));
	REQUIRE(d.has_value());
	CHECK_EQ((*d)[0], uint8_t(-5 * 16 + 7));
	CHECK_FALSE(reopened.Load(core::morton::Encode3(0, 0, 0)).has_value());
	std::filesystem::remove_all(dir);
}

TEST(Sql_Benchmark) {
	sql::Database db;
	REQUIRE(db.Execute("CREATE TABLE ent (id INT PRIMARY KEY, chunk INT INDEX, kind TEXT)").IsOk());
	auto ins = db.Prepare("INSERT INTO ent VALUES (?, ?, ?)").Unwrap();
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < 50000; ++i) {
		std::vector<sql::Value> p{sql::Value(i), sql::Value(int64_t(core::morton::Encode3(i % 64, 0, i / 64 % 64))), sql::Value("mob")};
		(void)db.Execute(ins, p);
	}
	const double insMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	auto sel = db.Prepare("SELECT id FROM ent WHERE chunk = ?").Unwrap();
	t0 = std::chrono::steady_clock::now();
	size_t found = 0;
	for (int i = 0; i < 1000; ++i) {
		std::vector<sql::Value> p{sql::Value(int64_t(core::morton::Encode3(i % 64, 0, (i / 64) % 64)))};
		found += db.Execute(sel, p).Value().rows.size();
	}
	const double selMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("    sql : 50k INSERT préparés %.1f ms, 1000 SELECT indexés %.2f ms (%zu lignes)\n", insMs, selMs, found);
	CHECK_GT(found, 0u);
}
