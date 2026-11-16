#include "sql/btree.hpp"

#include <algorithm>

namespace sql {

BTreeIndex::BTreeIndex() { Clear(); }
BTreeIndex::~BTreeIndex() = default;
BTreeIndex::BTreeIndex(BTreeIndex &&) noexcept = default;
BTreeIndex &BTreeIndex::operator=(BTreeIndex &&) noexcept = default;

void BTreeIndex::Clear() {
	m_nodes.clear();
	auto leaf = std::make_unique<Leaf>();
	m_root = leaf.get();
	m_nodes.push_back(std::move(leaf));
	m_height = 0;
	m_size = 0;
}

const BTreeIndex::Leaf *BTreeIndex::FindLeaf(int64_t key, uint32_t row) const {
	const Key k{key, row};
	const Node *n = m_root;
	while (!n->isLeaf) {
		const auto *in = static_cast<const Inner *>(n);
		// premier séparateur > k  → enfant correspondant
		const int i = static_cast<int>(std::upper_bound(in->keys, in->keys + in->count - 1, k) - in->keys);
		n = in->children[i];
	}
	return static_cast<const Leaf *>(n);
}

void BTreeIndex::InsertRec(Node *node, int depth, const Key &k, Key &splitKey, Node *&splitNode) {
	splitNode = nullptr;
	if (node->isLeaf) {
		auto *leaf = static_cast<Leaf *>(node);
		Key *pos = std::lower_bound(leaf->keys, leaf->keys + leaf->count, k);
		if (pos != leaf->keys + leaf->count && *pos == k)
			return; // déjà présent
		++m_size;
		if (leaf->count < kFanout) {
			std::move_backward(pos, leaf->keys + leaf->count, leaf->keys + leaf->count + 1);
			*pos = k;
			++leaf->count;
			return;
		}
		// Découpe : moitié droite dans une nouvelle feuille.
		Key tmp[kFanout + 1];
		const auto at = pos - leaf->keys;
		std::copy(leaf->keys, pos, tmp);
		tmp[at] = k;
		std::copy(pos, leaf->keys + kFanout, tmp + at + 1);
		auto fresh = std::make_unique<Leaf>();
		constexpr int left = (kFanout + 1) / 2;
		leaf->count = left;
		std::copy(tmp, tmp + left, leaf->keys);
		fresh->count = kFanout + 1 - left;
		std::copy(tmp + left, tmp + kFanout + 1, fresh->keys);
		fresh->next = leaf->next;
		leaf->next = fresh.get();
		splitKey = fresh->keys[0];
		splitNode = fresh.get();
		m_nodes.push_back(std::move(fresh));
		return;
	}
	auto *in = static_cast<Inner *>(node);
	const int i = static_cast<int>(std::upper_bound(in->keys, in->keys + in->count - 1, k) - in->keys);
	Key childSplit;
	Node *childNode = nullptr;
	InsertRec(in->children[i], depth + 1, k, childSplit, childNode);
	if (!childNode)
		return;
	// Insère (childSplit, childNode) après children[i].
	if (in->count < kFanout) {
		std::move_backward(in->keys + i, in->keys + in->count - 1, in->keys + in->count);
		std::move_backward(in->children + i + 1, in->children + in->count, in->children + in->count + 1);
		in->keys[i] = childSplit;
		in->children[i + 1] = childNode;
		++in->count;
		return;
	}
	Key keys[kFanout];
	Node *kids[kFanout + 1];
	std::copy(in->keys, in->keys + i, keys);
	keys[i] = childSplit;
	std::copy(in->keys + i, in->keys + kFanout - 1, keys + i + 1);
	std::copy(in->children, in->children + i + 1, kids);
	kids[i + 1] = childNode;
	std::copy(in->children + i + 1, in->children + kFanout, kids + i + 2);
	// kFanout+1 enfants, kFanout clés : gauche garde `left` enfants.
	constexpr int left = (kFanout + 1) / 2;
	auto fresh = std::make_unique<Inner>();
	in->count = left;
	std::copy(kids, kids + left, in->children);
	std::copy(keys, keys + left - 1, in->keys);
	splitKey = keys[left - 1];
	fresh->count = kFanout + 1 - left;
	std::copy(kids + left, kids + kFanout + 1, fresh->children);
	std::copy(keys + left, keys + kFanout, fresh->keys);
	splitNode = fresh.get();
	m_nodes.push_back(std::move(fresh));
}

void BTreeIndex::Insert(int64_t key, uint32_t row) {
	Key splitKey;
	Node *splitNode = nullptr;
	InsertRec(m_root, 0, Key{key, row}, splitKey, splitNode);
	if (splitNode) {
		auto root = std::make_unique<Inner>();
		root->count = 2;
		root->children[0] = m_root;
		root->children[1] = splitNode;
		root->keys[0] = splitKey;
		m_root = root.get();
		m_nodes.push_back(std::move(root));
		++m_height;
	}
}

bool BTreeIndex::Erase(int64_t key, uint32_t row) {
	auto *leaf = const_cast<Leaf *>(FindLeaf(key, row));
	const Key k{key, row};
	// La clé peut se trouver dans une feuille suivante si des feuilles vides précèdent.
	for (; leaf; leaf = leaf->next) {
		Key *pos = std::lower_bound(leaf->keys, leaf->keys + leaf->count, k);
		if (pos != leaf->keys + leaf->count) {
			if (!(*pos == k))
				return false;
			std::move(pos + 1, leaf->keys + leaf->count, pos);
			--leaf->count;
			--m_size;
			return true;
		}
	}
	return false;
}

bool BTreeIndex::LowerBound(int64_t key, int64_t &outKey) const {
	for (const Leaf *leaf = FindLeaf(key, 0); leaf; leaf = leaf->next) {
		const Key *pos = std::lower_bound(leaf->keys, leaf->keys + leaf->count, Key{key, 0});
		if (pos != leaf->keys + leaf->count) {
			outKey = pos->key;
			return true;
		}
	}
	return false;
}

} // namespace sql
