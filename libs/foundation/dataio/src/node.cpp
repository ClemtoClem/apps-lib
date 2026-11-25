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

// ── Lectures tolérantes ──────────────────────────────────────────────────────

bool Node::AsBool(bool def) const noexcept { return IsBool() ? boolValue : def; }

double Node::AsNumber(double def) const noexcept {
	if (IsInt())
		return static_cast<double>(intValue);
	if (IsFloat())
		return floatValue;
	return def;
}

int64_t Node::AsInt(int64_t def) const noexcept {
	if (IsInt())
		return intValue;
	if (IsFloat())
		return static_cast<int64_t>(floatValue);
	return def;
}

StringView Node::AsString(StringView def) const noexcept { return IsString() ? stringValue.View() : def; }

std::vector<NodePtr> Node::AsArray() const {
	std::vector<NodePtr> out;
	if (IsArray())
		for (const String &k : order)
			out.push_back(Get(k));
	return out;
}

std::vector<std::pair<String, NodePtr>> Node::AsObject() const {
	std::vector<std::pair<String, NodePtr>> out;
	if (IsObject())
		for (const String &k : order)
			out.emplace_back(k, Get(k));
	return out;
}

// ── Accès tolérants (nœud nul partagé) ───────────────────────────────────────

namespace {
[[nodiscard]] const NodePtr &NullNode() {
	static const NodePtr n = Node::MakeNone();
	return n;
}
} // namespace

NodePtr Node::operator[](StringView key) const {
	if (const auto it = children.find(String(key)); it != children.end())
		return it->second;
	return NullNode();
}

NodePtr Node::operator[](size_t index) const {
	if (index < order.size())
		return Get(order[index]);
	return NullNode();
}

NodePtr Node::At(StringView dottedPath) const {
	const Node *cur = this;
	NodePtr last = nullptr;
	while (!dottedPath.IsEmpty()) {
		const size_t dot = dottedPath.Find('.');
		const StringView segment = dottedPath.Substr(0, dot);
		if (!cur)
			return NullNode();
		last = cur->Get(String(segment));
		if (!last)
			return NullNode();
		cur = last.get();
		dottedPath = dot == StringView::NPOS ? StringView{} : dottedPath.Substr(dot + 1);
	}
	return last ? last : NullNode();
}

// ── Fusion / égalité ─────────────────────────────────────────────────────────

void Node::MergeFrom(const NodePtr &other) {
	if (!other)
		return;
	if (!IsObject() || !other->IsObject()) {
		*this = *other->Clone(); // remplacement profond
		return;
	}
	for (const String &k : other->Keys()) {
		const NodePtr mine = Get(k), theirs = other->Get(k);
		if (mine && mine->IsObject() && theirs && theirs->IsObject())
			mine->MergeFrom(theirs);
		else
			Set(k, theirs->Clone());
	}
}

bool Node::Equals(const NodePtr &other) const {
	if (!other || type != other->type)
		return false;
	switch (type) {
	case NodeType::NONE:
		return true;
	case NodeType::STRING:
		return stringValue == other->stringValue;
	case NodeType::BOOL:
		return boolValue == other->boolValue;
	case NodeType::INT:
		return intValue == other->intValue;
	case NodeType::FLOAT:
		return floatValue == other->floatValue;
	case NodeType::ARRAY:
		if (GetSize() != other->GetSize())
			return false;
		for (size_t i = 0; i < GetSize(); ++i)
			if (!At(i)->Equals(other->At(i)))
				return false;
		return true;
	case NodeType::OBJECT:
		if (GetSize() != other->GetSize())
			return false;
		for (const String &k : order) {
			const NodePtr a = Get(k), b = other->Get(k);
			if (!b || !a->Equals(b))
				return false;
		}
		return true;
	}
	return false;
}

} // namespace dataio
