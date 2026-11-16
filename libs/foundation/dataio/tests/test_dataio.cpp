#include <cpp-maker/test.hpp>

#include <dataio/binary.hpp>
#include <dataio/json.hpp>

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

TEST(Json_ParseWithCommentsAndWrite) {
	auto r = dataio::ParseJson(R"(// commentaire
	{ "a": 1, "b": [true, null, "xé"], /* bloc */ "c": {"d": 2.5,}, # fin
	})");
	REQUIRE(r.IsOk());
	const dataio::Json &j = r.Value();
	CHECK_EQ(j["a"].AsInt(), 1);
	CHECK_EQ(j.At("c.d").AsNumber(), 2.5);
	CHECK_EQ(j["b"][2].AsString(), std::string_view("x\xc3\xa9"));
	auto again = dataio::ParseJson(dataio::WriteJson(j, -1));
	REQUIRE(again.IsOk());
	CHECK(again.Value() == j);
}

TEST(Json_ErrorPosition) {
	auto r = dataio::ParseJson("{\n  \"a\": ,\n}");
	REQUIRE(r.IsError());
	CHECK_EQ(r.Error().line, 2);
}

TEST(Json_Merge) {
	dataio::Json a = dataio::ParseJson(R"({"p":{"speed":1,"jump":2},"x":1})").Value();
	a.MergeFrom(dataio::ParseJson(R"({"p":{"speed":5}})").Value());
	CHECK_EQ(a.At("p.speed").AsInt(), 5);
	CHECK_EQ(a.At("p.jump").AsInt(), 2);
}

TEST(Binary_RoundTripAndRle) {
	dataio::ByteWriter w;
	w.Put<uint32_t>(0xdeadbeef);
	w.PutVarInt(-123456789);
	w.PutString("chunk");
	std::vector<uint16_t> voxels(4096, 0);
	for (int i = 0; i < 100; ++i) voxels[size_t(i)] = 7;
	dataio::RleEncode16(voxels, w);
	CHECK_LT(w.Size(), 32u);
	dataio::ByteReader r(w.Bytes());
	CHECK_EQ(r.Get<uint32_t>(), 0xdeadbeefu);
	CHECK_EQ(r.GetVarInt(), -123456789);
	CHECK_EQ(r.GetString(), std::string("chunk"));
	std::vector<uint16_t> out(4096);
	CHECK(dataio::RleDecode16(r, out));
	CHECK(out == voxels);
	const uint8_t msg[] = {'1','2','3','4','5','6','7','8','9'};
	CHECK_EQ(dataio::Crc32(msg), 0xCBF43926u);
}

TEST(Json_ReplaceArrayElementKeepsRest) {
	const std::string text = "// en-tête conservé\n[\n\t{\"id\": \"a\", \"v\": [1, 2]},\n\t// commentaire entre éléments\n\t{\"id\": \"b\", \"s\": \"]}\\\"\"},\n\t3\n]\n";
	const auto spans = dataio::JsonArrayElementSpans(text);
	REQUIRE(spans.size() == 3u);
	CHECK_EQ(text.substr(spans[2].first, spans[2].second - spans[2].first), std::string("3"));
	dataio::Json v = dataio::Json::MakeObject();
	v.Set("id", dataio::Json("b"));
	v.Set("n", dataio::Json(7));
	auto out = dataio::ReplaceJsonArrayElement(text, 1, v);
	REQUIRE(out.has_value());
	CHECK(out->starts_with("// en-tête conservé\n[\n\t{\"id\": \"a\", \"v\": [1, 2]},\n\t// commentaire entre éléments\n\t{\n\t\t\"id\": \"b\""));
	auto parsed = dataio::ParseJson(*out);
	REQUIRE(parsed.IsOk());
	CHECK_EQ(parsed.Value()[1]["n"].AsInt(), 7);
	CHECK_EQ(parsed.Value()[0]["id"].AsString(), std::string_view("a"));
	CHECK(!dataio::ReplaceJsonArrayElement(text, 9, v).has_value());
	CHECK(dataio::JsonArrayElementSpans("{\"a\": 1}").empty());
	auto appended = dataio::AppendJsonArrayElement(text, dataio::Json(42));
	REQUIRE(appended.has_value());
	auto p2 = dataio::ParseJson(*appended);
	REQUIRE(p2.IsOk());
	CHECK_EQ(p2.Value().Size(), 4u);
	CHECK_EQ(p2.Value()[3].AsInt(), 42);
	auto empty = dataio::AppendJsonArrayElement("[]", dataio::Json(1));
	REQUIRE(empty.has_value());
	CHECK_EQ(dataio::ParseJson(*empty).Value().Size(), 1u);
}

