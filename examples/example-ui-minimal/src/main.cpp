// example-ui-minimal — le strict nécessaire pour une interface retenue :
// `app::Application` (fenêtre, barre de titre, boucle), un arbre de widgets décrit une
// fois par la fabrique (`ui::Column`, `ui::Slider`…), des valeurs liées (`Bind`) et des
// fonctions de rappel (`OnClick`). Pour tous les widgets, voir example-ui.
//
//   cpp-maker run example-ui-minimal
#include <format>
#include <memory>

#include <app/app.hpp>
#include <ui/builder.hpp>
#include <ui/retained.hpp>

namespace {

class Minimal final : public app::Application {
protected:
	bool OnInit() override {
		m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
		if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
			m_ui->Sheet(std::move(sheet.Value()));
		// Une carte centrée : titre, curseur lié à m_volume, bouton compteur, bascule.
		m_ui->Root(ui::Box().Children({
			ui::Column()
				.Class("panel")
				.Position(ui::Position::Absolute)
				.Anchor(ui::Anchor::Center)
				.Width(380.f)
				.Gap(12.f)
				.Padding(20.f)
				.Children({
					ui::Text("Bonjour !").FontSize(22.f).Bold(),
					ui::Text("Une carte, un curseur, un bouton.").Class("muted"),
					ui::Separator(),
					ui::Row().Gap(12.f).AlignItems(ui::Align::Center).Children({
						ui::Text("Volume").Width(70.f),
						ui::Slider(0.f, 100.f).Bind(m_volume).Grow().Tooltip("Glisser ou utiliser la molette"),
						ui::Text("").Ref(m_volumeText).Width(36.f).TextAlign(ui::TextAlign::Right),
					}),
					ui::Row().Gap(12.f).AlignItems(ui::Align::Center).Children({
						ui::Button("Cliquez-moi").Class("primary").Grow().OnClick([this] {
							++m_count;
							m_clicks.SetText(std::format("{} clic{}", m_count, m_count > 1 ? "s" : ""));
						}),
						ui::ToggleButton("Option").Bind(m_option).Tooltip("Un bouton basculant"),
					}),
					ui::Text("Aucun clic").Ref(m_clicks).Class("muted"),
				}),
		}));
		return true;
	}

	void OnUpdate(double) override {
		if (Events().KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
			RequestQuit();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		m_volumeText.SetText(std::format("{:.0f}", m_volume));
		m_ui->Present(FrameSeconds(), area);
	}

private:
	std::unique_ptr<ui::Ui> m_ui;
	ui::Widget m_clicks, m_volumeText;
	float m_volume = 50.f;
	bool m_option = true;
	int m_count = 0;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Interface retenue — exemple minimal";
	config.width = 900;
	config.height = 600;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::DASHBOARD);
	config.ParseArgs(argc, argv);
	return Minimal().Run(config);
}
