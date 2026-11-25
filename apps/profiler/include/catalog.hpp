#pragma once
// Profiler — catalogue des captures : dossiers explorés, captures récentes, fichiers associés
// (rapport d'incident et journal écrits par diagnostics::session).
#include <filesystem>
#include <string>
#include <vector>

namespace profiler {

/// Dossiers explorés : captures enregistrées par `profiler record`
/// (~/.local/share/apps-lib/profiler/captures) et diagnostics des applications
/// (`diagnostics::session::DefaultDirectory()`, ~/.local/share/apps-lib/diagnostics).
[[nodiscard]] std::vector<std::filesystem::path> CaptureDirectories();
/// Captures `.vcap` de ces dossiers, la plus récente d'abord.
[[nodiscard]] std::vector<std::filesystem::path> FindCaptures();
/// Fichiers écrits à côté d'une capture par une session de diagnostic (vides s'ils manquent).
struct Companions {
	std::filesystem::path incident; ///< `.crash.txt` : plantage ou blocage
	std::filesystem::path log;		///< `.log` : journal de la session
};
[[nodiscard]] Companions CompanionsOf(const std::filesystem::path &capture);
/// Contenu d'un fichier texte (vide s'il manque) ; `maxBytes` : seulement la fin.
[[nodiscard]] std::string ReadText(const std::filesystem::path &path, size_t maxBytes = 1 << 20);

} // namespace profiler
