#pragma once
/**
 * @file btree.hpp
 * @brief B+arbre en mémoire, clés `int64` → identifiants de lignes `uint32`.
 *
 * - Les doublons sont gérés en indexant le couple (clé, ligne), unique.
 * - Nœuds de 64 entrées (≈ 1 Ko) : bonne localité de cache, arbre peu profond
 *   (3 niveaux pour 250 000 entrées).
 * - Feuilles chaînées : un parcours de plage est un balayage séquentiel.
 * - Suppression paresseuse (pas de fusion de nœuds) : correcte et rapide ; une
 *   feuille vide est simplement sautée lors des parcours.
 *
 * Usage typique : index secondaire, clés de Morton (requêtes spatiales par plage).
 */
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sql {

class BTreeIndex {
public:
	static constexpr int kFanout = 64;

	BTreeIndex();
	~BTreeIndex();
	BTreeIndex(BTreeIndex &&) noexcept;
	BTreeIndex &operator=(BTreeIndex &&) noexcept;
	BTreeIndex(const BTreeIndex &) = delete;

	void Insert(int64_t key, uint32_t row);
	bool Erase(int64_t key, uint32_t row);
	void Clear();
	[[nodiscard]] size_t Size() const noexcept { return m_size; }

	/// Appelle `fn(key, row)` pour toutes les entrées de [lo, hi] dans l'ordre croissant.
	/// `fn` retourne `false` pour arrêter.
	template <typename Fn> void Range(int64_t lo, int64_t hi, Fn &&fn) const {
		const Leaf *leaf = FindLeaf(lo, 0);
		while (leaf) {
			for (int i = 0; i < leaf->count; ++i) {
				const Key &k = leaf->keys[i];
				if (k.key < lo)
					continue;
				if (k.key > hi)
					return;
				if (!fn(k.key, k.row))
					return;
			}
			leaf = leaf->next;
		}
	}
	/// Parcours décroissant de [lo, hi].
	template <typename Fn> void RangeReverse(int64_t lo, int64_t hi, Fn &&fn) const {
		std::vector<Key> tmp;
		Range(lo, hi, [&](int64_t k, uint32_t r) {
			tmp.push_back({k, r});
			return true;
		});
		for (auto it = tmp.rbegin(); it != tmp.rend(); ++it)
			if (!fn(it->key, it->row))
				return;
	}
	/// Première clé ≥ `key` (pour sauter dans un parcours Morton BIGMIN).
	[[nodiscard]] bool LowerBound(int64_t key, int64_t &outKey) const;

private:
	struct Key {
		int64_t key;
		uint32_t row;
		bool operator<(const Key &o) const noexcept { return key < o.key || (key == o.key && row < o.row); }
		bool operator==(const Key &o) const noexcept { return key == o.key && row == o.row; }
	};
	struct Node {
		bool isLeaf;
		int count = 0;
		explicit Node(bool leaf) : isLeaf(leaf) {}
		virtual ~Node() = default;
	};
	struct Leaf : Node {
		Key keys[kFanout];
		Leaf *next = nullptr;
		Leaf() : Node(true) {}
	};
	struct Inner : Node {
		Key keys[kFanout - 1]; // keys[i] = plus petite clé du sous-arbre children[i + 1]
		Node *children[kFanout];
		Inner() : Node(false) {}
	};

	[[nodiscard]] const Leaf *FindLeaf(int64_t key, uint32_t row) const;
	void InsertRec(Node *node, int depth, const Key &k, Key &splitKey, Node *&splitNode);

	Node *m_root = nullptr;
	int m_height = 0; // 0 : la racine est une feuille
	size_t m_size = 0;
	std::vector<std::unique_ptr<Node>> m_nodes; // propriété de tous les nœuds
};

} // namespace sql
