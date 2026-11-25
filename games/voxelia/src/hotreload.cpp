// Rechargement à chaud : surveillance des fichiers des mods (cf. app.hpp, HotReload).
#include <sdl3/sdl3.hpp>

#include "app.hpp"

namespace voxelia {

void HotReload::Update(const voxel::ModManager &mods, jobs::ThreadPool &pool) {
	if (m_scan.Poll()) {
		ScanResult &r = *m_scan.Get();
		if (m_primed) {
			auto append = [](std::vector<fs::path> &dst, std::vector<fs::path> &src) { dst.insert(dst.end(), src.begin(), src.end()); };
			append(m_pending.scripts, r.changes.scripts);
			append(m_pending.definitions, r.changes.definitions);
			append(m_pending.textures, r.changes.textures);
			append(m_pending.models, r.changes.models);
		}
		m_known = std::move(r.snapshot);
		m_primed = true;
		++m_scans;
	}
	const uint64_t now = sdl3::GetTicksMS();
	if (!enabled || m_scan.Loading() || now < m_next)
		return;
	m_next = now + 1000;
	std::vector<fs::path> roots;
	for (const voxel::ModInfo &m : mods.Mods())
		if (m.enabled)
			roots.push_back(m.directory);
	m_scan.Request(
		pool,
		[roots = std::move(roots), previous = m_known](std::stop_token stop) {
			ScanResult r;
			for (const fs::path &root : roots) {
				std::error_code ec;
				for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec && !stop.stop_requested(); it.increment(ec)) {
					if (it->is_directory(ec)) {
						const std::string name = it->path().filename().string();
						if (name == "sounds" || name == "tools" || name.starts_with('.'))
							it.disable_recursion_pending(); // sons, outils de génération : non surveillés
						continue;
					}
					const fs::path &p = it->path();
					const std::string ext = p.extension().string();
					if (ext != ".script" && ext != ".json" && ext != ".png" && ext != ".obj" && ext != ".b3d")
						continue;
					const auto time = it->last_write_time(ec);
					if (ec)
						continue;
					r.snapshot.emplace(p, time);
					const auto old = previous.find(p);
					if (old != previous.end() && old->second == time)
						continue;
					if (old == previous.end() && previous.empty())
						continue; // première analyse : état de référence
					const std::string rel = fs::relative(p, root, ec).generic_string();
					if (ext == ".script")
						r.changes.scripts.push_back(p);
					else if (ext == ".png")
						r.changes.textures.push_back(p);
					else if (rel.starts_with("models/") && ext == ".json")
						r.changes.models.push_back(p);
					else
						r.changes.definitions.push_back(p);
				}
			}
			return r;
		},
		jobs::Priority::Low);
}

FileChanges HotReload::Take() { return std::exchange(m_pending, {}); }

} // namespace voxelia
