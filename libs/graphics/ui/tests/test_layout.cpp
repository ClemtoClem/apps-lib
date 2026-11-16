// Mise en page et interactions, mesurées pour de vrai : renderer logiciel sur une surface
// en mémoire (aucune fenêtre), polices du dépôt (tests lancés depuis la racine), évènements
// souris injectés dans la file SDL. Couvre les tailles intrinsèques des conteneurs, le
// positionnement absolu, le rétrécissement flex, l'orientation des séparateurs, les
// sélecteurs de couleur et de date, et le menu contextuel de l'interface retenue.
#include <cpp-maker/test.hpp>

#include <array>
#include <optional>

#include <sdl3/render/canvas.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/render/render.hpp>
#include <sdl3/core/sdl_context.hpp>
#include <ui/builder.hpp>
#include <ui/i18n.hpp>
#include <ui/node_graph.hpp>
#include <ui/plot_view.hpp>
#include <ui/retained.hpp>
#include <ui/script_ui.hpp>
#include <sdl3/core/misc.hpp>
#include <ui/ui.hpp>

namespace {

constexpr float kW = 800.f, kH = 600.f;

/// Canvas sur renderer logiciel ; `ok` faux si SDL ou les polices manquent.
struct Fixture {
	std::optional<sdl3::SdlContext> sdl; // file d'évènements (souris simulée)
	std::optional<sdl3::Surface> surface;
	std::optional<sdl3::Renderer> renderer;
	sdl3::Canvas canvas;
	sdl3::EventsManager events;
	bool ok = false;

	Fixture() {
		if (auto context = sdl3::SdlContext::Create(sdl3::initFlags::EVENTS); context.IsOk())
			sdl.emplace(std::move(context.Value()));
		auto s = sdl3::Surface::Create(int(kW), int(kH));
		if (s.IsError())
			return;
		surface.emplace(std::move(s.Value()));
		auto r = sdl3::Renderer::CreateSoftware(*surface);
		if (r.IsError())
			return;
		renderer.emplace(std::move(r.Value()));
		ok = canvas.Init(*renderer, sdl3::CanvasFontSet{"assets/fonts/DejaVuSans.ttf", "assets/fonts/DejaVuSans-Bold.ttf",
												 "assets/fonts/DejaVuSansMono.ttf", "assets/fonts/MaterialIcons-Regular.otf"});
	}

	/// Monte `root`, met en page et dessine une image ; rend l'interface pour les requêtes.
	void Present(ui::Ui &ui, ui::WidgetBuilder root) {
		ui.Root(std::move(root));
		canvas.Begin();
		ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
		canvas.End();
	}
};

/// Injecte un appui (ou un relâchement) de bouton de souris en (x, y), puis lit la file.
void Mouse(Fixture &f, float x, float y, uint8_t button, bool down) {
	SDL_Event e{};
	e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.button = button;
	e.button.down = down;
	e.button.x = x;
	e.button.y = y;
	SDL_PushEvent(&e);
	f.events.Poll();
}

/// Une image de l'interface immédiate : `draw` dessine dans la colonne `area`.
template <typename Fn> void ImmediateFrame(Fixture &f, ui::Context &u, const sdl3::FRect &area, Fn &&draw) {
	f.canvas.Begin();
	u.Begin(0.016f);
	u.BeginColumn(area);
	draw();
	u.EndColumn();
	u.End();
	f.canvas.End();
}

bool Near(float a, float b, float tolerance = 1.f) { return a > b - tolerance && a < b + tolerance; }

} // namespace

TEST(Layout_NestedContainersHaveIntrinsicHeight) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Ui ui(f.canvas, f.events);
	f.Present(ui, ui::Column().Gap(10.f).Children({
					  ui::Row().Id("first").Gap(8.f).Children({ui::Text("Volume"), ui::Button("OK")}),
					  ui::Row().Id("second").Padding(6.f).Children({ui::Text("Deuxième rangée")}),
					  ui::Slider(0.f, 1.f).Id("slider"),
				  }));
	const sdl3::FRect first = ui.Find("first").Bounds(), second = ui.Find("second").Bounds(), slider = ui.Find("slider").Bounds();
	// Une rangée mesure son plus grand enfant (ici une ligne de texte, sans feuille de
	// style) au lieu de 0 ; plus de chevauchement.
	CHECK(Near(first.h, f.canvas.LineHeight(14.f)));
	CHECK(Near(second.y, first.Bottom() + 10.f));
	CHECK(second.h > f.canvas.LineHeight(14.f) + 11.f); // texte + padding 6 × 2
	CHECK(Near(slider.y, second.Bottom() + 10.f));
	CHECK(Near(slider.h, 24.f)); // hauteur par défaut d'un curseur
}

TEST(Layout_AbsoluteAnchorAndOffsets) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Ui ui(f.canvas, f.events);
	f.Present(ui, ui::Box().Children({
					  ui::Column().Id("card").Position(ui::Position::Absolute).Anchor(ui::Anchor::Center).Width(300.f).Height(200.f),
					  ui::Box().Id("corner").Position(ui::Position::Absolute).Right(10.f).Bottom(20.f).Width(50.f).Height(40.f),
					  ui::Box().Id("offset").Position(ui::Position::Absolute).Left(30.f).Top(15.f).Width(10.f).Height(10.f),
					  ui::Box().Id("stretch").Position(ui::Position::Absolute).Left(100.f).Right(100.f).Top(0.f).Height(5.f),
					  ui::Text("Dans le flux").Id("flow"),
				  }));
	const sdl3::FRect card = ui.Find("card").Bounds();
	CHECK(Near(card.x, (kW - 300.f) / 2.f) && Near(card.y, (kH - 200.f) / 2.f));
	CHECK(Near(card.w, 300.f) && Near(card.h, 200.f));
	const sdl3::FRect corner = ui.Find("corner").Bounds();
	CHECK(Near(corner.Right(), kW - 10.f) && Near(corner.Bottom(), kH - 20.f));
	const sdl3::FRect offset = ui.Find("offset").Bounds();
	CHECK(Near(offset.x, 30.f) && Near(offset.y, 15.f));
	CHECK(Near(ui.Find("stretch").Bounds().w, kW - 200.f)); // étiré entre left et right
	// Les enfants positionnés ne prennent pas de place dans le flux.
	CHECK(Near(ui.Find("flow").Bounds().y, 0.f));
}

TEST(Layout_FlexShrinkKeepsLabels) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Ui ui(f.canvas, f.events);
	f.Present(ui, ui::Column().Children({
					  ui::Row().Id("row").Width(260.f).Gap(6.f).Children({
						  ui::Button("Premier bouton").Id("a").Padding(20.f),
						  ui::Button("Deuxième").Id("b").Padding(20.f),
						  ui::Button("Trois").Id("c").Padding(20.f),
					  }),
				  }));
	const sdl3::FRect row = ui.Find("row").Bounds();
	const sdl3::FRect a = ui.Find("a").Bounds(), c = ui.Find("c").Bounds();
	// Le contenu (≈ 350 px) dépasse 260 px : les boutons rétrécissent pour tenir…
	CHECK(c.Right() <= row.Right() + 1.f);
	// … sans jamais couper leur libellé.
	CHECK(a.w >= f.canvas.MeasureText("Premier bouton", 14.f));
	CHECK(c.w >= f.canvas.MeasureText("Trois", 14.f));
}

TEST(Layout_SeparatorOrientation) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Ui ui(f.canvas, f.events);
	f.Present(ui, ui::Column().Width(400.f).Children({
					  ui::Separator().Id("h"),
					  ui::Row().Height(40.f).Children({ui::Text("A"), ui::Separator().Vertical().Id("v"), ui::Text("B")}),
				  }));
	const sdl3::FRect h = ui.Find("h").Bounds(), v = ui.Find("v").Bounds();
	CHECK(Near(h.w, 400.f) && Near(h.h, 12.f)); // filet horizontal sur toute la largeur
	CHECK(Near(v.w, 12.f) && Near(v.h, 40.f));	// filet vertical sur toute la hauteur
}

TEST(Pickers_ColorPickerDrag) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	sdl3::Color color = sdl3::Rgba(200, 40, 40);
	const sdl3::FRect area{0, 0, 400, 170}; // carré 170 × 170, barre de teinte en x 180..196
	bool changed = false;
	const auto frame = [&] { ImmediateFrame(f, u, area, [&] { changed = u.ColorPicker("couleur", color); }); };
	// Coin haut droit du carré : saturation et valeur maximales, teinte rouge conservée.
	Mouse(f, 169.f, 0.f, SDL_BUTTON_LEFT, true);
	frame();
	CHECK(changed);
	CHECK(color.r == 255 && color.g < 8 && color.b < 8);
	Mouse(f, 169.f, 0.f, SDL_BUTTON_LEFT, false);
	frame();
	// Milieu de la barre de teinte : cyan.
	Mouse(f, 188.f, 85.f, SDL_BUTTON_LEFT, true);
	frame();
	CHECK(color.r < 8 && color.g > 247 && color.b > 247);
	Mouse(f, 188.f, 85.f, SDL_BUTTON_LEFT, false);
	frame();
	// Noir (valeur nulle), puis retour au coin : la teinte cyan a été mémorisée.
	Mouse(f, 0.f, 169.f, SDL_BUTTON_LEFT, true);
	frame();
	CHECK(color.r < 3 && color.g < 3 && color.b < 3); // dernier pixel du carré : valeur ≈ 0,006
	Mouse(f, 0.f, 169.f, SDL_BUTTON_LEFT, false);
	frame();
	Mouse(f, 169.f, 0.f, SDL_BUTTON_LEFT, true);
	frame();
	CHECK(color.r < 8 && color.g > 247 && color.b > 247);
	CHECK_EQ(color.a, 255);
}

TEST(Pickers_DatePickerNavigationAndClick) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	ui::Date date{2026, 8, 20};
	bool changed = false;
	const sdl3::FRect area{0, 0, 280, 400}; // cases de 40 px ; semaines à partir de y = 64, pas de 34
	const auto frame = [&] { ImmediateFrame(f, u, area, [&] { changed = u.DatePicker("agenda", date); }); };
	const auto click = [&](float x, float y) {
		Mouse(f, x, y, SDL_BUTTON_LEFT, true);
		frame();
		Mouse(f, x, y, SDL_BUTTON_LEFT, false);
		frame();
	};
	frame();
	// Août 2026 commence un samedi : le lundi 10 est la 1re case de la 3e semaine.
	click(20.f, 146.f);
	CHECK(changed);
	CHECK(date == (ui::Date{2026, 8, 10}));
	// Mois suivant (septembre, commence un mardi) : le 1er est la 2e case de la 1re semaine.
	click(265.f, 15.f);
	CHECK(date == (ui::Date{2026, 8, 10})); // naviguer ne change pas la date choisie
	click(60.f, 78.f);
	CHECK(date == (ui::Date{2026, 9, 1}));
}

TEST(Retained_ContextMenu) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Ui ui(f.canvas, f.events);
	int refreshed = 0, about = 0;
	ui::MenuItem refresh{"Actualiser", "F5", false, false, false, "", [&] { ++refreshed; }};
	ui::MenuItem info{"À propos", "", false, false, false, "", [&] { ++about; }};
	f.Present(ui, ui::Column().Children({
					  ui::Box().Id("desktop").Height(300.f).ContextMenu({refresh, info}).Children({ui::Text("bureau").Id("label")}),
				  }));
	// Clic droit sur un descendant : le menu de l'ancêtre s'ouvre au point du clic.
	Mouse(f, 120.f, 5.f, SDL_BUTTON_RIGHT, true);
	f.canvas.Begin();
	ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
	f.canvas.End();
	REQUIRE(ui.Find("desktop")->openMenu == 0);
	CHECK(Near(ui.Find("desktop")->menuX, 120.f) && Near(ui.Find("desktop")->menuY, 5.f));
	Mouse(f, 120.f, 5.f, SDL_BUTTON_RIGHT, false);
	// Deuxième entrée (26 px par entrée) : « À propos ».
	Mouse(f, 140.f, 5.f + 26.f + 13.f, SDL_BUTTON_LEFT, true);
	f.canvas.Begin();
	ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
	f.canvas.End();
	CHECK_EQ(about, 1);
	CHECK_EQ(refreshed, 0);
	CHECK_EQ(ui.Find("desktop")->openMenu, -1); // refermé après le choix
}

TEST(I18n_TranslatorLoadJson) {
	ui::Translator tr;
	const uint64_t v0 = tr.Version();
	auto loaded = tr.LoadJson(R"({"locale": "en", "language": "English", "strings": {"ok": "Confirm", "tabs.0": "Controls", "n": 3}})");
	REQUIRE(loaded.IsOk());
	CHECK_EQ(loaded.Value(), 2u); // les valeurs non textuelles sont ignorées
	CHECK_EQ(tr.Locale(), std::string("en"));
	CHECK_EQ(tr.Language(), std::string("English"));
	CHECK(tr.Version() > v0);
	CHECK_EQ(tr.Tr("ok"), std::string_view("Confirm"));
	CHECK_EQ(tr.Tr("absent"), std::string_view("absent")); // repli sur la clé
	CHECK(tr.Find("absent") == nullptr);
	// Objet racine sans « strings » : toutes ses chaînes sont des traductions.
	REQUIRE(tr.LoadJson(R"({"ok": "Valider"})").IsOk());
	CHECK_EQ(tr.Tr("ok"), std::string_view("Valider"));
	CHECK(tr.Locale().empty());
	// Erreurs : la table reste intacte.
	CHECK(tr.LoadJson("[1, 2]").IsError());
	CHECK(tr.LoadJson("{ pas du json").IsError());
	CHECK(tr.LoadFile("assets/absent.json").IsError());
	CHECK_EQ(tr.Tr("ok"), std::string_view("Valider"));
}

TEST(I18n_RetainedWidgetsById) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Translator en;
	REQUIRE(en.LoadJson(R"({"strings": {
		"ok": "Confirm", "name.placeholder": "Your name", "ok.tooltip": "Save and close",
		"tabs.0": "Controls", "tabs.1": "Data", "choice.1": "Second",
		"menu.0": "File", "menu.0.0": "Open", "menu.0.2": "Quit", "desk.0": "Refresh"}})")
				.IsOk());
	ui::Ui ui(f.canvas, f.events);
	ui.SetTranslator(&en);
	ui::MenuItem open{"Ouvrir", "", false, false, false, "", nullptr}, sep{"", "", true, false, false, "", nullptr},
		quit{"Quitter", "", false, false, false, "", nullptr};
	f.Present(ui, ui::Column().Children({
					  ui::MenuBar().Id("menu").Menus({{"Fichier", {open, sep, quit}}}),
					  ui::Button("Valider").Id("ok").Tooltip("Enregistrer et fermer"),
					  ui::Button("Annuler").Id("cancel"), // aucune traduction : texte des sources
					  ui::LineEdit().Id("name").Placeholder("Votre nom"),
					  ui::TabBar().Id("tabs").Tabs({"Contrôles", "Données"}),
					  ui::ComboBox().Id("choice").Options({"Premier", "Deuxième"}),
					  ui::Box().Id("desk").Height(40.f).ContextMenu({open}),
				  }));
	CHECK_EQ(ui.Find("ok")->text, std::string("Confirm"));
	CHECK_EQ(ui.Find("ok")->tooltip->text, std::string("Save and close"));
	CHECK_EQ(ui.Find("cancel")->text, std::string("Annuler"));
	CHECK_EQ(ui.Find("name")->placeholder, std::string("Your name"));
	CHECK(ui.Find("tabs")->tabs == std::vector<std::string>({"Controls", "Data"}));
	CHECK(ui.Find("choice")->options == std::vector<std::string>({"Premier", "Second"}));
	const ui::Menu &menu = ui.Find("menu")->menus[0];
	CHECK_EQ(menu.title, std::string("File"));
	CHECK_EQ(menu.items[0].label, std::string("Open"));
	CHECK_EQ(menu.items[2].label, std::string("Quit"));
	CHECK_EQ(ui.Find("desk")->menus[0].items[0].label, std::string("Refresh"));

	// Changement de langue : une clé absente rétablit le texte des sources.
	REQUIRE(en.LoadJson(R"({"strings": {"ok": "OK"}})").IsOk());
	f.canvas.Begin();
	ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
	f.canvas.End();
	CHECK_EQ(ui.Find("ok")->text, std::string("OK"));
	CHECK_EQ(ui.Find("name")->placeholder, std::string("Votre nom"));
	CHECK_EQ(ui.Find("tabs")->tabs[0], std::string("Contrôles"));
	CHECK_EQ(ui.Find("menu")->menus[0].title, std::string("Fichier"));
	// Plus de traducteur : tous les textes des sources.
	ui.SetTranslator(nullptr);
	f.canvas.Begin();
	ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
	f.canvas.End();
	CHECK_EQ(ui.Find("ok")->text, std::string("Valider"));
	CHECK_EQ(ui.Find("ok")->tooltip->text, std::string("Enregistrer et fermer"));
}

TEST(Layout_TextClippedToParent) {
	Fixture f;
	if (!f.ok)
		SKIP("renderer logiciel ou polices indisponibles");
	ui::Ui ui(f.canvas, f.events);
	// Texte blanc bien plus large que sa rangée de 60 px, et bouton étroit au libellé long.
	f.Present(ui, ui::Column().Children({
					  ui::Row().Width(60.f).Height(30.f).Children({ui::Text("Un texte beaucoup trop long pour sa boîte").Color(sdl3::Rgba(255, 255, 255))}),
					  ui::Button("Libellé de bouton trop long").Width(50.f).Height(30.f).Color(sdl3::Rgba(255, 255, 255)),
				  }));
	auto pixels = f.renderer->ReadPixels();
	REQUIRE(pixels.IsOk());
	const auto lit = [&](int x0, int x1, int y0, int y1) {
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				if (const auto p = pixels.Value().ReadPixel(x, y); p.IsSome() && (p.Value().r > 40 || p.Value().g > 40 || p.Value().b > 40))
					return true;
		return false;
	};
	CHECK(lit(0, 60, 0, 30));	  // le début du texte est dessiné…
	CHECK(!lit(62, 400, 0, 30));  // … mais rien au-delà de la rangée
	CHECK(!lit(52, 400, 31, 60)); // ni au-delà du bouton
}

TEST(NodeGraph_EditorDragCreatesAndDetachesLinks) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	ui::NodeGraph graph;
	const uint32_t a = graph.AddNode("v", "A", {20, 20}).id;
	const uint32_t aOut = graph.AddOutput(a, "sortie");
	const uint32_t b = graph.AddNode("v", "B", {300, 20}).id;
	const uint32_t bIn = graph.AddInput(b, "entrée");
	ui::NodeGraphEditor editor;
	const sdl3::FRect area{0, 0, 600, 400};
	int changes = 0;
	editor.callbacks.onChanged = [&] { ++changes; };
	const auto frame = [&] { ImmediateFrame(f, u, area, [&] { editor.Draw(u, area, graph); }); };
	frame();
	const ui::GraphPoint from = editor.PinPosition(graph, *graph.FindPin(aOut));
	const ui::GraphPoint to = editor.PinPosition(graph, *graph.FindPin(bIn));
	// Glisser de la sortie de A à l'entrée de B : un lien.
	Mouse(f, from.x, from.y, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, to.x, to.y, SDL_BUTTON_LEFT, false);
	frame();
	REQUIRE(graph.Links().size() == 1u);
	CHECK_EQ(graph.LinkInto(bIn)->from, aOut);
	CHECK(changes >= 1);
	// Glisser depuis l'entrée reliée vers le vide : le lien est détaché.
	Mouse(f, to.x, to.y, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, 200.f, 300.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(graph.Links().empty());
	// Glisser un en-tête déplace le nœud.
	const float x0 = graph.FindNode(b)->pos.x;
	Mouse(f, 310.f, 30.f, SDL_BUTTON_LEFT, true);
	frame();
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	motion.motion.x = 360.f;
	motion.motion.y = 30.f;
	SDL_PushEvent(&motion);
	f.events.Poll();
	frame();
	Mouse(f, 360.f, 30.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(std::abs(graph.FindNode(b)->pos.x - (x0 + 50.f)) < 1.f);
}

TEST(PlotView_AutoFitAndInteractions) {
	ui::PlotView plot;
	ui::PlotLayer &bars = plot.Series("barres", ui::PlotStyle::Bars);
	bars.y = {2.f, 5.f, 3.f};
	ui::PlotLayer &candles = plot.Series("cours", ui::PlotStyle::Candles);
	candles.x = {0.f, 1.f, 2.f};
	candles.open = {10.f, 11.f, 9.f};
	candles.close = {11.f, 9.f, 12.f};
	candles.high = {12.f, 11.5f, 13.f};
	candles.low = {9.5f, 8.f, 8.5f};
	ui::PlotLayer &temp = plot.Series("température", ui::PlotStyle::Line);
	temp.y = {100.f, 300.f, 200.f};
	temp.secondaryAxis = true;
	plot.AutoFit();
	CHECK(plot.yMin <= 0.f);	 // les barres partent de 0
	CHECK(plot.yMax >= 13.f);	 // haut des chandeliers
	CHECK(plot.xMin <= -0.5f && plot.xMax >= 2.5f); // demi-barre de marge
	CHECK(plot.y2Min <= 100.f && plot.y2Max >= 300.f && plot.y2Min > 0.f); // axe secondaire séparé

	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	const sdl3::FRect area{0, 0, 500, 300};
	const auto frame = [&] { ImmediateFrame(f, u, area, [&] { plot.Draw(u, area); }); };
	frame();
	// Molette au centre du graphique : zoom avant sur les deux axes.
	const float spanX = plot.xMax - plot.xMin, spanY = plot.yMax - plot.yMin;
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	motion.motion.x = 250.f;
	motion.motion.y = 150.f;
	SDL_PushEvent(&motion);
	SDL_Event wheel{};
	wheel.type = SDL_EVENT_MOUSE_WHEEL;
	wheel.wheel.y = 2.f;
	SDL_PushEvent(&wheel);
	f.events.Poll();
	frame();
	CHECK(plot.xMax - plot.xMin < spanX * 0.8f);
	CHECK(plot.yMax - plot.yMin < spanY * 0.8f);
	// Double-clic : retour à l'ajustement automatique.
	for (int i = 0; i < 2; ++i) {
		Mouse(f, 250.f, 150.f, SDL_BUTTON_LEFT, true);
		frame();
		Mouse(f, 250.f, 150.f, SDL_BUTTON_LEFT, false);
		frame();
	}
	CHECK(std::abs((plot.xMax - plot.xMin) - spanX) < 0.01f);
	// Clic sur la première entrée de la légende (en haut à gauche du tracé) : série masquée.
	Mouse(f, 70.f, 30.f, SDL_BUTTON_LEFT, true);
	frame();
	CHECK(!plot.Layers()[0].visible);
}

TEST(PlotView_BarsAndAreasArePainted) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	ui::PlotView plot;
	plot.legend = false;
	ui::PlotLayer &bars = plot.Series("barres", ui::PlotStyle::Bars, sdl3::Rgba(255, 0, 0));
	for (int i = 1; i <= 6; ++i)
		bars.x.push_back(float(i)), bars.y.push_back(float(i));
	const sdl3::FRect area{0, 0, 400, 300};
	ImmediateFrame(f, u, area, [&] { plot.Draw(u, area); });
	auto pixels = f.renderer->ReadPixels();
	REQUIRE(pixels.IsOk());
	int red = 0;
	for (int y = 0; y < 300; y += 2)
		for (int x = 0; x < 400; x += 2)
			if (const auto p = pixels.Value().ReadPixel(x, y); p.IsSome() && p.Value().r > 200 && p.Value().g < 60)
				++red;
	CHECK(red > 500); // six barres pleines
}

TEST(Menus_StayOpenUntilChosen) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	// ── Menu contextuel de l'éditeur de nœuds ──
	ui::NodeGraph graph;
	ui::NodeGraphEditor editor;
	int added = 0;
	editor.callbacks.canvasMenu = [&](ui::GraphPoint) {
		return std::vector<ui::GraphMenuItem>{{"Ajouter", [&] { ++added; }, ""}, {"Autre", [] {}, ""}};
	};
	const sdl3::FRect area{0, 0, 600, 400};
	const auto graphFrame = [&] { ImmediateFrame(f, u, area, [&] { editor.Draw(u, area, graph); }); };
	graphFrame();
	Mouse(f, 200.f, 150.f, SDL_BUTTON_RIGHT, true); // ouvre le menu du fond
	graphFrame();
	Mouse(f, 200.f, 150.f, SDL_BUTTON_RIGHT, false);
	for (int i = 0; i < 5; ++i)
		graphFrame(); // le menu reste affiché (aucune action exécutée)
	CHECK_EQ(added, 0);
	Mouse(f, 220.f, 150.f + 4.f + 13.f, SDL_BUTTON_LEFT, true); // première entrée
	graphFrame();
	CHECK_EQ(added, 1);
	Mouse(f, 220.f, 150.f + 4.f + 13.f, SDL_BUTTON_LEFT, false);
	graphFrame();

	// ── Liste déroulante d'un Choice ──
	const std::array<std::string_view, 3> options = {"Hann", "Hamming", "Blackman"};
	int index = 0, clicksBelow = 0;
	const sdl3::FRect choice{20, 20, 200, 30};
	const auto choiceFrame = [&] {
		ImmediateFrame(f, u, {0, 0, 400, 300}, [&] {
			u.Choice(choice, "window", index, options);
			if (u.ButtonAt({20, 60, 200, 30}, "dessous")) // sous la liste : ne doit pas recevoir le clic
				++clicksBelow;
		});
	};
	Mouse(f, 120.f, 35.f, SDL_BUTTON_LEFT, true);
	choiceFrame();
	Mouse(f, 120.f, 35.f, SDL_BUTTON_LEFT, false);
	choiceFrame(); // relâché sur la valeur : la liste s'ouvre
	for (int i = 0; i < 3; ++i)
		choiceFrame();
	// Deuxième ligne de la liste (sous le Choice, lignes de rowHeight − 4 = 26 px).
	const float row = u.Style().rowHeight - 4.f;
	Mouse(f, 120.f, 52.f + row * 1.5f, SDL_BUTTON_LEFT, true);
	choiceFrame();
	Mouse(f, 120.f, 52.f + row * 1.5f, SDL_BUTTON_LEFT, false);
	choiceFrame();
	CHECK_EQ(index, 1); // « Hamming »
	CHECK_EQ(clicksBelow, 0);
}

TEST(MenuBar_OpenSwitchPickAndClose) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	const ui::MenuEntry file[] = {{"Nouveau", "Ctrl+N"}, {""}, {"Quitter", "Ctrl+Q"}};
	const ui::MenuEntry edit[] = {{"Annuler", "Ctrl+Z", false}, {"Tout sélectionner", "Ctrl+A"}};
	const ui::BarMenu menus[] = {{"Fichier", file}, {"Édition", edit}};
	ui::MenuChoice chosen;
	int clicksBelow = 0;
	const auto frame = [&] {
		ImmediateFrame(f, u, {0, 0, 500, 300}, [&] {
			if (const ui::MenuChoice m = u.MenuBar({0, 0, 500, 30}, menus); m.menu >= 0)
				chosen = m;
			if (u.ButtonAt({10, 40, 300, 200}, "dessous")) // sous le menu ouvert : jamais cliqué
				++clicksBelow;
		});
	};
	frame();
	Mouse(f, 20.f, 15.f, SDL_BUTTON_LEFT, true); // ouvre « Fichier »
	frame();
	Mouse(f, 20.f, 15.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(u.MenuOpen());
	// Survol de « Édition » : le menu bascule sans clic.
	const float editX = 4.f + f.canvas.MeasureText("Fichier", u.Style().fontSize - 1.f) + 20.f + 2.f + 10.f;
	Mouse(f, editX, 15.f, 0, false);
	frame();
	frame();
	CHECK(u.MenuOpen());
	// Entrée désactivée (« Annuler ») : sans effet ; « Tout sélectionner » : choisie.
	const float rowH = u.Style().rowHeight - 4.f;
	Mouse(f, editX + 20.f, 30.f + 1.f + 4.f + rowH * 0.5f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, editX + 20.f, 30.f + 1.f + 4.f + rowH * 0.5f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK_EQ(chosen.menu, -1);
	Mouse(f, editX + 20.f, 30.f + 1.f + 4.f + rowH * 1.5f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, editX + 20.f, 30.f + 1.f + 4.f + rowH * 1.5f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(chosen.Is(1, 1));
	CHECK(!u.MenuOpen());
	CHECK_EQ(clicksBelow, 0);
	// Réouverture puis clic ailleurs : fermé sans choix.
	chosen = {};
	Mouse(f, 20.f, 15.f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, 20.f, 15.f, SDL_BUTTON_LEFT, false);
	frame();
	Mouse(f, 450.f, 280.f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, 450.f, 280.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(!u.MenuOpen());
	CHECK_EQ(chosen.menu, -1);
}

namespace {

/// Touche enfoncée puis relâchée (modificateurs `mod`), puis lecture de la file.
void Key(Fixture &f, SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE) {
	SDL_Event e{};
	e.type = SDL_EVENT_KEY_DOWN;
	e.key.key = key;
	e.key.mod = mod;
	e.key.down = true;
	SDL_PushEvent(&e);
	e.type = SDL_EVENT_KEY_UP;
	e.key.down = false;
	SDL_PushEvent(&e);
	f.events.Poll();
}

/// Saisie de texte (évènement SDL_EVENT_TEXT_INPUT), puis lecture de la file.
void Type(Fixture &f, const char *text) {
	SDL_Event e{};
	e.type = SDL_EVENT_TEXT_INPUT;
	e.text.text = text;
	SDL_PushEvent(&e);
	f.events.Poll();
}

} // namespace

TEST(Text_KeypadDigitsWithoutDuplicates) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("évènements SDL indisponibles");
	Key(f, SDLK_KP_7, SDL_KMOD_NUM); // aucun évènement de saisie : le chiffre est ajouté
	CHECK(f.events.TextInput() == "7");
	SDL_Event down{};
	down.type = SDL_EVENT_KEY_DOWN;
	down.key.key = SDLK_KP_3;
	down.key.mod = SDL_KMOD_NUM;
	down.key.down = true;
	SDL_PushEvent(&down);
	SDL_Event text{};
	text.type = SDL_EVENT_TEXT_INPUT;
	text.text.text = "3";
	SDL_PushEvent(&text);
	f.events.Poll();
	CHECK(f.events.TextInput() == "3"); // fourni par SDL : pas de doublon
	Key(f, SDLK_KP_7, SDL_KMOD_NONE);	// sans Verr. num. : touche de déplacement, pas de chiffre
	CHECK(f.events.TextInput().empty());
}

TEST(Text_ImmediateFieldEditing) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	std::string value = "Bonjour";
	bool submitted = false;
	const sdl3::FRect field{20, 20, 300, 30};
	const auto frame = [&] { ImmediateFrame(f, u, {0, 0, 400, 300}, [&] { u.TextInputAt(field, "champ", value, &submitted); }); };
	frame();
	Mouse(f, 300.f, 35.f, SDL_BUTTON_LEFT, true); // focus, curseur en fin de texte
	frame();
	Mouse(f, 300.f, 35.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(u.HasFocus("champ"));
	Type(f, " 12");
	frame();
	Key(f, SDLK_KP_5, SDL_KMOD_NUM); // chiffre du pavé numérique
	frame();
	CHECK(value == "Bonjour 125");
	Key(f, SDLK_LEFT, SDL_KMOD_CTRL); // mot précédent
	frame();
	Key(f, SDLK_END, SDL_KMOD_SHIFT); // sélection jusqu'à la fin
	frame();
	REQUIRE(u.EditState("champ") != nullptr);
	CHECK(u.EditState("champ")->Selected(value) == "125");
	Type(f, "42"); // la frappe remplace la sélection
	frame();
	CHECK(value == "Bonjour 42");
	Key(f, SDLK_HOME);
	frame();
	Key(f, SDLK_DELETE, SDL_KMOD_CTRL); // supprime le mot suivant
	frame();
	CHECK(value == "42");
	Key(f, SDLK_A, SDL_KMOD_CTRL);
	frame();
	CHECK(u.EditState("champ")->Selected(value) == "42");
	Key(f, SDLK_BACKSPACE);
	frame();
	CHECK(value.empty());
	Key(f, SDLK_KP_ENTER);
	frame();
	CHECK(submitted);
}

TEST(Text_ImmediateTextAreaAndLabels) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	std::string notes = "a";
	const sdl3::FRect area{20, 60, 300, 120};
	const auto frame = [&] {
		ImmediateFrame(f, u, {0, 0, 400, 300}, [&] {
			u.Label("Libellé sélectionnable");
			u.TextArea(area, "notes", notes);
		});
	};
	frame();
	Mouse(f, 300.f, 170.f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, 300.f, 170.f, SDL_BUTTON_LEFT, false);
	frame();
	Key(f, SDLK_RETURN); // nouvelle ligne
	frame();
	Type(f, "b");
	frame();
	Key(f, SDLK_PAGEUP); // première ligne, même colonne
	frame();
	Key(f, SDLK_HOME);
	frame();
	Type(f, "0");
	frame();
	CHECK(notes == "0a\nb");
	Key(f, SDLK_ESCAPE);
	frame();
	CHECK(!u.HasFocus("notes"));
	// Libellé : glisser du début du texte jusqu'à sa fin le sélectionne.
	const float lh = f.canvas.LineHeight(u.Style().fontSize);
	const float y = (std::max(lh, u.Style().rowHeight * 0.75f)) * 0.5f;
	Mouse(f, 1.f, y, SDL_BUTTON_LEFT, true);
	frame();
	SDL_Event move{};
	move.type = SDL_EVENT_MOUSE_MOTION;
	move.motion.x = 1.f + f.canvas.MeasureText("Libellé", u.Style().fontSize);
	move.motion.y = y;
	SDL_PushEvent(&move);
	f.events.Poll();
	frame();
	Mouse(f, move.motion.x, y, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(u.HasTextSelection());
	CHECK(u.WantsKeyboard()); // Ctrl+C / Ctrl+A reviennent au texte sélectionné
	Mouse(f, 390.f, 290.f, SDL_BUTTON_LEFT, true); // appui ailleurs : désélection
	frame();
	Mouse(f, 390.f, 290.f, SDL_BUTTON_LEFT, false);
	frame();
	CHECK(!u.HasTextSelection());
}

TEST(Text_RetainedLineEditAndTextArea) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Ui ui(f.canvas, f.events);
	std::string name = "abc", body = "x";
	int submits = 0;
	// Arbre monté une fois : le focus et le curseur vivent dans ses nœuds.
	ui.Root(ui::Column().Gap(8.f).Children({
		ui::LineEdit().Id("nom").Bind(name).Width(240.f).Height(30.f).OnSubmit([&] { ++submits; }),
		ui::TextArea().Id("corps").Bind(body).Width(240.f).Height(90.f),
		ui::Text("Texte statique").Id("statique"),
	}));
	const auto frame = [&] {
		f.canvas.Begin();
		ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
		f.canvas.End();
	};
	frame();
	frame();
	const sdl3::FRect nameBox = ui.Find("nom").Bounds();
	Mouse(f, nameBox.Right() - 4.f, nameBox.y + nameBox.h * 0.5f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, nameBox.Right() - 4.f, nameBox.y + nameBox.h * 0.5f, SDL_BUTTON_LEFT, false);
	frame();
	Type(f, "d1");
	frame();
	Key(f, SDLK_KP_2, SDL_KMOD_NUM);
	frame();
	CHECK(name == "abcd12");
	Key(f, SDLK_HOME, SDL_KMOD_SHIFT); // tout jusqu'au début
	frame();
	Key(f, SDLK_RIGHT, SDL_KMOD_SHIFT); // le curseur avance : sélection « bcd12 »
	frame();
	Key(f, SDLK_DELETE);
	frame();
	CHECK(name == "a");
	Key(f, SDLK_RETURN);
	frame();
	CHECK_EQ(submits, 1);
	const sdl3::FRect bodyBox = ui.Find("corps").Bounds();
	Mouse(f, bodyBox.Right() - 4.f, bodyBox.y + 6.f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, bodyBox.Right() - 4.f, bodyBox.y + 6.f, SDL_BUTTON_LEFT, false);
	frame();
	Key(f, SDLK_RETURN);
	frame();
	Type(f, "y");
	frame();
	Key(f, SDLK_UP);
	frame();
	Key(f, SDLK_HOME);
	frame();
	Type(f, ">");
	frame();
	CHECK(body == ">x\ny");
}

TEST(Text_CodeEditors) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	auto rules = std::make_shared<ui::SyntaxRules>();
	rules->keywords = {"fn"};
	// Interface retenue : fabrique ui::CodeEditor() liée à une chaîne.
	ui::Ui ui(f.canvas, f.events);
	std::string code = "fn f() {";
	ui.Root(ui::Column().Children({ui::CodeEditor().Id("code").Bind(code).Syntax(rules).ErrorLine(1).Width(400.f).Height(200.f)}));
	const auto frame = [&] {
		f.canvas.Begin();
		ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
		f.canvas.End();
	};
	frame();
	frame();
	const sdl3::FRect box = ui.Find("code").Bounds();
	Mouse(f, box.Right() - 4.f, box.y + 8.f, SDL_BUTTON_LEFT, true); // fin de la première ligne
	frame();
	Mouse(f, box.Right() - 4.f, box.y + 8.f, SDL_BUTTON_LEFT, false);
	frame();
	Key(f, SDLK_RETURN); // indentation automatique
	frame();
	Type(f, "x");
	frame();
	Key(f, SDLK_RETURN);
	frame();
	Type(f, "}"); // « } » retire le cran
	frame();
	CHECK(code == "fn f() {\n\tx\n}");
	Key(f, SDLK_A, SDL_KMOD_CTRL);
	frame();
	Key(f, SDLK_TAB); // sélection sur plusieurs lignes : indentées
	frame();
	CHECK(code == "\tfn f() {\n\t\tx\n\t}");
	Key(f, SDLK_TAB, SDL_KMOD_SHIFT);
	frame();
	CHECK(code == "fn f() {\n\tx\n}");
	// Interface immédiate : clic dans la marge des numéros → ligne entière sélectionnée.
	ui::Context u(f.canvas, f.events);
	std::string text = "a\nbb\nc";
	const sdl3::FRect area{20, 20, 300, 150};
	ui::CodeEditorOptions options;
	options.syntax = rules.get();
	const auto immediate = [&] { ImmediateFrame(f, u, {0, 0, 400, 300}, [&] { u.CodeEditor(area, "imm", text, options); }); };
	immediate();
	const float lh = f.canvas.LineHeight(u.Style().fontSize - 1.f, sdl3::CanvasFont::Mono);
	Mouse(f, area.x + 4.f, area.y + 4.f + lh * 1.5f, SDL_BUTTON_LEFT, true);
	immediate();
	Mouse(f, area.x + 4.f, area.y + 4.f + lh * 1.5f, SDL_BUTTON_LEFT, false);
	immediate();
	REQUIRE(u.EditState("imm") != nullptr);
	CHECK(u.EditState("imm")->Selected(text) == "bb\n");
}

TEST(Immediate_WidgetsOverAClickableRow) {
	// Ligne cliquable (Interact) portant un bouton et un champ dessinés par-dessus, comme les
	// boutons Afficher / Verrouiller d'une arborescence ou Renommer d'une carte : le widget du
	// dessus reçoit le clic, la ligne seulement en dehors de lui.
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Context u(f.canvas, f.events);
	const sdl3::FRect row{20, 20, 400, 40}, button{330, 26, 80, 28}, field{120, 26, 180, 28};
	int rowClicks = 0, buttonClicks = 0;
	std::string text = "nom";
	const auto frame = [&] {
		ImmediateFrame(f, u, {0, 0, 500, 300}, [&] {
			rowClicks += u.Interact(u.MakeId("row"), row).leftClicked ? 1 : 0;
			buttonClicks += u.ButtonAt(button, "Bouton") ? 1 : 0;
			u.TextInputAt(field, "champ", text);
		});
	};
	const auto move = [&](float x, float y) {
		SDL_Event e{};
		e.type = SDL_EVENT_MOUSE_MOTION;
		e.motion.x = x;
		e.motion.y = y;
		SDL_PushEvent(&e);
		f.events.Poll();
	};
	const auto click = [&](float x, float y, bool hoverFirst) {
		move(x, y);
		if (hoverFirst)
			frame(); // survol d'abord : le widget du dessus est connu
		Mouse(f, x, y, SDL_BUTTON_LEFT, true);
		frame();
		Mouse(f, x, y, SDL_BUTTON_LEFT, false);
		frame();
	};
	click(370.f, 40.f, true); // survolé puis cliqué
	CHECK_EQ(buttonClicks, 1);
	CHECK_EQ(rowClicks, 0);
	click(30.f, 40.f, true); // hors du bouton : la ligne
	CHECK_EQ(rowClicks, 1);
	click(380.f, 36.f, false); // clic sans image de survol : le bouton reprend l'activation
	CHECK_EQ(buttonClicks, 2);
	CHECK_EQ(rowClicks, 1);
	click(200.f, 40.f, false); // le champ prend le focus, la ligne n'est pas cliquée
	CHECK(u.HasFocus("champ"));
	CHECK_EQ(rowClicks, 1);
}

TEST(Text_ClipboardCopyCutPaste) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	// Pilote vidéo « dummy » : presse-papiers interne de SDL, indépendant du bureau (le
	// gestionnaire de presse-papiers ne peut pas le reprendre, aucun affichage requis).
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	auto video = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO);
	if (video.IsError() || !sdl3::clipboard::SetText("essai") || sdl3::clipboard::GetText() != "essai")
		SKIP("presse-papiers indisponible (pas d'affichage)");
	ui::Context u(f.canvas, f.events);
	std::string value = "copier coller";
	const sdl3::FRect field{20, 20, 300, 30};
	const auto frame = [&] { ImmediateFrame(f, u, {0, 0, 400, 300}, [&] { u.TextInputAt(field, "champ", value); }); };
	frame();
	Mouse(f, 300.f, 35.f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, 300.f, 35.f, SDL_BUTTON_LEFT, false);
	frame();
	Key(f, SDLK_LEFT, SDL_KMOD_CTRL | SDL_KMOD_SHIFT); // sélection du dernier mot
	frame();
	Key(f, SDLK_C, SDL_KMOD_CTRL);
	frame();
	CHECK(sdl3::clipboard::GetText() == "coller");
	Key(f, SDLK_X, SDL_KMOD_CTRL);
	frame();
	CHECK(value == "copier ");
	CHECK(sdl3::clipboard::GetText() == "coller");
	Key(f, SDLK_HOME);
	frame();
	(void)sdl3::clipboard::SetText("<");
	Key(f, SDLK_V, SDL_KMOD_CTRL);
	frame();
	CHECK(value == "<copier ");
	(void)sdl3::clipboard::SetText("+");
	Key(f, SDLK_INSERT, SDL_KMOD_SHIFT); // Maj+Inser : coller
	frame();
	CHECK(value == "<+copier ");
	// TextEdit directement : copie, coupe et collage.
	ui::TextEdit ed;
	std::string text = "abc";
	ed.SelectAll(text);
	CHECK(ed.Cut(text));
	CHECK(text.empty());
	CHECK(sdl3::clipboard::GetText() == "abc");
	CHECK(ed.Paste(text));
	CHECK(text == "abc");
	ed.secret = true; // mot de passe : jamais copié
	ed.SelectAll(text);
	CHECK(!ed.Copy(text));
	// Libellé sélectionné : Ctrl+A puis Ctrl+C copient son texte.
	ui::Context v(f.canvas, f.events);
	const auto labels = [&] { ImmediateFrame(f, v, {0, 0, 400, 300}, [&] { v.Label("Texte à copier"); }); };
	labels();
	Mouse(f, 2.f, 10.f, SDL_BUTTON_LEFT, true);
	labels();
	Mouse(f, 2.f, 10.f, SDL_BUTTON_LEFT, false);
	labels();
	Key(f, SDLK_A, SDL_KMOD_CTRL);
	labels();
	Key(f, SDLK_C, SDL_KMOD_CTRL);
	labels();
	CHECK(sdl3::clipboard::GetText() == "Texte à copier");
}

TEST(ScriptUi_BuildUpdateAndCallbacks) {
	Fixture f;
	if (!f.ok || !f.sdl)
		SKIP("renderer logiciel, polices ou évènements SDL indisponibles");
	ui::Ui ui(f.canvas, f.events);
	script::Interpreter vm;
	ui::ScriptUi bridge(vm, ui);
	auto r = vm.Run(R"(
		let clics = 0
		let panneau = ui.column({anchor: "top_left", offset: [20, 20], gap: 8, pad: 10, bg: "#202020ff", w: 300})
		let titre = ui.label("Bonjour", {parent: panneau, font_size: 18, bold: true, color: "#ffd060"})
		let bouton = ui.button("Compter", fn(b) { clics += 1  titre.text = "Clics : " .. clics }, {parent: panneau, w: 200})
		let vie = ui.progress(30, 120, {parent: panneau, w: 200})
		let caisse = ui.checkbox("Son", true, fn(w, v) { }, {parent: panneau})
		let jauge = ui.slider(0, 10, 4, fn(w, v) { }, {parent: panneau})
	)");
	REQUIRE(r.IsOk());
	CHECK_EQ(bridge.Count(), 6u);
	const auto frame = [&] {
		bridge.Sync();
		f.canvas.Begin();
		ui.Present(0.016f, sdl3::FRect{0, 0, kW, kH});
		f.canvas.End();
		bridge.Collect();
	};
	frame();
	frame();
	ui::Node *root = ui.Find("script-ui-root").Get();
	REQUIRE(root != nullptr);
	REQUIRE(root->children.size() == 1u);
	ui::Node &panel = *root->children[0];
	REQUIRE(panel.children.size() == 5u);
	CHECK(panel.children[0]->text == "Bonjour");
	CHECK_NEAR(panel.children[2]->value, 0.25f, 1e-5f); // 30 / 120
	CHECK(panel.children[3]->checked);
	CHECK_NEAR(panel.bounds.x, 20.f, 1.f); // ancre + décalage
	// Clic sur le bouton : rappel du script après l'image, qui met à jour le libellé.
	const sdl3::FRect b = panel.children[1]->bounds;
	Mouse(f, b.x + b.w * 0.5f, b.y + b.h * 0.5f, SDL_BUTTON_LEFT, true);
	frame();
	Mouse(f, b.x + b.w * 0.5f, b.y + b.h * 0.5f, SDL_BUTTON_LEFT, false);
	frame();
	frame(); // le texte modifié par le rappel est posé au Sync suivant
	CHECK_EQ(vm.GetGlobal("clics").AsInt(), 1);
	CHECK(panel.children[0]->text == "Clics : 1");
	// Champs → nœuds : valeur, visibilité ; retrait.
	REQUIRE(vm.Run("vie.value = 60\njauge.visible = false").IsOk());
	frame();
	CHECK_NEAR(panel.children[2]->value, 0.5f, 1e-5f);
	CHECK(panel.children[4]->hidden);
	REQUIRE(vm.Run("ui.remove(vie)").IsOk());
	CHECK_EQ(panel.children.size(), 4u);
	CHECK(vm.Run("vie.set_bg(\"#ffffff\")").IsError()); // widget retiré
	// Ancre en bas à droite : collée au coin, décalage compris.
	REQUIRE(vm.Run("let coin = ui.column({anchor: \"bottom_right\", offset: [-40, -44], pad: 10, w: 200, h: 80})").IsOk());
	frame();
	frame();
	const sdl3::FRect corner = root->children.back()->bounds;
	CHECK_NEAR(corner.Right(), kW - 40.f, 1.5f);
	CHECK_NEAR(corner.Bottom(), kH - 44.f, 1.5f);
	REQUIRE(vm.Run("ui.clear()").IsOk());
	CHECK(root->children.empty());
	CHECK(bridge.Errors().empty());
}
