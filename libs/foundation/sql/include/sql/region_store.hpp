#pragma once
/**
 * @file region_store.hpp
 * @brief Magasin persistant de blobs **partitionné par région**, indexé par clé de
 *        Morton 3D (chunks, entités sauvegardées, événements spatialisés).
 *
 * Partitionnement
 * - Une région regroupe 2^regionBits chunks par axe : `regionKey = key >> (3·regionBits)`.
 *   Grâce à l'ordre Z, les chunks d'une même région sont contigus dans l'espace des clés.
 * - Un fichier par région (`r.<regionKey en hexa>.vreg`), ouvert à la demande, avec
 *   son propre verrou : des régions différentes se lisent/écrivent en parallèle.
 *
 * Format d'un fichier (journal en ajout seul)
 * - en-tête `VREG` + version ;
 * - enregistrements `[magic u32][clé u64][taille u32][crc32 u32][octets]` ;
 *   taille 0xFFFFFFFF = suppression. Le dernier enregistrement d'une clé gagne.
 * - À l'ouverture, un balayage reconstruit l'index mémoire `RobinMap<clé, (offset, taille)>` ;
 *   un enregistrement tronqué (arrêt brutal) est ignoré.
 * - Compaction automatique quand l'espace mort dépasse 50 % (réécriture + renommage atomique).
 *
 * Asynchronisme : `LoadAsync` / `SaveAsync` / `EraseAsync` s'exécutent sur le pool
 * de threads (chargements en priorité haute, sauvegardes en priorité basse) et
 * livrent les résultats via callback (appelé sur le thread du pool).
 */
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <core/robin_map.hpp>
#include <jobs/jobs.hpp>

namespace sql {

class RegionStore {
public:
	struct Stats {
		size_t regionsOpen = 0;
		size_t records = 0;
		uint64_t liveBytes = 0;
		uint64_t deadBytes = 0;
		uint64_t reads = 0;
		uint64_t writes = 0;
		uint64_t compactions = 0;
	};

	/// `regionBits` = log2 du nombre de chunks par axe et par région (4 → 16³ chunks).
	RegionStore(std::filesystem::path directory, int regionBits = 4, jobs::ThreadPool *pool = nullptr);
	~RegionStore();
	RegionStore(const RegionStore &) = delete;
	RegionStore &operator=(const RegionStore &) = delete;

	// Synchrone, thread-safe.
	[[nodiscard]] std::optional<std::vector<uint8_t>> Load(uint64_t key);
	bool Save(uint64_t key, std::span<const uint8_t> data);
	bool Erase(uint64_t key);
	[[nodiscard]] bool Contains(uint64_t key);
	/// Clés présentes dans la région contenant `key`.
	[[nodiscard]] std::vector<uint64_t> KeysInRegion(uint64_t key);

	// Asynchrone (pool de threads).
	using LoadCallback = std::move_only_function<void(uint64_t key, std::optional<std::vector<uint8_t>> data)>;
	void LoadAsync(uint64_t key, LoadCallback cb, jobs::Priority priority = jobs::Priority::High);
	void SaveAsync(uint64_t key, std::vector<uint8_t> data, std::move_only_function<void(uint64_t, bool)> done = {});
	void EraseAsync(uint64_t key);
	/// Attend toutes les opérations asynchrones en cours.
	void Flush();
	[[nodiscard]] size_t PendingOperations() const noexcept { return static_cast<size_t>(m_pending.Pending()); }

	/// Ferme les fichiers de régions (ils seront rouverts à la demande).
	void CloseAll();
	void CompactAll();
	[[nodiscard]] Stats GetStats();
	[[nodiscard]] uint64_t RegionOf(uint64_t key) const noexcept { return key >> (3 * m_regionBits); }
	[[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_dir; }

private:
	struct Entry {
		uint64_t offset;
		uint32_t size;
	};
	struct Region {
		std::mutex mutex;
		std::fstream file;
		std::filesystem::path path;
		core::RobinMap<uint64_t, Entry> index;
		uint64_t fileSize = 0;
		uint64_t deadBytes = 0;
		bool opened = false;
	};

	Region &GetRegion(uint64_t regionKey);
	bool OpenLocked(Region &r);
	bool AppendLocked(Region &r, uint64_t key, std::span<const uint8_t> data, bool tombstone);
	void CompactLocked(Region &r);

	std::filesystem::path m_dir;
	int m_regionBits;
	jobs::ThreadPool *m_pool;
	std::mutex m_mapMutex;
	core::RobinMap<uint64_t, std::unique_ptr<Region>> m_regions;
	jobs::WaitGroup m_pending;
	std::atomic<uint64_t> m_reads{0}, m_writes{0}, m_compactions{0};
};

/**
 * Énumère les intervalles de clés de Morton 3D couvrant exactement la boîte
 * [min, max] (coordonnées entières incluses), en découpant l'octree implicite.
 * Chaque intervalle [lo, hi] est contigu en ordre Z : idéal pour un parcours de
 * B+arbre (`BTreeIndex::Range`) ou un regroupement par région.
 * `maxRanges` borne le découpage (les derniers intervalles peuvent déborder de la
 * boîte : filtrer par décodage si une couverture exacte est nécessaire).
 */
void MortonBoxRanges(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ,
					 std::vector<std::pair<uint64_t, uint64_t>> &out, size_t maxRanges = 64);

} // namespace sql
