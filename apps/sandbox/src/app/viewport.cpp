#include "sandbox/app/viewport.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "sandbox/modeling/operations.hpp"
#include "sandbox/modeling/triangulate.hpp"

namespace sandbox {

namespace {

constexpr float kAxisLength = 1000.f;

/// Couleur linéaire d'une couleur de matériau (composantes d'écran 0..1).
sdl3::LinearColor Linear(const modeling::Color &c) {
	return sdl3::Srgb(uint8_t(std::clamp(c.r, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.g, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.b, 0.f, 1.f) * 255.f));
}

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; }

} // namespace

bool Viewport::Init(sdl3::GpuDevice &device) {
	m_r3d = sdl3::Renderer3D::Create(device, 2048);
	return m_r3d != nullptr;
}

sdl3::Material Viewport::MaterialFor(const Document &doc, uint32_t index, float opacity) const {
	const modeling::Material fallback{String(), {0.93f, 0.92f, 0.88f, 1.f}};
	const modeling::Material &src = index < doc.Materials().size() ? doc.Materials()[index] : fallback;
	const modeling::Color c = src.color;
	sdl3::Material m;
	switch (src.kind) {
	case modeling::MaterialKind::Metal: m = sdl3::Material::Metal(Linear(c), src.roughness); break;
	case modeling::MaterialKind::Glass: m = sdl3::Material::Glass(Linear(c), c.a < 1.f ? c.a : 0.35f); break;
	case modeling::MaterialKind::Emissive: m = sdl3::Material::Emissive(Linear(c), src.strength); break;
	case modeling::MaterialKind::Unlit: m = sdl3::Material::Unlit(Linear(c)); break;
	case modeling::MaterialKind::Plastic: m = sdl3::Material::Plastic(Linear(c), src.roughness); break;
	}
	m.doubleSided = true; // faces visibles des deux côtés, comme dans SketchUp
	m.opacity = std::min(opacity, src.kind == modeling::MaterialKind::Glass ? m.opacity : c.a);
	if (m.opacity < 1.f)
		m.castShadow = false;
	return m;
}

void Viewport::Sync(const modeling::Model &model, Cache &cache) {
	if (cache.revision == model.Revision())
		return;
	cache.revision = model.Revision();
	const modeling::MeshData mesh = modeling::BuildMesh(model);
	// Un maillage sdl3 par matériau (les groupes du noyau peuvent se répéter).
	std::map<uint32_t, sdl3::MeshData> byMaterial;
	for (const modeling::MeshGroup &g : mesh.groups) {
		sdl3::MeshData &out = byMaterial[g.material == modeling::INVALID_ID ? 0u : g.material];
		std::unordered_map<uint32_t, uint32_t> remap;
		for (uint32_t i = g.start; i < g.start + g.count; ++i) {
			const uint32_t src = mesh.indices[i];
			auto [it, added] = remap.try_emplace(src, uint32_t(out.vertices.size()));
			if (added)
				out.vertices.push_back({mesh.positions[src], mesh.normals[src], mesh.uvs[src]});
			out.indices.push_back(it->second);
		}
	}
	size_t used = 0;
	for (auto &[material, data] : byMaterial) {
		if (used < cache.parts.size()) {
			if (!m_r3d->Replace(cache.parts[used].mesh, data))
				cache.parts[used].mesh = m_r3d->Upload(data);
			cache.parts[used].material = material;
		} else
			cache.parts.push_back({m_r3d->Upload(data), material});
		++used;
	}
	for (size_t i = used; i < cache.parts.size(); ++i)
		m_r3d->Release(cache.parts[i].mesh);
	cache.parts.resize(used);
	cache.edges.clear();
	for (modeling::EdgeId e : modeling::VisibleEdges(model)) {
		const modeling::Edge &edge = model.Edges()[e];
		cache.edges.push_back({e, {model.Position(edge.a), model.Position(edge.b)}});
	}
}

void Viewport::DrawModel(const Editor &editor, const modeling::Model &model, Cache &cache, const math::FMatrix4 &transform, bool active, float opacity,
						 const DisplayOptions &options) {
	Sync(model, cache);
	const Document &doc = editor.Doc();
	for (const Part &p : cache.parts)
		if (p.mesh)
			m_r3d->Draw(p.mesh, transform, MaterialFor(doc, p.material, opacity));
	if (!options.edges && !active)
		return;
	const modeling::Selection &sel = const_cast<Editor &>(editor).Selection();
	const uint8_t alpha = opacity < 1.f ? 70 : 235;
	for (const auto &[id, seg] : cache.edges) {
		const math::FVector3 a = transform.TransformPoint(seg.first), b = transform.TransformPoint(seg.second);
		if (active && sel.HasEdge(id))
			m_r3d->Line(a, b, 40, 110, 255);
		else if (options.edges)
			m_r3d->Line(a, b, 24, 26, 30, alpha);
	}
}

void Viewport::DrawSelection(Editor &editor) {
	const modeling::Model &model = editor.ActiveModel();
	const modeling::Selection &sel = editor.Selection();
	const math::FMatrix4 transform = editor.ActiveTransform();
	// Faces sélectionnées : voile bleu translucide sur la même géométrie (même profondeur).
	uint64_t key = Mix(0xcbf29ce484222325ull, model.Revision());
	key = Mix(key, editor.Context());
	for (modeling::FaceId f : sel.faces)
		key = Mix(key, f + 1);
	if (sel.faces.empty()) {
		m_selectionKey = 0;
	} else {
		if (key != m_selectionKey) {
			m_selectionKey = key;
			const modeling::MeshData mesh = modeling::BuildMesh(modeling::ExtractFaces(model, sel.faces));
			sdl3::MeshData data;
			for (size_t i = 0; i < mesh.positions.size(); ++i)
				data.vertices.push_back({mesh.positions[i], mesh.normals[i], mesh.uvs[i]});
			data.indices = mesh.indices;
			if (m_selectionMesh == 0 || !m_r3d->Replace(m_selectionMesh, data))
				m_selectionMesh = m_r3d->Upload(data);
		}
		sdl3::Material veil = sdl3::Material::Unlit(sdl3::Srgb(60, 120, 255));
		veil.opacity = 0.35f;
		veil.doubleSided = true;
		m_r3d->Draw(m_selectionMesh, transform, veil);
	}
	// Groupes sélectionnés : boîte englobante bleue (déplacée avec l'aperçu de l'outil).
	const Option<math::FVector3> offset = editor.EntityMoveOffset();
	for (EntityId id : editor.SelectedEntities())
		if (const Entity *e = editor.Doc().Find(id)) {
			const math::FMatrix4 m = offset ? math::FMatrix4::Translation(*offset) * e->transform : e->transform;
			m_r3d->WireBox(e->model.Bounds(), m, 40, 110, 255);
		}
	// Groupe ouvert : boîte en pointillés.
	if (editor.Context() != kRootId)
		if (const Entity *e = editor.Doc().Find(editor.Context())) {
			const math::FAABB b = e->model.Bounds();
			math::FVector3 c[8];
			for (int i = 0; i < 8; ++i)
				c[i] = e->transform.TransformPoint({i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z});
			static constexpr int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (const auto &ed : edges)
				m_r3d->DashedLine(c[ed[0]], c[ed[1]], 0.08f, 0.06f, 90, 90, 100);
		}
}

void Viewport::DrawAxes() {
	static constexpr struct {
		math::FVector3 dir;
		uint8_t r, g, b;
	} kAxes[] = {{{1.f, 0.f, 0.f}, 220, 50, 50}, {{0.f, 1.f, 0.f}, 50, 90, 230}, {{0.f, 0.f, 1.f}, 40, 170, 60}};
	for (const auto &a : kAxes) {
		m_r3d->Line({}, a.dir * kAxisLength, a.r, a.g, a.b);
		m_r3d->DashedLine({}, a.dir * -kAxisLength, 0.25f, 0.2f, a.r, a.g, a.b, 200);
	}
}

void Viewport::DrawPreview(const Editor &editor) {
	const modeling::ToolPreview p = editor.Preview();
	const math::FMatrix4 t = editor.ActiveTransform();
	for (const auto &[a, b] : p.lines)
		m_r3d->Line(t.TransformPoint(a), t.TransformPoint(b), 20, 20, 24);
	for (const auto &[a, b] : p.guides)
		m_r3d->DashedLine(t.TransformPoint(a), t.TransformPoint(b), 0.06f, 0.05f, 30, 30, 36);
	for (size_t i = 0; i < p.polygon.size(); ++i) // contour survolé ou visé
		m_r3d->Line(t.TransformPoint(p.polygon[i]), t.TransformPoint(p.polygon[(i + 1) % p.polygon.size()]), 255, 200, 40);
	// Inférence d'axe : trait coloré depuis le point d'ancrage.
	const modeling::Inference &at = editor.Hovered();
	if (at.IsAxis() && !p.lines.empty()) {
		const modeling::Color c = modeling::InferenceColor(at.kind);
		m_r3d->Line(t.TransformPoint(p.lines.back().first), t.TransformPoint(at.point), uint8_t(c.r * 255.f), uint8_t(c.g * 255.f), uint8_t(c.b * 255.f));
	}
}

void Viewport::Render(Editor &editor, sdl3::Renderer &view2d, int width, int height, const DisplayOptions &options, const game::GameSession *game) {
	if (!m_r3d || !m_target.Resize(*m_r3d, &view2d, width, height))
		return;
	Document &doc = editor.Doc();
	if (doc.Revision() != m_docRevision) { // annulation, chargement, palette : tout est recalculé
		m_docRevision = doc.Revision();
		for (auto &[id, cache] : m_cache)
			cache.revision = ~uint64_t(0);
	}
	// Groupes disparus : maillages libérés.
	for (auto it = m_cache.begin(); it != m_cache.end();) {
		if (it->first != kRootId && !doc.Find(it->first)) {
			for (const Part &p : it->second.parts)
				m_r3d->Release(p.mesh);
			it = m_cache.erase(it);
		} else
			++it;
	}
	if (game) { // partie : état de la session, ni sélection ni aides
		DisplayOptions play = options;
		play.xray = false;
		DrawModel(editor, doc.Root(), m_cache[kRootId], math::FMatrix4::Identity(), false, 1.f, play);
		for (const Entity &e : doc.Entities())
			if (doc.IsVisible(e) && game->IsVisible(e.id))
				DrawModel(editor, e.model, m_cache[e.id], game->TransformOf(e.id), false, 1.f, play);
	}
	const bool inGroup = editor.Context() != kRootId;
	const float faded = 0.3f, base = options.xray ? 0.5f : 1.f;
	if (!game)
		DrawModel(editor, doc.Root(), m_cache[kRootId], math::FMatrix4::Identity(), !inGroup, inGroup ? faded : base, options);
	const Option<math::FVector3> offset = editor.EntityMoveOffset();
	for (const Entity &e : doc.Entities()) {
		if (!doc.IsVisible(e) || game)
			continue;
		const bool active = e.id == editor.Context();
		const bool moving = offset && std::ranges::find(editor.SelectedEntities(), e.id) != editor.SelectedEntities().end();
		const math::FMatrix4 m = moving ? math::FMatrix4::Translation(*offset) * e.transform : e.transform;
		const bool inside = active || doc.IsAncestor(editor.Context(), e.id); // descendants du groupe ouvert : nets
		DrawModel(editor, e.model, m_cache[e.id], m, active, inGroup && !inside ? faded : base, options);
	}
	if (!game) {
		DrawSelection(editor);
		if (options.axes)
			DrawAxes();
		DrawPreview(editor);
	}

	// Environnement du document (ciel, soleil, brouillard, exposition).
	const EnvironmentSettings &e = doc.Environment();
	sdl3::Lighting lighting;
	const float hour = (e.sunHour - 12.f) / 12.f * math::PI; // 6 h → est, 18 h → ouest
	lighting.sun.direction = math::FVector3{-std::sin(hour), -0.9f + 0.35f * std::abs(std::sin(hour)), -0.45f};
	lighting.sun.intensity = e.sunIntensity;
	lighting.sun.color = Linear(e.sunColor);
	lighting.sun.castShadow = options.shadows;
	const math::FAABB box = doc.Bounds();
	if (box.min.x <= box.max.x) {
		lighting.sun.shadowCenter = box.Center();
		lighting.sun.shadowExtent = std::clamp(box.Extent().Length() * 1.2f, 4.f, 120.f);
	}
	sdl3::Environment &env = lighting.environment;
	env.zenith = Linear(e.zenith);
	env.horizon = Linear(e.horizon);
	env.ground = Linear(e.ground);
	env.intensity = e.ambient;
	env.drawSky = options.sky;
	env.showSun = false;
	env.background = Linear(e.horizon);
	env.fogColor = Linear(e.fog);
	env.fogDensity = e.fogDensity;
	lighting.exposure = e.exposure;
	lighting.post = PostFor(options.quality); // bloom, rayons, saturation, vignettage selon la qualité
	sdl3::Camera3D camera = editor.camera.Camera();
	if (game) {
		camera = game->Camera();
		game->Lights(lighting); // lampes allumées (8 ponctuelles et 4 spots au plus sont rendus)
		lighting.environment.drawSky = true;
		env.showSun = true;
	} else if (options.lamps) { // aperçu des lampes en édition, comme dans Godot
		for (const Entity &ent : doc.Entities())
			if (ent.behavior.light && doc.IsVisible(ent))
				game::AddLamp(ent.behavior, ent.transform, doc.WorldBounds(ent).Center(), lighting);
	}
	auto buffer = m_r3d->Device().AcquireCommandBuffer();
	m_r3d->Render(buffer, m_target, camera, lighting);
	(void)buffer.Submit();
}

} // namespace sandbox
