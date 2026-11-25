#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include <core/robin_map.hpp>
#include <dataio/json.hpp>

namespace profiler {

namespace {
double Percentile(std::vector<double> v, double p) {
	if (v.empty())
		return 0;
	std::ranges::sort(v);
	const double idx = p * double(v.size() - 1);
	const size_t lo = size_t(std::floor(idx)), hi = std::min(v.size() - 1, lo + 1);
	return v[lo] + (v[hi] - v[lo]) * (idx - double(lo));
}
const char *SeverityName(Severity s) {
	switch (s) {
	case Severity::Critical: return "CRITIQUE";
	case Severity::Warning: return "attention";
	default: return "info";
	}
}
} // namespace

Report Analyze(const diagnostics::capture::Capture &c, const AnalyzeOptions &opt) {
	Report r;
	r.targetFps = opt.targetFps;
	r.durationMs = double(c.durationNs) / 1e6;
	const int64_t from = int64_t(opt.fromMs * 1e6);
	const int64_t to = opt.toMs > 0 ? int64_t(opt.toMs * 1e6) : c.durationNs;
	r.fromMs = double(from) / 1e6;
	r.toMs = double(to) / 1e6;
	const double windowMs = std::max(1e-3, r.toMs - r.fromMs);

	// ── Images ───────────────────────────────────────────────────────────
	for (size_t i = 1; i < c.frames.size(); ++i)
		if (c.frames[i] >= from && c.frames[i] <= to)
			r.frames.durationsMs.push_back(double(c.frames[i] - c.frames[i - 1]) / 1e6);
	FrameStats &f = r.frames;
	f.count = f.durationsMs.size();
	if (f.count) {
		double sum = 0;
		for (double d : f.durationsMs)
			sum += d;
		f.avgMs = sum / double(f.count);
		f.medianMs = Percentile(f.durationsMs, 0.5);
		f.p95Ms = Percentile(f.durationsMs, 0.95);
		f.p99Ms = Percentile(f.durationsMs, 0.99);
		auto [mn, mx] = std::ranges::minmax(f.durationsMs);
		f.minMs = mn;
		f.maxMs = mx;
		const double budget = 1000.0 / opt.targetFps;
		size_t over = 0;
		for (double d : f.durationsMs) {
			over += d > budget;
			f.stutters += d > 2.0 * f.medianMs;
		}
		f.overBudgetPercent = 100.0 * double(over) / double(f.count);
	}

	// ── Zones (temps total, propre, extrêmes, écart type) et fils ────────
	struct Acc {
		ZoneStats s;
		double sumSq = 0;
	};
	core::RobinMap<uint32_t, Acc> acc;
	for (const auto &t : c.threads) {
		if (!opt.thread.empty() && t.name != opt.thread && std::to_string(t.id) != opt.thread)
			continue;
		ThreadStats ts;
		ts.name = t.name;
		ts.id = t.id;
		// Temps propre = durée − somme des enfants directs (pile de zones ouvertes).
		struct Open {
			const diagnostics::capture::ZoneSpan *z;
			double childMs;
		};
		std::vector<Open> stack;
		auto close = [&](const Open &o) {
			const double dur = double(o.z->end - o.z->begin) / 1e6;
			Acc &a = acc[o.z->name];
			a.s.calls++;
			a.s.totalMs += dur;
			a.s.selfMs += std::max(0.0, dur - o.childMs);
			a.s.maxMs = std::max(a.s.maxMs, dur);
			a.s.minMs = std::min(a.s.minMs, dur);
			a.sumSq += dur * dur;
			if (!stack.empty())
				stack.back().childMs += dur;
		};
		for (const auto &z : t.zones) {
			if (z.end < from || z.begin > to)
				continue;
			while (!stack.empty() && stack.back().z->end <= z.begin) {
				const Open o = stack.back();
				stack.pop_back();
				close(o);
			}
			if (z.depth == 0)
				ts.busyMs += double(std::min(z.end, to) - std::max(z.begin, from)) / 1e6;
			stack.push_back({&z, 0});
			++ts.zones;
		}
		while (!stack.empty()) {
			const Open o = stack.back();
			stack.pop_back();
			close(o);
		}
		ts.busyPercent = 100.0 * ts.busyMs / windowMs;
		r.threads.push_back(std::move(ts));

		// Compteurs.
		for (const auto &e : t.events) {
			if (e.type != diagnostics::trace::EventType::Counter || e.timeNs < from || e.timeNs > to)
				continue;
			const std::string &name = c.Name(e.name);
			auto it = std::ranges::find(r.counters, name, &CounterStats::name);
			if (it == r.counters.end()) {
				r.counters.push_back({name, e.value, e.value, 0, 0, 0, {}});
				it = r.counters.end() - 1;
			}
			it->min = std::min(it->min, e.value);
			it->max = std::max(it->max, e.value);
			it->avg += e.value;
			it->last = e.value;
			it->samples++;
			it->series.emplace_back(double(e.timeNs) / 1e6, e.value);
		}
	}
	for (auto &cs : r.counters)
		cs.avg = cs.samples ? cs.avg / double(cs.samples) : 0;
	for (auto &e : acc) {
		ZoneStats z = e.value.s;
		z.name = c.Name(e.key);
		const double mean = z.AvgMs();
		z.stddevMs = z.calls ? std::sqrt(std::max(0.0, e.value.sumSq / double(z.calls) - mean * mean)) : 0;
		z.perFrameMs = f.count ? z.totalMs / double(f.count) : 0;
		r.zones.push_back(std::move(z));
	}
	std::ranges::sort(r.zones, std::greater<>{}, &ZoneStats::selfMs);
	std::ranges::sort(r.threads, std::greater<>{}, &ThreadStats::busyMs);

	// ── Diagnostic ───────────────────────────────────────────────────────
	const double budget = 1000.0 / opt.targetFps;
	if (f.count == 0)
		r.findings.push_back({Severity::Warning, "aucune image marquée (DIAG_FRAME) dans la fenêtre analysée"});
	if (f.count && f.p95Ms > budget)
		r.findings.push_back({f.p95Ms > 2 * budget ? Severity::Critical : Severity::Warning,
							  std::format("p95 des images {:.2f} ms > budget {:.2f} ms ({:.0f} ips visés)", f.p95Ms, budget, opt.targetFps)});
	if (f.count && f.stutters > f.count / 50)
		r.findings.push_back({Severity::Warning, std::format("{} saccade(s) (images > 2× la médiane)", f.stutters)});
	if (!r.zones.empty() && f.count) {
		const ZoneStats &hot = r.zones.front();
		if (hot.selfMs / double(f.count) > budget * 0.25)
			r.findings.push_back({Severity::Warning, std::format("« {} » consomme {:.2f} ms propres par image ({:.0f} % du budget)", hot.name,
																hot.selfMs / double(f.count), 100 * hot.selfMs / double(f.count) / budget)});
	}
	for (const auto &z : r.zones)
		if (z.calls > 10 && z.maxMs > 8 * std::max(z.AvgMs(), 0.01) && z.maxMs > budget * 0.5)
			r.findings.push_back({Severity::Info, std::format("« {} » : pic {:.2f} ms pour une moyenne {:.3f} ms (variabilité)", z.name, z.maxMs, z.AvgMs())});
	for (const auto &t : r.threads)
		if (t.busyPercent > 90 && t.name != "main")
			r.findings.push_back({Severity::Info, std::format("fil « {} » occupé à {:.0f} %", t.name, t.busyPercent)});
	if (r.findings.empty())
		r.findings.push_back({Severity::Info, "rien d'anormal détecté"});
	std::ranges::stable_sort(r.findings, std::greater<>{}, [](const Finding &x) { return int(x.severity); });
	return r;
}

std::string FormatReport(const Report &r, size_t top) {
	std::string s;
	s += std::format("Capture : {:.1f} ms analysées ({:.1f} → {:.1f} ms)\n\n", r.toMs - r.fromMs, r.fromMs, r.toMs);
	const FrameStats &f = r.frames;
	s += std::format("Images ({}) : moyenne {:.2f} ms ({:.0f} ips) · médiane {:.2f} · p95 {:.2f} · p99 {:.2f} · max {:.2f}\n", f.count, f.avgMs,
					 f.avgMs > 0 ? 1000 / f.avgMs : 0, f.medianMs, f.p95Ms, f.p99Ms, f.maxMs);
	s += std::format("         {:.1f} % au-delà du budget de {:.2f} ms, {} saccade(s)\n\n", f.overBudgetPercent, 1000 / r.targetFps, f.stutters);
	s += "Diagnostic :\n";
	for (const auto &x : r.findings)
		s += std::format("  [{}] {}\n", SeverityName(x.severity), x.message);
	s += std::format("\nZones (par temps propre) — {} au total\n", r.zones.size());
	s += std::format("  {:<34} {:>8} {:>10} {:>10} {:>9} {:>9} {:>9} {:>9}\n", "zone", "appels", "propre ms", "total ms", "moy ms", "max ms",
					 "σ ms", "/image");
	for (size_t i = 0; i < r.zones.size() && i < top; ++i) {
		const ZoneStats &z = r.zones[i];
		s += std::format("  {:<34} {:>8} {:>10.2f} {:>10.2f} {:>9.3f} {:>9.3f} {:>9.3f} {:>9.3f}\n", z.name.substr(0, 34), z.calls, z.selfMs,
						 z.totalMs, z.AvgMs(), z.maxMs, z.stddevMs, z.perFrameMs);
	}
	s += "\nFils\n";
	for (const auto &t : r.threads)
		s += std::format("  {:<20} #{:<3} occupé {:>9.2f} ms ({:5.1f} %), {} zones\n", t.name, t.id, t.busyMs, t.busyPercent, t.zones);
	if (!r.counters.empty()) {
		s += "\nCompteurs\n";
		for (const auto &cs : r.counters)
			s += std::format("  {:<28} min {:>10.1f}  moy {:>10.1f}  max {:>10.1f}  dernier {:>10.1f}\n", cs.name, cs.min, cs.avg, cs.max, cs.last);
	}
	return s;
}

std::string CompareReports(const Report &a, const Report &b, size_t top) {
	std::string s = "Comparaison (avant → après)\n";
	auto line = [&](const char *label, double x, double y) {
		const double pct = x > 0 ? 100.0 * (y - x) / x : 0;
		s += std::format("  {:<16} {:>9.2f} → {:>9.2f} ms  ({:+.1f} %)\n", label, x, y, pct);
	};
	line("image moyenne", a.frames.avgMs, b.frames.avgMs);
	line("image médiane", a.frames.medianMs, b.frames.medianMs);
	line("image p95", a.frames.p95Ms, b.frames.p95Ms);
	line("image max", a.frames.maxMs, b.frames.maxMs);
	s += "\nZones (temps propre par image)\n";
	std::map<std::string, std::pair<double, double>> rows;
	for (const auto &z : a.zones)
		rows[z.name].first = a.frames.count ? z.selfMs / double(a.frames.count) : z.selfMs;
	for (const auto &z : b.zones)
		rows[z.name].second = b.frames.count ? z.selfMs / double(b.frames.count) : z.selfMs;
	std::vector<std::pair<std::string, std::pair<double, double>>> sorted(rows.begin(), rows.end());
	std::ranges::sort(sorted, [](const auto &x, const auto &y) {
		return std::fabs(x.second.second - x.second.first) > std::fabs(y.second.second - y.second.first);
	});
	for (size_t i = 0; i < sorted.size() && i < top; ++i) {
		const auto &[name, v] = sorted[i];
		s += std::format("  {:<34} {:>9.3f} → {:>9.3f} ms  ({:+.3f})\n", name.substr(0, 34), v.first, v.second, v.second - v.first);
	}
	return s;
}

std::string ExportChromeTrace(const diagnostics::capture::Capture &c) {
	// Format « Trace Event » (chrome://tracing, ui.perfetto.dev) : événements complets « X ».
	dataio::NodePtr events = dataio::Node::MakeArray();
	for (const auto &t : c.threads) {
		dataio::NodePtr meta = dataio::Node::MakeObject();
		meta->Set("name", dataio::Node::MakeString("thread_name"));
		meta->Set("ph", dataio::Node::MakeString("M"));
		meta->Set("pid", dataio::Node::MakeInt(1));
		meta->Set("tid", dataio::Node::MakeInt(int64_t(t.id)));
		{
			dataio::NodePtr args = dataio::Node::MakeObject();
			args->Set("name", dataio::Node::MakeString(t.name));
			meta->Set("args", std::move(args));
		}
		events->Push(std::move(meta));
		for (const auto &z : t.zones) {
			dataio::NodePtr e = dataio::Node::MakeObject();
			e->Set("name", dataio::Node::MakeString(c.Name(z.name)));
			e->Set("ph", dataio::Node::MakeString("X"));
			e->Set("pid", dataio::Node::MakeInt(1));
			e->Set("tid", dataio::Node::MakeInt(int64_t(t.id)));
			e->Set("ts", dataio::Node::MakeFloat(double(z.begin) / 1000.0));
			e->Set("dur", dataio::Node::MakeFloat(double(z.end - z.begin) / 1000.0));
			events->Push(std::move(e));
		}
		for (const auto &ev : t.events) {
			if (ev.type != diagnostics::trace::EventType::Counter)
				continue;
			dataio::NodePtr e = dataio::Node::MakeObject();
			e->Set("name", dataio::Node::MakeString(c.Name(ev.name)));
			e->Set("ph", dataio::Node::MakeString("C"));
			e->Set("pid", dataio::Node::MakeInt(1));
			e->Set("ts", dataio::Node::MakeFloat(double(ev.timeNs) / 1000.0));
			{
				dataio::NodePtr args = dataio::Node::MakeObject();
				args->Set("value", dataio::Node::MakeFloat(ev.value));
				e->Set("args", std::move(args));
			}
			events->Push(std::move(e));
		}
	}
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("traceEvents", std::move(events));
	root->Set("displayTimeUnit", dataio::Node::MakeString("ms"));
	return dataio::WriteJson(root, -1);
}

std::vector<CallTree> BuildCallTrees(const diagnostics::capture::Capture &c, double fromMs, double toMs) {
	const bool ranged = toMs > fromMs;
	std::vector<CallTree> trees;
	for (const auto &t : c.threads) {
		CallTree tree;
		tree.thread = t.name;
		tree.threadId = t.id;
		// Zones triées par début (puis profondeur) : intervalles imbriqués, une pile d'ancêtres.
		struct Open {
			int64_t end;
			CallNode *node;
		};
		std::vector<Open> stack;
		const auto childNamed = [](std::vector<CallNode> &list, const std::string &name) -> CallNode & {
			for (CallNode &n : list)
				if (n.name == name)
					return n;
			CallNode created;
			created.name = name;
			list.push_back(std::move(created));
			return list.back();
		};
		for (const auto &z : t.zones) {
			const double beginMs = double(z.begin) / 1e6;
			if (ranged && (beginMs < fromMs || beginMs > toMs))
				continue;
			while (!stack.empty() && (z.begin >= stack.back().end || z.depth < stack.size()))
				stack.pop_back();
			// Ajouter un enfant ne déplace que les frères de la zone : la pile (ancêtres) reste valide.
			CallNode &node = childNamed(stack.empty() ? tree.roots : stack.back().node->children, c.Name(z.name));
			const double ms = double(z.end - z.begin) / 1e6;
			node.minMs = node.calls ? std::min(node.minMs, ms) : ms;
			node.maxMs = std::max(node.maxMs, ms);
			node.totalMs += ms;
			++node.calls;
			stack.push_back({z.end, &node});
		}
		// Temps propre : total moins le total des enfants.
		const auto finish = [](auto &self, std::vector<CallNode> &list) -> double {
			double sum = 0;
			for (CallNode &n : list) {
				n.selfMs = std::max(0.0, n.totalMs - self(self, n.children));
				sum += n.totalMs;
			}
			return sum;
		};
		tree.busyMs = finish(finish, tree.roots);
		if (!tree.roots.empty())
			trees.push_back(std::move(tree));
	}
	return trees;
}

} // namespace profiler
