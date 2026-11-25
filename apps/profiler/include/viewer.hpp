#pragma once
#include <string>

#include <app/application.hpp>

namespace profiler {
/// Ouvre le visualiseur (application `app::Application`) sur la capture `path` (vide : la
/// plus récente des dossiers explorés). `config` porte les options communes déjà lues par
/// `AppConfig::ParseArgs` (`--frames`, `--screenshot`, `--wait`, `--resize-test`…).
int RunViewer(app::AppConfig config, const std::string &path);
} // namespace profiler
