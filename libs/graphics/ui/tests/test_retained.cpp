#include <cpp-maker/test.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <ui/builder.hpp>
#include <ui/css.hpp>
#include <ui/i18n.hpp>
#include <ui/icons.hpp>
#include <ui/style.hpp>

// ── Analyse des valeurs CSS ──────────────────────────────────────────────────

TEST(Retained_ParseLength) {
	CHECK(ui::ParseLength("12px") == ui::Length::Px(12.f));
	CHECK(ui::ParseLength("50%") == ui::Length::Pct(50.f));
	CHECK(ui::ParseLength("auto").has_value());
	CHECK(ui::ParseLength("1.5em") == ui::Length::Em(1.5f));
	CHECK(ui::ParseLength("2rem") == ui::Length::Rem(2.f));
	CHECK(ui::ParseLength("100vw") == ui::Length::Vw(100.f));
	CHECK(ui::ParseLength("100vh") == ui::Length::Vh(100.f));
	CHECK(ui::ParseLength("1fr") == ui::Length::Fr(1.f));
	CHECK(!ui::ParseLength("nimporte").has_value());
	// calc(100% - 8px)
	CHECK(ui::ParseLength("calc(100% - 8px)") == ui::Length::Pct(100.f).Plus(-8.f));
}

TEST(Retained_ParseColor) {
	auto red = ui::ParseColor("#ff0000");
	CHECK(red.has_value());
	CHECK(red->r == 255 && red->g == 0 && red->b == 0 && red->a == 255);
	auto red50 = ui::ParseColor("#ff000080");
	CHECK(red50.has_value() && red50->a == 128);
	auto shortHex = ui::ParseColor("#0f0");
	CHECK(shortHex.has_value() && shortHex->g == 255 && shortHex->r == 0);
	auto named = ui::ParseColor("red");
	CHECK(named.has_value() && named->r == 255 && named->a == 255);
	auto transparent = ui::ParseColor("transparent");
	CHECK(transparent.has_value() && transparent->a == 0);
	CHECK(!ui::ParseColor("pas-une-couleur").has_value());
}

// ── Déclarations (raccourcis) ────────────────────────────────────────────────

TEST(Retained_SetDeclaration) {
	ui::StyleProps p;
	CHECK(ui::SetDeclaration(p, "margin", "4px 8px"));
	CHECK(p.marginTop == 4.f && p.marginRight == 8.f && p.marginBottom == 4.f && p.marginLeft == 8.f);
	CHECK(ui::SetDeclaration(p, "padding", "16px"));
	CHECK(p.paddingTop == 16.f && p.paddingLeft == 16.f);
	CHECK(ui::SetDeclaration(p, "display", "flex"));
	CHECK(p.display == ui::Display::Flex);
	CHECK(ui::SetDeclaration(p, "width", "50%"));
	CHECK(p.width == ui::Length::Pct(50.f));
	CHECK(ui::SetDeclaration(p, "font-size", "20px"));
	CHECK(p.fontSize == 20.f);
	CHECK(!ui::SetDeclaration(p, "propriete-inconnue", "x")); // jamais d'exception
}

// ── Feuille CSS ──────────────────────────────────────────────────────────────

TEST(Retained_StyleSheet) {
	auto sheet = ui::StyleSheet::Parse(R"(
		button { background-color: #242c42; padding: 8px 14px; }
		button.danger { background-color: #5c2430; }
		.title { font-size: 20px; font-weight: 700; }
	)");
	CHECK_EQ(sheet->Rules().size(), 3u);
	CHECK(sheet->Warnings().empty());
	// Sélecteur de classe + type.
	CHECK_EQ(sheet->Rules()[2].selectors.size(), 1u);
	CHECK(sheet->Rules()[2].selectors[0].Subject().classes.size() == 1u);
	CHECK(sheet->Rules()[2].selectors[0].Subject().classes[0] == "title");
}

// ── Builder ──────────────────────────────────────────────────────────────────

TEST(Retained_BuilderTree) {
	ui::WidgetBuilder root = ui::Column().Id("root").Gap(8).Padding(16).Children({
		ui::Text("Titre").Id("title").Class("heading"),
		ui::Button("Quitter").Id("quit").Class("danger"),
	});
	auto node = root.Build();
	CHECK_EQ(node->type, std::string("box"));
	CHECK_EQ(node->id, std::string("root"));
	CHECK_EQ(node->children.size(), 2u);
	CHECK_EQ(node->children[0]->type, std::string("text"));
	CHECK_EQ(node->children[0]->id, std::string("title"));
	CHECK_EQ(node->children[1]->type, std::string("button"));
	CHECK_EQ(node->children[1]->id, std::string("quit"));
	CHECK(node->style.paddingLeft == 16.f);
	CHECK(node->style.gap == 8.f);
	// Le builder dépose le style inline (pas encore calculé).
	CHECK(node->children[0]->style.IsEmpty());
}

// ── Spécificité des sélecteurs ───────────────────────────────────────────────

TEST(Retained_SelectorSpecificity) {
	auto id = ui::ParseSelector("#quit");
	auto cls = ui::ParseSelector(".danger");
	auto type = ui::ParseSelector("button");
	CHECK(id.IsOk() && cls.IsOk() && type.IsOk());
	CHECK(id.Value().specificity > cls.Value().specificity);
	CHECK(cls.Value().specificity > type.Value().specificity);
}

// ── Liaisons (data binding) et catalogue ─────────────────────────────────────

TEST(Retained_BindingsAndWidgets) {
	float volume = 0.5f;
	bool muted = false;
	int clicked = 0;
	auto root = ui::Column().Gap(4).Children({
		ui::Slider(0.f, 1.f).Bind(volume),
		ui::Checkbox("Muet").Bind(muted),
		ui::Button("OK").OnClick([&] { ++clicked; }),
	});
	auto node = root.Build();
	CHECK_EQ(node->children.size(), 3u);
	// Curseur lié à `volume`.
	CHECK(node->children[0]->valueRef == &volume);
	CHECK(node->children[0]->valueMin == 0.f && node->children[0]->valueMax == 1.f);
	// Case liée à `muted`.
	CHECK(node->children[1]->checkedRef == &muted);
	// Bouton : callback stocké, pas encore appelé.
	CHECK_EQ(clicked, 0);
	CHECK(node->children[2]->onClick != nullptr);
	// Longueur implicite : `Length(140.f)` = 140 pixels.
	ui::Length l = 140.f;
	CHECK(l.unit == ui::Unit::Px && l.value == 140.f);
}

// ── Catalogue étendu (fabrique des nouveaux widgets) ─────────────────────────

TEST(Retained_NewWidgets) {
	auto root = ui::Column().Gap(4).Children({
		ui::Separator(),
		ui::ImageView(ui::icons::Codepoint(ui::icons::MaterialIcons::SETTINGS)),
		ui::RichTextLabel("**gras** et [lien](https://x)"),
		ui::ToggleButton("Bascule"),
		ui::RadioButton("Rouge").Group("couleur"),
		ui::ToolButton(ui::icons::Codepoint(ui::icons::MaterialIcons::CLOSE)),
		ui::LineEdit().Placeholder("Saisie").Password(),
		ui::TextArea(),
		ui::SpinBox(-10.f, 10.f, 0.5f),
		ui::Knob(0.f, 1.f),
		ui::ComboBox().Options({"A", "B", "C"}),
		ui::Expander("Section"),
		ui::Splitter(),
		ui::Flow(),
	});
	auto n = root.Build();
	CHECK_EQ(n->children.size(), 14u);
	CHECK_EQ(n->children[0]->type, std::string("separator"));
	CHECK_EQ(n->children[1]->type, std::string("image"));
	CHECK_EQ(n->children[2]->type, std::string("richtext"));
	CHECK_EQ(n->children[3]->type, std::string("toggle"));
	CHECK_EQ(n->children[4]->type, std::string("radio"));
	CHECK_EQ(n->children[4]->group, std::string("couleur"));
	CHECK_EQ(n->children[5]->type, std::string("toolbutton"));
	CHECK_EQ(n->children[6]->type, std::string("lineedit"));
	CHECK(n->children[6]->placeholder == "Saisie" && n->children[6]->password);
	CHECK_EQ(n->children[7]->type, std::string("textarea"));
	CHECK_EQ(n->children[8]->type, std::string("spinbox"));
	CHECK(n->children[8]->step == 0.5f && n->children[8]->valueMin == -10.f);
	CHECK_EQ(n->children[9]->type, std::string("knob"));
	CHECK_EQ(n->children[10]->type, std::string("combobox"));
	CHECK_EQ(n->children[10]->options.size(), 3u);
	CHECK_EQ(n->children[11]->type, std::string("expander"));
	CHECK(n->children[11]->open); // déplié par défaut
	CHECK_EQ(n->children[12]->type, std::string("splitter"));
	CHECK_EQ(n->children[13]->type, std::string("box")); // Flow = box en ligne + wrap
}

// ── Catalogue P3 : pilule, titre, variantes sémantiques, désactivation ───────

TEST(Retained_BadgeSemanticsAndDisabled) {
	auto badge = ui::Badge("Bêta", sdl3::Color{255, 120, 0, 255}).Build();
	CHECK_EQ(badge->type, std::string("badge"));
	CHECK(badge->style.borderTopLeftRadius > 100.f); // pilule (rayon borné au rendu)
	CHECK(badge->style.backgroundColor.has_value() && badge->style.backgroundColor->a > 0);

	auto heading = ui::Heading("Titre").Build();
	CHECK_EQ(heading->type, std::string("text"));
	CHECK_EQ(heading->classes.size(), 1u);
	CHECK(heading->classes[0] == "heading");

	auto primary = ui::PrimaryButton("OK").Build();
	CHECK_EQ(primary->type, std::string("button"));
	CHECK_EQ(primary->classes.size(), 1u);
	CHECK(primary->classes[0] == "primary");
	auto danger = ui::DangerButton("Suppr").Build();
	CHECK_EQ(danger->classes.size(), 1u);
	CHECK(danger->classes[0] == "danger");
	auto success = ui::SuccessButton("Enregistrer").Build();
	CHECK_EQ(success->classes.size(), 1u);
	CHECK(success->classes[0] == "success");

	auto disabled = ui::Button("Non dispo").Disabled().Build();
	CHECK(disabled->disabled);
}

// ── Données complexes (Modèle-Vue) ───────────────────────────────────────────

TEST(Retained_DataWidgets) {
	ui::TableModel model;
	model.rowCount = [] { return size_t(3); };
	model.columnCount = [] { return size_t(2); };
	model.text = [](size_t, size_t c) { return c == 0 ? "nom" : "valeur"; };
	std::vector<ui::TreeItem> roots = {{"a", "A", 0, sdl3::Color{}, false, {}}};

	auto root = ui::Column().Children({
		ui::ListView().Model(model).MultiSelect(),
		ui::TreeView().Nodes(roots),
		ui::TableView().Model(model).Headers({"Nom", "Valeur"}),
	});
	auto n = root.Build();
	CHECK_EQ(n->children.size(), 3u);
	CHECK_EQ(n->children[0]->type, std::string("listview"));
	CHECK(n->children[0]->modelView.multiSelect);
	CHECK(n->children[0]->modelView.model.rowCount() == 3u);
	CHECK_EQ(n->children[1]->type, std::string("treeview"));
	CHECK_EQ(n->children[1]->modelView.tree.size(), 1u);
	CHECK_EQ(n->children[2]->type, std::string("tableview"));
	CHECK_EQ(n->children[2]->modelView.headers.size(), 2u);
	CHECK_EQ(n->children[2]->modelView.headers[0], std::string("Nom"));
	CHECK(n->children[2]->modelView.model.columnCount() == 2u);
}

// ── Navigation (onglets, menus) ──────────────────────────────────────────────

TEST(Retained_NavigationWidgets) {
	std::vector<ui::Menu> menus;
	menus.push_back({"Fichier", {{"Nouveau", "Ctrl+N", false, false, false, "", [] {}}, {"Séparateur", "", true, false, false, "", nullptr}}});
	menus.push_back({"Édition", {{"Copier", "Ctrl+C", false, false, false, "", [] {}}}});

	auto root = ui::Column().Children({
		ui::MenuBar().Menus(menus),
		ui::TabWidget().Tabs({"A", "B"}).Children({ui::Text("page A"), ui::Text("page B")}),
		ui::TabBar().Tabs({"X", "Y", "Z"}),
	});
	auto n = root.Build();
	CHECK_EQ(n->children.size(), 3u);
	CHECK_EQ(n->children[0]->type, std::string("menubar"));
	CHECK_EQ(n->children[0]->menus.size(), 2u);
	CHECK_EQ(n->children[0]->menus[0].title, std::string("Fichier"));
	CHECK_EQ(n->children[0]->menus[0].items.size(), 2u);
	CHECK(n->children[0]->menus[0].items[1].separator);
	CHECK_EQ(n->children[1]->type, std::string("tabwidget"));
	CHECK_EQ(n->children[1]->tabs.size(), 2u);
	CHECK_EQ(n->children[1]->children.size(), 2u);
	CHECK_EQ(n->children[2]->type, std::string("tabbar"));
	CHECK_EQ(n->children[2]->tabs.size(), 3u);
}

// ── Parité du catalogue (Selectable / Switch / MenuButton / SettingRow) ──────

TEST(Retained_CatalogParity) {
	auto root = ui::Column().Children({
		ui::Selectable("Élément").Selected().Icon(0xE1C2),
		ui::Switch("Activé").Selected(),
		ui::MenuButton("Ouvrir", "Projet récent", 0xE1C2),
		ui::SettingRow("Volume", "Gain global").Children({ui::Slider(0.f, 1.f)}),
	});
	auto n = root.Build();
	REQUIRE(n->children.size() == 4u);
	CHECK_EQ(n->children[0]->type, std::string("selectable"));
	CHECK(n->children[0]->checked);
	CHECK(n->children[0]->icon != 0);
	CHECK_EQ(n->children[1]->type, std::string("switchbox"));
	CHECK(n->children[1]->checked);
	CHECK_EQ(n->children[2]->type, std::string("menubutton"));
	CHECK_EQ(n->children[2]->text, std::string("Ouvrir"));
	CHECK_EQ(n->children[2]->placeholder, std::string("Projet récent"));
	CHECK(n->children[2]->icon != 0);
	CHECK_EQ(n->children[3]->type, std::string("settingrow"));
	CHECK_EQ(n->children[3]->text, std::string("Volume"));
	CHECK_EQ(n->children[3]->placeholder, std::string("Gain global"));
	CHECK_EQ(n->children[3]->children.size(), 1u);
	CHECK_EQ(n->children[3]->children[0]->type, std::string("slider"));
}

// ── Fenêtrage (SubWindow / Popup) ────────────────────────────────────────────

TEST(Retained_Windowing) {
	auto root = ui::Column().Children({
		ui::SubWindow("Éditeur").At(40.f, 40.f).Size(300.f, 200.f).Resizable().Children({ui::Text("contenu")}),
		ui::Popup().At(120.f, 80.f).Children({ui::Text("infobulle")}),
	});
	auto n = root.Build();
	CHECK_EQ(n->children.size(), 2u);
	CHECK_EQ(n->children[0]->type, std::string("subwindow"));
	CHECK(n->children[0]->floating.active);
	CHECK(n->children[0]->floating.x == 40.f && n->children[0]->floating.y == 40.f);
	CHECK(n->children[0]->floating.w == 300.f && n->children[0]->floating.h == 200.f);
	CHECK(n->children[0]->floating.resizable);
	CHECK_EQ(n->children[1]->type, std::string("popup"));
	CHECK(n->children[1]->floating.active);
	CHECK(n->children[1]->floating.x == 120.f);
}

// ── Comportements (animations, drag & drop, i18n) ────────────────────────────

TEST(Retained_Behaviors) {
	// Easing.
	CHECK(ui::Ease(0.f, ui::Easing::Linear) == 0.f);
	CHECK(ui::Ease(1.f, ui::Easing::Linear) == 1.f);
	CHECK(ui::Ease(1.f, ui::Easing::OutCubic) == 1.f);
	CHECK(ui::Ease(0.5f, ui::Easing::InQuad) < 0.5f); // démarre lentement

	// Animations (glissé).
	auto slide = ui::Text("coucou").SlideIn(0.4f, 20.f, ui::Easing::OutQuad).Build();
	CHECK(slide->animTime == 0.f);
	CHECK(slide->animDuration == 0.4f);
	CHECK(slide->animOffset == 20.f);

	// Drag & drop.
	auto drag = ui::Text("source").Draggable("payload-1").Build();
	CHECK(drag->dragDrop.draggable);
	CHECK_EQ(drag->dragDrop.payload, std::string("payload-1"));
	bool dropped = false;
	std::string received;
	auto drop = ui::Text("cible").DropTarget([&](std::string p) { dropped = true; received = std::move(p); }).Build();
	CHECK(drop->dragDrop.dropTarget);
	CHECK(drop->dragDrop.onDrop != nullptr);

	// I18n.
	ui::Translator tr;
	tr.Add("bonjour", "Bonjour");
	tr.Add("au_revoir", "Au revoir");
	CHECK(tr.Tr("bonjour") == std::string_view("Bonjour"));
	CHECK(tr.Tr("inconnu") == std::string_view("inconnu")); // repli sur la clé
	CHECK(tr.Contains("au_revoir"));
	CHECK_EQ(tr.Size(), 2u);
}

// ── DataViz ──────────────────────────────────────────────────────────────────

TEST(Retained_DataViz) {
	ui::PlotSeries line;
	line.name = "série";
	line.kind = ui::PlotKind::Line;
	line.points = {{0.f, 1.f}, {1.f, 2.f}, {2.f, 3.f}};
	ui::PlotData data;
	data.title = "Mon graphe";
	data.series.push_back(line);

	auto root = ui::Column().Children({
		ui::Plot().Plot(data),
		ui::PieChart().Pie({{"A", 1.f, sdl3::Color{255, 0, 0, 255}}, {"B", 2.f, sdl3::Color{0, 255, 0, 255}}}),
		ui::Heatmap().Heatmap({1.f, 2.f, 3.f, 4.f}, 2, 0.f, 5.f),
	});
	auto n = root.Build();
	CHECK_EQ(n->children.size(), 3u);
	CHECK_EQ(n->children[0]->type, std::string("plot"));
	CHECK_EQ(n->children[0]->plot.series.size(), 1u);
	CHECK_EQ(n->children[0]->plot.title, std::string("Mon graphe"));
	CHECK_EQ(n->children[0]->plot.series[0].points.size(), 3u);
	CHECK_EQ(n->children[1]->type, std::string("pie"));
	CHECK_EQ(n->children[1]->pie.size(), 2u);
	CHECK_EQ(n->children[2]->type, std::string("heatmap"));
	CHECK_EQ(n->children[2]->heat.size(), 4u);
	CHECK_EQ(n->children[2]->heatCols, 2u);
}

// ── Thèmes (assets/themes) ───────────────────────────────────────────────────

TEST(Retained_Themes) {
	int parsed = 0;
	for (const auto &entry : std::filesystem::directory_iterator("assets/themes")) {
		if (entry.path().extension() != ".css")
			continue;
		std::ifstream in(entry.path());
		std::stringstream ss;
		ss << in.rdbuf();
		auto sheet = ui::StyleSheet::Parse(ss.str(), entry.path().filename().string());
		CHECK(sheet->Warnings().empty()); // aucune propriété inconnue (midnight.css est vide)
		++parsed;
	}
	CHECK(parsed >= 20); // la palette Textual + les thèmes de base
}
