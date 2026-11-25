#include <cpp-maker/test.hpp>
#include <fstream>
#include <filesystem>
#include <sandbox/modeling/io.hpp>
#include <sandbox/modeling/operations.hpp>
#include <sandbox/modeling/triangulate.hpp>

#include <cmath>

using namespace modeling;

namespace {

/// Volume d'un solide fermé (théorème de la divergence sur les triangles).
float Volume(const Model &m) {
	const MeshData mesh = BuildMesh(m);
	float v = 0.f;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		const Vec3 &a = mesh.positions[mesh.indices[i]], &b = mesh.positions[mesh.indices[i + 1]],
				   &c = mesh.positions[mesh.indices[i + 2]];
		v += a.Dot(b.Cross(c)) / 6.f;
	}
	return v;
}

/// Toute arête d'un solide fermé borde exactement deux faces.
bool Closed(const Model &m) {
	for (size_t e = 0; e < m.Edges().size(); ++e)
		if (m.Edges()[e].alive && m.FacesOfEdge(EdgeId(e)).size() != 2)
			return false;
	return true;
}

FaceId Ground(Model &m, float w = 2.f, float d = 1.f) {
	// Rectangle au sol, normale vers le haut.
	return m.AddFace({{0, 0, 0}, {0, 0, d}, {w, 0, d}, {w, 0, 0}}).Unwrap();
}

} // namespace

TEST(Modeling, Model_FaceConstructionWeldsAndOrients) {
	Model m;
	const FaceId f = Ground(m);
	CHECK_EQ(m.VertexCount(), 4u);
	CHECK_EQ(m.EdgeCount(), 4u);
	CHECK_NEAR(m.Faces()[f].normal.y, 1.f, 1e-5f);
	CHECK_NEAR(m.FaceArea(f), 2.f, 1e-5f);
	// Même contour (autre sens, autre départ) : la face existante est rendue.
	CHECK_EQ(m.AddFace({{2, 0, 0}, {0, 0, 0}, {0, 0, 1}, {2, 0, 1}}).Unwrap(), f);
	// Non plan : refusé, sauf en triangulation automatique.
	CHECK(m.AddFace({{5, 0, 0}, {6, 0, 0}, {6, 1, 1}, {5, 0, 1}}).IsNone());
	CHECK(m.AddFace({{5, 0, 0}, {6, 0, 0}, {6, 1, 1}, {5, 0, 1}}, {}, {.autoTriangulate = true}).IsSome());
	// Face avec trou : aire déduite, trou orienté à l'inverse.
	const FaceId holed = m.AddFace({{0, 0, 5}, {4, 0, 5}, {4, 0, 9}, {0, 0, 9}},
								   {{{1, 0, 6}, {2, 0, 6}, {2, 0, 7}, {1, 0, 7}}}).Unwrap();
	CHECK_NEAR(m.FaceArea(holed), 15.f, 1e-4f);
	CHECK(!m.FaceContains(holed, {1.5f, 0, 6.5f}));
	CHECK(m.FaceContains(holed, {3.f, 0, 8.f}));
}

TEST(Modeling, Model_CirclesArcsAndMaterials) {
	Model m;
	const uint32_t red = m.AddMaterial("Rouge", {1, 0, 0});
	CHECK_EQ(m.AddMaterial("Rouge", {0.9f, 0, 0}), red); // dédoublonné par nom
	const FaceId disk = m.AddCircle({0, 0, 0}, {0, 1, 0}, 1.f, {.segments = 64, .material = red}).Unwrap();
	CHECK_NEAR(m.FaceArea(disk), math::PI, 0.01f);
	CHECK_EQ(m.Faces()[disk].material, red);
	const uint32_t curve = m.Edges()[m.EdgesOfFace(disk)[0]].curve;
	CHECK(curve != INVALID_ID);
	const auto arc = m.AddArc({5, 0, 0}, {0, 1, 0}, {1, 0, 0}, 1.f, 0.f, math::PI, {.segments = 8});
	CHECK_EQ(arc.size(), 8u);
}

TEST(Modeling, PushPull_FreeFaceBecomesAClosedBox) {
	Model m;
	const FaceId f = Ground(m);
	const FaceId top = PushPull(m, f, 3.f).Unwrap();
	CHECK_EQ(m.FaceCount(), 6u);
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), 6.f, 1e-4f);              // normales vers l'extérieur
	CHECK_NEAR(m.Faces()[top].normal.y, 1.f, 1e-5f); // dessus vers le haut
	CHECK_NEAR(m.Faces()[f].normal.y, -1.f, 1e-5f);  // fond retourné
}

TEST(Modeling, PushPull_OnASolidMovesTheFaceBothWays) {
	Model m;
	const FaceId top = PushPull(m, Ground(m), 3.f).Unwrap();
	CHECK_EQ(PushPull(m, top, 1.f).Unwrap(), top); // tirer : la boîte grandit
	CHECK_EQ(m.FaceCount(), 6u);
	CHECK_NEAR(Volume(m), 8.f, 1e-4f);
	(void)PushPull(m, top, -2.5f);                  // pousser : elle rapetisse
	CHECK_NEAR(Volume(m), 3.f, 1e-4f);
	CHECK(Closed(m));
	CHECK(PushPull(m, top, -100.f).IsError());
}

TEST(Modeling, PushPull_HalfOfATopFaceMakesAStepWithMergedSides) {
	// Boîte 2 × 1 × 1 ; une ligne coupe le dessus en x = 1 ; on tire la
	// moitié droite de 1 : les flancs avant/arrière de la marche sont
	// coplanaires avec ceux de la boîte et ne font plus qu'une face.
	Model m;
	const FaceId top = PushPull(m, Ground(m, 2.f, 1.f), 1.f).Unwrap();
	const VertexId a = m.SplitEdge(m.FindEdge(m.FindVertex({0, 1, 0}), m.FindVertex({2, 1, 0})), {1, 1, 0});
	const VertexId b = m.SplitEdge(m.FindEdge(m.FindVertex({0, 1, 1}), m.FindVertex({2, 1, 1})), {1, 1, 1});
	REQUIRE(a != INVALID_ID && b != INVALID_ID);
	const FaceId other = m.SplitFace(a, b).Unwrap();
	const FaceId right = m.FaceCentroid(top).x > 1.f ? top : other;
	CHECK(Closed(m));
	REQUIRE(PushPull(m, right, 1.f).IsOk());
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), 3.f, 1e-4f);
	// Boîte (6 faces) + dessus gauche + marche : avant et arrière en L fusionnés.
	CHECK_EQ(m.FaceCount(), 8u);
	size_t lShaped = 0;
	for (const Face &f : m.Faces())
		lShaped += f.alive && f.outer.size() == 6 ? 1 : static_cast<size_t>(0);
	CHECK_EQ(lShaped, 2u);
}

TEST(Modeling, SplitFace_LineAcrossAFaceCutsIt) {
	Model m;
	const FaceId f = m.AddFace({{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {2, 0, 0}, {1, 0, 0}}).Unwrap();
	const Option<FaceId> other = m.SplitFace(m.FindVertex({1, 0, 0}), m.FindVertex({1, 0, 1}));
	REQUIRE(other.IsSome());
	CHECK_EQ(m.FaceCount(), 2u);
	CHECK_NEAR(m.FaceArea(f) + m.FaceArea(other.Unwrap()), 2.f, 1e-5f);
	CHECK_NEAR(m.FaceArea(f), 1.f, 1e-5f);
	// Les deux moitiés, tirées ensemble, refusionnent avec MergeCoplanar.
	CHECK_EQ(m.MergeCoplanar(f), f);
	CHECK_EQ(m.FaceCount(), 1u);
	CHECK_EQ(m.Faces()[f].outer.size(), 4u); // sommets alignés cicatrisés
}

TEST(Modeling, Offset_InsetAndOutset) {
	Model m;
	const FaceId f = m.AddFace({{0, 0, 0}, {0, 0, 4}, {4, 0, 4}, {4, 0, 0}}).Unwrap();
	const FaceId inner = Offset(m, f, 1.f).Unwrap();
	CHECK_NEAR(m.FaceArea(inner), 4.f, 1e-4f);
	CHECK_NEAR(m.FaceArea(f), 12.f, 1e-4f); // anneau
	CHECK(Offset(m, inner, 5.f).IsError());   // trop grand
	const FaceId ring = Offset(m, f, -1.f).Unwrap();
	CHECK_NEAR(m.FaceArea(ring), 36.f - 16.f, 1e-3f);
}

TEST(Modeling, FollowMe_SweepsAProfileAlongAPath) {
	Model m;
	// Profil carré 0.2 × 0.2 dans le plan x = 0, chemin en L le long de x puis z.
	const FaceId profile = m.AddFace({{0, 0, 0}, {0, 0.2f, 0}, {0, 0.2f, 0.2f}, {0, 0, 0.2f}}).Unwrap();
	const auto end = FollowMe(m, profile, {{0, 0.1f, 0.1f}, {2, 0.1f, 0.1f}, {2, 0.1f, 2}});
	REQUIRE(end.IsOk());
	CHECK(Closed(m));
	CHECK(Volume(m) > 0.f);
	// Volume d'une barre en L à onglet : (2 + 1.9) × 0.04 environ.
	CHECK_NEAR(Volume(m), 0.04f * 3.8f, 0.01f);
}

TEST(Modeling, Revolve_BuildsASmoothClosedSolid) {
	Model m;
	const auto faces = Revolve(m, {0, 0, 0}, {0, 1, 0}, {{0.5f, 0.f}, {0.5f, 1.f}}, {.segments = 48});
	REQUIRE(faces.IsOk());
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), math::PI * 0.25f, 0.01f); // cylindre r = 0.5, h = 1
	size_t soft = 0;
	for (const Edge &e : m.Edges())
		soft += e.alive && e.soft ? 1 : 0;
	CHECK_EQ(soft, 48u); // les génératrices, pas les bords des disques
	// Lissage : les normales d'un coin de flanc ne sont plus celle de la face.
	const MeshData mesh = BuildMesh(m);
	CHECK(mesh.TriangleCount() > 0u);
}

TEST(Modeling, Extrude_PrismAlongANormalOrAnyDirection) {
	Model m;
	REQUIRE(Extrude(m, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 2, 0}).IsOk());
	CHECK_NEAR(std::fabs(Volume(m)), 2.f, 1e-4f);
	Model slanted;
	REQUIRE(Extrude(slanted, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {1, 2, 0}).IsOk());
	CHECK_NEAR(std::fabs(Volume(slanted)), 2.f, 1e-3f); // prisme oblique : même volume
}

TEST(Modeling, Transforms_MoveRotateScaleAndErase) {
	Model m;
	(void)PushPull(m, Ground(m, 1.f, 1.f), 1.f);
	std::vector<VertexId> all;
	for (size_t v = 0; v < m.Vertices().size(); ++v)
		if (m.Vertices()[v].alive)
			all.push_back(VertexId(v));
	Move(m, all, {10, 0, 0});
	CHECK_NEAR(m.Bounds().min.x, 10.f, 1e-5f);
	Scale(m, all, {10, 0, 0}, {2, 2, 2});
	CHECK_NEAR(Volume(m), 8.f, 1e-3f);
	Rotate(m, all, {10, 0, 0}, {0, 1, 0}, math::PI * 0.5f);
	CHECK_NEAR(Volume(m), 8.f, 1e-3f);
	Erase(m, {m.EdgesOfFace(0)[0]});
	CHECK_EQ(m.FaceCount(), 4u); // l'arête emporte ses deux faces
}

TEST(Modeling, BuildMesh_GroupsByMaterialAndMapsTrianglesToFaces) {
	Model m;
	const uint32_t red = m.AddMaterial("Rouge", {1, 0, 0});
	const FaceId a = Ground(m);
	const FaceId b = m.AddFace({{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}).Unwrap();
	Paint(m, b, red);
	const MeshData mesh = BuildMesh(m);
	CHECK_EQ(mesh.TriangleCount(), 4u);
	CHECK_EQ(mesh.groups.size(), 2u);
	CHECK_EQ(mesh.triangleFace.size(), 4u);
	CHECK(std::find(mesh.triangleFace.begin(), mesh.triangleFace.end(), a) != mesh.triangleFace.end());
	const auto hit = Raycast(m, math::FRay({0.5f, 5, 0.5f}, {0, -1, 0}));
	REQUIRE(hit.IsSome());
	CHECK_EQ(hit.Value().face, b);
	CHECK_NEAR(hit.Value().point.y, 1.f, 1e-5f);
}

TEST(Modeling, PushPullThroughOpensAWall) {
	// Boîte creuse : dessus décalé vers l'intérieur puis creusé (fond de 10 cm), porte poussée
	// à travers le mur de 20 cm : une ouverture, plus aucune face dans la porte.
	Model m;
	const auto floor = m.AddFace({{0, 0, 0}, {0, 0, 4}, {6, 0, 4}, {6, 0, 0}});
	REQUIRE(floor.IsSome());
	auto box = PushPull(m, *floor, m.Faces()[*floor].normal.y > 0.f ? 2.8f : -2.8f);
	REQUIRE(box.IsOk());
	FaceId topFace = box.Value();
	if (m.Faces()[topFace].normal.y < 0.f)
		topFace = INVALID_ID;
	for (size_t f = 0; f < m.Faces().size() && topFace == INVALID_ID; ++f)
		if (m.IsAlive(FaceId(f)) && m.Faces()[f].normal.y > 0.99f && std::fabs(m.FaceCentroid(FaceId(f)).y - 2.8f) < 1e-4f)
			topFace = FaceId(f);
	REQUIRE(topFace != INVALID_ID);
	auto inner = Offset(m, topFace, 0.2f);
	REQUIRE(inner.IsOk());
	REQUIRE(PushPull(m, inner.Value(), -2.7f).IsOk());
	CHECK(Closed(m));
	const float hollow = Volume(m);
	CHECK_NEAR(hollow, 6.f * 4.f * 2.8f - 5.6f * 3.6f * 2.7f, 1e-2f);
	// Porte sur la face avant (z = 4) : rectangle imprimé puis poussé de l'épaisseur du mur.
	const auto door = m.AddRectangle({2.6f, 0.1f, 4.f}, {0.9f, 0.f, 0.f}, {0.f, 2.1f, 0.f});
	REQUIRE(door.IsSome());
	REQUIRE(m.Imprint(*door).IsSome());
	const float sign = m.Faces()[*door].normal.z > 0.f ? -1.f : 1.f; // vers l'intérieur du mur
	REQUIRE(PushPull(m, *door, 0.2f * sign).IsOk());
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), hollow - 0.9f * 2.1f * 0.2f, 1e-2f);
	// Un rayon horizontal passe par la porte jusqu'au mur du fond (z = 0,2 côté intérieur).
	const auto hit = Raycast(m, math::FRay({3.05f, 1.f, 6.f}, {0.f, 0.f, -1.f}));
	REQUIRE(hit.IsSome());
	CHECK_NEAR(hit->point.z, 0.2f, 1e-3f);
}

TEST(Modeling, Io_ImportGltfPlacesNodesAndMaterials) {
	// Cube unité (8 sommets, 12 triangles indexés en 16 bits), nœud décalé de 1 m et agrandi ×2.
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sandbox-test-gltf";
	std::filesystem::create_directories(dir);
	const float positions[24] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};
	const uint16_t indices[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
	{
		std::ofstream bin(dir / "cube.bin", std::ios::binary);
		bin.write(reinterpret_cast<const char *>(positions), sizeof(positions));
		bin.write(reinterpret_cast<const char *>(indices), sizeof(indices));
	}
	{
		std::ofstream gltf(dir / "cube.gltf");
		gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
			"nodes":[{"mesh":0,"translation":[1,0,0],"scale":[2,2,2]}],
			"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
			"materials":[{"name":"Rouge","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}],
			"buffers":[{"uri":"cube.bin","byteLength":168}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96},{"buffer":0,"byteOffset":96,"byteLength":72}],
			"accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
						 {"bufferView":1,"componentType":5123,"count":36,"type":"SCALAR"}]})";
	}
	std::vector<modeling::Material> palette{{"Défaut", {0.9f, 0.9f, 0.9f, 1.f}, 1.f}};
	auto model = modeling::ImportGltf(String((dir / "cube.gltf").string()), palette);
	REQUIRE(model.IsOk());
	const modeling::Model &m = model.Value();
	CHECK_EQ(m.FaceCount(), 6u); // triangles coplanaires fusionnés en faces
	const math::FAABB box = m.Bounds();
	CHECK_NEAR(box.min.x, 1.f, 1e-4f);
	CHECK_NEAR(box.max.x, 3.f, 1e-4f);
	CHECK_NEAR(box.max.y, 2.f, 1e-4f);
	REQUIRE(palette.size() == 2u); // « Rouge » ajouté à la palette
	CHECK(palette[1].name == "Rouge");
	CHECK_NEAR(palette[1].color.r, 1.f, 1e-6f);
	for (size_t f = 0; f < m.Faces().size(); ++f)
		if (m.IsAlive(modeling::FaceId(f)))
			CHECK_EQ(m.Faces()[f].material, 1u);
	// Second import : le matériau existant est réutilisé.
	REQUIRE(modeling::ImportGltf(String((dir / "cube.gltf").string()), palette).IsOk());
	CHECK_EQ(palette.size(), 2u);
	CHECK(modeling::ImportGltf("absent.gltf", palette).IsError());
	std::filesystem::remove_all(dir);
}
