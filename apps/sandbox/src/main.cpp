// sandbox — modeleur 3D à la SketchUp sur apps-lib (refonte de la sandbox de SDL3_Wrapper).
//
//   cpp-maker run sandbox [-- FICHIER.sandbox | DOSSIER_DE_PROJET] [--empty] [--tool=r] [--view=top] [--tab=N]
//                          [--select=NOM] [--open-group=NOM] [--shadows] [--code | --workspace=overlay|script] [--import=F.gltf] [--frames=N --screenshot=F.png]
#include <string>

#include "sandbox/app/sandbox_app.hpp"

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Sandbox";
	config.width = 1440;
	config.height = 880;
	config.minWidth = 900;
	config.minHeight = 600;
	config.customFrame = true;
	config.gpu = true;
	config.audio = true;				 // sons du mode Jeu
	config.clock.stepSeconds = 1.0 / 120.0; // pas de simulation du mode Jeu
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR);
	sandbox::LaunchOptions launch;
	for (const std::string &arg : config.ParseArgs(argc, argv)) {
		const auto value = [&](std::string_view key) { return arg.substr(key.size()); };
		if (arg == "--empty")
			launch.home = false;
		else if (arg == "--sample")
			launch.sample = true;
		else if (arg == "--code")
			launch.workspace = "script";
		else if (arg.starts_with("--workspace="))
			launch.workspace = value("--workspace=");
		else if (arg.starts_with("--import="))
			launch.import = value("--import=");
		else if (arg.starts_with("--tool="))
			launch.tool = value("--tool=");
		else if (arg.starts_with("--view="))
			launch.view = value("--view=");
		else if (arg.starts_with("--tab="))
			launch.tab = std::atoi(value("--tab=").c_str());
		else if (arg.starts_with("--select="))
			launch.selectGroup = value("--select=");
		else if (arg.starts_with("--open-group="))
			launch.openGroup = value("--open-group=");
		else if (arg == "--play")
			launch.play = true;
		else if (arg == "--shadows")
			launch.shadows = true;
		else if (!arg.starts_with("--"))
			launch.open = arg;
	}
	sandbox::RegisterIcons();
	return sandbox::SandboxApp(std::move(launch)).Run(config);
}
