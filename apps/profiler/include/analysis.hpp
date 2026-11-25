#pragma once
// Profiler — analyse d'une capture `.vcap` : images, zones, fils, compteurs, diagnostic.
#include <cstdint>
#include <string>
#include <vector>

#include <diagnostics/capture.hpp>

namespace profiler {

struct FrameStats {
	size_t count = 0;
	double avgMs = 0, medianMs = 0, p95Ms = 0, p99Ms = 0, maxMs = 0, minMs = 0;
	double overBudgetPercent = 0;
	size_t stutters = 0; // images > 2 × médiane
	std::vector<double> durationsMs;
};

struct ZoneStats {
	std::string name;
	uint64_t calls = 0;
	double totalMs = 0, selfMs = 0, maxMs = 0, minMs = 1e30;
	double AvgMs() const { return calls ? totalMs / double(calls) : 0; }
	double stddevMs = 0;
	double perFrameMs = 0;
};

struct ThreadStats {
	std::string name;
	uint64_t id = 0;
	double busyMs = 0; // somme des zones de profondeur 0
	double busyPercent = 0;
	size_t zones = 0;
};

struct CounterStats {
	std::string name;
	double min = 0, max = 0, avg = 0, last = 0;
	size_t samples = 0;
	std::vector<std::pair<double, double>> series; // (ms, valeur)
};

enum class Severity { Info, Warning, Critical };
struct Finding {
	Severity severity;
	std::string message;
};

struct Report {
	double durationMs = 0;
	double fromMs = 0, toMs = 0;
	double targetFps = 60;
	FrameStats frames;
	std::vector<ZoneStats> zones;	 // triées par temps propre décroissant
	std::vector<ThreadStats> threads;
	std::vector<CounterStats> counters;
	std::vector<Finding> findings; // triés par gravité
};

struct AnalyzeOptions {
	double fromMs = 0, toMs = 0; // 0/0 = toute la capture
	double targetFps = 60;
	std::string thread;			 // filtre (nom ou id), vide = tous
};

/// Nœud de l'arbre des appels d'un fil : zones regroupées par chemin d'appel (même nom sous
/// le même parent), temps cumulés sur la plage analysée.
struct CallNode {
	std::string name;
	uint64_t calls = 0;
	double totalMs = 0, selfMs = 0, minMs = 0, maxMs = 0;
	std::vector<CallNode> children;
	[[nodiscard]] double AvgMs() const { return calls ? totalMs / double(calls) : 0.0; }
};

/// Arbre des appels d'un fil (racines : zones de profondeur 0).
struct CallTree {
	std::string thread;
	uint64_t threadId = 0;
	double busyMs = 0; ///< somme des racines
	std::vector<CallNode> roots;
};

/// Arbres des appels de chaque fil, pour les zones commençant dans [fromMs, toMs] (0/0 : tout).
[[nodiscard]] std::vector<CallTree> BuildCallTrees(const diagnostics::capture::Capture &c, double fromMs = 0, double toMs = 0);

[[nodiscard]] Report Analyze(const diagnostics::capture::Capture &c, const AnalyzeOptions &opt);
[[nodiscard]] std::string FormatReport(const Report &r, size_t top);
[[nodiscard]] std::string CompareReports(const Report &before, const Report &after, size_t top);
[[nodiscard]] std::string ExportChromeTrace(const diagnostics::capture::Capture &c);

} // namespace profiler
