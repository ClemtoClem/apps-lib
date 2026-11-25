#include "sandbox/editor/editor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "sandbox/modeling/io.hpp"
#include "sandbox/modeling/operations.hpp"
#include "sandbox/modeling/primitives.hpp"
#include "sandbox/modeling/triangulate.hpp"

namespace sandbox {

namespace {

constexpr float kDeg = math::DEG2RAD;

math::FRay ToLocal(const math::FRay &ray, const math::FMatrix4 &inverse) {
	return {inverse.TransformPoint(ray.origin), inverse.TransformDirection(ray.dir).Normalized()};
}

bool IsModifyingTool(modeling::ToolKind k) { return k != modeling::ToolKind::SELECT && k != modeling::ToolKind::TAPE; }

} // namespace

// ── NavCamera ────────────────────────────────────────────────────────────────

NavCamera::NavCamera() {
	m_orbit.target = {0.f, 1.f, 0.f};
	m_orbit.distance = 16.f;
	m_orbit.yaw = 0.75f;
	m_orbit.pitch = 0.42f;
	m_orbit.minDistance = 0.2f;
	m_orbit.maxDistance = 2000.f;
	m_camera.fovY = 35.f * kDeg;
	m_camera.zNear = 0.05f;
	m_camera.zFar = 4000.f;
	Apply();
}

void NavCamera::Apply() {
	m_orbit.Apply(m_camera);
	// Projection parallèle : même cadrage apparent à la distance de la cible.
	m_camera.orthoHeight = 2.f * m_orbit.distance * std::tan(m_camera.fovY * 0.5f);
}

void NavCamera::Orbit(float dx, float dy) {
	m_orbit.Rotate(dx, dy);
	Apply();
}

void NavCamera::Pan(float dx, float dy, float viewportHeight) {
	// Un pixel = la hauteur visible à la distance de la cible divisée par la hauteur de l'image.
	const float worldPerPixel = 2.f * m_orbit.distance * std::tan(m_camera.fovY * 0.5f) / std::max(viewportHeight, 1.f);
	const math::FVector3 forward = (m_orbit.target - m_orbit.Eye()).Normalized();
	const math::FVector3 right = forward.Cross({0.f, 1.f, 0.f}).Normalized(), up = right.Cross(forward);
	m_orbit.target = m_orbit.target - right * (dx * worldPerPixel) + up * (dy * worldPerPixel);
	Apply();
}

void NavCamera::ZoomAt(float steps, const math::FVector3 &focus) {
	const float factor = std::pow(0.85f, steps);
	const float before = m_orbit.distance;
	m_orbit.Zoom(factor);
	// Le point visé reste sous le curseur : la cible glisse vers lui dans la même proportion.
	const float k = 1.f - m_orbit.distance / before;
	m_orbit.target = m_orbit.target + (focus - m_orbit.target) * k;
	Apply();
}

void NavCamera::ZoomExtents(const math::FAABB &box, float aspect) {
	if (box.min.x > box.max.x)
		return;
	const math::FVector3 center = box.Center();
	const float radius = std::max(0.5f, box.Extent().Length());
	const float halfFov = std::min(m_camera.fovY, m_camera.fovY * aspect) * 0.5f;
	m_orbit.target = center;
	m_orbit.distance = std::clamp(radius / std::sin(halfFov) * 1.05f, m_orbit.minDistance, m_orbit.maxDistance);
	Apply();
}

void NavCamera::SetView(StandardView view, const math::FAABB &box, float aspect) {
	switch (view) {
	case StandardView::Iso: m_orbit.yaw = 0.75f, m_orbit.pitch = 0.5f; break;
	case StandardView::Top: m_orbit.yaw = 0.f, m_orbit.pitch = 1.5f; break;
	case StandardView::Bottom: m_orbit.yaw = 0.f, m_orbit.pitch = -1.5f; break;
	case StandardView::Front: m_orbit.yaw = 0.f, m_orbit.pitch = 0.f; break;
	case StandardView::Back: m_orbit.yaw = math::PI, m_orbit.pitch = 0.f; break;
	case StandardView::Left: m_orbit.yaw = -math::PI * 0.5f, m_orbit.pitch = 0.f; break;
	case StandardView::Right: m_orbit.yaw = math::PI * 0.5f, m_orbit.pitch = 0.f; break;
	}
	Apply();
	ZoomExtents(box, aspect);
}

void NavCamera::SetOrthographic(bool on) {
	m_camera.orthographic = on;
	Apply();
}

void NavCamera::SetFov(float degrees) {
	m_camera.fovY = std::clamp(degrees, 10.f, 120.f) * kDeg;
	Apply();
}

SceneView NavCamera::Save(String name) const {
	return {std::move(name), m_camera.position, m_camera.target, m_camera.fovY / kDeg, m_camera.orthographic, m_camera.orthoHeight};
}

void NavCamera::Restore(const SceneView &v) {
	const math::FVector3 d = v.eye - v.target;
	m_orbit.target = v.target;
	m_orbit.distance = std::max(d.Length(), m_orbit.minDistance);
	m_orbit.pitch = std::asin(std::clamp(d.y / m_orbit.distance, -1.f, 1.f));
	m_orbit.yaw = std::atan2(d.x, d.z);
	m_camera.fovY = v.fovDegrees * kDeg;
	m_camera.orthographic = v.orthographic;
	Apply();
}

// ── Editor ───────────────────────────────────────────────────────────────────

Editor::Editor() : m_tool(modeling::MakeTool(modeling::ToolKind::SELECT)) {
	inference.pixelTolerance = 12.f;
}

void Editor::Reset(Document doc) {
	m_doc = std::move(doc);
	m_history.Clear();
	m_context = kRootId;
	m_selection.Clear();
	m_entities.clear();
	m_tool = modeling::MakeTool(modeling::ToolKind::SELECT);
	m_measure.clear();
	m_moveAnchor = m_moveOffset = NONE;
	m_modified = false;
	m_amendable = false;
	m_lastGroupMove = NONE;
	material = 0;
}

namespace {

/// Fin d'une opération : les autres instances du composant ouvert suivent.
struct InstanceSync {
	Editor &editor;
	~InstanceSync() { (void)editor.SyncInstances(); }
};

} // namespace

modeling::Model &Editor::ActiveModel() {
	if (modeling::Model *m = m_doc.ModelOf(m_context))
		return *m;
	m_context = kRootId; // groupe supprimé (annulation) : retour à la racine
	return m_doc.Root();
}

bool Editor::OpenGroup(EntityId id) {
	const Entity *e = m_doc.Find(id);
	if (!e || m_doc.IsLocked(*e))
		return false;
	m_tool->Reset();
	m_context = id;
	m_syncedRevision = e->model.Revision();
	m_selection.Clear();
	m_entities.clear();
	SetStatus(String::Format("Édition de « {} » — Échap ou double-clic dehors pour sortir", e->name));
	return true;
}

void Editor::CloseGroup() {
	if (m_context == kRootId)
		return;
	(void)SyncInstances();
	const EntityId was = m_context;
	m_tool->Reset();
	const Entity *e = m_doc.Find(was);
	m_context = e ? e->parent : kRootId; // groupe imbriqué : on remonte d'un niveau
	m_selection.Clear();
	m_entities = {was};
	if (m_context != kRootId)
		if (const Entity *p = m_doc.Find(m_context))
			m_syncedRevision = p->model.Revision();
	SetStatus(m_context == kRootId ? String() : String::Format("Édition de « {} »", m_doc.Find(m_context)->name));
}

void Editor::Reveal(EntityId id) {
	if (!m_doc.Find(id))
		return;
	CloseAllGroups();
	std::vector<EntityId> chain; // ancêtres, du parent vers le haut
	for (const Entity *up = m_doc.Find(m_doc.Find(id)->parent); up; up = m_doc.Find(up->parent))
		chain.push_back(up->id);
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		if (!OpenGroup(*it))
			break;
	SelectEntity(id);
}

bool Editor::Reparent(EntityId id, EntityId parent) {
	const Entity *e = m_doc.Find(id);
	if (!e || e->parent == parent || id == parent || (parent && m_doc.IsAncestor(id, parent)))
		return false;
	const Document before = m_doc;
	if (!m_doc.Reparent(id, parent))
		return false;
	CloseAllGroups();
	m_entities = {id};
	Commit(before, String("Changer de parent"));
	return true;
}

void Editor::CloseAllGroups() {
	while (m_context != kRootId) {
		const EntityId before = m_context;
		CloseGroup();
		if (m_context == before)
			break;
	}
}

EntityId Editor::PickEntity(const math::FRay &ray, float *distance) const {
	EntityId best = 0;
	float bestT = 1e30f;
	for (const Entity &e : m_doc.Entities()) {
		// Comme SketchUp : seuls les groupes du niveau ouvert se choisissent ; toucher un
		// descendant choisit son ancêtre de ce niveau.
		const EntityId top = e.id == m_context ? 0 : m_doc.TopUnder(e.id, m_context);
		if (!top || !m_doc.IsVisible(e) || m_doc.IsLocked(*m_doc.Find(top)))
			continue;
		const math::FMatrix4 inv = e.transform.Inverse();
		const math::FRay local = ToLocal(ray, inv);
		if (const auto hit = modeling::Raycast(e.model, local)) {
			const float t = e.transform.TransformPoint(hit->point).Distance(ray.origin); // distance du monde
			if (t < bestT)
				bestT = t, best = top;
		}
	}
	if (distance)
		*distance = bestT;
	return best;
}

bool Editor::EmptyAt(const math::FRay &ray) const {
	if (m_context != kRootId)
		return false; // la sélection par cadre ne s'applique qu'aux groupes (à la racine)
	if (PickEntity(ray) != 0)
		return false;
	return !modeling::Raycast(m_doc.Root(), ray); // aucune face libre non plus
}

void Editor::SetTool(modeling::ToolKind kind) {
	m_tool = modeling::MakeTool(kind);
	m_amendable = false;
	m_lastGroupMove = NONE;
	m_measure.clear();
	m_moveAnchor = m_moveOffset = NONE;
	SetStatus(String());
}

bool Editor::ToolBusy() const { return m_tool->IsActive() || m_moveAnchor.IsSome(); }

modeling::ToolContext Editor::Ctx() {
	modeling::ToolContext ctx{ActiveModel(), m_selection, material};
	ctx.angleStepDeg = inference.angleStepDeg;
	return ctx;
}

modeling::ToolEvent Editor::LocalEvent(const Pointer &p) {
	const math::FMatrix4 transform = ActiveTransform();
	const math::FMatrix4 inv = transform.Inverse();
	modeling::InferenceQuery q;
	q.ray = ToLocal(p.ray, inv);
	q.cursor = p.cursor;
	q.project = [&p, transform](const modeling::Vec3 &local) { return p.project(transform.TransformPoint(local)); };
	q.anchor = m_tool->Anchor();
	modeling::InferenceSettings settings = inference;
	settings.groundY = inv.TransformPoint({0.f, 0.f, 0.f}).y; // sol du monde dans le repère du groupe
	const modeling::Inference at = modeling::Infer(ActiveModel(), q, settings);
	return {at, q.ray, p.shift, p.ctrl};
}

void Editor::Hover(const Pointer &p) {
	modeling::ToolContext ctx = Ctx();
	const modeling::ToolEvent e = LocalEvent(p);
	m_hover = e.at;
	if (m_moveAnchor && e.at.IsValid())
		m_moveOffset = Some(e.at.point - *m_moveAnchor); // racine : repère du monde
	m_tool->Hover(ctx, e);
}

void Editor::SetStatus(String text, bool error) {
	m_status = std::move(text);
	m_statusError = error;
}

void Editor::Commit(const Document &before, String label) {
	m_history.Push(before, std::move(label));
	m_modified = true;
	m_amendable = false; // toute autre modification ferme la correction
	m_lastGroupMove = NONE;
	m_doc.Touch();
}

void Editor::Edit(String label, bool merge) {
	if (merge && m_history.CanUndo() && m_history.UndoLabel() == label) {
		m_modified = true;
		m_doc.Touch();
		return; // même geste : un seul pas d'annulation
	}
	Commit(m_doc, std::move(label));
}

void Editor::MoveGroups(const math::FVector3 &delta) {
	for (EntityId id : m_entities)
		if (Entity *ent = m_doc.Find(id)) {
			// Un descendant d'un autre groupe sélectionné suit déjà son ancêtre.
			if (std::ranges::any_of(m_entities, [&](EntityId other) { return other != id && m_doc.IsAncestor(other, id); }))
				continue;
			m_doc.SetTransform(id, math::FMatrix4::Translation(delta) * ent->transform);
		}
}

void Editor::Report(const modeling::ToolResult &r, const Document &before) {
	if (r.error)
		SetStatus(*r.error, true);
	else if (!r.message.IsEmpty())
		SetStatus(r.message);
	if (r.changed) {
		ActiveModel().Touch();
		Commit(before, String(m_tool->Name()));
		m_selection.Prune(ActiveModel());
		m_amendable = m_tool->CanAmend();
		m_amendLabel = r.message;
	}
}

bool Editor::HandleRootClick(const Pointer &p) {
	using modeling::ToolKind;
	const ToolKind kind = m_tool->Kind();
	// Déplacement d'un groupe sélectionné : premier clic = point de départ, second = arrivée.
	if (kind == ToolKind::MOVE && (!m_entities.empty() || m_moveAnchor)) {
		const modeling::ToolEvent e = LocalEvent(p);
		if (!e.at.IsValid())
			return true;
		if (!m_moveAnchor) {
			m_moveAnchor = Some(e.at.point);
			m_moveOffset = Some(math::FVector3{});
			SetStatus(String("Cliquez la destination (ou tapez une distance)"));
			return true;
		}
		const Document before = m_doc;
		const math::FVector3 delta = e.at.point - *m_moveAnchor;
		MoveGroups(delta);
		m_moveAnchor = m_moveOffset = NONE;
		Commit(before, String("Déplacer le groupe"));
		SetStatus(String::Format("Groupe déplacé de {}", modeling::FormatLength(delta.Length())));
		if (delta.LengthSq() > 1e-12f) { // une distance tapée ensuite corrige ce déplacement
			m_lastGroupMove = Some(delta.Normalized());
			m_amendable = true;
			m_amendLabel = String::Format("Distance {}", modeling::FormatLength(delta.Length()));
		}
		return true;
	}
	float entityT = 1e30f;
	const EntityId hit = PickEntity(p.ray, &entityT);
	if (hit == 0)
		return false;
	// Une face libre plus proche que le groupe garde la main.
	if (const auto rootHit = modeling::Raycast(m_doc.Root(), p.ray); rootHit && rootHit->distance < entityT)
		return false;
	switch (kind) {
	case ToolKind::SELECT:
		m_selection.Clear();
		SelectEntity(hit, p.shift || p.ctrl);
		return true;
	case ToolKind::PAINT: {
		const Document before = m_doc;
		Entity *ent = m_doc.Find(hit);
		for (size_t f = 0; f < ent->model.Faces().size(); ++f)
			if (ent->model.IsAlive(modeling::FaceId(f)))
				modeling::Paint(ent->model, modeling::FaceId(f), material);
		ent->model.Touch();
		Commit(before, String("Peindre le groupe"));
		SetStatus(String::Format("« {} » peint", ent->name));
		return true;
	}
	case ToolKind::ERASER: {
		const Document before = m_doc;
		const String name = m_doc.Find(hit)->name;
		m_doc.RemoveEntity(hit);
		std::erase(m_entities, hit);
		Commit(before, String("Effacer le groupe"));
		SetStatus(String::Format("« {} » effacé", name));
		return true;
	}
	default: return false; // les outils de dessin s'appuient sur la géométrie (inférences)
	}
}

void Editor::Click(const Pointer &p) {
	const InstanceSync sync{*this};
	m_amendable = false; // un nouveau geste commence : la correction se ferme
	m_lastGroupMove = NONE;
	if (m_context == kRootId && HandleRootClick(p))
		return;
	if (m_tool->Kind() == modeling::ToolKind::SELECT && !(p.shift || p.ctrl))
		m_entities.clear();
	modeling::ToolContext ctx = Ctx();
	const modeling::ToolEvent e = LocalEvent(p);
	m_hover = e.at;
	const bool modifying = IsModifyingTool(m_tool->Kind());
	const Document before = modifying ? m_doc : Document();
	m_tool->Hover(ctx, e);
	Report(m_tool->Click(ctx, e), before);
}

void Editor::Escape() {
	if (!m_measure.IsEmpty()) {
		m_measure.clear();
		return;
	}
	if (m_moveAnchor) {
		m_moveAnchor = m_moveOffset = NONE;
		return;
	}
	if (m_tool->IsActive()) {
		m_tool->Reset();
		return;
	}
	if (m_tool->Kind() != modeling::ToolKind::SELECT) {
		SetTool(modeling::ToolKind::SELECT);
		return;
	}
	if (!m_selection.IsEmpty() || !m_entities.empty()) {
		ClearSelection();
		return;
	}
	CloseGroup();
}

modeling::ToolPreview Editor::Preview() const {
	modeling::ToolContext ctx{const_cast<Editor *>(this)->ActiveModel(), const_cast<modeling::Selection &>(m_selection), material};
	modeling::ToolPreview p = m_tool->Preview(ctx);
	if (CanAmend()) { // opération posée : sa cote reste modifiable
		p.measure = m_amendLabel;
		p.hint = String("Tapez une autre cote puis Entrée pour corriger ce qui vient d'être fait, ou continuez");
	}
	if (m_moveAnchor) {
		p.hint = String("Cliquez la destination du groupe — Échap pour annuler");
		p.measure = String::Format("Distance {}", modeling::FormatLength(m_moveOffset ? m_moveOffset->Length() : 0.f));
		if (m_moveOffset)
			p.guides.push_back({*m_moveAnchor, *m_moveAnchor + *m_moveOffset});
	} else if (m_context == kRootId && m_tool->Kind() == modeling::ToolKind::MOVE && !m_entities.empty())
		p.hint = String("Cliquez un point de départ pour déplacer le groupe sélectionné");
	return p;
}

Option<math::FVector3> Editor::EntityMoveOffset() const noexcept { return m_moveAnchor ? m_moveOffset : Option<math::FVector3>(NONE); }

bool Editor::TypeText(std::string_view text) {
	// Saisie de cotes : commence par un chiffre (ou un signe) pendant un outil ; ensuite tout y va.
	if (m_measure.IsEmpty()) {
		const bool starts = !text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-' || text[0] == ',' || text[0] == '.' || text[0] == ';');
		if (!starts || m_tool->Kind() == modeling::ToolKind::SELECT)
			return false;
	}
	m_measure.Append(text);
	return true;
}

void Editor::Backspace() {
	if (m_measure.IsEmpty())
		return;
	// Retire le dernier caractère UTF-8 entier (« ° »).
	size_t n = m_measure.size() - 1;
	while (n > 0 && (static_cast<unsigned char>(m_measure[n]) & 0xC0) == 0x80)
		--n;
	m_measure.resize(n);
}

bool Editor::CanAmend() const { return m_amendable && !m_tool->IsActive() && !m_moveAnchor && (m_lastGroupMove || m_tool->CanAmend()); }

void Editor::SubmitMeasure() {
	const InstanceSync sync{*this};
	if (m_measure.IsEmpty())
		return;
	std::vector<float> values = modeling::ParseMeasurement(m_measure);
	m_measure.clear();
	// Champs vides (« 2; ») : seul le rectangle en garde la place, les autres outils ne lisent que des cotes.
	if (m_tool->Kind() != modeling::ToolKind::RECTANGLE || m_lastGroupMove || m_moveAnchor)
		std::erase_if(values, [](float v) { return std::isnan(v); });
	if (values.empty()) {
		SetStatus(String("Saisie invalide (ex. 2,5 · 3;2 · 2; · 45° · 120cm)"), true);
		return;
	}
	if (m_moveAnchor && m_moveOffset && m_moveOffset->Length() > 1e-6f) { // distance du déplacement de groupe
		const Document before = m_doc;
		const math::FVector3 dir = m_moveOffset->Normalized();
		MoveGroups(dir * values[0]);
		m_moveAnchor = m_moveOffset = NONE;
		Commit(before, String("Déplacer le groupe"));
		m_lastGroupMove = Some(dir);
		m_amendable = true;
		m_amendLabel = String::Format("Distance {}", modeling::FormatLength(values[0]));
		return;
	}
	if (CanAmend()) {
		// Correction : retour à l'état d'avant l'opération, refaite avec la nouvelle cote.
		const String label = m_history.UndoLabel();
		const Option<math::FVector3> groupMove = m_lastGroupMove;
		m_history.Revert(m_doc);
		m_selection.Prune(ActiveModel());
		const Document before = m_doc;
		if (groupMove) {
			MoveGroups(*groupMove * values[0]);
			Commit(before, label);
			m_lastGroupMove = groupMove;
			m_amendable = true;
			m_amendLabel = String::Format("Distance {}", modeling::FormatLength(values[0]));
			SetStatus(String::Format("Déplacement corrigé : {}", modeling::FormatLength(values[0])));
			return;
		}
		modeling::ToolContext ctx = Ctx();
		const modeling::ToolResult r = m_tool->Amend(ctx, values);
		Report(r, before);
		if (r.changed)
			SetStatus(r.message + " (corrigé)");
		else if (!r.error) // rien refait : l'état d'origine revient
			m_history.Push(before, label);
		return;
	}
	modeling::ToolContext ctx = Ctx();
	const Document before = m_doc;
	Report(m_tool->Type(ctx, values), before);
}

Option<Editor::Placement> Editor::EntityPlacement(EntityId id) const {
	const Entity *e = m_doc.Find(id);
	if (!e)
		return NONE;
	const math::FMatrix4 &m = e->transform;
	const math::FVector3 scale{math::FVector3{m.m[0][0], m.m[0][1], m.m[0][2]}.Length(), math::FVector3{m.m[1][0], m.m[1][1], m.m[1][2]}.Length(),
							   math::FVector3{m.m[2][0], m.m[2][1], m.m[2][2]}.Length()};
	const math::FAABB local = e->model.Bounds();
	const math::FVector3 extent = local.min.x <= local.max.x ? local.max - local.min : math::FVector3{};
	float yaw = std::atan2(-m.m[0][2], m.m[0][0]) * math::RAD2DEG;
	if (std::abs(yaw) < 0.005f)
		yaw = 0.f; // jamais « -0° »
	return Some(Placement{{m.m[3][0], m.m[3][1], m.m[3][2]}, yaw, extent * scale});
}

bool Editor::SetEntityPlacement(EntityId id, const Placement &p, String label, bool merge) {
	Entity *e = m_doc.Find(id);
	if (!e || e->locked)
		return false;
	const math::FAABB local = e->model.Bounds();
	const math::FVector3 extent = local.min.x <= local.max.x ? local.max - local.min : math::FVector3{};
	const Placement current = EntityPlacement(id).Unwrap();
	math::FVector3 scale;
	for (int i = 0; i < 3; ++i) { // axe plat (face posée au sol) : échelle inchangée
		const float old = extent[i] > 1e-6f ? current.size[i] / extent[i] : 1.f;
		scale[i] = extent[i] > 1e-6f && p.size[i] > 1e-6f ? p.size[i] / extent[i] : old;
	}
	Edit(std::move(label), merge);
	e = m_doc.Find(id);
	m_doc.SetTransform(id, math::FMatrix4::Translation(p.position) * math::FMatrix4::Rotation({0.f, 1.f, 0.f}, p.yawDegrees * math::DEG2RAD) *
							   math::FMatrix4::Scale(scale)); // les enfants suivent
	return true;
}

Option<math::FAABB> Editor::SelectionBounds() {
	const std::vector<modeling::VertexId> vertices = m_selection.Vertices(ActiveModel());
	if (vertices.empty())
		return NONE;
	math::FAABB box{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
	for (modeling::VertexId v : vertices) {
		box.min = math::Min(box.min, ActiveModel().Position(v));
		box.max = math::Max(box.max, ActiveModel().Position(v));
	}
	return Some(box);
}

bool Editor::ResizeSelection(const math::FVector3 &size, String label, bool merge) {
	const InstanceSync sync{*this};
	const Option<math::FAABB> box = SelectionBounds();
	if (!box)
		return false;
	const math::FVector3 extent = box->max - box->min;
	math::FVector3 factors{1.f, 1.f, 1.f};
	bool any = false;
	for (int i = 0; i < 3; ++i)
		if (extent[i] > 1e-6f && size[i] > 1e-6f && std::abs(size[i] - extent[i]) > 1e-7f) {
			factors[i] = size[i] / extent[i];
			any = true;
		}
	if (!any)
		return false;
	Edit(std::move(label), merge);
	modeling::Scale(ActiveModel(), m_selection.Vertices(ActiveModel()), box->min, factors);
	ActiveModel().Touch();
	m_selection.Prune(ActiveModel());
	return true;
}

void Editor::SelectEntity(EntityId id, bool add) {
	if (!add)
		m_entities.clear();
	if (const auto it = std::ranges::find(m_entities, id); it != m_entities.end())
		m_entities.erase(it);
	else if (m_doc.Find(id))
		m_entities.push_back(id);
}

void Editor::ClearSelection() {
	m_selection.Clear();
	m_entities.clear();
}

void Editor::SelectAll() {
	const modeling::Model &m = ActiveModel();
	m_selection.Clear();
	for (size_t f = 0; f < m.Faces().size(); ++f)
		if (m.IsAlive(modeling::FaceId(f)))
			m_selection.faces.push_back(modeling::FaceId(f));
	for (size_t e = 0; e < m.Edges().size(); ++e)
		if (m.IsEdgeAlive(modeling::EdgeId(e)) && m.FacesOfEdge(modeling::EdgeId(e)).empty())
			m_selection.edges.push_back(modeling::EdgeId(e));
	m_entities.clear();
	if (m_context == kRootId)
		for (const Entity &e : m_doc.Entities())
			if (m_doc.IsVisible(e))
				m_entities.push_back(e.id);
}

void Editor::MoveSelected(const math::FVector3 &delta) { MoveGroups(delta); }

void Editor::RotateSelected(const math::FVector3 &center, const math::FVector3 &axis, float angle) {
	const math::FMatrix4 rot = math::FMatrix4::Translation(center) * math::FMatrix4::Rotation(axis, angle) *
							   math::FMatrix4::Translation(center * -1.f);
	for (EntityId id : m_entities)
		if (Entity *ent = m_doc.Find(id)) {
			if (std::ranges::any_of(m_entities, [&](EntityId other) { return other != id && m_doc.IsAncestor(other, id); }))
				continue;
			m_doc.SetTransform(id, rot * ent->transform);
		}
}

void Editor::ScaleSelected(const math::FVector3 &center, float factor) {
	if (factor <= 1e-4f)
		return;
	const math::FMatrix4 s = math::FMatrix4::Translation(center) * math::FMatrix4::Scale({factor, factor, factor}) *
							 math::FMatrix4::Translation(center * -1.f);
	for (EntityId id : m_entities)
		if (Entity *ent = m_doc.Find(id)) {
			if (std::ranges::any_of(m_entities, [&](EntityId other) { return other != id && m_doc.IsAncestor(other, id); }))
				continue;
			m_doc.SetTransform(id, s * ent->transform);
		}
}

void Editor::AlignSelection(Align3D align) {
	// Groupes de niveau supérieur seulement (les descendants suivent leur ancêtre).
	std::vector<EntityId> tops;
	for (EntityId id : m_entities)
		if (!std::ranges::any_of(m_entities, [&](EntityId o) { return o != id && m_doc.IsAncestor(o, id); }))
			tops.push_back(id);
	if (tops.size() < 2)
		return;
	const int axis = int(align) / 3;	 // 0 X, 1 Y, 2 Z
	const int mode = int(align) % 3;	 // 0 bord bas, 1 centre, 2 bord haut
	const auto coord = [&](const math::FAABB &box, int m) {
		return m == 0 ? box.min[axis] : (m == 2 ? box.max[axis] : box.Center()[axis]);
	};
	float lo = 1e30f, hi = -1e30f;
	for (EntityId id : tops) {
		const math::FAABB box = m_doc.WorldBounds(*m_doc.Find(id));
		lo = std::min(lo, box.min[axis]);
		hi = std::max(hi, box.max[axis]);
	}
	const float target = mode == 0 ? lo : (mode == 2 ? hi : (lo + hi) * 0.5f);
	const Document before = m_doc;
	for (EntityId id : tops) {
		Entity *ent = m_doc.Find(id);
		const math::FAABB box = m_doc.WorldBounds(*ent);
		const float delta = target - coord(box, mode);
		if (std::abs(delta) < 1e-6f)
			continue;
		math::FVector3 d{};
		d[axis] = delta;
		m_doc.SetTransform(id, math::FMatrix4::Translation(d) * ent->transform);
	}
	Commit(before, String("Aligner les groupes"));
}

void Editor::DistributeSelection(int axis) {
	std::vector<EntityId> tops;
	for (EntityId id : m_entities)
		if (!std::ranges::any_of(m_entities, [&](EntityId o) { return o != id && m_doc.IsAncestor(o, id); }))
			tops.push_back(id);
	if (tops.size() < 3)
		return; // la répartition n'a d'effet qu'à partir de 3 groupes
	std::ranges::sort(tops, [&](EntityId a, EntityId b) {
		return m_doc.WorldBounds(*m_doc.Find(a)).Center()[axis] < m_doc.WorldBounds(*m_doc.Find(b)).Center()[axis];
	});
	const float first = m_doc.WorldBounds(*m_doc.Find(tops.front())).Center()[axis];
	const float last = m_doc.WorldBounds(*m_doc.Find(tops.back())).Center()[axis];
	const Document before = m_doc;
	const size_t n = tops.size();
	for (size_t i = 1; i + 1 < n; ++i) { // les extrémités restent en place
		Entity *ent = m_doc.Find(tops[i]);
		const float current = m_doc.WorldBounds(*ent).Center()[axis];
		const float target = first + (last - first) * float(i) / float(n - 1);
		const float delta = target - current;
		if (std::abs(delta) < 1e-6f)
			continue;
		math::FVector3 d{};
		d[axis] = delta;
		m_doc.SetTransform(tops[i], math::FMatrix4::Translation(d) * ent->transform);
	}
	Commit(before, String("Répartir les groupes"));
}

bool Editor::DeleteSelection() {
	const InstanceSync sync{*this};
	if (m_selection.IsEmpty() && m_entities.empty())
		return false;
	const Document before = m_doc;
	if (!m_selection.IsEmpty()) {
		modeling::Erase(ActiveModel(), m_selection.edges, m_selection.faces);
		ActiveModel().Touch();
	}
	for (EntityId id : m_entities)
		m_doc.RemoveEntity(id);
	ClearSelection();
	Commit(before, String("Effacer"));
	SetStatus(String("Sélection effacée"));
	return true;
}

bool Editor::GroupSelection() {
	if (m_selection.IsEmpty() && !m_entities.empty()) { // groupes choisis : un groupe parent
		const Document before = m_doc;
		const EntityId id = m_doc.GroupEntities(m_entities);
		if (id == 0)
			return false;
		m_entities = {id};
		Commit(before, String("Grouper"));
		SetStatus(String::Format("« {} » créé : {} enfant(s)", m_doc.Find(id)->name, m_doc.Children(id).size()));
		return true;
	}
	if (m_context != kRootId || m_selection.IsEmpty()) {
		SetStatus(String("Grouper : sélectionnez des faces ou des arêtes de la géométrie libre"), true);
		return false;
	}
	const Document before = m_doc;
	const EntityId id = m_doc.MakeGroup(m_selection);
	if (id == 0)
		return false;
	m_doc.Root().Touch();
	m_selection.Clear();
	m_entities = {id};
	Commit(before, String("Grouper"));
	SetStatus(String::Format("« {} » créé", m_doc.Find(id)->name));
	return true;
}

bool Editor::ExplodeSelection() {
	if (m_entities.empty())
		return false;
	const Document before = m_doc;
	for (EntityId id : m_entities)
		m_doc.Explode(id);
	m_doc.Root().Touch();
	m_entities.clear();
	Commit(before, String("Éclater"));
	SetStatus(String("Groupe éclaté en géométrie libre"));
	return true;
}

bool Editor::DuplicateSelection() {
	if (m_entities.empty())
		return false;
	const Document before = m_doc;
	std::vector<EntityId> copies;
	for (EntityId id : m_entities) {
		const math::FAABB box = m_doc.WorldBounds(*m_doc.Find(id));
		copies.push_back(m_doc.Duplicate(id, {box.max.x - box.min.x + 0.5f, 0.f, 0.f}));
	}
	m_entities = copies;
	Commit(before, String("Dupliquer"));
	return true;
}

bool Editor::MakeComponent() {
	if (m_entities.size() != 1 || !m_doc.Find(m_entities.front()) || m_doc.Find(m_entities.front())->component)
		return false;
	Checkpoint("Créer un composant");
	m_doc.MakeComponent(m_entities.front());
	SetStatus(String("Composant créé — ses copies (Ctrl+D, Déplacer + Ctrl) partagent sa géométrie"));
	return true;
}

bool Editor::MakeUnique() {
	if (m_entities.size() != 1 || !m_doc.Find(m_entities.front()) || !m_doc.Find(m_entities.front())->component)
		return false;
	Checkpoint("Rendre unique");
	return m_doc.MakeUnique(m_entities.front());
}

Result<EntityId, String> Editor::ImportModel(const String &path, const math::FVector3 &at, float scale) {
	std::vector<modeling::Material> palette = m_doc.Materials();
	Result<modeling::Model, String> model = Err(String());
	if (std::string_view(path).ends_with(".model")) { // modèle éditable du sandbox : palette rapprochée par nom
		auto loaded = modeling::LoadModel(path);
		if (loaded.IsError())
			return Err(loaded.Error());
		modeling::Model merged;
		merged.Materials() = palette;
		(void)merged.Append(loaded.Value());
		palette = merged.Materials();
		model = Ok(std::move(merged));
	} else {
		model = modeling::ImportGltf(path, palette);
	}
	if (model.IsError())
		return Err(model.Error());
	const math::FAABB box = model.Value().Bounds();
	CloseAllGroups();
	Checkpoint("Importer");
	for (size_t i = m_doc.Materials().size(); i < palette.size(); ++i)
		(void)m_doc.AddMaterial(palette[i].name, palette[i].color); // mêmes index que `palette`
	// Posé au sol : base du modèle à la hauteur de `at`, centré dessus, à l'échelle demandée.
	const math::FVector3 base{(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f};
	const math::FMatrix4 place = math::FMatrix4::Translation(at) * math::FMatrix4::Scale(math::FVector3(std::max(scale, 1e-4f))) *
								 math::FMatrix4::Translation(-base);
	const std::string stem = std::filesystem::path(path.c_str()).stem().string();
	const EntityId id = m_doc.AddEntity(m_doc.UniqueName(stem.empty() ? std::string_view("Modèle") : std::string_view(stem)), std::move(model.Value()),
										place)
							.id;
	ClearSelection();
	SelectEntity(id);
	SetStatus(String::Format("Importé : {}", stem));
	return Ok(id);
}

bool Editor::SyncInstances() {
	if (m_context == kRootId)
		return false;
	const Entity *e = m_doc.Find(m_context);
	if (!e || !e->component || e->model.Revision() == m_syncedRevision)
		return false;
	m_syncedRevision = e->model.Revision();
	return m_doc.SyncInstances(m_context);
}

void Editor::PaintSelection(uint32_t materialIndex) {
	const InstanceSync sync{*this};
	material = materialIndex;
	if (m_selection.faces.empty() && m_entities.empty())
		return;
	const Document before = m_doc;
	for (modeling::FaceId f : m_selection.faces)
		modeling::Paint(ActiveModel(), f, materialIndex);
	ActiveModel().Touch();
	for (EntityId id : m_entities)
		if (Entity *e = m_doc.Find(id)) {
			for (size_t f = 0; f < e->model.Faces().size(); ++f)
				if (e->model.IsAlive(modeling::FaceId(f)))
					modeling::Paint(e->model, modeling::FaceId(f), materialIndex);
			e->model.Touch();
		}
	Commit(before, String("Peindre"));
}

bool Editor::Undo() {
	m_amendable = false;
	m_lastGroupMove = NONE;
	if (!m_history.CanUndo())
		return false;
	const String label = m_history.UndoLabel();
	m_tool->Reset();
	m_history.Undo(m_doc);
	m_selection.Prune(ActiveModel());
	std::erase_if(m_entities, [this](EntityId id) { return !m_doc.Find(id); });
	m_modified = true;
	SetStatus(String::Format("Annulé : {}", label));
	return true;
}

bool Editor::Redo() {
	m_amendable = false;
	m_lastGroupMove = NONE;
	if (!m_history.CanRedo())
		return false;
	const String label = m_history.RedoLabel();
	m_tool->Reset();
	m_history.Redo(m_doc);
	m_selection.Prune(ActiveModel());
	std::erase_if(m_entities, [this](EntityId id) { return !m_doc.Find(id); });
	m_modified = true;
	SetStatus(String::Format("Rétabli : {}", label));
	return true;
}

void Editor::Checkpoint(String label) { Commit(m_doc, std::move(label)); }

void Editor::ClearHistory() { m_history.Clear(); }

} // namespace sandbox
