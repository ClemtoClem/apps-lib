// Tests de platform.sdl3 (rendu 3D) : couleurs, transformations, caméra, géométries (sens des
// triangles, normales), bloc d'éclairage, puis un rendu réel sur SDL_GPU relu en mémoire
// (ignoré sans device Vulkan).
#include "test.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include <sdl3/render/render3d.hpp>
#include <sdl3/core/sdl_context.hpp>

namespace {

bool Near(const math::FVector3 &a, const math::FVector3 &b, float eps = 1e-4f) { return (a - b).Length() < eps; }

/// Chaque triangle tourne dans le sens direct vu de l'extérieur : sa normale géométrique
/// suit les normales de ses sommets ; normales unitaires, indices valides.
void CheckMesh(const sdl3::MeshData &m) {
	REQUIRE(!m.vertices.empty());
	REQUIRE(m.indices.size() % 3 == 0);
	int wrong = 0, degenerate = 0;
	for (size_t i = 0; i < m.indices.size(); i += 3) {
		REQUIRE(m.indices[i] < m.vertices.size() && m.indices[i + 1] < m.vertices.size() && m.indices[i + 2] < m.vertices.size());
		const sdl3::MeshVertex &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
		const math::FVector3 face = (b.position - a.position).Cross(c.position - a.position);
		if (face.Length() < 1e-9f) {
			++degenerate;
			continue;
		}
		if (face.Dot(a.normal + b.normal + c.normal) <= 0.f)
			++wrong;
	}
	CHECK_EQ(wrong, 0);
	CHECK(degenerate * 2 <= int(m.TriangleCount())); // triangles plats : pôles, sommet d'un cône
	for (const sdl3::MeshVertex &v : m.vertices)
		CHECK_NEAR(v.normal.Length(), 1.f, 1e-3f);
}

} // namespace

TEST(Color_SrgbToLinear) {
	CHECK_NEAR(sdl3::Srgb(0, 0, 0).x, 0.f, 1e-6f);
	CHECK_NEAR(sdl3::Srgb(255, 255, 255).y, 1.f, 1e-6f);
	CHECK_NEAR(sdl3::Srgb(0x808080).z, 0.2158f, 1e-3f); // gris moyen d'écran ≈ 21,6 % linéaire
	const sdl3::LinearColor c = sdl3::Srgb(0xff8000);
	CHECK_NEAR(c.x, 1.f, 1e-6f);
	CHECK_NEAR(c.z, 0.f, 1e-6f);
}

TEST(Transform_MatrixAndInverse) {
	sdl3::Transform t;
	t.position = {1.f, 2.f, 3.f};
	t.rotation = math::FQuaternion::AxisAngle({0.f, 1.f, 0.f}, math::PI * 0.5f);
	t.scale = {2.f, 1.f, 1.f};
	const math::FMatrix4 m = t.Matrix();
	// (1, 0, 0) : échelle 2 → (2, 0, 0) ; quart de tour autour de Y → (0, 0, -2) ; translation.
	CHECK(Near(m.TransformPoint({1.f, 0.f, 0.f}), {1.f, 2.f, 1.f}));
	const math::FVector3 p{0.3f, -1.2f, 4.f};
	CHECK(Near(t.InverseTransformPoint(m.TransformPoint(p)), p));
	// La matrice de rotation coïncide avec la rotation du quaternion.
	const math::FQuaternion q = math::FQuaternion::AxisAngle({1.f, 2.f, 0.5f}, 0.8f);
	CHECK(Near(sdl3::RotationMatrix(q).TransformPoint({0.2f, 0.7f, -1.f}), q.Rotate({0.2f, 0.7f, -1.f})));
}

TEST(Ray_IntersectAabb) {
	const math::FAABB box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
	const auto hit = sdl3::IntersectRayAabb({{0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}}, box);
	REQUIRE(hit.has_value());
	CHECK_NEAR(*hit, 4.f, 1e-5f);
	CHECK(!sdl3::IntersectRayAabb({{0.f, 3.f, 5.f}, {0.f, 0.f, -1.f}}, box).has_value());
	CHECK(!sdl3::IntersectRayAabb({{0.f, 0.f, 5.f}, {0.f, 0.f, 1.f}}, box).has_value()); // derrière
	const auto inside = sdl3::IntersectRayAabb({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, box);
	REQUIRE(inside.has_value());
	CHECK_EQ(*inside, 0.f);
}

TEST(Camera_ProjectionAndScreenRay) {
	sdl3::Camera3D cam;
	cam.position = {0.f, 3.f, 8.f};
	cam.target = {0.f, 1.f, 0.f};
	const math::FMatrix4 vp = cam.ViewProjection(16.f / 9.f);
	const math::FVector4 clip = vp * math::FVector4(cam.target, 1.f);
	CHECK_NEAR(clip.x / clip.w, 0.f, 1e-5f); // la cible est au centre de l'image
	CHECK_NEAR(clip.y / clip.w, 0.f, 1e-5f);
	CHECK(clip.z / clip.w > 0.f && clip.z / clip.w < 1.f);
	const math::FRay center = cam.ScreenRay(0.5f, 0.5f, 16.f / 9.f);
	CHECK(Near(center.dir, (cam.target - cam.position).Normalized()));
	// Un point projeté puis relancé en rayon depuis l'écran retombe sur le même rayon.
	const math::FVector3 p{1.5f, 2.f, -1.f};
	const math::FVector4 c = vp * math::FVector4(p, 1.f);
	const float u = (c.x / c.w + 1.f) * 0.5f, v = (1.f - c.y / c.w) * 0.5f;
	const math::FRay ray = cam.ScreenRay(u, v, 16.f / 9.f);
	CHECK(Near(ray.dir, (p - cam.position).Normalized(), 1e-3f));
}

TEST(Orbit_Controller) {
	sdl3::OrbitController orbit;
	orbit.target = {1.f, 0.f, 0.f};
	orbit.distance = 5.f;
	orbit.yaw = 0.f;
	orbit.pitch = 0.f;
	CHECK(Near(orbit.Eye(), {1.f, 0.f, 5.f}));
	orbit.Rotate(0.f, 10000.f);
	CHECK(orbit.pitch <= 1.5f); // tangage borné : jamais à la verticale
	orbit.Zoom(1000.f);
	CHECK_EQ(orbit.distance, orbit.maxDistance);
	// Souris : un glissé commencé hors de la vue est ignoré ; commencé dedans, il continue dehors.
	const float yaw = orbit.yaw;
	CHECK(!orbit.HandleMouse(10.f, 10.f, true, false, 0.f, false));
	CHECK(!orbit.HandleMouse(50.f, 10.f, true, false, 0.f, false));
	CHECK_EQ(orbit.yaw, yaw);
	CHECK(!orbit.HandleMouse(50.f, 10.f, false, false, 0.f, true)); // relâché
	CHECK(!orbit.HandleMouse(50.f, 10.f, true, false, 0.f, true));	 // début dans la vue
	CHECK(orbit.Dragging());
	CHECK(orbit.HandleMouse(90.f, 10.f, true, false, 0.f, false)); // continue dehors
	CHECK(orbit.yaw < yaw);
	CHECK(!orbit.HandleMouse(90.f, 10.f, false, false, 0.f, false));
	CHECK(!orbit.Dragging());
	const math::FVector3 before = orbit.target;
	CHECK(!orbit.HandleMouse(0.f, 0.f, false, true, 0.f, true)); // début du déplacement
	CHECK(orbit.HandleMouse(30.f, 0.f, false, true, 0.f, true));
	CHECK(!Near(orbit.target, before)); // déplacement de la cible
	(void)orbit.HandleMouse(30.f, 0.f, false, false, 0.f, true);
	const float d = orbit.distance;
	CHECK(orbit.HandleMouse(30.f, 0.f, false, false, 1.f, true)); // molette : rapproche
	CHECK(orbit.distance < d);
	sdl3::Camera3D cam;
	orbit.Apply(cam);
	CHECK(Near(cam.target, orbit.target));
	CHECK_NEAR((cam.position - cam.target).Length(), orbit.distance, 1e-3f);
}

TEST(Mesh_Generators) {
	const sdl3::MeshData box = sdl3::MeshData::Box(2.f, 1.f, 4.f);
	CHECK_EQ(box.vertices.size(), 24u);
	CHECK_EQ(box.TriangleCount(), 12u);
	CHECK(Near(box.Bounds().min, {-1.f, -0.5f, -2.f}));
	CHECK(Near(box.Bounds().max, {1.f, 0.5f, 2.f}));
	CheckMesh(box);
	const sdl3::MeshData sphere = sdl3::MeshData::Sphere(0.75f, 24, 12);
	CHECK(Near(sphere.Bounds().max, {0.75f, 0.75f, 0.75f}, 1e-3f));
	for (const sdl3::MeshVertex &v : sphere.vertices)
		CHECK_NEAR(v.position.Length(), 0.75f, 1e-4f);
	CheckMesh(sphere);
	CheckMesh(sdl3::MeshData::Plane(3.f, 2.f));
	CheckMesh(sdl3::MeshData::Cylinder(0.5f, 0.5f, 2.f, 20));
	CheckMesh(sdl3::MeshData::Cylinder(0.f, 1.f, 1.5f, 20)); // cône
	const sdl3::MeshData torus = sdl3::MeshData::Torus(1.f, 0.25f, 12, 32);
	CHECK_NEAR(torus.Bounds().max.y, 0.25f, 1e-3f); // axe Y : épaisseur = tube
	CHECK_NEAR(torus.Bounds().max.x, 1.25f, 1e-3f);
	CheckMesh(torus);
	CheckMesh(sdl3::MeshData::TorusKnot(0.6f, 0.2f, 96, 12, 2, 3));
}

TEST(Lights_Packing) {
	sdl3::Lighting l;
	l.sun.direction = {0.f, -2.f, 0.f};
	l.points.resize(sdl3::kMaxPointLights + 3); // surnombre ignoré
	l.points[0].color = {1.f, 0.5f, 0.f};
	l.points[0].intensity = 4.f;
	sdl3::SpotLight spot;
	spot.angle = 0.6f;
	spot.penumbra = 0.5f;
	spot.direction = {0.f, 0.f, -3.f};
	l.spots = {spot};
	const sdl3::LightBlock b = sdl3::PackLights(l, {1.f, 2.f, 3.f}, true, 1.f / 1024.f);
	CHECK(Near(b.sunDir.Xyz(), {0.f, 1.f, 0.f})); // vers la lumière, normalisée
	CHECK_EQ(b.sunColor.w, 1.f);
	CHECK_EQ(b.groundColor.w, float(sdl3::kMaxPointLights));
	CHECK_EQ(b.counts.x, 1.f);
	CHECK(Near(b.pointColor[0].Xyz(), {4.f, 2.f, 0.f}));
	CHECK(Near(b.spotDir[0].Xyz(), {0.f, 0.f, -1.f}));
	CHECK_NEAR(b.spotDir[0].w, std::cos(0.6f), 1e-6f);
	CHECK(b.spotColor[0].w > b.spotDir[0].w); // cône intérieur plus serré que l'extérieur
	static_assert(sizeof(sdl3::LightBlock) % 16 == 0);
}

TEST(Sun_ShadowProjection) {
	sdl3::DirectionalLight sun;
	sun.direction = {-0.3f, -1.f, 0.2f};
	sun.shadowCenter = {2.f, 0.f, -1.f};
	sun.shadowExtent = 6.f;
	const math::FMatrix4 vp = sdl3::SunViewProjection(sun);
	const math::FVector4 c = vp * math::FVector4(sun.shadowCenter, 1.f);
	CHECK_NEAR(c.x, 0.f, 1e-4f);
	CHECK_NEAR(c.y, 0.f, 1e-4f);
	CHECK(c.z > 0.f && c.z < 1.f);
	// Un point plus près du soleil a une profondeur plus faible.
	const math::FVector4 nearer = vp * math::FVector4(sun.shadowCenter - sun.direction.Normalized() * 2.f, 1.f);
	CHECK(nearer.z < c.z);
}

TEST(Gpu_RendersLitCube) {
	auto sdl = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO);
	if (!sdl)
		SKIP("SDL vidéo indisponible");
	auto device = sdl3::GpuDevice::Create(SDL_GPU_SHADERFORMAT_SPIRV);
	if (!device)
		SKIP("aucun device SDL_GPU SPIR-V");
	auto renderer = sdl3::Renderer3D::Create(device.Value(), 512);
	REQUIRE(renderer != nullptr);
	const sdl3::MeshId cube = renderer->Upload(sdl3::MeshData::Cube(2.f));
	REQUIRE(cube != 0);
	CHECK(Near(renderer->Bounds(cube).max, {1.f, 1.f, 1.f}));
	CHECK_EQ(renderer->Upload({}), 0u); // maillage vide refusé

	constexpr int W = 64, H = 48;
	sdl3::RenderTarget3D target;
	REQUIRE(target.Resize(*renderer, nullptr, W, H)); // hors écran : pas de renderer 2D
	CHECK(target.View() == nullptr);

	sdl3::Camera3D cam;
	cam.position = {0.f, 0.f, 6.f};
	sdl3::Lighting lighting;
	lighting.sun.direction = {0.f, 0.f, -1.f}; // de face
	lighting.sun.intensity = 3.f;
	lighting.environment.drawSky = false;
	lighting.environment.background = {0.f, 0.f, 1.f};
	lighting.environment.intensity = 0.f;
	renderer->Draw(cube, math::FMatrix4::Identity(), sdl3::Material::Plastic({1.f, 0.f, 0.f}));
	renderer->Line({-3.f, -1.4f, 0.f}, {3.f, -1.4f, 0.f}, 0, 255, 0);
	auto cmd = device.Value().AcquireCommandBuffer();
	renderer->Render(cmd, target, cam, lighting);
	REQUIRE(cmd.Submit());
	CHECK_EQ(renderer->Stats().drawCalls, 2u); // ombre + image
	CHECK_EQ(renderer->Stats().triangles, 12u);
	CHECK_EQ(renderer->Stats().lines, 1u);

	const std::vector<uint8_t> px = target.ReadPixels(device.Value());
	REQUIRE(px.size() == size_t(W * H * 4));
	const auto at = [&](int x, int y) { return &px[size_t(y * W + x) * 4]; };
	const uint8_t *center = at(W / 2, H / 2), *corner = at(1, 1);
	// Face rouge éclairée (reflet spéculaire au centre : le soleil est derrière la caméra).
	CHECK(center[0] > 150 && center[0] > center[1] + 50 && center[0] > center[2] + 50);
	CHECK(corner[2] > 200 && corner[0] < 30);					// fond bleu
	int green = 0; // la ligne, sous le cube
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			green += at(x, y)[1] > 200 && at(x, y)[0] < 80 ? 1 : 0;
	CHECK(green > 20);
}

TEST(Gpu_ShadowFallsAwayFromSun) {
	auto sdl = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO);
	if (!sdl)
		SKIP("SDL vidéo indisponible");
	auto device = sdl3::GpuDevice::Create(SDL_GPU_SHADERFORMAT_SPIRV);
	if (!device)
		SKIP("aucun device SDL_GPU SPIR-V");
	auto renderer = sdl3::Renderer3D::Create(device.Value(), 1024);
	REQUIRE(renderer != nullptr);
	const sdl3::MeshId plane = renderer->Upload(sdl3::MeshData::Plane(20.f, 20.f));
	const sdl3::MeshId box = renderer->Upload(sdl3::MeshData::Box(1.f, 3.f, 1.f));
	constexpr int W = 128, H = 96;
	sdl3::RenderTarget3D target;
	REQUIRE(target.Resize(*renderer, nullptr, W, H));

	// Vue de dessus : droite de l'image = +X, haut de l'image = -Z.
	sdl3::Camera3D cam;
	cam.position = {0.f, 15.f, 0.f};
	cam.target = {};
	cam.up = {0.f, 0.f, -1.f};
	sdl3::Lighting lighting;
	lighting.sun.direction = {-1.f, -0.6f, 0.f}; // vient de +X : l'ombre part vers -X
	lighting.sun.shadowExtent = 8.f;
	lighting.environment.drawSky = false;
	lighting.environment.intensity = 0.3f;
	renderer->Draw(plane, math::FMatrix4::Identity(), sdl3::Material::Plastic({0.8f, 0.8f, 0.8f}, 0.9f));
	renderer->Draw(box, math::FMatrix4::Translation({0.f, 1.5f, 0.f}), sdl3::Material::Plastic({0.8f, 0.8f, 0.8f}, 0.9f));
	auto cmd = device.Value().AcquireCommandBuffer();
	renderer->Render(cmd, target, cam, lighting);
	REQUIRE(cmd.Submit());
	const std::vector<uint8_t> px = target.ReadPixels(device.Value());
	REQUIRE(px.size() == size_t(W * H * 4));
	// 1 unité ≈ 6,9 pixels : échantillons à 2,5 unités de part et d'autre de la boîte.
	const auto luma = [&](int x, int y) { const uint8_t *p = &px[size_t(y * W + x) * 4]; return int(p[0]) + p[1] + p[2]; };
	const int shadowSide = luma(W / 2 - 17, H / 2), litSide = luma(W / 2 + 17, H / 2);
	const int beside = luma(W / 2 - 17, H / 2 - 20); // hors de l'ombre, du même côté
	CHECK(shadowSide * 2 < litSide);
	CHECK(shadowSide * 2 < beside);
}

TEST(Camera_OrthographicRays) {
	sdl3::Camera3D cam;
	cam.position = {0.f, 0.f, 10.f};
	cam.orthographic = true;
	cam.orthoHeight = 4.f;
	const math::FRay center = cam.ScreenRay(0.5f, 0.5f, 2.f), corner = cam.ScreenRay(1.f, 0.f, 2.f);
	CHECK(Near(center.dir, corner.dir)); // rayons parallèles
	CHECK(Near(corner.origin, {4.f, 2.f, 10.f})); // demi-largeur = 2 × aspect, demi-hauteur = 2
	const math::FVector4 c = cam.ViewProjection(2.f) * math::FVector4(4.f, 2.f, 0.f, 1.f);
	CHECK_NEAR(c.x / c.w, 1.f, 1e-5f); // coin de l'image
	CHECK_NEAR(c.y / c.w, 1.f, 1e-5f);
}

TEST(Gpu_ReplaceReleaseAndDashes) {
	auto sdl = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO);
	if (!sdl)
		SKIP("SDL vidéo indisponible");
	auto device = sdl3::GpuDevice::Create(SDL_GPU_SHADERFORMAT_SPIRV);
	if (!device)
		SKIP("aucun device SDL_GPU SPIR-V");
	auto renderer = sdl3::Renderer3D::Create(device.Value(), 256);
	REQUIRE(renderer != nullptr);
	const sdl3::MeshId a = renderer->Upload(sdl3::MeshData::Cube(1.f));
	const sdl3::MeshId b = renderer->Upload(sdl3::MeshData::Cube(1.f));
	REQUIRE(a != 0 && b != 0);
	CHECK(renderer->Replace(a, sdl3::MeshData::Box(4.f, 2.f, 2.f)));
	CHECK_NEAR(renderer->Bounds(a).max.x, 2.f, 1e-5f);
	CHECK(!renderer->Replace(99, sdl3::MeshData::Cube()));
	renderer->Release(b);
	renderer->Draw(b, math::FMatrix4::Identity(), sdl3::Material::Plastic({1.f, 1.f, 1.f})); // ignoré : libéré
	const sdl3::MeshId c = renderer->Upload(sdl3::MeshData::Sphere());
	CHECK_EQ(c, b); // emplacement réutilisé
	sdl3::RenderTarget3D target;
	REQUIRE(target.Resize(*renderer, nullptr, 32, 32));
	renderer->DashedLine({-5.f, 0.f, 0.f}, {5.f, 0.f, 0.f}, 0.5f, 0.5f, 255, 0, 0);
	sdl3::Camera3D cam;
	cam.position = {0.f, 0.f, 12.f};
	sdl3::Lighting lighting;
	auto cmd = device.Value().AcquireCommandBuffer();
	renderer->Render(cmd, target, cam, lighting);
	REQUIRE(cmd.Submit());
	CHECK_EQ(renderer->Stats().lines, 10u); // 10 m en traits de 0,5 m et espaces de 0,5 m
}
