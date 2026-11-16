// Définitions de data/node.hpp
#include "dataio/node.hpp"

namespace dataio {

// ── Node ─────────────────────────────────────────────────────────────────────

Node::Node(NodeType t) : type(t) {}

NodePtr Node::MakeString(String v) {
	auto n = std::make_shared<Node>(NodeType::STRING);
	n->stringValue = std::move(v);
	return n;
}

NodePtr Node::MakeBool(bool v) {
	auto n = std::make_shared<Node>(NodeType::BOOL);
	n->boolValue = v;
	return n;
}

NodePtr Node::MakeInt(int64_t v) {
	auto n = std::make_shared<Node>(NodeType::INT);
	n->intValue = v;
	return n;
}

NodePtr Node::MakeFloat(double v) {
	auto n = std::make_shared<Node>(NodeType::FLOAT);
	n->floatValue = v;
	return n;
}

void Node::Set(const String &key, NodePtr child) {
	const auto [it, inserted] = children.insert_or_assign(key, std::move(child));
	(void)it;
	if (inserted)
		order.push_back(key);
}

NodePtr Node::Get(const String &key) const {
	const auto it = children.find(key);
	return it == children.end() ? nullptr : it->second;
}

void Node::Remove(const String &key) {
	children.erase(key);
	order.erase(std::remove(order.begin(), order.end(), key), order.end());
}

NodePtr Node::Clone() const {
	switch (type) {
		case NodeType::NONE:
			return MakeNone();
		case NodeType::STRING:
			return MakeString(stringValue);
		case NodeType::BOOL:
			return MakeBool(boolValue);
		case NodeType::INT:
			return MakeInt(intValue);
		case NodeType::FLOAT:
			return MakeFloat(floatValue);
		case NodeType::OBJECT: {
			auto n = MakeObject();
			for (auto &k : order)
				n->Set(k, Get(k)->Clone());
			return n;
		}
		case NodeType::ARRAY: {
			auto n = MakeArray();
			for (auto &k : order)
				n->Push(Get(k)->Clone());
			return n;
		}
	}
	return MakeNone();
}

} // namespace dataio
