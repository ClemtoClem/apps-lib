// example-ui — catalogue complet du module `ui`.
//
// La classe principale `ExampleUi` dérive d'`app::Application` (cycle de vie : fenêtre,
// canvas, boucle, captures) et exerce l'intégralité des fonctionnalités du module ui :
// fabrique de widgets (builder factory), CSS, liaison de valeurs (Bind), Modèle-Vue
// (List/Tree/Table), navigation (onglets, menus, menu contextuel), fenêtrage (SubWindow /
// Popup / Modal), positionnement absolu, sélecteurs immédiats (couleur, date),
// comportements (animations, glisser-déposer) et visualisation (Plot / Pie / Heatmap).
//
// Thèmes : une feuille CSS (assets/themes/) et un ui::Theme, échangés à chaud.
// Langues : examples/example-ui/i18n/<langue>.json associe chaque identifiant de widget
// (`.Id("clé")`) à une phrase ; ui::Ui::SetTranslator remplace les textes à la volée.
//
//   L       changer de langue              T       changer de thème
//   Échap   quitter
//
//   cpp-maker run example-ui
//   build/host/debug/bin/example-ui --tab=2 --lang=en --theme=light --frames=60 --screenshot=capture.png
#include <app/app.hpp>
#include <ui/builder.hpp>
#include <ui/i18n.hpp>
#include <ui/retained.hpp>
#include <ui/ui.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

// ── Thèmes ──────────────────────────────────────────────────────────────────

struct ThemeDef {
	std::string_view key;	///< identifiant (--theme=…), aussi clé de traduction « theme.<key> »
	std::string_view name;	///< nom dans la langue des sources
	std::string_view css;	///< feuille appliquée par-dessus assets/ui.css
	ui::Theme (*make)();
};
constexpr std::array kThemes = {
	ThemeDef{"midnight", "Nuit", "assets/themes/midnight.css", &app::MidnightTheme},
	ThemeDef{"light", "Clair", "assets/themes/light.css", &app::LightTheme},
	ThemeDef{"contrast", "Contraste élevé", "assets/themes/contrast.css", &app::ContrastTheme},
};

constexpr std::array<std::string_view, 2> kLocales = {"fr", "en"};

/// Phrase traduite de `key`, ou `source` (langue des sources : le français).
std::string T(std::string_view key, std::string_view source) {
	const std::string *found = ui::I18n().Find(key);
	return found ? *found : std::string(source);
}

ui::MenuItem Item(std::string label, std::string shortcut, std::function<void()> action, bool checkable = false, bool checked = false,
				  std::string group = {}) {
	return ui::MenuItem{std::move(label), std::move(shortcut), false, checkable, checked, std::move(group), std::move(action)};
}
const ui::MenuItem kSeparator{"", "", true, false, false, "", nullptr};

class ExampleUi final : public app::Application {
public:
	ExampleUi(int tab, size_t locale, size_t theme) : m_tab(tab), m_locale(locale), m_theme(theme) {}

protected:
	bool OnInit() override {
		BuildData();
		// Interface retenue principale et modal séparé (overlay, arrière-plan gelé).
		m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
		m_ui->SetImmediateContext(&Ui()); // feuilles `Custom(immediate)` : sélecteurs
		m_modalUi = std::make_unique<ui::Ui>(Canvas(), Events());
		for (ui::Ui *u : {m_ui.get(), m_modalUi.get()})
			u->SetTranslator(&ui::I18n()); // textes des widgets traduits par leur Id
		ApplyTheme(m_theme);
		ApplyLocale(m_locale);
		BuildMainUi();
		BuildModalUi();
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		m_dt = float(frameSeconds);
		if (Ui().WantsKeyboard())
			return; // saisie en cours dans un champ : pas de raccourcis
		sdl3::EventsManager &in = Events();
		if (in.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
			RequestQuit();
		if (in.KeyScan(SDL_SCANCODE_L).IsJustPressed())
			ApplyLocale((m_locale + 1) % kLocales.size());
		if (in.KeyScan(SDL_SCANCODE_T).IsJustPressed())
			ApplyTheme((m_theme + 1) % kThemes.size());
	}

	void OnRender(const sdl3::FRect &area, double) override {
		Canvas().FillRect(area, Theme().background);
		m_ui->SetBlocked(m_modalOpen); // la modale gèle l'interface principale
		m_ui->Present(m_dt, area);
		if (m_modalOpen)
			m_modalUi->Present(m_dt, area);
	}

	void TitleButtons(std::vector<ui::FrameButton> &, std::vector<ui::FrameButton> &right) override {
		m_titleLanguage = T("title.language", "Langue : ") + ui::I18n().Language();
		m_titleTheme = T("title.theme", "Thème : ") + T(std::format("theme.{}", kThemes[m_theme].key), kThemes[m_theme].name);
		right.push_back({.id = "lang", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PUBLIC), .label = kLocales[m_locale], .tooltip = m_titleLanguage});
		right.push_back({.id = "theme", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), .tooltip = m_titleTheme});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "lang")
			ApplyLocale((m_locale + 1) % kLocales.size());
		else if (id == "theme")
			ApplyTheme((m_theme + 1) % kThemes.size());
	}

private:
	void Log(std::string message) {
		m_log.push_back(std::move(message));
		if (m_log.size() > 14)
			m_log.erase(m_log.begin());
		Ui().Notify(m_log.back(), Theme().accent, 3.f);
	}

	// ── Thèmes et langues ───────────────────────────────────────────────────
	/// Coches du menu Affichage (langue : items 0-1, thèmes : items 3…) selon l'état courant.
	void SyncViewMenu() {
		if (!m_ui)
			return;
		ui::Widget menu = m_ui->Find("menu");
		if (!menu || menu->menus.size() < 3)
			return;
		std::vector<ui::MenuItem> &items = menu->menus[2].items;
		for (size_t i = 0; i < kLocales.size(); ++i)
			items[i].checked = i == m_locale;
		for (size_t i = 0; i < kThemes.size(); ++i)
			items[3 + i].checked = i == m_theme;
	}

	void ApplyTheme(size_t index) {
		m_theme = index;
		const ThemeDef &def = kThemes[index];
		const ui::Theme theme = def.make();
		Ui().Style() = theme; // interface immédiate et barre de titre
		for (ui::Ui *u : {m_ui.get(), m_modalUi.get()}) {
			u->SetTheme(theme); // couleurs que le CSS ne décrit pas
			u->ClearSheets();
			if (auto base = ui::StyleSheet::Load("assets/ui.css"); base.IsOk())
				u->Sheet(std::move(base.Value()));
			if (auto sheet = ui::StyleSheet::Load(std::string(def.css)); sheet.IsOk())
				u->Sheet(std::move(sheet.Value()));
		}
		SyncViewMenu();
		if (m_ready)
			Log(T("log.theme", "Thème : ") + T(std::format("theme.{}", def.key), def.name));
	}

	void ApplyLocale(size_t index) {
		m_locale = index;
		const std::string path = std::format("examples/example-ui/i18n/{}.json", kLocales[index]);
		if (auto loaded = ui::I18n().LoadFile(path); loaded.IsError()) {
			Log(loaded.Error());
			return;
		}
		SyncViewMenu();
		SetTitle(T("app.title", "example-ui — catalogue complet"));
		if (m_ready)
			Log(T("log.language", "Langue : ") + ui::I18n().Language());
		m_ready = true;
	}

	// ── Données de démonstration (Modèle-Vue + DataViz) ────────────────────
	void BuildData() {
		for (int i = 0; i < 40; ++i)
			m_items.push_back(std::format("{} {}", "Élément", i + 1));

		m_listModel.rowCount = [this] { return m_items.size(); };
		m_listModel.text = [this](size_t row, size_t) { return std::format("{} {}", T("item", "Élément"), row + 1); };

		m_tableModel.rowCount = [this] { return m_items.size(); };
		m_tableModel.columnCount = [] { return size_t(3); };
		m_tableModel.text = [](size_t row, size_t col) {
			if (col == 0)
				return std::format("{} {}", T("item", "Élément"), row + 1);
			if (col == 1)
				return std::format("{}", (row * 7) % 100);
			return std::format("{:.2f}", double(row) * 1.5);
		};

		const sdl3::Color folder = sdl3::Rgba(236, 180, 72), file = sdl3::Rgba(120, 170, 255);
		m_roots = {
			{"src", "src", ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), folder, true,
			 {{"main", "main.cpp", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}},
			  {"ui", "ui", ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), folder, true, {{"builder", "builder.cpp", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}}}},
			  {"app", "app.hpp", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}}}},
			{"readme", "README.md", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}},
			{"assets", "assets", ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), folder, true, {{"ui.css", "ui.css", ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), file, false, {}}}},
		};

		ui::PlotSeries line, area, bars, dots;
		line.name = "sin";
		line.kind = ui::PlotKind::Line;
		line.color = sdl3::Rgba(74, 144, 255);
		area.name = "cos/2";
		area.kind = ui::PlotKind::Area;
		area.color = sdl3::Rgba(96, 200, 120, 120);
		for (int i = 0; i <= 40; ++i) {
			const float x = float(i) / 40.f * 6.2831853f;
			line.points.emplace_back(x, std::sin(x));
			area.points.emplace_back(x, std::cos(x) * 0.5f);
		}
		bars.kind = ui::PlotKind::Bar;
		bars.color = sdl3::Rgba(236, 180, 72);
		for (int i = 0; i < 8; ++i)
			bars.points.emplace_back(float(i), std::fabs(std::sin(float(i) * 0.9f)) * 2.f);
		dots.kind = ui::PlotKind::Scatter;
		dots.color = sdl3::Rgba(222, 76, 82);
		for (int i = 0; i < 20; ++i)
			dots.points.emplace_back(float(i) * 0.5f, std::sin(float(i) * 1.3f));
		m_plot.title = "sin / cos / barres / nuage";
		m_plot.xLabel = "x";
		m_plot.yLabel = "y";
		m_plot.series = {line, area, bars, dots};

		m_pie = {
			{"Nord", 30.f, sdl3::Rgba(74, 144, 255)},
			{"Sud", 20.f, sdl3::Rgba(96, 200, 120)},
			{"Est", 15.f, sdl3::Rgba(236, 180, 72)},
			{"Ouest", 10.f, sdl3::Rgba(222, 76, 82)},
		};

		m_heatCols = 16;
		for (size_t r = 0; r < 12; ++r)
			for (size_t c = 0; c < m_heatCols; ++c)
				m_heat.push_back(std::sin(float(r) * 0.7f) * std::cos(float(c) * 0.5f));
	}

	// ── Interface principale (barre de menus + onglets + fenêtres flottantes) ─
	void BuildMainUi() {
		std::vector<ui::MenuItem> view = {
			Item("Langue : Français", "L", [this] { ApplyLocale(0); }, true, m_locale == 0, "langue"),
			Item("Langue : English", "L", [this] { ApplyLocale(1); }, true, m_locale == 1, "langue"),
			kSeparator,
		};
		for (size_t i = 0; i < kThemes.size(); ++i)
			view.push_back(Item(std::format("Thème : {}", kThemes[i].name), "T", [this, i] { ApplyTheme(i); }, true, m_theme == i, "theme"));
		view.push_back(kSeparator);
		view.push_back(Item("Réinitialiser la vue", "", [this] { m_tab = 0; }));

		m_ui->Root(ui::Column().Gap(6).Children({
			// Barre de menus : actions, séparateurs, cases à cocher, radios (traduits : menu.N.M).
			ui::MenuBar().Id("menu").Menus({
				{"Fichier",
				 {Item("Nouveau", "Ctrl+N", [this] { Log(T("log.new", "Fichier → Nouveau")); }),
				  Item("Ouvrir…", "Ctrl+O", [this] { Log(T("log.open", "Fichier → Ouvrir")); }), kSeparator,
				  Item("Quitter", "Échap", [this] { RequestQuit(); })}},
				{"Édition",
				 {Item("Copier", "Ctrl+C", [this] { Log(T("log.copy", "Édition → Copier")); }),
				  Item("Coller", "Ctrl+V", [this] { Log(T("log.paste", "Édition → Coller")); }), kSeparator,
				  Item("Mode avancé", "", [this] { Log(T("log.advanced", "Mode avancé basculé")); }, true)}},
				{"Affichage", view},
				{"Aide", {Item("À propos…", "F1", [this] { OpenModal(); })}},
			}).Height(30).SlideIn(),
			// Onglets : une page par famille de widgets.
			ui::TabWidget().Id("tabs").Tabs({"Contrôles", "Texte", "Données", "Graphiques", "Mise en page", "Sélecteurs"}).Bind(m_tab).Grow().Children({
				BuildControlsTab(),
				BuildTextTab(),
				BuildDataTab(),
				BuildPlotTab(),
				BuildLayoutTab(),
				BuildPickersTab(),
			}),
			// Fenêtre flottante (SubWindow) : déplaçable, redimensionnable, Z-order.
			ui::SubWindow("Propriétés").Id("props").At(940.f, 500.f).Size(300.f, 190.f).Resizable().Children({
				ui::Text("Catalogue complet du module ui").Id("props.name"),
				ui::Separator(),
				ui::Label("Glissez la barre de titre,\nles bords et les coins.").Id("props.help"),
				ui::Button("Fermer").Id("props.close").OnClick([this] { Log(T("log.closed", "Fenêtre fermée (bouton)")); }),
			}),
			// Popup flottante (fermée au clic extérieur).
			ui::Popup().Id("popup").Ref(m_popup).Hidden().At(480.f, 160.f).Children({
				ui::Text("Info-bulle flottante").Id("popup.title").FontSize(14).Bold(),
				ui::Text("Un clic à l'extérieur la referme.").Id("popup.text"),
			}),
		}));
	}

	ui::WidgetBuilder BuildControlsTab() {
		return ui::Column().Padding(20).Gap(8).Children({
			ui::Text("Catalogue complet du module ui").Id("title").FontSize(20).Bold(),
			ui::Text("Chaque onglet exerce une famille de widgets ; L change la langue, T le thème.").Id("intro").Class("muted"),
			ui::Separator(),
			// Boutons et états.
			ui::Row().Gap(8).Children({
				ui::Button("Bouton").Id("btn").Class("primary").OnClick([this] { Log(T("log.button", "Bouton cliqué")); }),
				ui::ToggleButton("Bascule").Id("toggle").Bind(m_toggle),
				ui::Checkbox("Case").Id("check").Bind(m_checkbox),
				ui::ToolButton(ui::icons::Codepoint(ui::icons::MaterialIcons::SETTINGS)).Id("tool").Tooltip("Réglages (infobulle)"),
				ui::Spacer(),
				ui::Button("Ouvrir le modal").Id("open-modal").OnClick([this] { OpenModal(); }).Tooltip("Ouvre une boîte modale"),
				ui::Button("Popup").Id("popup-btn").OnClick([this] { m_popup->hidden = false; }),
				ui::Button("Supprimer").Id("danger").Class("danger").OnClick([this] { Log(T("log.danger", "Action dangereuse")); }),
			}),
			ui::Row().Gap(8).Children({
				ui::RadioButton("Rouge").Id("red").Group("couleur"),
				ui::RadioButton("Vert").Id("green").Group("couleur"),
				ui::RadioButton("Bleu").Id("blue").Group("couleur"),
			}),
			// Saisie (alignés en haut : la zone multi-lignes est plus haute).
			ui::Row().Gap(8).AlignItems(ui::Align::Start).Children({
				ui::LineEdit().Id("line").Bind(m_line).Placeholder("Saisie sur une ligne…").Width(200),
				ui::LineEdit().Id("password").Password().Placeholder("Mot de passe").Width(160),
				ui::TextArea().Id("area").Bind(m_area).Placeholder("Zone multi-lignes…").Width(220).Height(64),
			}),
			// Valeurs.
			ui::Row().Gap(16).AlignItems(ui::Align::Center).Children({
				ui::Slider(0.f, 1.f).Bind(m_volume).Width(180),
				ui::SpinBox(-10.f, 10.f, 0.5f).Bind(m_spin),
				ui::Knob(0.f, 1.f).Bind(m_knob).Width(48).Height(48),
				ui::ComboBox().Id("combo").Options({"Option A", "Option B", "Option C"}).Bind(m_combo).Width(160),
			}),
			// Progression.
			ui::Row().Gap(8).Children({
				ui::ProgressBar().Bind(m_volume).Width(200),
				ui::ProgressBar().Indeterminate().Width(200),
			}),
			// Glisser-déposer : source et cible.
			ui::Row().Gap(8).Children({
				ui::Button("Source (glisser)").Id("drag").Draggable("élément-démo"),
				ui::Panel("Cible de dépôt").Id("drop").DropTarget([this](std::string p) { Log(T("log.drop", "Dépôt reçu : ") + p); }).Padding(10).Grow(),
			}),
		});
	}

	ui::WidgetBuilder BuildTextTab() {
		return ui::Column().Padding(20).Gap(8).Children({
			ui::Text("Affichage et typographie").Id("text.title").FontSize(20).Bold(),
			ui::Label("Libellé multi-lignes :\n— première ligne\n— deuxième ligne (gras + souligné)").Id("text.label").Bold().Underline(),
			ui::RichTextLabel("Texte enrichi : **gras**, *italique*, _souligné_, `code` et un [lien](https://example.com)")
				.Id("text.rich")
				.OnLink([this](std::string url) { Log(T("log.link", "Lien cliqué : ") + url); }),
			ui::Separator(),
			ui::Row().Gap(12).Children({
				ui::ImageView(ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE)).Width(48).Height(48),
				ui::ImageView(ui::icons::Codepoint(ui::icons::MaterialIcons::MUSIC_NOTE)).Width(48).Height(48),
				ui::ImageView(ui::icons::Codepoint(ui::icons::MaterialIcons::COLORIZE)).Width(48).Height(48),
				ui::Separator().Vertical().Height(48),
				ui::Column().Gap(4).Children({
					ui::Text("Widget personnalisé (Custom) :").Id("text.custom").FontSize(13),
					ui::Custom([this](sdl3::Canvas &c, const sdl3::FRect &r) {
						const ui::Theme &th = Theme();
						c.FillRoundRect(r, 8.f, th.widget);
						c.TextCentered(r, T("text.painted", "dessiné par l'application"), th.text, 13);
					}).Width(260).Height(48),
					ui::Text("Feuille immédiate (Custom immediate) :").Id("text.immediate").FontSize(13),
					ui::Custom([this](ui::Context &u) {
						if (u.Button(T("text.button", "bouton immédiat")))
							Log(T("log.immediate", "Bouton immédiat cliqué"));
					}).Width(220).Height(32),
				}),
			}),
		});
	}

	ui::WidgetBuilder BuildDataTab() {
		return ui::Column().Padding(20).Gap(8).Children({
			ui::Text("Données complexes (Modèle-Vue)").Id("data.title").FontSize(20).Bold(),
			ui::Splitter().Height(300).Children({
				ui::Panel("Liste (virtuelle)").Id("data.list").Child(ui::ListView().Model(m_listModel).MultiSelect().Bind(m_listSel).Grow().OnActivate([this](int i) {
					Log(std::format("{} {}", T("item", "Élément"), i + 1));
				})),
				ui::Splitter().Children({
					ui::Panel("Arborescence").Id("data.tree").Child(ui::TreeView().Nodes(m_roots).Grow().OnSelectId([this](std::string id) { m_treeSel = id; })),
					ui::Panel("Tableau (tri + redimensionnement)").Id("data.table").Child(
						ui::TableView().Id("table").Model(m_tableModel).Headers({"Nom", "Score", "Poids"}).Grow()),
				}),
			}),
		});
	}

	ui::WidgetBuilder BuildPlotTab() {
		return ui::Column().Padding(20).Gap(8).Children({
			ui::Text("Visualisation de données (DataViz)").Id("plot.title").FontSize(20).Bold(),
			ui::Splitter().Height(320).Children({
				ui::Panel("Courbes").Id("plot.curves").Child(ui::Plot().Plot(m_plot).Grow()),
				ui::Splitter().Children({
					ui::Panel("Répartition").Id("plot.pie").Child(ui::PieChart().Pie(m_pie).Grow()),
					ui::Panel("Matrice").Id("plot.heat").Child(ui::Heatmap().Heatmap(m_heat, m_heatCols, -1.f, 1.f).Grow()),
				}),
			}),
		});
	}

	ui::WidgetBuilder BuildLayoutTab() {
		// Zone défilante : un contenu vertical construit en boucle.
		ui::WidgetBuilder scrollContent = ui::Column().Gap(4);
		for (int i = 1; i <= 12; ++i)
			scrollContent.Child(ui::Text(std::format("Ligne {}", i)).Id(std::format("line{}", i)));
		// Grille 2 colonnes via le display grid.
		ui::WidgetBuilder grid = ui::Column().Display(ui::Display::Grid).GridColumns(2).Gap(8).Children({
			ui::Panel("A").Child(ui::Text("cellule A").Id("cell.a")),
			ui::Panel("B").Child(ui::Text("cellule B").Id("cell.b")),
			ui::Panel("C").Child(ui::Text("cellule C").Id("cell.c")),
			ui::Panel("D").Child(ui::Text("cellule D").Id("cell.d")),
		});
		return ui::Column().Padding(20).Gap(8).Children({
			ui::Text("Conteneurs et mises en page").Id("layout.title").FontSize(20).Bold(),
			ui::Expander("Section dépliante (Expander)").Id("layout.expander").Child(
				ui::Label("Contenu repliable.\nCliquez sur l'en-tête pour plier/déplier.").Id("layout.expander.text")),
			ui::Panel("Zone défilante (Scroll)").Id("layout.scroll").Child(ui::Scroll().Height(110).Child(scrollContent)),
			ui::Text("Retour à la ligne (Flow) :").Id("layout.flow"),
			ui::Flow().Gap(6).Children({
				ui::Button("un").Id("n1"), ui::Button("deux").Id("n2"), ui::Button("trois").Id("n3"), ui::Button("quatre").Id("n4"),
				ui::Button("cinq").Id("n5"), ui::Button("six").Id("n6"), ui::Button("sept").Id("n7"),
			}),
			ui::Text("Grille (Grid) :").Id("layout.grid"),
			grid,
			ui::Text("Barre d'onglets autonome (TabBar) :").Id("layout.tabbar"),
			ui::TabBar().Id("subtabs").Tabs({"Premier", "Second", "Troisième"}).Bind(m_subTab).Height(30),
		});
	}

	ui::WidgetBuilder BuildPickersTab() {
		return ui::Column().Padding(20).Gap(10).Children({
			ui::Text("Sélecteurs, menu contextuel et positionnement").Id("pickers.title").FontSize(20).Bold(),
			ui::Row().Gap(14).Children({
				ui::Panel("Couleur (ColorPicker)").Id("pickers.color").Width(400).Child(ui::Custom([this](ui::Context &u) {
					if (u.ColorPicker("example-color", m_color))
						m_colorText = std::format("#{:02X}{:02X}{:02X}", m_color.r, m_color.g, m_color.b);
				}).Height(180)),
				ui::Panel("Date (DatePicker)").Id("pickers.date").Width(300).Child(ui::Custom([this](ui::Context &u) {
					if (u.DatePicker("example-date", m_date))
						Log(std::format("{} {:02}/{:02}/{}", T("log.date", "Date choisie :"), m_date.day, m_date.month, m_date.year));
				}).Height(270)),
				// Zone à menu contextuel, avec une pastille en position absolue (coin bas droit).
				ui::Panel("Menu contextuel").Id("pickers.context").Grow().Height(320)
					.ContextMenu({Item("Actualiser", "F5", [this] { Log(T("log.refresh", "Zone actualisée")); }),
								  Item("Propriétés…", "", [this] { OpenModal(); }), kSeparator,
								  Item("Supprimer", "Suppr", [this] { Log(T("log.danger", "Action dangereuse")); })})
					.Children({
						ui::Label("Clic droit dans ce panneau :\nmenu contextuel au point du clic.").Id("pickers.context.help"),
						ui::Text("position absolue").Id("pickers.badge").Class("accent")
							.Position(ui::Position::Absolute).Right(12.f).Bottom(10.f),
					}),
			}),
		});
	}

	// ── Modal (overlay) ─────────────────────────────────────────────────────
	void BuildModalUi() {
		m_modalUi->Root(ui::Modal().Title("Préférences").Id("prefs").OnClose([this] { m_modalOpen = false; }).Child(
			ui::Column().Gap(8).Children({
				ui::Slider(0.f, 1.f).Bind(m_volume).Width(320),
				ui::Checkbox("Activer le mode avancé").Id("prefs.advanced").Bind(m_checkbox),
				ui::Row().Gap(8).Children({
					ui::Button("OK").Id("prefs.ok").Class("primary").OnClick([this] {
						m_modalOpen = false;
						Log(T("log.prefs", "Préférences validées"));
					}),
					ui::Button("Annuler").Id("prefs.cancel").OnClick([this] { m_modalOpen = false; }),
				}),
			})));
	}

	void OpenModal() { m_modalOpen = true; }

	// ── État ─────────────────────────────────────────────────────────────────
	float m_dt = 0.f;
	int m_tab = 0, m_combo = 0, m_subTab = 0, m_listSel = 0;
	size_t m_locale = 0, m_theme = 0;
	bool m_ready = false, m_toggle = false, m_checkbox = true, m_modalOpen = false;
	float m_volume = 0.6f, m_spin = 3.f, m_knob = 0.5f;
	std::string m_line = "éditable", m_area, m_treeSel, m_colorText, m_titleLanguage, m_titleTheme;
	sdl3::Color m_color = sdl3::Rgba(74, 144, 255);
	ui::Date m_date{2026, 11, 15};
	std::vector<std::string> m_log;

	std::vector<std::string> m_items;
	ui::TableModel m_listModel, m_tableModel;
	std::vector<ui::TreeItem> m_roots;
	ui::PlotData m_plot;
	std::vector<ui::PieSlice> m_pie;
	std::vector<float> m_heat;
	size_t m_heatCols = 0;

	std::unique_ptr<ui::Ui> m_ui;
	std::unique_ptr<ui::Ui> m_modalUi;
	ui::Widget m_popup;
};

/// Index de `value` dans `keys` (repli : 0).
template <typename Range, typename Key> size_t IndexOf(const Range &keys, std::string_view value, Key key) {
	for (size_t i = 0; i < std::size(keys); ++i)
		if (key(keys[i]) == value)
			return i;
	return 0;
}

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "example-ui — catalogue complet";
	config.width = 1280;
	config.height = 800;
	config.minWidth = 900;
	config.minHeight = 560;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::EXTENSION);
	int tab = 0;
	size_t locale = 0, theme = 0;
	for (const std::string &arg : config.ParseArgs(argc, argv)) {
		if (arg.starts_with("--tab="))
			tab = std::clamp(std::atoi(arg.c_str() + 6), 0, 5);
		else if (arg.starts_with("--lang="))
			locale = IndexOf(kLocales, std::string_view(arg).substr(7), [](std::string_view k) { return k; });
		else if (arg.starts_with("--theme="))
			theme = IndexOf(kThemes, std::string_view(arg).substr(8), [](const ThemeDef &d) { return d.key; });
	}
	return ExampleUi(tab, locale, theme).Run(config);
}
