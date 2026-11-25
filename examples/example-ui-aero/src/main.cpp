// example-ui-aero — thème « verre fumé » (Frutiger Aero) sur l'interface retenue.
//
// Regroupe les trois démonstrations Aero de SDL3_Wrapper en une application à onglets :
//   Contrôles  boutons, bascule, case, curseur, champ numérique, molette, progression et
//              sélecteur de couleur (widget immédiat intégré par ui::Custom) ;
//   Listes     liste à sélection multiple, arborescence, tableau triable, graphique ;
//   Bureau     barre de menus, bureau avec menu contextuel (clic droit), fenêtres
//              internes (calendrier, panneau divisé) et boîte modale « À propos ».
// Le bureau en dégradé est dessiné par l'application ; le verre vient de la feuille CSS.
//
//   cpp-maker run example-ui-aero
//   build/host/debug/bin/example-ui-aero --tab=2      # ouvre l'onglet « Bureau »
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <ui/builder.hpp>
#include <ui/retained.hpp>

namespace {

/// Verre fumé : panneaux translucides, texte clair, accent cyan.
constexpr const char *kAeroCss = R"css(
:root { color: #f2f7ff; font-size: 14px; }
.glass {
	background-color: #ffffff1f;
	border: 1px solid #ffffff4d;
	border-radius: 12px;
	padding: 16px;
	gap: 12px;
}
.caption { color: #cfe3ff; width: 130px; }
.title { font-size: 18px; font-weight: 700; color: #ffffff; }
.status { color: #e2efff; font-size: 12px; padding: 0 10px; background-color: #ffffff1c; border-radius: 8px; }
button {
	padding: 7px 16px;
	border-radius: 8px;
	background-color: #ffffff2e;
	border: 1px solid #ffffff59;
	color: #ffffff;
}
button:hover { background-color: #ffffff47; }
button:active { background-color: #5cc8ff73; }
button.primary { background-color: #2f9be6; border: 1px solid #8fd3ff; }
button.primary:hover { background-color: #48adf0; }
toggle { padding: 7px 16px; border-radius: 8px; background-color: #ffffff2e; border: 1px solid #ffffff59; color: #ffffff; }
toggle:checked { background-color: #2f9be6; border: 1px solid #8fd3ff; }
)css";

ui::Theme AeroTheme() {
	ui::Theme t = app::MidnightTheme();
	t.background = sdl3::Rgba(18, 52, 98);
	t.title = sdl3::Rgba(16, 44, 84, 210);
	t.panel = sdl3::Rgba(20, 50, 92, 235);
	t.panelBorder = sdl3::Rgba(120, 180, 240, 110);
	t.accent = sdl3::Rgba(64, 176, 240);
	t.accentHover = sdl3::Rgba(110, 200, 250);
	t.widget = sdl3::Rgba(255, 255, 255, 40);
	t.widgetHover = sdl3::Rgba(255, 255, 255, 64);
	t.widgetActive = sdl3::Rgba(92, 200, 255, 110);
	t.textDim = sdl3::Rgba(190, 214, 245);
	return t;
}

const std::vector<std::string> kFruits = {"Pomme", "Banane", "Cerise", "Datte", "Figue", "Goyave", "Kiwi", "Mangue", "Myrtille", "Orange"};

struct Player {
	std::string name;
	int score, level;
};
const std::vector<Player> kPlayers = {{"Alice", 87, 12}, {"Bob", 64, 9}, {"Chloé", 95, 15}, {"David", 72, 10},
									  {"Élise", 81, 11}, {"Farid", 58, 7}, {"Gaëlle", 90, 14}};

class AeroDesktop final : public app::Application {
public:
	explicit AeroDesktop(int tab) : m_tab(tab) {}

protected:
	bool OnInit() override {
		m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
		m_ui->SetImmediateContext(&Ui()); // feuilles ui::Custom(Context&) : sélecteurs
		m_ui->SetTheme(AeroTheme());		// couleurs que le CSS ne décrit pas (slider, case, tableau…)
		if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
			m_ui->Sheet(std::move(sheet.Value()));
		m_ui->Sheet(ui::StyleSheet::Parse(kAeroCss, "aero"));
		m_ui->Root(ui::Column().Padding(14.f).Gap(10.f).Children({
			ui::TabWidget().Tabs({"Contrôles", "Listes", "Bureau"}).Bind(m_tab).Grow().Children({
				ControlsPage(),
				ListsPage(),
				DesktopPage(),
			}),
			ui::Text("Prêt.").Ref(m_status).Class("status"),
			// Boîte modale « À propos », masquée jusqu'à son ouverture.
			ui::Modal()
				.Title("À propos")
				.Width(380.f)
				.Hidden()
				.Ref(m_about)
				.OnClose([this] { m_about->hidden = true; })
				.Children({
					ui::Text("Bureau Aero").Class("title"),
					ui::Label("Démonstration du thème verre fumé :\nfenêtres internes, menus, calendrier\net sélecteur de couleur."),
					ui::Button("Fermer").Class("primary").OnClick([this] { m_about->hidden = true; }),
				}),
		}));
		return true;
	}

	void OnUpdate(double) override {
		if (Events().KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed()) {
			if (m_about && !m_about->hidden)
				m_about->hidden = true;
			else
				RequestQuit();
		}
	}

	void OnRender(const sdl3::FRect &area, double) override {
		DrawDesktop(area);
		m_ui->Present(FrameSeconds(), area);
	}

	void TitleButtons(std::vector<ui::FrameButton> &, std::vector<ui::FrameButton> &right) override {
		right.push_back({.id = "about", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::INFO), .tooltip = "À propos"});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "about")
			m_about->hidden = false;
	}

private:
	void Status(std::string text) { m_status.SetText(std::move(text)); }

	/// Bureau : dégradé de ciel et halos lumineux (signature Frutiger Aero).
	void DrawDesktop(const sdl3::FRect &area) {
		sdl3::Canvas &c = Canvas();
		c.FillGradientV(area, sdl3::Rgba(22, 74, 140), sdl3::Rgba(96, 164, 222));
		const float t = float(FrameCount()) * 0.004f;
		const struct {
			float x, y, r;
			sdl3::Color color;
		} halos[] = {{0.18f, 0.25f, 0.30f, sdl3::Rgba(140, 220, 255, 26)},
					 {0.80f, 0.70f, 0.38f, sdl3::Rgba(120, 255, 210, 20)},
					 {0.55f, 0.10f, 0.22f, sdl3::Rgba(255, 255, 255, 22)}};
		for (const auto &h : halos) {
			const float cx = area.x + area.w * (h.x + 0.02f * std::sin(t + h.r * 10.f)), cy = area.y + area.h * h.y;
			// Douze disques superposés de rayon décroissant : halo doux, plus dense au centre.
			for (int i = 12; i >= 1; --i)
				c.FillCircle(cx, cy, area.h * h.r * float(i) / 12.f, sdl3::WithAlpha(h.color, uint8_t(h.color.a / 3)));
		}
	}

	ui::WidgetBuilder Caption(std::string_view text) { return ui::Text(text).Class("caption"); }

	ui::WidgetBuilder ControlsPage() {
		return ui::Row().Gap(14.f).Padding(14.f).Children({
			ui::Column().Class("glass").Grow().Children({
				ui::Text("Contrôles").Class("title"),
				ui::Row().Gap(10.f).AlignItems(ui::Align::Center).Children({
					Caption("Boutons"),
					ui::Button("Cliquez-moi").Class("primary").OnClick([this] { Status(std::format("Clic n° {}", ++m_clicks)); }),
					ui::ToggleButton("Bascule").Bind(m_toggle),
					ui::Checkbox("Option").Bind(m_check),
				}),
				ui::Row().Gap(10.f).AlignItems(ui::Align::Center).Children({
					Caption("Volume"),
					ui::Slider(0.f, 100.f).Bind(m_volume).Grow().OnChange([this](float v) { Status(std::format("Volume : {:.0f} %", v)); }),
				}),
				ui::Row().Gap(10.f).AlignItems(ui::Align::Center).Children({
					Caption("Quantité"),
					ui::SpinBox(0.f, 10.f, 1.f).Bind(m_quantity).Width(120.f),
					Caption("Intensité").Width(80.f),
					ui::Knob(0.f, 1.f).Bind(m_knob).Width(44.f).Height(44.f),
				}),
				ui::Row().Gap(10.f).AlignItems(ui::Align::Center).Children({
					Caption("Progression"),
					ui::ProgressBar().Bind(m_progress).Grow().Height(10.f),
				}),
			}),
			ui::Column().Class("glass").Width(380.f).Children({
				ui::Text("Couleur").Class("title"),
				ui::Custom([this](ui::Context &u) {
					if (u.ColorPicker("aero-color", m_color))
						Status(std::format("Couleur : #{:02X}{:02X}{:02X}", m_color.r, m_color.g, m_color.b));
				}).Height(180.f),
			}),
		});
	}

	ui::WidgetBuilder ListsPage() {
		ui::TableModel fruits;
		fruits.rowCount = [] { return kFruits.size(); };
		fruits.text = [](size_t row, size_t) { return kFruits[row]; };
		ui::TableModel players;
		players.rowCount = [] { return kPlayers.size(); };
		players.columnCount = [] { return size_t(3); };
		players.text = [](size_t row, size_t col) {
			const Player &p = kPlayers[row];
			return col == 0 ? p.name : std::to_string(col == 1 ? p.score : p.level);
		};
		const sdl3::Color folder = sdl3::Rgba(255, 210, 110), file = sdl3::Rgba(170, 215, 255);
		std::vector<ui::TreeItem> tree = {
			{"docs", "Documents", ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), folder, true,
			 {{"docs/rapport", "rapport.pdf", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}},
			  {"docs/notes", "notes.txt", ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), file, false, {}}}},
			{"img", "Images", ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), folder, true,
			 {{"img/1", "photo1.png", ui::icons::Codepoint(ui::icons::MaterialIcons::INSERT_DRIVE_FILE), file, false, {}}, {"img/2", "photo2.png", ui::icons::Codepoint(ui::icons::MaterialIcons::INSERT_DRIVE_FILE), file, false, {}}}},
		};
		ui::PlotSeries progress;
		progress.name = "Progression";
		progress.color = sdl3::Rgba(92, 210, 255);
		progress.kind = ui::PlotKind::Area;
		const float samples[] = {3.f, 5.f, 2.f, 8.f, 6.f, 9.f, 4.f, 7.f, 10.f, 8.f};
		for (size_t i = 0; i < std::size(samples); ++i)
			progress.points.emplace_back(float(i), samples[i]);
		ui::PlotData plot;
		plot.title = "Progression hebdomadaire";
		plot.series = {progress};

		return ui::Row().Gap(14.f).Padding(14.f).Children({
			ui::Column().Class("glass").Width(260.f).Children({
				ui::Text("Fruits (Ctrl / Maj + clic)").Class("caption").Width(ui::Length::Auto()),
				ui::ListView().Model(fruits).MultiSelect().Height(190.f).OnSelect([this](int i) { Status("Fruit : " + kFruits[size_t(i)]); }),
				ui::Text("Arborescence").Class("caption"),
				ui::TreeView().Nodes(tree).Grow().OnSelectId([this](std::string id) { Status("Fichier : " + id); }),
			}),
			ui::Column().Class("glass").Grow().Children({
				ui::Text("Joueurs (clic sur un en-tête : tri)").Class("caption").Width(ui::Length::Auto()),
				ui::TableView().Model(players).Headers({"Nom", "Score", "Niveau"}).Height(200.f),
				ui::Plot().Plot(plot).Grow(),
			}),
		});
	}

	ui::WidgetBuilder DesktopPage() {
		const auto item = [](std::string label, std::string shortcut, std::function<void()> action) {
			return ui::MenuItem{std::move(label), std::move(shortcut), false, false, false, "", std::move(action)};
		};
		const ui::MenuItem separator{"", "", true, false, false, "", nullptr};
		return ui::Column().Children({
			ui::MenuBar().Menus({
				{"Fichier",
				 {item("Nouveau", "Ctrl+N", [this] { Status("Nouveau document"); }), item("Ouvrir…", "Ctrl+O", [this] { Status("Ouvrir…"); }),
				  separator, item("Quitter", "Échap", [this] { RequestQuit(); })}},
				{"Aide", {item("À propos", "", [this] { m_about->hidden = false; })}},
			}),
			ui::Box()
				.Grow()
				.ContextMenu({item("Actualiser", "F5", [this] { Status("Bureau actualisé"); }),
							  item("À propos", "", [this] { m_about->hidden = false; })})
				.Children({
					ui::Text("Clic droit sur le bureau : menu contextuel. Les fenêtres se déplacent par leur titre.").Class("status"),
				}),
			ui::SubWindow("Agenda").At(40.f, 170.f).Size(300.f, 330.f).Resizable().Children({
				ui::Custom([this](ui::Context &u) {
					if (u.DatePicker("agenda", m_date))
						Status(std::format("Rendez-vous le {:02}/{:02}/{}", m_date.day, m_date.month, m_date.year));
				}).Height(270.f),
			}),
			ui::SubWindow("Panneau divisé").At(380.f, 200.f).Size(440.f, 230.f).Resizable().Children({
				ui::Splitter().Height(170.f).Children({
					ui::Panel("Gauche").Child(ui::Label("Faites glisser\nla poignée centrale.")),
					ui::Panel("Droite").Child(ui::Label("Les deux panneaux\nse partagent la place.")),
				}),
			}),
		});
	}

	std::unique_ptr<ui::Ui> m_ui;
	ui::Widget m_status, m_about;
	int m_tab = 0, m_clicks = 0; ///< onglet affiché (--tab=N au lancement)
	bool m_toggle = true, m_check = false;
	float m_volume = 60.f, m_quantity = 3.f, m_knob = 0.4f, m_progress = 0.65f;
	sdl3::Color m_color = sdl3::Rgba(64, 176, 240);
	ui::Date m_date{2026, 11, 15};
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Bureau Aero";
	config.width = 1100;
	config.height = 720;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::WORKSPACES);
	config.theme = AeroTheme();
	int tab = 0;
	for (const std::string &arg : config.ParseArgs(argc, argv))
		if (arg.starts_with("--tab="))
			tab = std::clamp(std::atoi(arg.c_str() + 6), 0, 2);
	return AeroDesktop(tab).Run(config);
}
