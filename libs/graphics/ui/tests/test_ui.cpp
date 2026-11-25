#include <cpp-maker/test.hpp>

#include <string>
#include <vector>

#include <ui/dock.hpp>
#include <ui/layout.hpp>
#include <ui/ui.hpp>
#include <ui/window_frame.hpp>

TEST(Dock_CaptureApplyRoundTrip) {
	const std::vector<ui::DockTab> catalog = {{"explorer", "Explorateur", 0}, {"viewport", "Vue 3D", 0}, {"script", "Script", 0},
											  {"console", "Console", 0}, {"props", "Propriétés", 0}};
	ui::DockLayout dock;
	ui::DockState ws;
	ws.regions[0] = {{"explorer", "inconnu"}, "explorer", true, 250};
	ws.regions[1] = {{"viewport", "script"}, "script", true, 0};
	ws.regions[2] = {{"props"}, "props", false, 360};
	ws.regions[3] = {{"console"}, "", true, 180};
	dock.Apply(ws, catalog);
	CHECK_EQ(dock.Tabs(ui::DockLayout::Side::Left).size(), 1u); // « inconnu » ignoré
	CHECK(dock.Active(ui::DockLayout::Side::Center) == "script");
	CHECK(!dock.Visible(ui::DockLayout::Side::Right));
	CHECK(dock.Active(ui::DockLayout::Side::Bottom) == "console"); // premier onglet par défaut
	CHECK(dock.Find("props") == ui::DockLayout::Side::Right);
	CHECK(dock.Find("absent") == ui::DockLayout::Side::Count);

	const ui::DockState back = dock.Capture();
	CHECK_EQ(back.regions[1].tabs.size(), 2u);
	CHECK_EQ(back.regions[1].active, std::string("script"));
	CHECK_EQ(back.regions[0].size, 250.f);
	CHECK(!back.regions[2].visible);
	// Un second Apply de la capture redonne la même disposition.
	ui::DockLayout other;
	other.Apply(back, catalog);
	CHECK(other.Active(ui::DockLayout::Side::Center) == "script");
	CHECK_EQ(other.Capture().regions[3].tabs, back.regions[3].tabs);
}

TEST(Ui_MoveItem) {
	std::vector<int> v{0, 1, 2, 3, 4};
	ui::MoveItem(v, 0, 3);
	CHECK_EQ(v, (std::vector<int>{1, 2, 3, 0, 4}));
	ui::MoveItem(v, 4, 0);
	CHECK_EQ(v, (std::vector<int>{4, 1, 2, 3, 0}));
	ui::MoveItem(v, 9, 0); // hors bornes : sans effet
	CHECK_EQ(v.size(), 5u);
}

// ── Placement dynamique (ui/layout.hpp) ──────────────────────────────────────
TEST(Layout_PlaceCutRowGridStack) {
	const sdl3::FRect screen{0, 0, 1280, 720};
	const sdl3::FRect bottom = ui::Place(screen, 400, 40, ui::Anchor::Bottom, 0, 10);
	CHECK_EQ(bottom.x, 440.f);
	CHECK_EQ(bottom.y, 670.f);
	const sdl3::FRect tr = ui::Place(screen, 100, 50, ui::Anchor::TopRight, 8, 8);
	CHECK_EQ(tr.x, 1172.f);
	CHECK_EQ(tr.y, 8.f);
	CHECK_EQ(ui::Place(screen, 5000, 50, ui::Anchor::Center).w, 1280.f); // borné au parent
	CHECK_EQ(ui::FitScale(1280, 720), 1.f);
	CHECK_EQ(ui::FitScale(2560, 1440), 2.f);
	CHECK_EQ(ui::FitScale(640, 1440), 0.6f); // borne basse
	const sdl3::FRect fa = ui::FitAspect(screen, 1.f);
	CHECK_EQ(fa.w, 720.f);
	CHECK_EQ(fa.x, 280.f);

	sdl3::FRect body{0, 0, 300, 200};
	const sdl3::FRect top = ui::CutTop(body, 30, 10);
	CHECK_EQ(top.h, 30.f);
	CHECK_EQ(body.y, 40.f);
	CHECK_EQ(body.h, 160.f);
	const sdl3::FRect right = ui::CutRight(body, 100, 4);
	CHECK_EQ(right.x, 200.f);
	CHECK_EQ(body.w, 196.f);

	const ui::FlexItem items[] = {{50, 0}, {0, 1}, {0, 2, 0, 60}, {20, 0}};
	const auto row = ui::Row({0, 0, 400, 30}, items, 10);
	CHECK_EQ(row.size(), 4u);
	CHECK_EQ(row[2].w, 60.f);			   // part bornée par max
	CHECK_EQ(row[1].w, 240.f);			   // reçoit le reste : 400 − 50 − 60 − 20 − 3×10
	CHECK_EQ(row[3].x + row[3].w, 400.f); // remplit la rangée
	const ui::FlexItem fixed[] = {{100, 0}, {100, 0}};
	CHECK_EQ(ui::Row({0, 0, 400, 30}, fixed, 0, ui::Align::Center)[0].x, 100.f);
	CHECK_EQ(ui::Column({0, 0, 50, 300}, fixed, 0, ui::Align::End)[1].y, 200.f);

	const ui::FlexItem wide[] = {{100, 0, 20}, {100, 0, 20}};
	const auto shrunk = ui::Row({0, 0, 150, 30}, wide, 10); // 210 demandés pour 150
	CHECK_EQ(shrunk[0].w, 70.f);
	CHECK_EQ(shrunk[1].x + shrunk[1].w, 150.f);
		const ui::GridLayout g = ui::Grid({0, 0, 200, 400}, 10, 40, 4);
	CHECK_EQ(g.columns, 4); // (200 + 4) / 44
	CHECK_EQ(g.rows, 3);
	CHECK_EQ(g.Cell(5).y, 44.f);
	CHECK_EQ(g.HitTest(g.Cell(7).x + 3, g.Cell(7).y + 3, 10), 7);
	CHECK_EQ(g.HitTest(g.Cell(1).x + 42, g.Cell(1).y + 3, 10), -1); // dans l'écart
	CHECK_EQ(ui::Grid({0, 0, 400, 100}, 9, 40, 4, 9).bounds.w, 9 * 40.f + 8 * 4.f);

	ui::AnchorStack stack(bottom, ui::Anchor::Top, 4);
	const sdl3::FRect a = stack.Push(100, 20, ui::Align::Start);
	const sdl3::FRect b = stack.Push(400, 10, ui::Align::Center);
	CHECK_EQ(a.y, bottom.y - 24.f);
	CHECK_EQ(a.x, bottom.x);
	CHECK_EQ(b.y, a.y - 14.f);
	CHECK_EQ(stack.Used().h, 34.f);
	const sdl3::FRect k = ui::KeepInside({1250, -20, 100, 50}, screen);
	CHECK_EQ(k.x, 1180.f);
	CHECK_EQ(k.y, 0.f);
}

// ── Encadrement de fenêtre personnalisé (ui/window_frame.hpp) ────────────────
TEST(WindowFrame_LayoutAndHitTest) {
	const sdl3::FRect window{0, 0, 1000, 600};
	const float leftW[] = {30, 30, 80};
	const float rightW[] = {30, 30};
	const ui::FrameLayout l = ui::ComputeFrameLayout(window, 40, leftW, rightW, true);
	// Barre en haut, contenu dessous.
	CHECK_EQ(l.bar.h, 40.f);
	CHECK_EQ(l.content.y, 40.f);
	CHECK_EQ(l.content.h, 560.f);
	// Boutons de la fenêtre collés à droite : réduire, agrandir, fermer.
	CHECK_EQ(l.close.x + l.close.w, 1000.f);
	CHECK_EQ(l.maximize.x + l.maximize.w, l.close.x);
	CHECK_EQ(l.minimize.x + l.minimize.w, l.maximize.x);
	CHECK(l.close.w >= 34.f);
	// Icône puis boutons de gauche, dans l'ordre, sans chevauchement.
	CHECK(l.icon.w > 0.f && l.icon.x >= 8.f);
	REQUIRE(l.left.size() == 3u && l.right.size() == 2u);
	CHECK(l.left[0].x >= l.icon.x + l.icon.w);
	CHECK(l.left[1].x >= l.left[0].x + l.left[0].w);
	CHECK_EQ(l.left[2].w, 80.f);
	// Boutons de droite avant les boutons de la fenêtre.
	CHECK(l.right[1].x + l.right[1].w <= l.minimize.x);
	CHECK(l.right[0].x + l.right[0].w <= l.right[1].x);
	// Zone du titre entre les deux groupes.
	CHECK(l.title.x >= l.left[2].x + l.left[2].w);
	CHECK(l.title.x + l.title.w <= l.right[0].x);

	// Boutons absents : rectangles vides, la place revient au reste.
	const ui::FrameLayout bare = ui::ComputeFrameLayout(window, 40, {}, {}, false, false, false, true);
	CHECK_EQ(bare.minimize.w, 0.f);
	CHECK_EQ(bare.maximize.w, 0.f);
	CHECK_EQ(bare.icon.w, 0.f);
	CHECK(bare.title.w > l.title.w);
	// Fenêtre étroite : les boutons de gauche qui ne tiennent plus sont écartés.
	const ui::FrameLayout narrow = ui::ComputeFrameLayout({0, 0, 330, 300}, 40, leftW, rightW, true);
	CHECK_EQ(narrow.left[2].w, 0.f);
	CHECK(narrow.left[0].w > 0.f);

	// Zones : la barre libre déplace la fenêtre, les boutons reçoivent leurs clics.
	using ui::FrameHit;
	CHECK(ui::HitTestFrame(l, 500, 20, false) == FrameHit::Caption);
	CHECK(ui::HitTestFrame(l, l.left[0].x + 5, 20, false) == FrameHit::Client);
	CHECK(ui::HitTestFrame(l, l.right[1].x + 5, 20, false) == FrameHit::Client);
	CHECK(ui::HitTestFrame(l, l.close.x + 10, 20, false) == FrameHit::Client);
	CHECK(ui::HitTestFrame(l, l.icon.x + 2, 20, false) == FrameHit::Caption); // l'icône déplace aussi
	CHECK(ui::HitTestFrame(l, 500, 300, false) == FrameHit::Client);
	// Bords et coins de redimensionnement.
	CHECK(ui::HitTestFrame(l, 2, 300, false) == FrameHit::Left);
	CHECK(ui::HitTestFrame(l, 997, 300, false) == FrameHit::Right);
	CHECK(ui::HitTestFrame(l, 500, 2, false) == FrameHit::Top);
	CHECK(ui::HitTestFrame(l, 500, 598, false) == FrameHit::Bottom);
	CHECK(ui::HitTestFrame(l, 2, 2, false) == FrameHit::TopLeft);
	CHECK(ui::HitTestFrame(l, 990, 2, false) == FrameHit::TopRight); // coin élargi
	CHECK(ui::HitTestFrame(l, 2, 590, false) == FrameHit::BottomLeft);
	CHECK(ui::HitTestFrame(l, 998, 598, false) == FrameHit::BottomRight);
	// Fenêtre agrandie : plus de bords, le bouton fermer reste cliquable jusqu'au coin.
	CHECK(ui::HitTestFrame(l, 2, 300, true) == FrameHit::Client);
	CHECK(ui::HitTestFrame(l, 998, 2, true) == FrameHit::Client);
	CHECK(ui::HitTestFrame(l, 500, 2, true) == FrameHit::Caption);
	// Hors de la fenêtre.
	CHECK(ui::HitTestFrame(l, -5, 20, false) == FrameHit::Client);

	// Liaison sans fenêtre : seule la fermeture est rapportée.
	ui::WindowChrome chrome(nullptr);
	chrome.Update(l, 2.f); // 2 pixels de fenêtre par unité logique
	CHECK(chrome.HitTest(1000, 40) == FrameHit::Caption);
	CHECK(!chrome.Maximized());
	CHECK(chrome.Apply(ui::FrameAction::Close));
	CHECK(!chrome.Apply(ui::FrameAction::Minimize));
}
