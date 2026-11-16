#include <cpp-maker/test.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include <voxel/voxel.hpp>

namespace {

voxel::BlockRegistry MakeRegistry() {
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "stone", "graphics": {"texture": "core:stone"}},
		{"id": "dirt", "graphics": {"texture": "core:dirt"}},
		{"id": "grass", "graphics": {"textures": {"top": "core:grass_top", "side": "core:grass_side", "bottom": "core:dirt"}}},
		{"id": "water", "tags": ["fluid"], "graphics": {"texture": "core:water", "material": "TRANSPARENT"}},
		{"id": "sand", "graphics": {"texture": "core:sand"}},
		{"id": "glowstone", "graphics": {"texture": "core:glowstone", "light": 15}}
	])");
	reg.LoadDefinitions(json.Value(), "core");
	return reg;
}

template <typename Pred> bool Pump(voxel::World &w, const math::FVector3 &focus, Pred &&done, int maxFrames = 4000) {
	for (int i = 0; i < maxFrames; ++i) {
		w.Update(focus, 1.f / 60.f);
		if (done())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

std::filesystem::path TempWorld(const char *name) {
	auto p = std::filesystem::temp_directory_path() / name;
	std::filesystem::remove_all(p);
	return p;
}

} // namespace

TEST(Voxel_ChunkSparseStorage) {
	voxel::Chunk c({1, 2, 3});
	CHECK(c.IsUniform());
	CHECK_EQ(c.MemoryBytes(), sizeof(voxel::Chunk));
	c.Set(1, 1, 1, 5);
	CHECK_FALSE(c.IsUniform());
	CHECK_EQ(c.Get(1, 1, 1), 5);
	CHECK_EQ(c.NonAirCount(), 1u);
	c.Set(1, 1, 1, voxel::kAir);
	c.Compact();
	CHECK(c.IsUniform());
	CHECK_EQ(c.Key(), voxel::MakeChunkKey(1, 2, 3));
	CHECK(voxel::ChunkCoordOfKey(c.Key()) == math::IVec3(1, 2, 3));
	CHECK(voxel::ChunkOfVoxel(-1, 33, 0) == math::IVec3(-1, 1, 0));
}

TEST(Voxel_Orientation) {
	// Identité : les axes ne bougent pas.
	const auto &north = voxel::RotateOrient({0, 1, 0}, voxel::OrientFromName("north").value());
	CHECK(north == math::FVector3(0, 1, 0));
	// Cap est : la face avant (−Z) pointe vers +X.
	const auto &east = voxel::RotateOrient({0, 0, -1}, voxel::OrientFromName("east").value());
	CHECK(std::fabs(east.x - 1.f) < 1e-5f);
	CHECK(std::fabs(east.z) < 1e-5f);
	// « axis=x » : le haut local +Y devient +X.
	const auto &axisx = voxel::RotateOrient({0, 1, 0}, voxel::OrientFromName("axis=x").value());
	CHECK(std::fabs(axisx.x - 1.f) < 1e-5f);
	CHECK(std::fabs(axisx.y) < 1e-5f);
	// OrientFace : la face du monde doit afficher la face locale correspondante.
	CHECK_EQ(voxel::OrientFace(voxel::OrientFromName("east").value(), int(voxel::Face::PosX)), int(voxel::Face::NegZ));
	// Stockage dans un chunk : orientation conservée.
	voxel::Chunk c({0, 0, 0});
	c.Set(2, 2, 2, 7, uint8_t(voxel::OrientFromName("south").value()));
	CHECK_EQ(c.GetOrient(2, 2, 2), uint8_t(voxel::OrientFromName("south").value()));
	CHECK_EQ(c.GetOrient(3, 2, 2), uint8_t(0));
	// Angles : rotation autour de X de 90° → haut = +Z.
	CHECK_EQ(voxel::OrientFromAngles({90, 0, 0}), voxel::OrientFromName("axis=z").value());
}

TEST(Voxel_GreedyMesherMergesFaces) {
	voxel::BlockRegistry reg = MakeRegistry();
	auto ctx = voxel::MeshContext::Build(reg);
	std::vector<voxel::BlockId> pad(voxel::kPaddedVolume, voxel::kAir);
	const voxel::BlockId stone = reg.Find("core:stone");
	for (int z = 0; z < voxel::kChunkSize; ++z)
		for (int x = 0; x < voxel::kChunkSize; ++x)
			pad[size_t(voxel::PaddedIndex(x, 0, z))] = stone; // une dalle 32×32×1
	voxel::MeshData mesh;
	voxel::BuildChunkMesh(pad, {}, *ctx, mesh);
	// 6 faces de la dalle → 6 quads (au lieu de 32·32·2 + 4·32 faces naïves).
	CHECK_EQ(mesh.opaque.size(), 6u * 4u);
	CHECK(mesh.translucent.empty());
}

TEST(Voxel_ChunkBlobRoundTrip) {
	auto voxels = std::make_unique<voxel::BlockId[]>(voxel::kChunkVolume);
	for (int i = 0; i < 1000; ++i)
		voxels[size_t(i)] = 3;
	voxel::EntityRecord rec;
	rec.kind = "core:sheep";
	rec.position = {1, 2, 3};
	rec.props = "{\"wool\":true}";
	auto blob = voxel::WorldStorage::EncodeChunk(voxels.get(), 0, nullptr, std::span(&rec, 1));
	CHECK_LT(blob.size(), 128u);
	voxel::DecodedChunk out;
	REQUIRE(voxel::WorldStorage::DecodeChunk(blob, out));
	CHECK_EQ(out.voxels[999], 3);
	CHECK_EQ(out.voxels[1000], 0);
	REQUIRE_EQ(out.entities.size(), 1u);
	CHECK_EQ(out.entities[0].kind, std::string("core:sheep"));
	CHECK_EQ(out.entities[0].props, rec.props);
}

TEST(Voxel_StreamingSaveAndReload) {
	const auto dir = TempWorld("apps_lib_voxel_world");
	voxel::BlockRegistry reg = MakeRegistry();
	jobs::ThreadPool pool(3);
	math::FVector3 focus{8, 70, 8}; // altitude corrigée à l'ouverture : surface du générateur en (8, 8)
	math::IVec3 edited;
	voxel::BlockId glow = reg.Find("core:glowstone");
	{
		ecs::World ecs;
		ecs.SetPool(pool);
		voxel::WorldConfig cfg;
		cfg.directory = dir;
		cfg.seed = 42;
		cfg.viewDistance = 2;
		cfg.verticalDistance = 1;
		voxel::World world(reg, ecs, pool, cfg);
		REQUIRE(world.Open());
		focus.y = float(world.Terrain().SurfaceHeight(8, 8)) + 2.f; // quel que soit le relief (mer à Y = 0)
		world.SetMeshContext(voxel::MeshContext::Build(reg));
		REQUIRE(Pump(world, focus, [&] { return world.Stats().pendingLoads == 0 && world.LoadedChunkCount() > 30; }));
		const int h = world.SurfaceHeight(8, 8);
		edited = {8, h + 1, 8};
		CHECK(world.SetBlock(edited.x, edited.y, edited.z, glow));
		CHECK_EQ(world.GetBlock(edited.x, edited.y, edited.z), glow);
		voxel::EntityRecord rec;
		rec.kind = "test:marker";
		rec.position = {8.5f, float(h) + 2.f, 8.5f};
		const ecs::Entity e = world.SpawnEntity(rec);
		CHECK(ecs.Get<voxel::GridCell>(e)->slot != UINT32_MAX);
		world.SaveAll();
		CHECK_GE(world.Storage().SavedChunksInBox({-1, -2, -1}, {1, 7, 1}).size(), 1u);
		auto stored = world.Storage().FindStoredEntities({-1, -2, -1}, {1, 7, 1}, "test:marker");
		CHECK_EQ(stored.size(), 1u);
	}
	{
		ecs::World ecs;
		ecs.SetPool(pool);
		voxel::WorldConfig cfg;
		cfg.directory = dir;
		cfg.seed = 999; // ignorée : la graine est relue dans la base
		cfg.viewDistance = 2;
		cfg.verticalDistance = 1;
		voxel::World world(reg, ecs, pool, cfg);
		REQUIRE(world.Open());
		CHECK_EQ(world.Config().seed, 42u);
		REQUIRE(Pump(world, focus, [&] { return world.IsLoaded(edited.x, edited.y, edited.z) && world.Stats().pendingLoads == 0; }));
		CHECK_EQ(world.GetBlock(edited.x, edited.y, edited.z), glow);
		std::vector<ecs::Entity> near;
		world.QueryEntities({8.5f, float(edited.y) + 1.f, 8.5f}, 4.f, near, "test:marker");
		CHECK_EQ(near.size(), 1u);
		CHECK_GE(world.Stats().chunksLoadedFromDisk, 1u);
	}
	std::filesystem::remove_all(dir);
}

TEST(Voxel_EntityGrid_100k) {
	const auto dir = TempWorld("apps_lib_voxel_grid");
	voxel::BlockRegistry reg = MakeRegistry();
	jobs::ThreadPool pool(0);
	ecs::World ecs;
	ecs.SetPool(pool);
	voxel::WorldConfig cfg;
	cfg.directory = dir;
	cfg.viewDistance = 3;
	cfg.verticalDistance = 1;
	voxel::World world(reg, ecs, pool, cfg);
	REQUIRE(world.Open());
	const math::FVector3 focus{0, 70, 0};
	REQUIRE(Pump(world, focus, [&] { return world.Stats().pendingLoads == 0 && world.LoadedChunkCount() > 60; }));
	// 100 000 entités réparties sur les chunks de surface chargés.
	constexpr int N = 100'000;
	math::Rng rng(7);
	for (int i = 0; i < N; ++i) {
		voxel::EntityRecord r;
		r.kind = (i % 10 == 0) ? "core:zombie" : "core:sheep";
		r.position = {rng.NextFloat() * 160.f - 80.f, 64.f + rng.NextFloat() * 20.f, rng.NextFloat() * 160.f - 80.f};
		world.SpawnEntity(r);
	}
	world.UpdateEntityGrid(); // inscription initiale
	// Déplacement massif (toutes les entités bougent), puis mise à jour de la grille.
	auto t0 = std::chrono::steady_clock::now();
	ecs.MakeQuery<voxel::Transform>().ParEach([](voxel::Transform &t) { t.position.x += 7.3f; });
	const double moveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	t0 = std::chrono::steady_clock::now();
	world.UpdateEntityGrid();
	const double gridMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	// Cohérence : la grille contient chaque entité exactement dans le chunk de sa position.
	size_t registered = 0, mismatched = 0;
	world.ForEachChunk([&](voxel::Chunk &c) {
		for (size_t s = 0; s < c.entities.size(); ++s) {
			++registered;
			const auto *g = ecs.Get<voxel::GridCell>(c.entities[s]);
			if (!g || g->chunk != c.Key() || g->slot != s)
				++mismatched;
		}
	});
	CHECK_EQ(mismatched, 0u);
	// Recherche de voisinage O(1) par chunk vs force brute.
	const math::FVector3 player{10, 70, 10};
	t0 = std::chrono::steady_clock::now();
	std::vector<ecs::Entity> zombies;
	world.QueryEntities(player, 24.f, zombies, "core:zombie");
	const double queryMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	size_t brute = 0;
	ecs.MakeQuery<const voxel::Transform, const voxel::Kind, const voxel::GridCell>().Each(
		[&](const voxel::Transform &t, const voxel::Kind &k, const voxel::GridCell &g) {
			if (g.slot != UINT32_MAX && k.id == "core:zombie" && (t.position - player).LengthSq() <= 24.f * 24.f)
				++brute;
		});
	CHECK_EQ(zombies.size(), brute);
	std::printf("    grille 100k : déplacement %.2f ms, UpdateEntityGrid %.2f ms (%zu inscrites, %zu transferts), voisinage %.3f ms (%zu zombies)\n",
				moveMs, gridMs, registered, world.Stats().gridMoves, queryMs, zombies.size());
	std::filesystem::remove_all(dir);
}

TEST(Voxel_Explosion_BlastResistanceAndChain) {
	const auto dir = TempWorld("apps_lib_voxel_explosion");
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "stone", "graphics": {"texture": "core:stone"}},
		{"id": "glass", "graphics": {"texture": "core:glass"}, "gameplay": {"blastResistance": 0.3}},
		{"id": "bedrock", "graphics": {"texture": "core:bedrock"}, "gameplay": {"blastResistance": 1000}},
		{"id": "tnt", "graphics": {"texture": "core:tnt"}, "gameplay": {"explosion": {"radius": 3, "delay": 1}}}
	])");
	reg.LoadDefinitions(json.Value(), "core");
	jobs::ThreadPool pool(2);
	ecs::World ecs;
	ecs.SetPool(pool);
	voxel::WorldConfig cfg;
	cfg.directory = dir;
	cfg.seed = 3;
	cfg.viewDistance = 2;
	cfg.verticalDistance = 1;
	voxel::World world(reg, ecs, pool, cfg);
	REQUIRE(world.Open());
	const math::FVector3 focus{0, 70, 0};
	REQUIRE(Pump(world, focus, [&] { return world.Stats().pendingLoads == 0 && world.LoadedChunkCount() > 0; }));
	const voxel::BlockId glass = reg.Find("core:glass");
	const voxel::BlockId bedrock = reg.Find("core:bedrock");
	const voxel::BlockId tnt = reg.Find("core:tnt");
	// Verre fragile (à l'est), bedrock indestructible (à l'ouest), TNT (au sud).
	CHECK(world.SetBlock(1, 70, 1, glass));
	CHECK(world.SetBlock(-1, 70, 1, bedrock));
	CHECK(world.SetBlock(1, 70, -1, tnt));
	// Explosion au centre (0,5 ; 70,5 ; 0,5), puissance 2,5 : le verre et la TNT
	// sautent, le bedrock résiste, et la TNT est signalée pour enchaînement.
	const std::vector<math::IVec3> chain = world.Explode({0.5f, 70.5f, 0.5f}, 2.5f, 8.f, true);
	CHECK_EQ(world.GetBlock(1, 70, 1), voxel::kAir);	 // verre détruit
	CHECK_EQ(world.GetBlock(-1, 70, 1), bedrock);		 // bedrock intact
	CHECK_EQ(world.GetBlock(1, 70, -1), voxel::kAir);	 // TNT détruite
	CHECK_EQ(chain.size(), 1u);							 // la TNT est enchaînée
	CHECK(chain[0] == math::IVec3(1, 70, -1));
	// Sans destruction de blocs (règle « mobGriefing »), rien n'est cassé.
	CHECK(world.SetBlock(2, 70, 0, glass));
	(void)world.Explode({0.5f, 70.5f, 0.5f}, 2.5f, 8.f, false);
	CHECK_EQ(world.GetBlock(2, 70, 0), glass);
	std::filesystem::remove_all(dir);
}

TEST(Voxel_FallingBlock_ProgressiveGravity) {
	const auto dir = TempWorld("apps_lib_voxel_falling");
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "stone", "graphics": {"texture": "core:stone"}},
		{"id": "sand", "graphics": {"texture": "core:sand"}, "simulation": {"falling": {"enabled": true}}}
	])");
	reg.LoadDefinitions(json.Value(), "core");
	jobs::ThreadPool pool(2);
	ecs::World ecs;
	ecs.SetPool(pool);
	voxel::WorldConfig cfg;
	cfg.directory = dir;
	cfg.seed = 11;
	cfg.viewDistance = 2;
	cfg.verticalDistance = 1;
	voxel::World world(reg, ecs, pool, cfg);
	REQUIRE(world.Open());
	const int h = world.SurfaceHeight(8, 8);
	const math::FVector3 focus{8, float(h) + 2.f, 8};
	REQUIRE(Pump(world, focus, [&] { return world.Stats().pendingLoads == 0 && world.IsLoaded(8, h, 8); }));
	const voxel::BlockId sand = reg.Find("core:sand");
	const int top = h + 10;
	CHECK(world.SetBlock(8, top, 8, sand));
	// Première image : la gravité part de zéro, le bloc n'a pas encore franchi un mètre
	// (pas de téléport instantané).
	world.Update(focus, 1.f / 60.f);
	CHECK_EQ(world.GetBlock(8, top, 8), sand);
	// La chute se termine : le sable atterrit sur le sol (h+1).
	REQUIRE(Pump(world, focus, [&] { return world.GetBlock(8, h + 1, 8) == sand; }, 4000));
	CHECK_EQ(world.GetBlock(8, top, 8), voxel::kAir);
	CHECK_EQ(world.GetBlock(8, h + 1, 8), sand);
	std::filesystem::remove_all(dir);
}

TEST(Voxel_OutlinerCollectFilterSort) {
	ecs::World w;
	const ecs::Entity player = w.Spawn(voxel::Transform{{0, 64, 0}}, voxel::Kind{"core:player"});
	w.Spawn(voxel::Transform{{10, 64, 0}}, voxel::Kind{"core:zombie"}, voxel::Wander{});
	w.Spawn(voxel::Transform{{3, 64, 0}}, voxel::Kind{"core:cow"}, voxel::Wander{});
	w.Spawn(voxel::Transform{{2, 64, 0}}, voxel::Kind{"voxel:item"});
	w.Spawn(voxel::Transform{{200, 64, 0}}, voxel::Kind{"core:arrow_projectile"});
	w.Spawn(voxel::Transform{{5, 64, 0}}); // sans Kind : ignorée

	auto all = voxel::CollectEntities(w, player, {}, {0, 64, 0});
	CHECK_EQ(all.total, 5u);
	REQUIRE(all.entries.size() == 5u);
	// Catégorie puis distance : joueur, cow (3) avant zombie (10), objet, projectile.
	CHECK(all.entries[0].entity == player);
	CHECK_EQ(all.entries[1].kind, std::string("core:cow"));
	CHECK_EQ(all.entries[2].kind, std::string("core:zombie"));
	CHECK(all.entries[3].category == voxel::EntityCategory::Item);
	CHECK(all.entries[4].category == voxel::EntityCategory::Projectile);
	CHECK_EQ(all.perCategory[size_t(voxel::EntityCategory::Creature)], 2u);

	voxel::OutlinerFilter f;
	f.text = "ZOM";
	CHECK_EQ(voxel::CollectEntities(w, player, f, {0, 64, 0}).entries.size(), 1u);
	f = {};
	f.maxDistance = 50;
	CHECK_EQ(voxel::CollectEntities(w, player, f, {0, 64, 0}).entries.size(), 4u);
	f = {};
	f.category = voxel::EntityCategory::Creature;
	auto creatures = voxel::CollectEntities(w, player, f, {0, 64, 0});
	CHECK_EQ(creatures.entries.size(), 2u);
	CHECK_EQ(creatures.total, 5u);
	// Limite : les plus proches, compteurs exacts.
	auto limited = voxel::CollectEntities(w, player, {}, {0, 64, 0}, 2);
	CHECK_EQ(limited.entries.size(), 2u);
	CHECK_EQ(limited.perCategory[size_t(voxel::EntityCategory::Creature)], 2u);
}

namespace {
std::filesystem::path ModsDir() { return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() / "games/voxelia/mods"; }
}

TEST(Model_NodeboxFormsAndBounds) {
	for (const char *src : {R"([[-0.5, -0.5, -0.5, 0.5, 0, 0.5], [-0.5, 0, 0, 0.5, 0.5, 0.5]])",
							R"({"type": "FIXED", "fixed": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5], [0.5, 0.5, 0.5, -0.5, 0, 0]]})",
							R"({"shape": "FIXED", "fixed": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5], [-0.5, 0, 0, 0.5, 0.5, 0.5]]})"}) {
		auto m = voxel::ParseNodebox(dataio::ParseJson(src).Value());
		REQUIRE(m.IsOk());
		CHECK_EQ(m.Value().BoxCount(), 2u);
		const auto b = m.Value().Bounds();
		CHECK(std::fabs(b.min.y + 0.5f) < 1e-5f && std::fabs(b.max.y - 0.5f) < 1e-5f);
		// Écriture puis relecture : mêmes boîtes.
		auto back = voxel::ParseNodebox(voxel::BoxListToJson(m.Value()));
		REQUIRE(back.IsOk());
		CHECK(std::fabs(back.Value().parts[0].boxes[1].from.z) < 1e-5f);
	}
	CHECK(!voxel::ParseNodebox(dataio::ParseJson("{}").Value()).IsOk());
	CHECK_EQ(voxel::MakeCrossModel().TriangleCount(), 4u);
}

TEST(Model_RigRoundTripKeepsUnknownKeys) {
	const auto def = dataio::ParseJson(R"({"type": "model", "id": "biped", "format": "rig", "textureSize": [64, 32], "custom": 1,
		"parts": [{"name": "head", "pivot": [0, 24, 0], "look": true,
		           "boxes": [{"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]}, {"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [32, 0], "inflate": 0.5, "only64": true}]},
		          {"name": "right_arm", "pivot": [-5, 22, 0], "rotation": [-90, 0, 0], "walk": ["x", 0, 40],
		           "boxes": [{"from": [-8, 12, -2], "size": [4, 12, 4], "uv": [40, 16]}]}]})")
						 .Value();
	auto m = voxel::ParseRig(def);
	REQUIRE(m.IsOk());
	voxel::Model model = m.Value();
	CHECK(model.boxUv);
	CHECK_EQ(model.unitsPerBlock, 16.f);
	CHECK_EQ(model.parts.size(), 2u);
	// Box UV : la face avant (+Z) de la tête 8×8×8 en (0,0) est la région (8,8)-(16,16).
	const auto uv = voxel::BoxUvLayout(model.parts[0].boxes[0]);
	CHECK_EQ(uv[size_t(voxel::BoxFace::PosZ)].u0, 8.f);
	CHECK_EQ(uv[size_t(voxel::BoxFace::PosZ)].v1, 16.f);
	// Bras tourné de -90° en X : il pointe vers l'avant (+Z).
	const auto bounds = model.Bounds();
	CHECK(bounds.max.z > 0.6f);
	model.parts[0].pivot.y = 25;
	model.animations.push_back({"wave", 2.f, true, {{"right_arm", {{0, {0, 0, 0}}, {1, {0, 0, 90}}}, {}}}});
	const dataio::Json out = voxel::RigToJson(model, def);
	CHECK_EQ(out["custom"].AsInt(), 1);
	CHECK(out["parts"][0]["look"].AsBool());
	CHECK(out["parts"][0]["boxes"][1]["only64"].AsBool());
	CHECK_EQ(out["parts"][1]["walk"][2].AsInt(), 40);
	auto again = voxel::ParseRig(out);
	REQUIRE(again.IsOk());
	CHECK_EQ(again.Value().parts[0].pivot.y, 25.f);
	REQUIRE(again.Value().animations.size() == 1u);
	CHECK_EQ(again.Value().animations[0].tracks[0].rotation.size(), 2u);
	// Pose : la clé à t=0,5 donne 45° ; les mouvements procéduraux s'ajoutent.
	const auto pose = voxel::SamplePose(again.Value(), &again.Value().animations[0], 0.5f);
	CHECK(std::fabs(pose[1].rotation.z - 45.f) < 1e-3f);
	const auto walking = voxel::SamplePose(again.Value(), nullptr, 0.f, 0.3f);
	CHECK(std::fabs(walking[1].rotation.x) > 1.f);
}

TEST(Model_KeysAndObj) {
	std::vector<voxel::Keyframe> keys;
	voxel::SetKey(keys, 1.f, {10, 0, 0});
	voxel::SetKey(keys, 0.f, {0, 0, 0});
	voxel::SetKey(keys, 1.f, {20, 0, 0}); // remplace
	CHECK_EQ(keys.size(), 2u);
	CHECK(std::fabs(voxel::SampleKeys(keys, 0.25f).x - 5.f) < 1e-4f);
	CHECK(voxel::RemoveKey(keys, 0.f));
	CHECK_EQ(voxel::SampleKeys(keys, 0.f).x, 20.f);

	auto obj = voxel::ParseObj("o plateau\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\ng pied\nf -4 -3 -2\n");
	REQUIRE(obj.IsOk());
	CHECK_EQ(obj.Value().parts.size(), 2u);
	CHECK_EQ(obj.Value().TriangleCount(), 3u);
	CHECK_EQ(obj.Value().parts[0].vertices[0].uv.y, 1.f); // v inversé (origine en haut)
	auto back = voxel::ParseObj(voxel::ObjToText(obj.Value()));
	REQUIRE(back.IsOk());
	CHECK_EQ(back.Value().TriangleCount(), 3u);
	CHECK(!voxel::ParseObj("v 0 0 0\nf 1 2 3\n").IsOk());
}

TEST(Model_AutoBoxUv) {
	// Cadrillage automatique : deux boîtes de tailles différentes rangées sans chevauchement.
	voxel::Model m;
	m.format = voxel::ModelFormat::Rig;
	m.boxUv = true;
	m.unitsPerBlock = 16.f;
	m.textureWidth = 16;
	m.textureHeight = 16;
	voxel::ModelPart p;
	p.name = "corps";
	voxel::ModelBox small;
	small.from = {0, 0, 0};
	small.size = {8, 8, 8};
	p.boxes.push_back(std::move(small));
	voxel::ModelBox big;
	big.from = {0, 0, 0};
	big.size = {16, 16, 2};
	p.boxes.push_back(std::move(big));
	m.parts.push_back(std::move(p));
	voxel::AutoBoxUv(m, 2);
	const math::FVector2 &u0 = m.parts[0].boxes[0].uv, &u1 = m.parts[0].boxes[1].uv;
	CHECK(u0.x != u1.x || u0.y != u1.y); // origines distinctes
	// Aucun chevauchement entre les rectangles UV de deux boîtes différentes.
	struct R {
		float x0, y0, x1, y1;
	};
	std::vector<R> rects;
	for (const voxel::ModelPart &part : m.parts)
		for (const voxel::ModelBox &box : part.boxes)
			for (const voxel::UvRect &f : voxel::BoxUvLayout(box))
				rects.push_back({std::min(f.u0, f.u1), std::min(f.v0, f.v1), std::max(f.u0, f.u1), std::max(f.v0, f.v1)});
	auto overlap = [](const R &a, const R &b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; };
	bool any = false;
	for (size_t i = 0; i < rects.size() && !any; ++i)
		for (size_t j = i + 1; j < rects.size() && !any; ++j)
			if (overlap(rects[i], rects[j]))
				any = true;
	CHECK(!any);
	// La texture a grandi pour contenir les boîtes (16×16×2 → 36×18 au minimum).
	CHECK(m.textureWidth >= 36);
	CHECK(m.textureHeight >= 18);
}

TEST(Model_MechanicRoundTrip) {
	const auto def = dataio::ParseJson(R"({"type": "model", "id": "porte", "format": "rig", "textureSize": [64, 64],
		"mechanic": {"type": "door", "animation": "ouvrir", "toggle": true},
		"parts": [{"name": "panneau", "pivot": [-8, 16, 0], "boxes": [{"from": [-8, 0, -1], "size": [16, 32, 2], "uv": [0, 0]}]}],
		"animations": [{"name": "ouvrir", "length": 0.5, "loop": false, "tracks": [{"part": "panneau", "rotation": [[0, 0, 0, 0], [0.5, 0, -90, 0]]}]}]})")
						 .Value();
	auto m = voxel::ParseRig(def);
	REQUIRE(m.IsOk());
	REQUIRE(m.Value().mechanic.has_value());
	CHECK(m.Value().mechanic->type == "door");
	CHECK(m.Value().mechanic->animation == "ouvrir");
	CHECK(m.Value().mechanic->toggle);
	// Aller-retour : la mécanique est conservée à l'écriture.
	const dataio::Json out = voxel::RigToJson(m.Value(), def);
	CHECK(out["mechanic"]["type"].AsString() == std::string_view("door"));
	CHECK(out["mechanic"]["animation"].AsString() == std::string_view("ouvrir"));
	CHECK(out["mechanic"]["toggle"].AsBool());
	// Sans mécanique, la clé disparaît.
	voxel::Model no = m.Value();
	no.mechanic.reset();
	const dataio::Json clean = voxel::RigToJson(no, def);
	CHECK(clean.Find("mechanic") == nullptr);
}

TEST(Model_ModFilesParse) {
	const auto mods = ModsDir();
	auto read = [](const std::filesystem::path &p) {
		std::ifstream f(p, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	// Tous les rigs du mod core.
	auto rigs = dataio::ParseJson(read(mods / "core/models/rigs.json"));
	REQUIRE(rigs.IsOk());
	size_t n = 0;
	for (const dataio::Json &def : rigs.Value().AsArray()) {
		auto m = voxel::ParseRig(def);
		CHECK(m.IsOk());
		n += m.IsOk();
	}
	CHECK(n >= 20u);
	auto table = voxel::ParseObj(read(mods / "core/models/table.obj"));
	REQUIRE(table.IsOk());
	CHECK(table.Value().TriangleCount() >= 10u);
	const std::string chair = read(mods / "core/models/chair.b3d");
	auto b3d = voxel::ParseB3d(std::span(reinterpret_cast<const uint8_t *>(chair.data()), chair.size()));
	REQUIRE(b3d.IsOk());
	CHECK(b3d.Value().TriangleCount() > 4u);
	const auto bb = b3d.Value().Bounds();
	std::printf("chair.b3d : %zu triangles, %.2f×%.2f×%.2f\n", b3d.Value().TriangleCount(), double(bb.Size().x), double(bb.Size().y), double(bb.Size().z));
	CHECK(!voxel::ParseB3d(std::span<const uint8_t>()).IsOk());
}

TEST(Voxel_NodeboxMeshConnectAndCollision) {
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "stone", "graphics": {"texture": "core:stone"}},
		{"id": "slab", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5]], "graphics": {"texture": "core:stone"}},
		{"id": "fence", "tags": ["fence"], "nodebox": [[-0.125, -0.5, -0.125, 0.125, 0.5, 0.125]],
		 "connects": {"to": ["#fence", "#solid"], "east": [[0.125, 0, -0.06, 0.5, 0.25, 0.06]], "west": [[-0.5, 0, -0.06, -0.125, 0.25, 0.06]]},
		 "physics": {"collision": {"shape": "FIXED", "fixed": [[-0.25, -0.5, -0.25, 0.25, 1, 0.25]]}},
		 "graphics": {"texture": "core:planks"}}
	])");
	REQUIRE(json.IsOk());
	reg.LoadDefinitions(json.Value(), "core");
	const voxel::BlockId stone = reg.Find("core:stone"), slab = reg.Find("core:slab"), fence = reg.Find("core:fence");
	CHECK(!reg.Get(slab).IsOpaque());
	CHECK(reg.Get(slab).HasShape());
	CHECK_EQ(reg.Get(slab).collision.size(), 1u); // la collision suit la forme
	CHECK(reg.Connects(reg.Get(fence).connect, fence));
	CHECK(reg.Connects(reg.Get(fence).connect, stone)); // #solid
	CHECK(!reg.Connects(reg.Get(fence).connect, slab));
	auto ctx = voxel::MeshContext::Build(reg);

	// Barrière isolée : 1 boîte ; avec une barrière à l'est : 2 boîtes.
	std::vector<voxel::BlockBox> boxes;
	voxel::ShapeBoxes(*ctx, fence, {voxel::kAir, voxel::kAir, voxel::kAir, voxel::kAir, voxel::kAir, voxel::kAir}, boxes);
	CHECK_EQ(boxes.size(), 1u);
	voxel::ShapeBoxes(*ctx, fence, {fence, voxel::kAir, voxel::kAir, voxel::kAir, voxel::kAir, voxel::kAir}, boxes);
	CHECK_EQ(boxes.size(), 2u);

	// Maillage : une dalle seule → 6 faces détaillées, aucun quad de cube ; posée sur la
	// pierre, sa face du dessous (collée à un cube plein) disparaît.
	std::vector<voxel::BlockId> pad(voxel::kPaddedVolume, voxel::kAir);
	pad[size_t(voxel::PaddedIndex(4, 4, 4))] = slab;
	voxel::MeshData mesh;
	voxel::BuildChunkMesh(pad, {}, *ctx, mesh);
	CHECK_EQ(mesh.detail.size(), 24u);
	CHECK(mesh.opaque.empty());
	pad[size_t(voxel::PaddedIndex(4, 3, 4))] = stone;
	voxel::BuildChunkMesh(pad, {}, *ctx, mesh);
	CHECK_EQ(mesh.detail.size(), 20u);
	CHECK_EQ(mesh.opaque.size(), 24u); // la pierre garde ses 6 faces : la dalle n'est pas opaque
}

TEST(Mechanics_JointsSolve) {
	ecs::World w;
	// Poulie : cabine (A) et contrepoids (B), ratio 1, longueur = yA + yB.
	const ecs::Entity cabin = w.Spawn(voxel::Transform{{0, 10, 0}}, voxel::Velocity{}, voxel::Mass{500.f});
	const ecs::Entity weight = w.Spawn(voxel::Transform{{2, 20, 0}}, voxel::Velocity{}, voxel::Mass{500.f});
	voxel::Joint pulley;
	pulley.type = voxel::JointType::Pulley;
	pulley.target = voxel::EnsureUid(w, weight);
	w.Add(cabin, voxel::Joints{{pulley}});
	for (int i = 0; i < 20; ++i)
		(void)voxel::SolveJoints(w, 0.05f);
	CHECK(std::fabs(w.Get<voxel::Transform>(cabin)->position.y - 10.f) < 1e-3f); // masses égales : équilibre
	CHECK(std::fabs(w.Get<voxel::Transform>(cabin)->position.y + w.Get<voxel::Transform>(weight)->position.y - 30.f) < 1e-3f);
	w.Get<voxel::Mass>(cabin)->kg = 800.f; // cabine plus lourde : elle descend, le contrepoids monte
	for (int i = 0; i < 20; ++i)
		(void)voxel::SolveJoints(w, 0.05f);
	const float yc = w.Get<voxel::Transform>(cabin)->position.y;
	CHECK(yc < 9.f);
	CHECK(std::fabs(yc + w.Get<voxel::Transform>(weight)->position.y - 30.f) < 1e-3f);
	// Moteur : monte à 2 m/s malgré la surcharge, jusqu'à la limite haute.
	voxel::Joint &pj = w.Get<voxel::Joints>(cabin)->list[0];
	pj.motor = true;
	pj.motorSpeed = 2.f;
	pj.motorForce = 50000.f;
	pj.minLimit = 0.f;
	pj.maxLimit = 12.f;
	for (int i = 0; i < 200; ++i)
		(void)voxel::SolveJoints(w, 0.05f);
	CHECK(std::fabs(w.Get<voxel::Transform>(cabin)->position.y - 12.f) < 1e-3f);

	// Charnière : la porte tourne de 90° autour de l'ancre puis s'arrête à la limite.
	const ecs::Entity frame = w.Spawn(voxel::Transform{{0, 0, 0}});
	const ecs::Entity door = w.Spawn(voxel::Transform{{1, 0, 0}}, voxel::Velocity{});
	voxel::Joint hinge;
	hinge.type = voxel::JointType::Hinge;
	hinge.target = voxel::EnsureUid(w, door);
	hinge.motor = true;
	hinge.motorSpeed = 90.f;
	hinge.maxLimit = 90.f;
	w.Add(frame, voxel::Joints{{hinge}});
	for (int i = 0; i < 40; ++i)
		(void)voxel::SolveJoints(w, 0.05f);
	const math::FVector3 dp = w.Get<voxel::Transform>(door)->position;
	CHECK(std::fabs(dp.x) < 1e-3f && std::fabs(std::fabs(dp.z) - 1.f) < 1e-3f);
	CHECK_EQ(w.Get<voxel::Joints>(frame)->list[0].position, 90.f);

	// Glissière : 1,5 m le long de X à 1 m/s, bornée.
	const ecs::Entity rail = w.Spawn(voxel::Transform{{0, 5, 0}});
	const ecs::Entity panel = w.Spawn(voxel::Transform{{0, 5, 1}});
	voxel::Joint slide;
	slide.type = voxel::JointType::Slider;
	slide.target = voxel::EnsureUid(w, panel);
	slide.motor = true;
	slide.motorSpeed = 1.f;
	slide.maxLimit = 1.5f;
	w.Add(rail, voxel::Joints{{slide}});
	for (int i = 0; i < 60; ++i)
		(void)voxel::SolveJoints(w, 0.05f);
	CHECK(std::fabs(w.Get<voxel::Transform>(panel)->position.x - 1.5f) < 1e-3f);

	// Attache : suit la position et le cap du porteur.
	const ecs::Entity carrier = w.Spawn(voxel::Transform{{0, 0, 0}});
	const ecs::Entity rider = w.Spawn(voxel::Transform{{0, 1, 0}});
	voxel::Joint attach;
	attach.target = voxel::EnsureUid(w, rider);
	w.Add(carrier, voxel::Joints{{attach}});
	(void)voxel::SolveJoints(w, 0.05f);
	w.Get<voxel::Transform>(carrier)->position = {5, 2, 0};
	const auto st = voxel::SolveJoints(w, 0.05f);
	CHECK(std::fabs(w.Get<voxel::Transform>(rider)->position.y - 3.f) < 1e-4f);
	CHECK_EQ(st.active, st.joints);
}

TEST(Mechanics_PersistenceAndRecordVersions) {
	ecs::World w;
	const ecs::Entity a = w.Spawn(voxel::Transform{{1, 2, 3}}, voxel::Kind{"core:cabin"});
	const uint64_t uid = voxel::EnsureUid(w, a);
	CHECK_EQ(voxel::EnsureUid(w, a), uid); // stable
	w.Add(a, voxel::Mass{420.f});
	w.Add(a, voxel::LightEmitter{{1, 0.5f, 0.2f}, 2.f, 12.f, {0, 1, 0}, true});
	w.Add(a, voxel::SoundEmitter{"core:metal_clang", 0.5f, 20.f, true, true});
	voxel::Joint j;
	j.type = voxel::JointType::Pulley;
	j.target = 0xFEDCBA9876543210ULL; // 64 bits complets
	j.ratio = 2.f;
	w.Add(a, voxel::Joints{{j}});
	const voxel::EntityRecord rec = voxel::CaptureEntity(w, a);
	CHECK_EQ(rec.uid, uid);
	CHECK(!rec.ext.empty());
	dataio::ByteWriter bw(256);
	voxel::WriteEntityRecord(bw, rec);
	std::vector<uint8_t> bytes = bw.Take();
	dataio::ByteReader br(bytes);
	voxel::EntityRecord back;
	REQUIRE(voxel::ReadEntityRecord(br, back));
	ecs::World w2;
	const ecs::Entity b = voxel::InstantiateEntity(w2, back);
	CHECK_EQ(w2.Get<voxel::Uid>(b)->value, uid);
	CHECK_EQ(w2.Get<voxel::Mass>(b)->kg, 420.f);
	CHECK_EQ(w2.Get<voxel::LightEmitter>(b)->range, 12.f);
	CHECK_EQ(w2.Get<voxel::SoundEmitter>(b)->sound, std::string("core:metal_clang"));
	REQUIRE(w2.Get<voxel::Joints>(b) != nullptr);
	CHECK_EQ(w2.Get<voxel::Joints>(b)->list[0].target, 0xFEDCBA9876543210ULL);
	CHECK_EQ(w2.Get<voxel::Joints>(b)->list[0].ratio, 2.f);
	CHECK(voxel::FindByUid(w2, uid) == b);
	// Enregistrement v1 (sans uid ni mécanique) toujours lisible.
	dataio::ByteWriter v1(128);
	v1.PutString("core:pig");
	for (int i = 0; i < 7; ++i)
		v1.Put<float>(0.f);
	v1.Put<float>(20.f);
	v1.Put<float>(20.f);
	for (int i = 0; i < 3; ++i)
		v1.Put<float>(0.3f);
	v1.PutString("");
	v1.PutU8(1);
	std::vector<uint8_t> old = v1.Take();
	dataio::ByteReader r1(old);
	voxel::EntityRecord oldRec;
	REQUIRE(voxel::ReadEntityRecord(r1, oldRec, 1));
	CHECK_EQ(oldRec.kind, std::string("core:pig"));
	CHECK_EQ(oldRec.uid, 0u);
	CHECK(oldRec.wander);
}

TEST(Terrain_SettingsJsonAndStructures) {
	voxel::TerrainSettings t;
	t.type = voxel::GeneratorType::Flat;
	t.shape.hillsAmplitude = 22.f;
	t.shape.caves = false;
	t.structures.push_back({"test:tower", 1.f, 16, 0xFF});
	const voxel::TerrainSettings back = voxel::TerrainFromJson(voxel::TerrainToJson(t));
	CHECK(back.type == voxel::GeneratorType::Flat);
	CHECK_EQ(back.shape.hillsAmplitude, 22.f);
	CHECK(!back.shape.caves);
	REQUIRE(back.structures.size() == 1u);
	CHECK_EQ(back.structures[0].spacing, 16);
	// Valeurs par défaut : un JSON vide redonne le générateur d'origine.
	CHECK_EQ(voxel::TerrainFromJson(dataio::Json::MakeObject()).shape.continentScale, 900.f);

	voxel::BlockRegistry reg = MakeRegistry();
	voxel::Structure tower;
	tower.id = "test:tower";
	tower.palette['#'] = "core:stone";
	tower.layers = {{"#"}, {"#"}, {"#"}}; // trois blocs empilés
	const voxel::StructureLookup lookup = [&](std::string_view id) { return id == tower.id ? &tower : nullptr; };
	const voxel::TerrainGenerator gen(42, voxel::TerrainPalette::Resolve(reg), t, &reg, lookup);
	std::vector<voxel::StructurePlacement> placed;
	gen.StructuresIn(0, 0, 64, 64, placed);
	CHECK(placed.size() >= 8u); // chance 1 : une tour par cellule de 16 (sous réserve du biome)
	const voxel::BlockId stone = reg.Find("core:stone");
	// Chaque tour apparaît dans le chunk qui la contient, quel que soit l'ordre de génération.
	std::vector<voxel::BlockId> chunk(voxel::kChunkVolume);
	gen.Generate({0, 0, 0}, chunk, nullptr);
	size_t inside = 0;
	for (const auto &p : placed) {
		if (p.origin.x >= 32 || p.origin.z >= 32 || p.origin.y < 0 || p.origin.y + 2 >= 32)
			continue;
		++inside;
		for (int dy = 0; dy < 3; ++dy)
			CHECK(chunk[size_t(voxel::VoxelIndex(p.origin.x, p.origin.y + dy, p.origin.z))] == stone);
	}
	CHECK(inside >= 2u);
	const voxel::ColumnSample s = gen.Sample(5, 5);
	CHECK_EQ(s.height, 0); // monde plat
	CHECK(std::string_view(voxel::TerrainBiomeName(s.biome)) == "Plaines");
}

TEST(Liquid_RenewableSources) {
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "water", "graphics": {"texture": "core:water"}, "liquid": {"range": 7, "tick": 0.25, "renewable": true}},
		{"id": "lava", "graphics": {"texture": "core:lava"}, "liquid": {"range": 3, "tick": 1.5}}
	])");
	reg.LoadDefinitions(json.Value(), "core");
	// L'eau est renouvelable (sources infinies 2×2), la lave ne l'est pas.
	CHECK(reg.Get(reg.Find("core:water")).renewable);
	CHECK_FALSE(reg.Get(reg.Find("core:lava")).renewable);
	CHECK_EQ(reg.Get(reg.Find("core:water")).liquidRange, 7u);
	CHECK_EQ(reg.Get(reg.Find("core:lava")).liquidRange, 3u);
}

TEST(Terrain_LavaLakesAndMagmaSea) {
	voxel::BlockRegistry reg;
	auto json = dataio::ParseJson(R"([
		{"id": "stone", "graphics": {"texture": "core:stone"}},
		{"id": "dirt", "graphics": {"texture": "core:dirt"}},
		{"id": "grass", "graphics": {"texture": "core:grass"}},
		{"id": "sand", "graphics": {"texture": "core:sand"}},
		{"id": "water", "graphics": {"texture": "core:water", "material": "TRANSPARENT"}, "liquid": {"range": 7}},
		{"id": "lava", "graphics": {"texture": "core:lava", "material": "TRANSPARENT"}, "liquid": {"range": 3}},
		{"id": "magma", "graphics": {"texture": "core:magma"}},
		{"id": "obsidian", "graphics": {"texture": "core:obsidian"}}
	])");
	reg.LoadDefinitions(json.Value(), "core");
	voxel::TerrainSettings t; // réglages par défaut (Normal, grottes/failles actives)
	voxel::TerrainGenerator gen(42, voxel::TerrainPalette::Resolve(reg), t, &reg);
	const voxel::BlockId magma = reg.Find("core:magma");
	const voxel::BlockId lava = reg.Find("core:lava");
	const voxel::BlockId obsidian = reg.Find("core:obsidian");
	std::vector<voxel::BlockId> chunk(voxel::kChunkVolume);

	// Mer de lave du noyau externe (profondeur ≈ 2 300 000 → y ≈ −2 300 000) :
	// continue, aucune faille ne la perce.
	const math::IVec3 seaChunk = voxel::ChunkOfVoxel(0, -2'300'000, 0);
	gen.Generate(seaChunk, chunk, nullptr);
	bool seaContinuous = true;
	for (const voxel::BlockId id : chunk)
		if (id != magma)
			seaContinuous = false;
	CHECK(seaContinuous);

	// Manteau supérieur (profondeur ≈ 50 000 → y ≈ −50 000) : lacs de lave
	// (lave liquide + croûte d'obsidienne) présents.
	const math::IVec3 mantleChunk = voxel::ChunkOfVoxel(0, -50'000, 0);
	gen.Generate(mantleChunk, chunk, nullptr);
	size_t lavaCount = 0, obsidianCount = 0;
	for (const voxel::BlockId id : chunk) {
		if (id == lava)
			++lavaCount;
		if (id == obsidian)
			++obsidianCount;
	}
	CHECK_GT(lavaCount, 0u);	 // il y a de la lave liquide
	CHECK_GT(obsidianCount, 0u); // … scellée par de l'obsidienne
}

// ── Avatar : conventions d'angles, postures et maillage ──────────────────────
TEST(Avatar_ConventionsAndPoses) {
	using voxel::AvatarPart;
	constexpr float kHalfPi = 1.5707963f;
	const math::FVector3 hand = voxel::AvatarHandAnchor();
	voxel::AvatarPose rest;
	// Repos : la main droite pend sous l'épaule droite.
	const math::FVector3 h0 = voxel::AvatarTransform(rest, AvatarPart::ArmRight, hand);
	CHECK(h0.x > 0.3f && h0.y < 0.f && std::fabs(h0.z) < 1e-4f);
	// Bras X = π/2 : horizontal vers l'avant (−Z).
	voxel::AvatarPose fwd;
	fwd.rotation[size_t(AvatarPart::ArmRight)] = {kHalfPi, 0.f, 0.f};
	const math::FVector3 h1 = voxel::AvatarTransform(fwd, AvatarPart::ArmRight, hand);
	CHECK(h1.z < -0.5f && std::fabs(h1.y - 0.35f) < 0.01f);
	// Bras droit Z > 0 : s'écarte vers +X.
	voxel::AvatarPose out;
	out.rotation[size_t(AvatarPart::ArmRight)] = {0.f, 0.f, kHalfPi};
	CHECK(voxel::AvatarTransform(out, AvatarPart::ArmRight, hand).x > 0.9f);
	// Couché (racine X = π/2) : la tête part vers +Z, le visage vers le haut.
	voxel::AvatarInput sleep;
	sleep.state = voxel::AvatarState::Sleep;
	const voxel::AvatarPose lying = voxel::ComputeAvatarPose(sleep);
	const math::FVector3 top = voxel::AvatarTransform(lying, AvatarPart::Head, {0.f, 0.5f, 0.f});
	CHECK(top.z > 0.8f && std::fabs(top.y) < 0.1f);
	CHECK(voxel::AvatarTransform(lying, AvatarPart::Head, {0.f, 0.25f, -0.25f}).y > 0.2f);
	// Accroupi : l'œil est plus bas que debout ; assis : jambes à l'horizontale.
	voxel::AvatarInput stand, crouch, sit;
	crouch.state = voxel::AvatarState::Crouch;
	sit.state = voxel::AvatarState::Sit;
	CHECK(voxel::AvatarEye(voxel::ComputeAvatarPose(crouch)).y < voxel::AvatarEye(voxel::ComputeAvatarPose(stand)).y - 0.1f);
	const math::FVector3 foot = voxel::AvatarTransform(voxel::ComputeAvatarPose(sit), AvatarPart::LegRight, {0.f, -0.65f, 0.f});
	CHECK(foot.z < -0.6f && foot.y > -0.3f);
	// Nages : crawl et brasse à plat ventre (tête devant), dos crawlé sur le dos (tête devant aussi).
	for (voxel::SwimStyle style : {voxel::SwimStyle::Crawl, voxel::SwimStyle::Breaststroke, voxel::SwimStyle::Backstroke}) {
		voxel::AvatarInput swim;
		swim.state = voxel::AvatarState::Swim;
		swim.swim = style;
		swim.speed = 3.f;
		swim.time = 0.3f;
		const voxel::AvatarPose p = voxel::ComputeAvatarPose(swim);
		const math::FVector3 head = voxel::AvatarTransform(p, AvatarPart::Body, {0.f, 0.65f, 0.f});
		CHECK(head.z < -0.3f);
		const math::FVector3 chest = voxel::AvatarTransform(p, AvatarPart::Body, {0.f, 0.3f, -0.125f});
		const math::FVector3 back = voxel::AvatarTransform(p, AvatarPart::Body, {0.f, 0.3f, 0.125f});
		CHECK(style == voxel::SwimStyle::Backstroke ? chest.y > back.y : chest.y < back.y);
	}
	// Chute libre : bras écartés.
	voxel::AvatarInput fall;
	fall.state = voxel::AvatarState::FreeFall;
	const voxel::AvatarPose fp = voxel::ComputeAvatarPose(fall);
	CHECK(voxel::AvatarTransform(fp, AvatarPart::ArmRight, hand).x > 0.8f);
	CHECK(voxel::AvatarTransform(fp, AvatarPart::ArmLeft, hand).x < -0.8f);
	// Coucou : la main droite au-dessus de la tête au milieu du geste.
	voxel::AvatarInput wave;
	wave.gesture = voxel::AvatarGesture::Wave;
	wave.gestureTime = 1.f;
	CHECK(voxel::AvatarTransform(voxel::ComputeAvatarPose(wave), AvatarPart::ArmRight, hand).y > 0.5f);
	// Première personne : les mains sont devant l'œil, dans le champ de vision (~35° sous l'horizon).
	voxel::AvatarInput fpv;
	fpv.firstPerson = true;
	const voxel::AvatarPose fpp = voxel::ComputeAvatarPose(fpv);
	const math::FVector3 eye = voxel::AvatarEye(fpp), hr = voxel::AvatarTransform(fpp, AvatarPart::ArmRight, hand);
	const math::FVector3 d = hr - eye;
	CHECK(d.z < -0.25f);
	CHECK(std::atan2(-d.y, -d.z) < 0.6f);		  // < ~35° sous l'horizon (champ vertical)
	CHECK(std::atan2(std::fabs(d.x), -d.z) < 0.75f); // < ~43° sur le côté
	// Identifiants.
	CHECK(voxel::ParseSwimStyle("brasse") == voxel::SwimStyle::Breaststroke);
	CHECK(voxel::ParseAvatarGesture("wave_both") == voxel::AvatarGesture::WaveBoth);
}

TEST(Avatar_RigGeometry) {
	// Le rig éditable doit reprendre la géométrie de l'avatar procédural : pieds à
	// −0,9 bloc, tête à +0,9, parties positionnées en absolu (autour de leur pivot).
	const voxel::Model rig = voxel::BuildAvatarRig();
	CHECK_EQ(rig.parts.size(), voxel::kAvatarParts);
	CHECK(rig.boxUv);
	CHECK_EQ(rig.unitsPerBlock, 16.f);
	const voxel::ModelBounds b = rig.Bounds();
	// 1,8 bloc de haut, centré sur l'origine (pieds ≈ −0,9, crâne ≈ +0,9).
	CHECK(std::fabs(b.Size().y - 1.8f) < 0.02f);
	CHECK(std::fabs(b.min.y + 0.9f) < 0.02f);
	CHECK(std::fabs(b.max.y - 0.9f) < 0.02f);
	// La tête tourne autour du cou (bas de la tête) : pivot.y == from.y.
	const voxel::ModelPart *head = nullptr, *body = nullptr;
	for (const voxel::ModelPart &p : rig.parts) {
		if (p.name == "head")
			head = &p;
		if (p.name == "body")
			body = &p;
	}
	REQUIRE(head && body);
	REQUIRE(!head->boxes.empty());
	CHECK(std::fabs(head->pivot.y - head->boxes[0].from.y) < 1e-3f); // cou = bas de la tête
	CHECK(std::fabs(head->boxes[0].from.y - 6.4f) < 1e-3f);		  // 0,4 bloc × 16
	// Le torse part du pivot (épaules) vers le bas.
	CHECK(std::fabs(body->boxes[0].from.y + 4.f) < 1e-3f); // −0,25 bloc × 16
}

TEST(Avatar_BlendAndMesh) {
	voxel::AvatarPose a, b;
	a.rotation[2] = {3.0f, 0.f, 0.f};
	b.rotation[2] = {-3.0f, 0.f, 0.f}; // écart court : en passant par ±π
	const voxel::AvatarPose m = voxel::BlendPose(a, b, 0.5f);
	CHECK(std::fabs(std::fabs(m.rotation[2].x) - 3.14159f) < 0.01f);
	voxel::AvatarMeshOptions opt;
	std::vector<voxel::AvatarVertex> v;
	voxel::BuildAvatarMesh(a, opt, v);
	CHECK_EQ(v.size(), 6u * 6u * 6u);
	opt.head = false;
	opt.extras.push_back(voxel::AvatarHeldBox(true, false, {64, 0, 80, 16}, {80, 0, 96, 16}));
	opt.textureWidth = 128.f;
	v.clear();
	voxel::BuildAvatarMesh(a, opt, v);
	CHECK_EQ(v.size(), 6u * 6u * 6u);
	for (const auto &x : v)
		CHECK(x.u >= 0.f && x.u <= 1.f && x.v >= 0.f && x.v <= 1.f);
}
