// Tests du document et de l'éditeur du sandbox, sans fenêtre : la souris est simulée par des
// rayons de la caméra de navigation (NavCamera), comme le fait l'interface.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>

#include <sandbox/document/document.hpp>
#include <sandbox/document/project.hpp>
#include <sandbox/editor/editor.hpp>
#include <sandbox/modeling/io.hpp>
#include <sandbox/modeling/operations.hpp>

using namespace sandbox;

namespace {

constexpr float kW = 1000.f, kH = 700.f;

/// Pointeur du monde visant `world` (projeté puis relancé depuis l'écran).
Pointer At(const Editor &ed, const math::FVector3 &world, bool shift = false) {
	const sdl3::Camera3D &cam = ed.camera.Camera();
	const math::FMatrix4 vp = cam.ViewProjection(kW / kH);
	const auto project = [vp](const math::FVector3 &p) -> Option<math::FVector2> {
		const math::FVector4 h = vp * math::FVector4(p, 1.f);
		if (h.w <= 1e-6f)
			return NONE;
		return Some(math::FVector2{(h.x / h.w + 1.f) * 0.5f * kW, (1.f - h.y / h.w) * 0.5f * kH});
	};
	const math::FVector2 px = project(world).Unwrap();
	Pointer p;
	p.cursor = px;
	p.ray = cam.ScreenRay(px.x / kW, px.y / kH, kW / kH);
	p.project = project;
	p.shift = shift;
	return p;
}

void ClickAt(Editor &ed, const math::FVector3 &world, bool shift = false) {
	const Pointer p = At(ed, world, shift);
	ed.Hover(p);
	ed.Click(p);
}

size_t AliveFaces(const modeling::Model &m) { return m.FaceCount(); }

} // namespace

TEST(Document, GroupExplodeDuplicateAndBounds) {
	Document doc;
	modeling::Model &root = doc.Root();
	const auto f = root.AddRectangle({1.f, 0.f, 1.f}, {2.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
	REQUIRE(f.IsSome());
	(void)modeling::PushPull(root, *f, root.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	CHECK_EQ(AliveFaces(root), 6u);
	modeling::Selection all;
	for (size_t i = 0; i < root.Faces().size(); ++i)
		if (root.IsAlive(modeling::FaceId(i)))
			all.faces.push_back(modeling::FaceId(i));
	const EntityId id = doc.MakeGroup(all);
	REQUIRE(id != 0);
	CHECK_EQ(AliveFaces(root), 0u); // retirée de la géométrie libre
	const Entity *g = doc.Find(id);
	REQUIRE(g != nullptr);
	CHECK(g->name == "Groupe");
	CHECK_NEAR(g->transform.m[3][0], 1.f, 1e-5f); // origine au coin bas de la boîte
	const math::FAABB box = doc.WorldBounds(*g);
	CHECK_NEAR(box.min.x, 1.f, 1e-5f);
	CHECK_NEAR(box.max.x, 3.f, 1e-5f);
	CHECK_NEAR(box.max.y, 1.f, 1e-5f);
	const EntityId copy = doc.Duplicate(id, {5.f, 0.f, 0.f});
	REQUIRE(copy != 0);
	CHECK(doc.Find(copy)->name == "Groupe 2");
	CHECK_NEAR(doc.Bounds().max.x, 8.f, 1e-4f);
	CHECK(doc.Explode(id));
	CHECK_EQ(AliveFaces(doc.Root()), 6u); // revenue dans la racine, au même endroit
	CHECK_NEAR(doc.Root().Bounds().min.x, 1.f, 1e-4f);
	CHECK(doc.Find(id) == nullptr);
}

TEST(Document, JsonRoundTripAndHistory) {
	Document doc;
	(void)doc.Root().AddRectangle({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {.material = 3});
	modeling::Model m;
	(void)m.AddRectangle({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 2.f, 0.f});
	Entity &e = doc.AddEntity("Mur", std::move(m), math::FMatrix4::Translation({4.f, 0.f, 0.f}));
	e.tag = 1;
	doc.Tags().push_back({"Murs", false});
	const uint32_t gold = doc.AddMaterial("Or", {0.9f, 0.75f, 0.2f, 1.f});
	doc.Views().push_back({"Plan", {0.f, 20.f, 0.f}, {}, 30.f, true, 12.f});
	const auto back = Document::FromJson(doc.ToJson());
	if (back.IsError())
		std::fprintf(stderr, "%s\n", back.Error().c_str());
	REQUIRE(back.IsOk());
	const Document &d = back.Value();
	CHECK_EQ(d.Root().FaceCount(), 1u);
	REQUIRE(d.Entities().size() == 1);
	CHECK(d.Entities()[0].name == "Mur");
	CHECK_NEAR(d.Entities()[0].transform.m[3][0], 4.f, 1e-6f);
	CHECK(!d.IsVisible(d.Entities()[0])); // balise masquée
	CHECK(d.Materials()[gold].name == "Or");
	REQUIRE(d.Views().size() == 1);
	CHECK(d.Views()[0].orthographic);
	CHECK(Document::FromJson("{\"format\":\"autre\"}").IsError());

	History h;
	Document live = d;
	h.Push(live, "Effacer");
	const uint64_t before = live.Revision();
	live.RemoveEntity(live.Entities()[0].id);
	CHECK(h.Undo(live));
	CHECK_EQ(live.Entities().size(), 1u);
	CHECK(live.Revision() != before); // le cache d'affichage voit la restauration
	CHECK(h.Redo(live));
	CHECK(live.Entities().empty());

	const std::string path = (std::filesystem::temp_directory_path() / "sandbox-test.sandbox").string();
	CHECK(d.Save(String(path)).IsNone());
	const auto loaded = Document::Load(String(path));
	REQUIRE(loaded.IsOk());
	CHECK_EQ(loaded.Value().Entities().size(), 1u);
	std::filesystem::remove(path);
}

TEST(Document, HistoryLabelsAndClear) {
	History h;
	Document live;
	h.Push(live, "Première");
	h.Push(live, "Deuxième");
	h.Push(live, "Troisième");
	// UndoLabels : de la plus récente à la plus ancienne.
	auto labels = h.UndoLabels();
	REQUIRE(labels.size() == 3u);
	CHECK(labels[0] == "Troisième");
	CHECK(labels[1] == "Deuxième");
	CHECK(labels[2] == "Première");
	CHECK_EQ(h.UndoCount(), 3u);
	CHECK_EQ(h.RedoCount(), 0u);
	// Undo : la dernière commande passe dans « rétablir ».
	CHECK(h.Undo(live));
	REQUIRE(h.RedoLabels().size() == 1u);
	CHECK(h.RedoLabels()[0] == "Troisième");
	CHECK_EQ(h.UndoCount(), 2u);
	// Clear vide les deux piles.
	h.Clear();
	CHECK_EQ(h.UndoCount(), 0u);
	CHECK_EQ(h.RedoCount(), 0u);
	CHECK(h.UndoLabels().empty());
	CHECK(h.RedoLabels().empty());
}

TEST(Editor, DrawRectanglePushPullUndoRedo) {
	Editor ed;
	ed.camera.Restore({"", {6.f, 7.f, 9.f}, {1.f, 0.f, 1.f}, 35.f, false, 10.f});
	ed.SetTool(modeling::ToolKind::RECTANGLE);
	ClickAt(ed, {0.f, 0.f, 0.f});
	CHECK(ed.ToolBusy());
	ClickAt(ed, {2.f, 0.f, 1.f});
	REQUIRE_EQ(ed.Doc().Root().FaceCount(), 1u);
	CHECK(ed.Modified());
	CHECK(ed.Undos().CanUndo());
	CHECK(ed.Undos().UndoLabel() == "Rectangle");
	// Pousser/Tirer de la cote tapée au clavier (boîte de mesures).
	ed.SetTool(modeling::ToolKind::PUSH_PULL);
	ClickAt(ed, {1.f, 0.f, 0.5f});
	CHECK(ed.TypeText("1"));
	CHECK(ed.TypeText(",5"));
	CHECK(ed.MeasureInput() == "1,5");
	ed.Backspace();
	ed.Backspace();
	CHECK(ed.TypeText(",5"));
	ed.SubmitMeasure();
	CHECK_EQ(ed.Doc().Root().FaceCount(), 6u);
	CHECK_NEAR(std::abs(ed.Doc().Root().Bounds().max.y - ed.Doc().Root().Bounds().min.y), 1.5f, 1e-3f);
	CHECK(ed.Undo());
	CHECK_EQ(ed.Doc().Root().FaceCount(), 1u);
	CHECK(ed.Redo());
	CHECK_EQ(ed.Doc().Root().FaceCount(), 6u);
	// Sans outil de dessin, les chiffres ne vont pas dans la boîte de mesures.
	ed.SetTool(modeling::ToolKind::SELECT);
	CHECK(!ed.TypeText("3"));
}

TEST(Editor, GroupSelectMoveAndOpen) {
	Editor ed;
	ed.camera.Restore({"", {6.f, 8.f, 10.f}, {1.f, 0.f, 1.f}, 35.f, false, 10.f});
	modeling::Model &root = ed.Doc().Root();
	const auto f = root.AddRectangle({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 0.f, 2.f});
	(void)modeling::PushPull(root, *f, root.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	ed.SelectAll();
	CHECK(ed.GroupSelection());
	REQUIRE_EQ(ed.SelectedEntities().size(), 1u);
	const EntityId id = ed.SelectedEntities()[0];
	CHECK_EQ(ed.Doc().Root().FaceCount(), 0u);
	// Un clic dans le vide désélectionne, un clic sur le groupe le sélectionne d'un bloc.
	ed.SetTool(modeling::ToolKind::SELECT);
	ClickAt(ed, {6.f, 0.f, -4.f});
	CHECK(ed.SelectedEntities().empty());
	ClickAt(ed, {1.f, 1.f, 1.f});
	REQUIRE_EQ(ed.SelectedEntities().size(), 1u);
	CHECK_EQ(ed.SelectedEntities()[0], id);
	// Déplacer le groupe : point de départ sur le sol, distance tapée le long de la direction.
	ed.SetTool(modeling::ToolKind::MOVE);
	ClickAt(ed, {3.f, 0.f, 3.f});
	ed.Hover(At(ed, {4.f, 0.f, 3.f}));
	REQUIRE(ed.EntityMoveOffset().IsSome());
	CHECK(ed.TypeText("2"));
	ed.SubmitMeasure();
	CHECK_NEAR(ed.Doc().Find(id)->transform.m[3][0], 2.f, 1e-3f);
	CHECK(ed.Undos().UndoLabel() == "Déplacer le groupe");
	// Ouvrir le groupe : les outils travaillent dans son repère.
	CHECK(ed.OpenGroup(id));
	CHECK_EQ(ed.Context(), id);
	CHECK_EQ(ed.ActiveModel().FaceCount(), 6u);
	CHECK_NEAR(ed.ActiveTransform().m[3][0], 2.f, 1e-3f);
	ed.SetTool(modeling::ToolKind::SELECT);
	ed.Escape(); // rien en cours, rien de sélectionné : on sort du groupe
	CHECK_EQ(ed.Context(), kRootId);
	CHECK(ed.ExplodeSelection());
	CHECK_EQ(ed.Doc().Root().FaceCount(), 6u);
	CHECK_NEAR(ed.Doc().Root().Bounds().min.x, 2.f, 1e-3f);
}

TEST(Editor, CameraNavigation) {
	NavCamera cam;
	const math::FAABB box{{-1.f, 0.f, -1.f}, {1.f, 2.f, 1.f}};
	cam.SetView(StandardView::Top, box, 1.5f);
	CHECK(cam.Camera().position.y > 2.f);
	CHECK_NEAR(cam.Target().y, 1.f, 1e-4f);
	const float d0 = cam.Camera().position.Distance(cam.Target());
	cam.ZoomAt(2.f, {1.f, 0.f, 1.f});
	CHECK(cam.Camera().position.Distance(cam.Target()) < d0);
	CHECK(cam.Target().x > 0.f); // la cible glisse vers le point visé
	cam.SetOrthographic(true);
	const math::FRay r = cam.Camera().ScreenRay(0.25f, 0.5f, 1.5f);
	const math::FRay c = cam.Camera().ScreenRay(0.5f, 0.5f, 1.5f);
	CHECK(r.dir.Distance(c.dir) < 1e-5f); // projection parallèle : rayons parallèles
	CHECK(r.origin.Distance(c.origin) > 0.1f);
	const SceneView saved = cam.Save("Vue");
	cam.Orbit(200.f, 0.f);
	cam.Restore(saved);
	CHECK(cam.Camera().position.Distance(saved.eye) < 1e-3f);
}

TEST(Editor, SampleProjectIsConsistent) {
	// Projet externe (tests lancés depuis la racine du dépôt) : index, scène, scripts en classes.
	auto project = sandbox::Project::Load("apps/sandbox/projects/maison/maison.project");
	REQUIRE(project.IsOk());
	CHECK(project.Value().name == "maison");
	const auto scene = project.Value().ActiveScene();
	REQUIRE(scene.has_value());
	CHECK(scene->filename() == "maison.scene");
	REQUIRE(project.Value().ScriptFile("interactions").has_value());
	CHECK(!project.Value().ScriptFile("inexistant").has_value());
	CHECK(sandbox::Project::Load("apps/sandbox/projects/maison/scenes/maison.scene").IsError()); // pas un index
	auto loaded = Document::Load(String(scene->string()));
	REQUIRE(loaded.IsOk());
	Editor ed;
	ed.Reset(std::move(loaded.Value()));
	CHECK(ed.Doc().Root().FaceCount() >= 6u);
	CHECK_EQ(ed.Doc().Entities().size(), 11u); // toit, terrain, table, arbre, 3 caisses, lanterne, radio, paillasson, porte
	CHECK(!ed.Modified());
	CHECK(!ed.Undos().CanUndo());
	const math::FAABB box = ed.Doc().Bounds();
	CHECK(box.max.y > 4.f); // faîtage du toit
	const Entity *door = nullptr;
	for (const Entity &e : ed.Doc().Entities())
		if (e.name == "Porte")
			door = &e;
	REQUIRE(door != nullptr);
	CHECK(door->behavior.scriptFile == "../scripts/porte.script"); // relatif à la scène
	CHECK(door->behavior.script.find("class Porte extends Behaviour") != std::string::npos); // lu dans son fichier
	CHECK(ed.Doc().Game().script.find("extends Scene") != std::string::npos);
	// Le JSON ne garde que la référence ; « Enregistrer sous » copie les scripts à côté.
	CHECK(ed.Doc().ToJson().find("this.turn") == std::string::npos);
	CHECK(ed.Doc().ToJson().find("\"format\": \"sandbox.scene\"") != std::string::npos);
	const std::filesystem::path copy = std::filesystem::temp_directory_path() / "sandbox-test-projet";
	std::filesystem::remove_all(copy);
	REQUIRE(!ed.Doc().Save(String((copy / "scenes" / "copie.scene").string())));
	CHECK(std::filesystem::exists(copy / "scripts" / "porte.script"));
	CHECK(std::filesystem::exists(copy / "scripts" / "scene.script"));
	auto back = Document::Load(String((copy / "scenes" / "copie.scene").string()));
	REQUIRE(back.IsOk());
	CHECK_EQ(back.Value().Entities().size(), 11u);
	CHECK(back.Value().Entities()[7].behavior.light); // comportements enregistrés
	CHECK(back.Value().Entities()[10].behavior.script == door->behavior.script);
	// Fichier manquant : signalé dans le script, recréé à l'enregistrement suivant.
	std::filesystem::remove(copy / "scripts" / "radio.script");
	auto missing = Document::Load(String((copy / "scenes" / "copie.scene").string()));
	REQUIRE(missing.IsOk());
	CHECK(missing.Value().Entities()[8].behavior.script.starts_with("# Fichier introuvable"));
	std::filesystem::remove_all(copy);
}

TEST(Editor, TypedDimensionsAndPartialFields) {
	const std::vector<float> partial = modeling::ParseMeasurement("2;");
	REQUIRE(partial.size() == 2);
	CHECK_NEAR(partial[0], 2.f, 1e-6f);
	CHECK(std::isnan(partial[1]));
	const std::vector<float> other = modeling::ParseMeasurement(";150cm");
	REQUIRE(other.size() == 2);
	CHECK(std::isnan(other[0]));
	CHECK_NEAR(other[1], 1.5f, 1e-6f);
	CHECK(modeling::ParseMeasurement(";").empty());
	CHECK(modeling::ParseMeasurement("2;abc").empty());

	Editor ed;
	ed.camera.Restore({"", {6.f, 7.f, 9.f}, {1.f, 0.f, 1.f}, 35.f, false, 10.f});
	ed.SetTool(modeling::ToolKind::RECTANGLE);
	ClickAt(ed, {0.f, 0.f, 0.f});
	ed.Hover(At(ed, {2.f, 0.f, 1.f})); // la souris donne 2 × 1
	CHECK(ed.TypeText(";3"));			// largeur libre (souris), profondeur imposée
	ed.SubmitMeasure();
	REQUIRE_EQ(ed.Doc().Root().FaceCount(), 1u);
	const math::FAABB box = ed.Doc().Root().Bounds();
	CHECK_NEAR(box.max.x - box.min.x, 2.f, 0.05f);
	CHECK_NEAR(box.max.z - box.min.z, 3.f, 1e-4f);
}

TEST(Editor, AmendLastOperationWithTypedValue) {
	Editor ed;
	ed.camera.Restore({"", {6.f, 7.f, 9.f}, {1.f, 0.f, 1.f}, 35.f, false, 10.f});
	ed.SetTool(modeling::ToolKind::RECTANGLE);
	ClickAt(ed, {0.f, 0.f, 0.f});
	ClickAt(ed, {2.f, 0.f, 1.f});
	REQUIRE_EQ(ed.Doc().Root().FaceCount(), 1u);
	CHECK(ed.CanAmend());
	CHECK(ed.Preview().measure.Contains("Rectangle")); // message de l'opération posée
	// Rectangle posé : la cote retapée le refait à 4 × 2,5, en un seul pas d'annulation.
	CHECK(ed.TypeText("4;2,5"));
	ed.SubmitMeasure();
	REQUIRE_EQ(ed.Doc().Root().FaceCount(), 1u);
	math::FAABB box = ed.Doc().Root().Bounds();
	CHECK_NEAR(box.max.x - box.min.x, 4.f, 1e-4f);
	CHECK_NEAR(box.max.z - box.min.z, 2.5f, 1e-4f);
	CHECK(ed.Status().Contains("corrigé"));
	CHECK(ed.Undo());
	CHECK_EQ(ed.Doc().Root().FaceCount(), 0u); // un seul pas : avant le rectangle
	CHECK(ed.Redo());

	// Pousser/Tirer : extrusion à la souris, puis cote retapée.
	ed.SetTool(modeling::ToolKind::PUSH_PULL);
	ClickAt(ed, {2.f, 0.f, 1.f});
	ClickAt(ed, {2.f, 0.8f, 1.f});
	REQUIRE_EQ(ed.Doc().Root().FaceCount(), 6u);
	CHECK(ed.CanAmend());
	CHECK(ed.TypeText("2,2"));
	ed.SubmitMeasure();
	CHECK_EQ(ed.Doc().Root().FaceCount(), 6u);
	box = ed.Doc().Root().Bounds();
	CHECK_NEAR(box.max.y - box.min.y, 2.2f, 1e-3f);
	CHECK(ed.TypeText("1")); // la correction reste ouverte : on peut retaper
	ed.SubmitMeasure();
	box = ed.Doc().Root().Bounds();
	CHECK_NEAR(box.max.y - box.min.y, 1.f, 1e-3f);
	// Un nouveau clic ferme la correction.
	ClickAt(ed, {8.f, 0.f, -3.f});
	CHECK(!ed.CanAmend());
}

TEST(Editor, AmendGroupMoveAndExactDimensions) {
	Editor ed;
	ed.camera.Restore({"", {6.f, 8.f, 10.f}, {1.f, 0.f, 1.f}, 35.f, false, 10.f});
	modeling::Model &root = ed.Doc().Root();
	const auto f = root.AddRectangle({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
	(void)modeling::PushPull(root, *f, root.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	ed.SelectAll();
	// Dimensions de la sélection : 2 × 1 × 1 → 3 × 1 × 1,5 depuis le coin bas.
	const Option<math::FAABB> sel = ed.SelectionBounds();
	REQUIRE(sel.IsSome());
	CHECK_NEAR(sel->max.x - sel->min.x, 2.f, 1e-4f);
	CHECK(ed.ResizeSelection({3.f, 1.f, 1.5f}, "Dimensions"));
	math::FAABB box = ed.Doc().Root().Bounds();
	CHECK_NEAR(box.max.x - box.min.x, 3.f, 1e-4f);
	CHECK_NEAR(box.max.z - box.min.z, 1.5f, 1e-4f);
	CHECK_NEAR(box.min.x, 0.f, 1e-4f);
	CHECK(ed.GroupSelection());
	const EntityId id = ed.SelectedEntities()[0];
	// Placement exact du groupe : position, rotation, dimensions.
	Editor::Placement p = ed.EntityPlacement(id).Unwrap();
	CHECK_NEAR(p.size.x, 3.f, 1e-4f);
	CHECK_NEAR(p.size.y, 1.f, 1e-4f);
	p.position = {5.f, 0.f, 2.f};
	p.size = {6.f, 2.f, 1.5f};
	p.yawDegrees = 90.f;
	CHECK(ed.SetEntityPlacement(id, p, "Dimensions du groupe"));
	const Editor::Placement q = ed.EntityPlacement(id).Unwrap();
	CHECK_NEAR(q.size.x, 6.f, 1e-3f);
	CHECK_NEAR(q.size.y, 2.f, 1e-3f);
	CHECK_NEAR(q.yawDegrees, 90.f, 1e-2f);
	CHECK_NEAR(q.position.x, 5.f, 1e-4f);
	box = ed.Doc().WorldBounds(*ed.Doc().Find(id));
	CHECK_NEAR(box.max.z - box.min.z, 6.f, 1e-3f); // quart de tour : la largeur part le long de Z
	// Glissé d'un champ : un seul pas d'annulation pour toute la suite.
	p = q;
	p.size.y = 2.5f;
	CHECK(ed.SetEntityPlacement(id, p, "Dimensions du groupe", true));
	p.size.y = 3.f;
	CHECK(ed.SetEntityPlacement(id, p, "Dimensions du groupe", true));
	CHECK(ed.Undo());
	CHECK_NEAR(ed.EntityPlacement(id).Unwrap().size.y, 1.f, 1e-3f); // retour avant le premier changement de dimensions
	CHECK(ed.Redo());
	// Déplacement d'un groupe à la souris, puis distance retapée.
	ed.SetTool(modeling::ToolKind::MOVE);
	ClickAt(ed, {0.f, 0.f, 5.f});
	ClickAt(ed, {1.f, 0.f, 5.f});
	const float x1 = ed.EntityPlacement(id).Unwrap().position.x;
	CHECK_NEAR(x1, 6.f, 0.1f);
	CHECK(ed.CanAmend());
	CHECK(ed.TypeText("3"));
	ed.SubmitMeasure();
	CHECK_NEAR(ed.EntityPlacement(id).Unwrap().position.x, 8.f, 1e-3f); // 5 + 3 le long de la même direction
	CHECK(ed.Undo());
	CHECK_NEAR(ed.EntityPlacement(id).Unwrap().position.x, 5.f, 1e-3f);
}

TEST(Editor, ComponentInstancesShareGeometry) {
	Document doc;
	modeling::Model m;
	if (auto f = m.AddRectangle({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}))
		(void)modeling::PushPull(m, *f, m.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	const EntityId chair = doc.AddEntity("Chaise", std::move(m)).id;
	Editor ed;
	ed.Reset(std::move(doc));
	ed.SelectEntity(chair);
	CHECK(ed.MakeComponent());
	CHECK(!ed.MakeComponent()); // déjà un composant
	const ComponentId component = ed.Doc().Find(chair)->component;
	CHECK(component != 0);
	REQUIRE(ed.DuplicateSelection()); // la copie d'une instance est une instance
	const EntityId copy = ed.SelectedEntities().front();
	CHECK_EQ(ed.Doc().Find(copy)->component, component);
	CHECK_EQ(ed.Doc().InstanceCount(component), 2u);
	// Édition d'une instance (groupe ouvert) : l'autre suit, sa transformation reste la sienne.
	const math::FMatrix4 copyPlace = ed.Doc().Find(copy)->transform;
	REQUIRE(ed.OpenGroup(chair));
	ed.SelectAll();
	REQUIRE(ed.ResizeSelection({2.f, 1.f, 1.f}, "Dimensions"));
	CHECK_NEAR(ed.Doc().Find(copy)->model.Bounds().max.x, 2.f, 1e-4f);
	CHECK_NEAR(ed.Doc().Find(copy)->transform.m[3][0], copyPlace.m[3][0], 1e-6f);
	ed.CloseGroup();
	// Fichier : la géométrie du composant n'est écrite qu'une fois.
	const String json = ed.Doc().ToJson();
	auto back = Document::FromJson(json);
	REQUIRE(back.IsOk());
	CHECK_EQ(back.Value().InstanceCount(component), 2u);
	CHECK_NEAR(back.Value().Find(back.Value().Entities()[1].id)->model.Bounds().max.x, 2.f, 1e-4f);
	REQUIRE(back.Value().FindComponent(component) != nullptr);
	CHECK(back.Value().FindComponent(component)->name == "Chaise");
	size_t models = 0;
	for (size_t at = json.find("\"model\""); at != std::string::npos; at = json.find("\"model\"", at + 1))
		++models;
	CHECK_EQ(models, 1u);
	// Rendue unique, la copie n'est plus modifiée par l'autre instance.
	ed.SelectEntity(copy);
	CHECK(ed.MakeUnique());
	CHECK_EQ(ed.Doc().InstanceCount(component), 1u);
	REQUIRE(ed.OpenGroup(chair));
	ed.SelectAll();
	REQUIRE(ed.ResizeSelection({3.f, 1.f, 1.f}, "Dimensions"));
	ed.CloseGroup();
	CHECK_NEAR(ed.Doc().Find(copy)->model.Bounds().max.x, 2.f, 1e-4f);
	// Annuler revient à deux instances.
	REQUIRE(ed.Undo());
	REQUIRE(ed.Undo());
	CHECK_EQ(ed.Doc().InstanceCount(component), 2u);
}

TEST(Editor, ImportGltfModelFromAssets) {
	// Modèle du dépôt (tests lancés depuis sa racine) : hiérarchie de 30 nœuds, 3 matériaux.
	Editor ed;
	const size_t before = ed.Doc().Materials().size();
	auto id = ed.ImportModel("assets/models/animals/Cow.gltf", {4.f, 0.f, -2.f});
	REQUIRE(id.IsOk());
	const Entity *cow = ed.Doc().Find(id.Value());
	REQUIRE(cow != nullptr);
	CHECK(cow->name == "Cow");
	CHECK(cow->model.FaceCount() > 50u);
	CHECK_EQ(ed.Doc().Materials().size(), before + 3u); // Black, White, Pink
	const math::FAABB box = ed.Doc().WorldBounds(*cow);
	CHECK_NEAR(box.min.y, 0.f, 1e-3f); // posé au sol
	CHECK_NEAR((box.min.x + box.max.x) * 0.5f, 4.f, 1e-3f);
	CHECK_NEAR((box.min.z + box.max.z) * 0.5f, -2.f, 1e-3f);
	CHECK(ed.SelectedEntities() == std::vector<EntityId>{id.Value()});
	REQUIRE(ed.Undo()); // un seul pas
	CHECK(ed.Doc().Find(id.Value()) == nullptr);
	CHECK_EQ(ed.Doc().Materials().size(), before);
	CHECK(ed.ImportModel("absent.gltf", {}).IsError());
}

TEST(Overlay, ItemsPickFitColorsAndJson) {
	Layer2D layer;
	Item2D &panel = layer.Add(Shape2D::Rect, {100.f, 100.f}); // 200 × 60
	const uint32_t panelId = panel.id;
	const uint32_t labelId = layer.Add(Shape2D::Text, {110.f, 110.f}).id;
	const uint32_t ringId = layer.Add(Shape2D::Circle, {600.f, 300.f}).id; // diamètre 80
	const uint32_t lineId = layer.Add(Shape2D::Line, {0.f, 700.f}).id;	  // jusqu'à (200, 700)
	CHECK(layer.Find(labelId)->name == "Texte");
	CHECK(layer.Add(Shape2D::Text, {0.f, 0.f}).name == "Texte 2"); // noms uniques
	// Le texte, ajouté après le panneau, est devant lui ; un z plus fort passe devant tout.
	CHECK_EQ(Pick(layer, {115.f, 115.f}), labelId);
	CHECK_EQ(Pick(layer, {290.f, 150.f}), panelId);
	layer.Find(panelId)->z = 5;
	CHECK_EQ(Pick(layer, {115.f, 115.f}), panelId);
	CHECK(layer.DrawOrder().back()->id == panelId);
	CHECK_EQ(Pick(layer, {630.f, 300.f}), ringId);
	CHECK_EQ(Pick(layer, {660.f, 300.f}), 0u);
	CHECK_EQ(Pick(layer, {100.f, 703.f}), lineId);
	layer.Find(ringId)->visible = false;
	CHECK_EQ(Pick(layer, {600.f, 300.f}), 0u); // masqué : pas sélectionnable
	// Écran de référence dans une vue 1000 × 1000 : échelle 1000/1280, centré verticalement.
	const OverlayView view = OverlayView::Fit(0.f, 0.f, 1000.f, 1000.f, layer.reference);
	CHECK_NEAR(view.scale, 1000.f / 1280.f, 1e-6f);
	CHECK_NEAR(view.ToScreen({640.f, 360.f}).y, 500.f, 1e-3f);
	CHECK_NEAR(view.ToReference(view.ToScreen({12.f, 34.f})).y, 34.f, 1e-3f);
	// Couleurs.
	REQUIRE(ParseColor("#ff8000").has_value());
	CHECK_NEAR(ParseColor("#ff8000")->g, 128.f / 255.f, 1e-6f);
	CHECK(!ParseColor("#0008").has_value()); // #rgb, #rrggbb ou #rrggbbaa seulement
	CHECK(!ParseColor("#12").has_value());
	CHECK(!ParseColor("#gg0000").has_value());
	CHECK(FormatColor({1.f, 0.f, 0.f, 0.5f}) == "#ff000080");
	CHECK_NEAR(ParseColor("#f00")->r, 1.f, 1e-6f);
	// Document : enregistré et relu à l'identique.
	Document doc;
	doc.Overlay() = layer;
	doc.Overlay().Find(labelId)->text = "Bonjour é";
	auto back = Document::FromJson(doc.ToJson());
	REQUIRE(back.IsOk());
	CHECK(back.Value().Overlay().items.size() == layer.items.size());
	CHECK(back.Value().Overlay().Find(labelId)->text == "Bonjour é");
	CHECK_EQ(back.Value().Overlay().Find(panelId)->z, 5);
	CHECK(!back.Value().Overlay().Find(ringId)->visible);
	CHECK(back.Value().Overlay().Find(lineId)->shape == Shape2D::Line);
	CHECK(layer.Remove(lineId));
	CHECK(layer.Find(lineId) == nullptr);
}

TEST(Document, TilemapLayerJsonRoundTrip) {
	TilemapLayer tm;
	tm.origin = {64.f, 32.f};
	tm.tileSize = 16.f;
	tm.width = 4;
	tm.height = 3;
	tm.terrain = {int8_t(0), int8_t(1), int8_t(1), int8_t(2), int8_t(1), int8_t(1), int8_t(2), int8_t(2), int8_t(3), int8_t(0), int8_t(0), int8_t(3)};
	tm.palette = {{0.f, 0.f, 0.f, 0.f}, {0.36f, 0.72f, 0.32f, 1.f}, {0.62f, 0.45f, 0.28f, 1.f}, {0.25f, 0.45f, 0.75f, 1.f}};
	Document doc;
	doc.Overlay().tilemap = tm;
	const auto back = Document::FromJson(doc.ToJson());
	REQUIRE(back.IsOk());
	const TilemapLayer &r = back.Value().Overlay().tilemap;
	CHECK(r.width == 4);
	CHECK(r.height == 3);
	CHECK_NEAR(r.origin.x, 64.f, 1e-6f);
	CHECK_NEAR(r.origin.y, 32.f, 1e-6f);
	CHECK_NEAR(r.tileSize, 16.f, 1e-6f);
	CHECK(r.terrain == tm.terrain);
	REQUIRE(r.palette.size() == tm.palette.size());
	constexpr float kStep = 1.f / 255.f; // les couleurs sont quantifiées sur 8 bits
	for (size_t i = 0; i < tm.palette.size(); ++i) {
		CHECK_NEAR(r.palette[i].r, tm.palette[i].r, kStep);
		CHECK_NEAR(r.palette[i].g, tm.palette[i].g, kStep);
		CHECK_NEAR(r.palette[i].b, tm.palette[i].b, kStep);
		CHECK_NEAR(r.palette[i].a, tm.palette[i].a, kStep);
	}
	// Un calque vide n'écrit aucun nœud « tilemap ».
	Document empty;
	CHECK(empty.ToJson().find("\"tilemap\"") == std::string::npos);
}

TEST(Document, MaterialKindsRoundTrip) {
	Document doc;
	const uint32_t steel = doc.AddMaterial("Acier", {0.7f, 0.7f, 0.75f, 1.f});
	const uint32_t neon = doc.AddMaterial("Néon", {0.2f, 0.9f, 1.f, 1.f});
	modeling::Material m = doc.Materials()[steel];
	m.kind = modeling::MaterialKind::Metal;
	m.roughness = 0.2f;
	doc.SetMaterial(steel, m);
	m = doc.Materials()[neon];
	m.kind = modeling::MaterialKind::Emissive;
	m.strength = 9.f;
	doc.SetMaterial(neon, m);
	CHECK(doc.Root().Materials()[steel].kind == modeling::MaterialKind::Metal); // palette recopiée dans les modèles
	auto back = Document::FromJson(doc.ToJson());
	REQUIRE(back.IsOk());
	CHECK(back.Value().Materials()[steel].kind == modeling::MaterialKind::Metal);
	CHECK_NEAR(back.Value().Materials()[steel].roughness, 0.2f, 1e-6f);
	CHECK(back.Value().Materials()[neon].kind == modeling::MaterialKind::Emissive);
	CHECK_NEAR(back.Value().Materials()[neon].strength, 9.f, 1e-6f);
	// Fichier de modèle seul et Append : le rendu suit la couleur.
	auto model = modeling::Deserialize(modeling::Serialize(back.Value().Root()));
	REQUIRE(model.IsOk());
	CHECK(model.Value().Materials()[neon].kind == modeling::MaterialKind::Emissive);
	modeling::Model target;
	(void)target.Append(model.Value());
	const auto it = std::ranges::find(target.Materials(), String("Acier"), &modeling::Material::name);
	REQUIRE(it != target.Materials().end());
	CHECK(it->kind == modeling::MaterialKind::Metal);
	CHECK(modeling::MaterialKindFromName("inconnu") == modeling::MaterialKind::Plastic);
}

TEST(Document, ProjectIndexAndObjects) {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "sandbox-test-objets";
	fs::remove_all(dir);
	// Index : écrit puis relu (format « sandbox.project »).
	sandbox::Project p = sandbox::Project::Create(dir / "essai.project", "essai");
	p.activeScene = "salle";
	sandbox::Project::AddUnique(p.scenes, "scenes/salle.scene");
	sandbox::Project::AddUnique(p.scenes, "scenes/salle.scene"); // sans doublon
	p.scripts.push_back({"outils", "module commun", "scripts/outils.script"});
	REQUIRE(!p.Save());
	auto back = sandbox::Project::Load(dir / "essai.project");
	REQUIRE(back.IsOk());
	CHECK_EQ(back.Value().scenes.size(), 1u);
	CHECK(back.Value().ActiveScene()->filename() == "salle.scene");
	CHECK(back.Value().ScriptFile("outils")->filename() == "outils.script");
	CHECK(back.Value().Relative(dir / "objects" / "a.object") == "objects/a.object");
	// Objet : un groupe enregistré (géométrie, matériau, comportement, script inclus) puis inséré
	// dans une autre scène dont la palette diffère.
	Document source;
	const uint32_t steel = source.AddMaterial("Acier", {0.6f, 0.6f, 0.65f, 1.f});
	modeling::Model box;
	if (auto f = box.AddRectangle({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {.material = steel}))
		(void)modeling::PushPull(box, *f, box.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	Entity &chest = source.AddEntity("Coffre", std::move(box));
	chest.behavior.script = "class Coffre extends Behaviour { fn OnUse() { this.hide() } }\n";
	chest.behavior.scriptFile = "../scripts/coffre.script";
	REQUIRE(!source.SaveObject(chest.id, dir / "objects" / "coffre.object", "un coffre"));
	Document target;
	const size_t palette = target.Materials().size();
	auto id = target.InsertObject(dir / "objects" / "coffre.object", {5.f, 0.f, 2.f});
	REQUIRE(id.IsOk());
	const Entity *inserted = target.Find(id.Value());
	REQUIRE(inserted != nullptr);
	CHECK(inserted->name == "Coffre");
	CHECK(inserted->behavior.script.find("class Coffre") != std::string::npos); // script inclus
	CHECK(inserted->behavior.scriptFile.IsEmpty());
	CHECK_EQ(target.Materials().size(), palette + 1u); // « Acier » ajouté
	const auto acier = std::ranges::find(target.Materials(), String("Acier"), &modeling::Material::name);
	REQUIRE(acier != target.Materials().end());
	for (size_t f = 0; f < inserted->model.Faces().size(); ++f)
		if (inserted->model.IsAlive(modeling::FaceId(f)))
			CHECK_EQ(inserted->model.Faces()[f].material, uint32_t(acier - target.Materials().begin()));
	const math::FAABB placed = target.WorldBounds(*inserted);
	CHECK_NEAR(placed.min.y, 0.f, 1e-4f);
	CHECK_NEAR((placed.min.x + placed.max.x) * 0.5f, 5.f, 1e-4f);
	CHECK(target.InsertObject(dir / "absent.object", {}).IsError());
	fs::remove_all(dir);
}

TEST(Editor, NestedGroupsHierarchy) {
	const auto cube = [](float s) {
		modeling::Model m;
		if (auto f = m.AddRectangle({0.f, 0.f, 0.f}, {s, 0.f, 0.f}, {0.f, 0.f, s}))
			(void)modeling::PushPull(m, *f, m.Faces()[*f].normal.y > 0.f ? s : -s);
		return m;
	};
	Document doc;
	const EntityId table = doc.AddEntity("Table", cube(2.f), math::FMatrix4::Translation({0.f, 0.f, 0.f})).id;
	const EntityId vase = doc.AddEntity("Vase", cube(0.3f), math::FMatrix4::Translation({0.5f, 2.f, 0.5f})).id;
	const EntityId fleur = doc.AddEntity("Fleur", cube(0.1f), math::FMatrix4::Translation({0.6f, 2.3f, 0.6f})).id;
	CHECK(doc.Reparent(vase, table));
	CHECK(doc.Reparent(fleur, vase));
	CHECK(!doc.Reparent(table, fleur)); // cycle refusé
	CHECK(!doc.Reparent(vase, vase));
	CHECK(doc.Children(0) == std::vector<EntityId>{table});
	CHECK(doc.Descendants(table) == (std::vector<EntityId>{vase, fleur}));
	CHECK(doc.IsAncestor(table, fleur));
	CHECK_EQ(doc.TopUnder(fleur, 0), table);
	CHECK_EQ(doc.TopUnder(fleur, table), vase);
	CHECK_EQ(doc.TopUnder(table, vase), 0u);
	// Déplacer le parent emporte les descendants ; un parent masqué ou verrouillé s'impose.
	doc.SetTransform(table, math::FMatrix4::Translation({10.f, 0.f, 0.f}));
	CHECK_NEAR(doc.Find(fleur)->transform.m[3][0], 10.6f, 1e-5f);
	doc.Find(table)->visible = false;
	CHECK(!doc.IsVisible(*doc.Find(fleur)));
	doc.Find(table)->visible = true;
	doc.Find(table)->locked = true;
	CHECK(doc.IsLocked(*doc.Find(vase)));
	doc.Find(table)->locked = false;
	// Fichier : hiérarchie et types relus (identifiants renumérotés).
	doc.Find(fleur)->type = "MeshInstance";
	(void)doc.RemoveEntity(doc.AddEntity("Trou", cube(1.f)).id); // décale la numérotation
	auto back = Document::FromJson(doc.ToJson());
	REQUIRE(back.IsOk());
	const auto named = [&](const Document &d, std::string_view n) {
		for (const Entity &e : d.Entities())
			if (std::string_view(e.name) == n)
				return e.id;
		return EntityId(0);
	};
	CHECK_EQ(back.Value().Find(named(back.Value(), "Fleur"))->parent, named(back.Value(), "Vase"));
	CHECK(back.Value().Find(named(back.Value(), "Fleur"))->type == "MeshInstance");
	// Éditeur : choisir au niveau ouvert, ouvrir un niveau après l'autre, sortir d'un niveau.
	Editor ed;
	ed.Reset(std::move(doc));
	const math::FVector3 fleurTop{10.65f, 2.4f, 0.65f};
	const math::FRay down{fleurTop + math::FVector3{0.f, 5.f, 0.f}, {0.f, -1.f, 0.f}};
	CHECK_EQ(ed.PickEntity(down), table); // la fleur appartient à la table
	REQUIRE(ed.OpenGroup(table));
	CHECK_EQ(ed.PickEntity(down), vase);
	REQUIRE(ed.OpenGroup(vase));
	CHECK_EQ(ed.PickEntity(down), fleur);
	ed.CloseGroup();
	CHECK_EQ(ed.Context(), table); // un niveau
	ed.CloseAllGroups();
	CHECK_EQ(ed.Context(), kRootId);
	ed.Reveal(fleur); // arborescence : ancêtres ouverts, nœud sélectionné
	CHECK_EQ(ed.Context(), vase);
	CHECK(ed.SelectedEntities() == std::vector<EntityId>{fleur});
	CHECK(ed.Reparent(fleur, 0)); // glissé sur « Modèle »
	CHECK_EQ(ed.Doc().Find(fleur)->parent, kRootId);
	CHECK(!ed.Reparent(table, vase)); // cycle refusé
	REQUIRE(ed.Undo());
	CHECK_EQ(ed.Doc().Find(fleur)->parent, vase);
	ed.CloseAllGroups();
	// Grouper des groupes, dupliquer et effacer un sous-arbre, éclater dans le parent.
	ed.SelectEntity(table);
	REQUIRE(ed.GroupSelection());
	const EntityId parent = ed.SelectedEntities().front();
	CHECK_EQ(ed.Doc().Find(table)->parent, parent);
	CHECK(ed.Doc().Find(parent)->model.IsEmpty());
	REQUIRE(ed.DuplicateSelection());
	const EntityId copy = ed.SelectedEntities().front();
	CHECK_EQ(ed.Doc().Descendants(copy).size(), 3u); // table, vase, fleur copiés
	REQUIRE(ed.DeleteSelection());
	CHECK(ed.Doc().Find(copy) == nullptr);
	CHECK_EQ(ed.Doc().Entities().size(), 4u);
	const size_t vaseFaces = ed.Doc().Find(vase)->model.FaceCount(), tableFaces = ed.Doc().Find(table)->model.FaceCount();
	REQUIRE(ed.Doc().Explode(vase));
	CHECK_EQ(ed.Doc().Find(fleur)->parent, table);						 // enfant passé au parent
	CHECK(ed.Doc().Find(table)->model.FaceCount() >= tableFaces + vaseFaces - 1); // géométrie dans le parent
}

TEST(Document, ImportsLegacySceneTree) {
	// Projet de l'ancien sandbox (arbre de nœuds « scene.tree ») : scènes et objets importés en
	// groupes imbriqués.
	const std::filesystem::path menu = "apps/sandbox/projects/dungeon/scenes/main_menu.scene";
	if (!std::filesystem::exists(menu))
		SKIP("projet dungeon absent");
	auto doc = Document::Load(String(menu.string()));
	REQUIRE(doc.IsOk());
	size_t meshes = 0, lights = 0, solids = 0, scripted = 0;
	for (const Entity &e : doc.Value().Entities()) {
		meshes += e.type == "MeshInstance" && e.model.FaceCount() > 0 ? 1u : 0u;
		lights += e.behavior.light ? 1u : 0u;
		solids += e.type == "MeshInstance" && e.behavior.body == BodyKind::Solid ? 1u : 0u;
		scripted += !e.behavior.scriptFile.IsEmpty() ? 1u : 0u;
	}
	CHECK_EQ(meshes, 17u);
	CHECK_EQ(lights, 4u);
	CHECK_EQ(solids, 17u); // RigidBody statiques
	CHECK(scripted >= 3u); // composants Script → ../scripts/<nom>.script
	CHECK(!doc.Value().Game().scriptFile.IsEmpty());
	// Instances d'objets : développées sous leur nœud ; les introuvables sont signalées.
	auto workshop = Document::Load("apps/sandbox/projects/dungeon/scenes/parts_workshop.scene");
	REQUIRE(workshop.IsOk());
	size_t nested = 0;
	for (const Entity &e : workshop.Value().Entities())
		nested += e.parent != 0 && workshop.Value().Find(e.parent) && workshop.Value().Find(e.parent)->parent != 0 ? 1u : 0u;
	CHECK(nested > 0u);
	for (const String &w : workshop.Value().LoadWarnings())
		CHECK(w.find("introuvable") != std::string::npos);
	// Objet de l'ancien format inséré dans une scène : son sous-arbre, avec sa lumière.
	Document target;
	auto torch = target.InsertObject("apps/sandbox/projects/dungeon/objects/torch.object", {1.f, 0.f, 2.f});
	REQUIRE(torch.IsOk());
	CHECK(!target.Descendants(torch.Value()).empty());
	CHECK(std::ranges::any_of(target.Entities(), [](const Entity &e) { return e.behavior.light; }));
}

namespace {

/// Un cube unitaire (1×1×1) dans le repère local, pour créer des groupes de test.
modeling::Model BoxModel() {
	modeling::Model m;
	const auto f = m.AddRectangle({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
	(void)modeling::PushPull(m, *f, m.Faces()[*f].normal.y > 0.f ? 1.f : -1.f);
	return m;
}

} // namespace

TEST(Editor, AlignAndDistributeSelection) {
	// Trois cubes à des positions X inégales.
	const auto make = [] {
		Document doc;
		const modeling::Model box = BoxModel();
		doc.AddEntity("a", box, math::FMatrix4::Translation({0.f, 0.f, 0.f}));
		doc.AddEntity("b", box, math::FMatrix4::Translation({2.f, 0.f, 0.f}));
		doc.AddEntity("c", box, math::FMatrix4::Translation({10.f, 0.f, 0.f}));
		return doc;
	};
	// Aligner à gauche : min.x commun = 0.
	{
		Editor ed;
		ed.Reset(make());
		for (const Entity &e : ed.Doc().Entities())
			ed.SelectEntity(e.id, true);
		REQUIRE(ed.SelectedEntities().size() == 3);
		ed.AlignSelection(Align3D::Left);
		for (EntityId id : ed.SelectedEntities())
			CHECK_NEAR(ed.Doc().WorldBounds(*ed.Doc().Find(id)).min.x, 0.f, 1e-5f);
	}
	// Aligner au centre : center.x commun = (0 + 11) / 2 = 5,5.
	{
		Editor ed;
		ed.Reset(make());
		for (const Entity &e : ed.Doc().Entities())
			ed.SelectEntity(e.id, true);
		ed.AlignSelection(Align3D::CenterX);
		for (EntityId id : ed.SelectedEntities())
			CHECK_NEAR(ed.Doc().WorldBounds(*ed.Doc().Find(id)).Center().x, 5.5f, 1e-5f);
	}
	// Répartir sur X : centres également espacés (0,5 · 5,5 · 10,5).
	{
		Editor ed;
		ed.Reset(make());
		for (const Entity &e : ed.Doc().Entities())
			ed.SelectEntity(e.id, true);
		ed.DistributeSelection(0);
		std::vector<float> xs;
		for (EntityId id : ed.SelectedEntities())
			xs.push_back(ed.Doc().WorldBounds(*ed.Doc().Find(id)).Center().x);
		std::ranges::sort(xs);
		CHECK_NEAR(xs[0], 0.5f, 1e-4f);
		CHECK_NEAR(xs[1], 5.5f, 1e-4f);
		CHECK_NEAR(xs[2], 10.5f, 1e-4f);
	}
}

TEST(Editor, ScaleAndRotateSelected) {
	Document doc;
	const modeling::Model box = BoxModel();
	doc.AddEntity("a", box, math::FMatrix4::Translation({2.f, 0.f, 0.f}));
	Editor ed;
	ed.Reset(std::move(doc));
	for (const Entity &e : ed.Doc().Entities())
		ed.SelectEntity(e.id, true);
	// Échelle ×2 autour de l'origine : le cube 1×1×1 à {2,0,0} devient 2×2×2, de {4,0,0} à {6,2,2}.
	ed.ScaleSelected({}, 2.f);
	const Entity *e = ed.Doc().Find(ed.SelectedEntities().front());
	const math::FAABB b = ed.Doc().WorldBounds(*e);
	CHECK_NEAR(b.min.x, 4.f, 1e-4f);
	CHECK_NEAR(b.max.x, 6.f, 1e-4f);
	CHECK_NEAR(b.max.y, 2.f, 1e-4f);
	CHECK_NEAR(b.max.z, 2.f, 1e-4f);
	// Rotation de 90° autour de Y (par l'origine) : la distance à l'origine est préservée.
	ed.RotateSelected({}, {0.f, 1.f, 0.f}, math::PI * 0.5f);
	const math::FVector3 c = ed.Doc().WorldBounds(*e).Center();
	CHECK_NEAR(c.Length(), std::sqrt(25.f + 1.f + 1.f), 1e-2f); // centre {5,1,1} → distance ≈ √27 (préservée par la rotation)
}
