// Tests d'engine.generator : formes, chemins et maillages procéduraux (positions, normales,
// indices), modificateurs, courbes de Bézier et écriture OBJ — mathématiques de foundation.math.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <string>

#include <generator/generator.hpp>

using namespace generator;

namespace {

/// Vérifie qu'un maillage est cohérent : indices valides, normales unitaires ; rend (sommets, triangles).
template <typename Mesh> std::pair<int, int> CheckMesh(const Mesh &mesh) {
	int vertices = 0;
	for (const MeshVertex &v : mesh.Vertices()) {
		++vertices;
		CHECK_NEAR(v.normal.Length(), 1.f, 1e-3f);
	}
	int triangles = 0;
	for (const Triangle &t : mesh.Triangles()) {
		++triangles;
		for (int k = 0; k < 3; ++k)
			CHECK(t.vertices[k] >= 0 && t.vertices[k] < vertices);
	}
	return {vertices, triangles};
}

} // namespace

TEST(Generator_PrimitivesAreConsistent) {
	const auto [boxV, boxT] = CheckMesh(BoxMesh({1.f, 2.f, 3.f}, {2, 2, 2}));
	CHECK(boxV > 0 && boxT > 0);
	for (const MeshVertex &v : BoxMesh({1.f, 2.f, 3.f}).Vertices()) { // demi-dimensions
		CHECK(std::abs(v.position.x) <= 1.f + 1e-4f);
		CHECK(std::abs(v.position.y) <= 2.f + 1e-4f);
		CHECK(std::abs(v.position.z) <= 3.f + 1e-4f);
	}
	CheckMesh(CylinderMesh());
	CheckMesh(CapsuleMesh());
	CheckMesh(TorusMesh());
	CheckMesh(ConeMesh());
	CheckMesh(DodecahedronMesh());
	CheckMesh(TeapotMesh());
	CheckMesh(TorusKnotMesh());
	CheckMesh(SpringMesh());
	CheckMesh(RoundedBoxMesh());
	CheckMesh(PlaneMesh());
}

TEST(Generator_SpheresLieOnTheirRadius) {
	for (const MeshVertex &v : SphereMesh(2.f).Vertices()) {
		CHECK_NEAR(v.position.Length(), 2.f, 1e-4f);
		CHECK_NEAR(v.normal.Dot(v.position / 2.f), 1.f, 1e-3f); // normale radiale
	}
	for (const MeshVertex &v : IcoSphereMesh(1.5f, 2).Vertices())
		CHECK_NEAR(v.position.Length(), 1.5f, 1e-4f);
	const auto [v, t] = CheckMesh(SphereMesh(1.f, 16, 8));
	CHECK(v >= 16 * 8);
	CHECK(t > 0);
}

TEST(Generator_ModifiersMoveAndRotate) {
	math::FVector3 sum{};
	int n = 0;
	for (const MeshVertex &v : MakeTranslateMesh(SphereMesh(1.f), {5.f, 0.f, 0.f}).Vertices()) {
		sum += v.position;
		++n;
	}
	CHECK_NEAR(sum.x / float(n), 5.f, 0.05f); // centre déplacé
	// Quart de tour autour de z : l'axe x va sur y.
	for (const MeshVertex &v : MakeRotateMesh(MakeTranslateMesh(BoxMesh({0.1f, 0.1f, 0.1f}), {3.f, 0.f, 0.f}), math::Radians(90.f), Axis::Z).Vertices())
		CHECK_NEAR(v.position.y, 3.f, 0.11f);
	CHECK(Count(SphereMesh().Vertices()) == Count(MakeScaleMesh(SphereMesh(), {2.f, 2.f, 2.f}).Vertices()));
}

TEST(Generator_BezierAndPaths) {
	const math::FVector2 control[4] = {{0.f, 0.f}, {1.f, 2.f}, {2.f, 2.f}, {3.f, 0.f}};
	const BezierShape<4> curve(control, 8);
	math::FVector2 first{}, last{};
	bool started = false;
	for (const ShapeVertex &v : curve.Vertices()) {
		if (!started)
			first = v.position, started = true;
		last = v.position;
		CHECK_NEAR(v.tangent.Length(), 1.f, 1e-3f);
	}
	CHECK_NEAR(first.x, 0.f, 1e-5f);
	CHECK_NEAR(last.x, 3.f, 1e-5f);
	CHECK(Count(HelixPath().Vertices()) > 0);
	CHECK(Count(KnotPath().Vertices()) > 0);
	CHECK(Count(CircleShape().Edges()) > 0);
}

TEST(Generator_WritesObj) {
	ObjWriter obj;
	obj.WriteMesh(BoxMesh({1.f, 1.f, 1.f}, {1, 1, 1}));
	const std::string text = obj.Str();
	CHECK(text.find("v ") != std::string::npos);
	CHECK(text.find("f ") != std::string::npos);
}

TEST(Generator_EveryModifierInstantiates) {
	// Chaque modificateur, instancié et parcouru : sommets et triangles (ou arêtes) cohérents.
	const auto sphere = SphereMesh(1.f, 8, 4);
	CheckMesh(MakeAxisFlipMesh(sphere, true, false, true));
	CheckMesh(MakeAxisSwapMesh(sphere, Axis::Z, Axis::X, Axis::Y));
	CheckMesh(MakeFlipMesh(sphere));
	CheckMesh(MakeMirrorMesh(BoxMesh()));
	CheckMesh(MakeRepeatMesh(sphere, 3, {3.f, 0.f, 0.f}));
	CheckMesh(MakeRotateMesh(sphere, math::FQuaternion::AxisAngle({0.f, 1.f, 0.f}, 0.5f)));
	CheckMesh(MakeSpherifyMesh(BoxMesh(), 1.f, 0.5f));
	CheckMesh(MakeUvFlipMesh(sphere));
	CheckMesh(MakeUvSwapMesh(sphere));
	CheckMesh(MakeMergeMesh(sphere, BoxMesh(), TorusMesh()));
	CheckMesh(SubdivideMesh<SphereMesh, 1>(sphere));
	CheckMesh(AnyMesh(sphere));
	CheckMesh(MakeExtrudeMesh(CircleShape(0.3f, 8), HelixPath()));
	CheckMesh(BezierMesh<4, 4>(
		{{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}}, {{0, 1, 1}, {1, 1, 1}, {2, 1, 1}, {3, 1, 1}},
		 {{0, 2, 1}, {1, 2, 1}, {2, 2, 1}, {3, 2, 1}}, {{0, 3, 0}, {1, 3, 0}, {2, 3, 0}, {3, 3, 0}}}));
	// Formes et chemins.
	const auto circle = CircleShape(1.f, 12);
	CHECK(Count(MakeAxisSwapShape(circle).Vertices()) == Count(circle.Vertices()));
	CHECK(Count(MakeFlipShape(circle).Edges()) == Count(circle.Edges()));
	CHECK(Count(MakeRepeatShape(circle, 2, {3.f, 0.f}).Vertices()) == 2 * Count(circle.Vertices()));
	CHECK(Count(MakeRotateShape(circle, 1.f).Vertices()) > 0);
	CHECK(Count(MakeScaleShape(circle, {2.f, 1.f}).Vertices()) > 0);
	CHECK(Count(MakeTranslateShape(circle, {1.f, 0.f}).Vertices()) > 0);
	CHECK(Count(MakeMergeShape(circle, RectangleShape()).Vertices()) > 0);
	CHECK(Count(SubdivideShape<CircleShape>(circle).Vertices()) > Count(circle.Vertices()));
	CHECK(Count(AnyShape(circle).Edges()) > 0);
	CHECK(Count(MakeShapeToPath(circle).Vertices()) > 0);
	CHECK(Count(GridShape().Edges()) > 0);
	CHECK(Count(RoundedRectangleShape().Vertices()) > 0);
	const auto helix = HelixPath();
	CHECK(Count(MakeAxisSwapPath(helix, Axis::Y, Axis::Z, Axis::X).Vertices()) == Count(helix.Vertices()));
	CHECK(Count(MakeFlipPath(helix).Edges()) == Count(helix.Edges()));
	CHECK(Count(MakeRepeatPath(helix, 2, {0.f, 0.f, 1.f}).Vertices()) == 2 * Count(helix.Vertices()));
	CHECK(Count(MakeRotatePath(helix, 1.f, Axis::Z).Vertices()) > 0);
	CHECK(Count(MakeScalePath(helix, {1.f, 2.f, 1.f}).Vertices()) > 0);
	CHECK(Count(MakeTranslatePath(helix, {1.f, 0.f, 0.f}).Vertices()) > 0);
	CHECK(Count(MakeSubdividePath(helix).Vertices()) > Count(helix.Vertices()));
	CHECK(Count(MakeMergePath(helix, LinePath()).Vertices()) > 0);
	CHECK(Count(AnyPath(helix).Edges()) > 0);
	// Écritures SVG : une forme, un chemin, un maillage.
	SvgWriter svg(200, 200);
	svg.Perspective(1.f, 1.f, 0.1f, 50.f);
	svg.ModelView(math::FMatrix4::LookAt({0.f, 0.f, 5.f}, {}, {0.f, 1.f, 0.f}));
	svg.WriteMesh(sphere);
	svg.WriteShape(circle);
	svg.WritePath(helix);
	CHECK(svg.Str().find("<svg") != std::string::npos);
}
