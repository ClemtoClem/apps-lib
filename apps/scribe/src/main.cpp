// Scribe — éditeur de documents (moteur engine.document).
//
//   scribe                       accueil (modèles, documents récents)
//   scribe [document.docdb | --template=demo|letter|…|<modèle personnel>] [--page=N]
//          [--lang=fr|en|de|it|zh|ja] [--theme=night|light|contrast]
//          [options des applications : --frames N, --screenshot P.png…]
//   scribe [document.docdb | --template=modèle intégré] --export=sortie.pdf|html|csv   (sans fenêtre)
#include <algorithm>
#include <cstdlib>
#include <string>

#include "scribe/app.hpp"

int main(int argc, char **argv) {
	app::AppConfig config;
	const std::vector<std::string> rest = config.ParseArgs(argc, argv);
	scribe::LaunchOptions options;
	std::string exportPath;
	for (const std::string &arg : rest) {
		if (arg.starts_with("--template="))
			options.templateName = arg.substr(11);
		else if (arg.starts_with("--export="))
			exportPath = arg.substr(9);
		else if (arg.starts_with("--lang="))
			options.language = arg.substr(7);
		else if (arg.starts_with("--theme="))
			options.theme = arg.substr(8);
		else if (arg.starts_with("--page="))
			options.page = size_t(std::max(0L, std::strtol(arg.c_str() + 7, nullptr, 10)));
		else if (!arg.empty() && arg.front() != '-')
			options.path = arg;
	}
	if (!exportPath.empty())
		return scribe::ExportWithoutInterface(options, exportPath);
	return scribe::RunScribe(config, options);
}
