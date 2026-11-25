// Définitions des valeurs du langage de script (Value, ClassDef, Instance, ScriptError).
#include "script/script.hpp"
#include "internal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

#include <core/string.hpp>

namespace script {

std::string NumberToString(double d) {
	if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15)
		return std::format("{}", static_cast<int64_t>(d));
	return std::format("{}", d);
}

bool Value::Truthy() const noexcept {
	switch (m_v.index()) {
	case 0: return false;
	case 1: return std::get<bool>(m_v);
	default: return true; // 0 et "" sont vrais (comme Lua) : seuls nil et false sont faux
	}
}
double Value::AsNumber(double def) const noexcept {
	if (const double *d = std::get_if<double>(&m_v))
		return *d;
	if (const bool *b = std::get_if<bool>(&m_v))
		return *b ? 1.0 : 0.0;
	if (const std::string *s = std::get_if<std::string>(&m_v)) {
		double v = def;
		const auto t = StringView(*s).Trim();
		std::from_chars(t.data(), t.data() + t.size(), v);
		return v;
	}
	return def;
}
std::string_view Value::AsString() const noexcept {
	if (const std::string *s = std::get_if<std::string>(&m_v))
		return *s;
	return {};
}
List *Value::AsList() const noexcept {
	if (const auto *l = std::get_if<std::shared_ptr<List>>(&m_v))
		return l->get();
	return nullptr;
}
Map *Value::AsMap() const noexcept {
	if (const auto *m = std::get_if<std::shared_ptr<Map>>(&m_v))
		return m->get();
	return nullptr;
}
Value Value::Get(std::string_view key) const {
	if (Map *m = AsMap())
		if (auto it = m->find(key); it != m->end())
			return it->second;
	return {};
}
std::string_view Value::TypeName() const noexcept {
	static constexpr std::string_view names[] = {"nil", "bool", "number", "string", "list", "map", "function", "function", "class", "object", "future"};
	return names[m_v.index()];
}
std::string Value::ToString() const {
	switch (m_v.index()) {
	case 0: return "nil";
	case 1: return std::get<bool>(m_v) ? "true" : "false";
	case 2: return NumberToString(std::get<double>(m_v));
	case 3: return std::get<std::string>(m_v);
	case 4: {
		std::string s = "[";
		const List &l = *std::get<std::shared_ptr<List>>(m_v);
		for (size_t i = 0; i < l.size(); ++i) {
			if (i)
				s += ", ";
			s += l[i].IsString() ? "\"" + l[i].ToString() + "\"" : l[i].ToString();
		}
		return s + "]";
	}
	case 5: {
		std::string s = "{";
		bool first = true;
		for (const auto &[k, v] : *std::get<std::shared_ptr<Map>>(m_v)) {
			s += std::format("{}{}: {}", first ? "" : ", ", k, v.IsString() ? "\"" + v.ToString() + "\"" : v.ToString());
			first = false;
		}
		return s + "}";
	}
	case 8: return std::format("<classe {}>", std::get<8>(m_v)->name);
	case 9: return std::format("<{}>", std::get<9>(m_v)->cls ? std::get<9>(m_v)->cls->name : std::string("objet"));
	case 10: {
		const Future &f = *std::get<10>(m_v);
		switch (f.GetState()) {
		case Future::State::Pending: return "<future en cours>";
		case Future::State::Done: return std::format("<future : {}>", f.value.ToString());
		default: return std::format("<future en échec : {}>", f.error);
		}
	}
	default: return "<function>";
	}
}
bool Value::Equals(const Value &o) const noexcept {
	if (m_v.index() != o.m_v.index())
		return false;
	switch (m_v.index()) {
	case 0: return true;
	case 1: return std::get<bool>(m_v) == std::get<bool>(o.m_v);
	case 2: return std::get<double>(m_v) == std::get<double>(o.m_v);
	case 3: return std::get<std::string>(m_v) == std::get<std::string>(o.m_v);
	case 4: return std::get<4>(m_v) == std::get<4>(o.m_v);
	case 5: return std::get<5>(m_v) == std::get<5>(o.m_v);
	case 6: return std::get<6>(m_v) == std::get<6>(o.m_v);
	case 7: return std::get<7>(m_v) == std::get<7>(o.m_v);
	case 8: return std::get<8>(m_v) == std::get<8>(o.m_v);
	case 9: return std::get<9>(m_v) == std::get<9>(o.m_v); // identité
	default: return std::get<10>(m_v) == std::get<10>(o.m_v); // future : identité
	}
}

ClassDef *Value::AsClass() const noexcept {
	const auto *c = std::get_if<std::shared_ptr<ClassDef>>(&m_v);
	return c ? c->get() : nullptr;
}
Instance *Value::AsInstance() const noexcept {
	const auto *o = std::get_if<std::shared_ptr<Instance>>(&m_v);
	return o ? o->get() : nullptr;
}
std::shared_ptr<Instance> Value::InstancePtr() const noexcept {
	const auto *o = std::get_if<std::shared_ptr<Instance>>(&m_v);
	return o ? *o : nullptr;
}
Future *Value::AsFuture() const noexcept {
	const auto *f = std::get_if<std::shared_ptr<Future>>(&m_v);
	return f ? f->get() : nullptr;
}
std::shared_ptr<Future> Value::FuturePtr() const noexcept {
	const auto *f = std::get_if<std::shared_ptr<Future>>(&m_v);
	return f ? *f : nullptr;
}

bool ClassDef::Derives(std::string_view other) const {
	if (name == other)
		return true;
	if (parent && parent->Derives(other))
		return true;
	if (std::ranges::any_of(natives, [&](const std::shared_ptr<ClassDef> &n) { return n->Derives(other); }))
		return true;
	return std::ranges::any_of(interfaces, [&](const std::shared_ptr<ClassDef> &i) { return i->Derives(other); });
}

std::vector<const ClassDef *> ClassDef::AllNatives() const {
	std::vector<const ClassDef *> out;
	if (native)
		out.push_back(this);
	for (const ClassDef *c = this; c; c = c->parent.get())
		for (const auto &n : c->natives)
			if (std::ranges::find(out, n.get()) == out.end())
				out.push_back(n.get());
	return out;
}

std::vector<const ClassDef *> ClassDef::AllInterfaces() const {
	std::vector<const ClassDef *> out;
	auto add = [&](const auto &self, const ClassDef &c) -> void {
		for (const auto &i : c.interfaces)
			if (std::ranges::find(out, i.get()) == out.end()) {
				out.push_back(i.get());
				self(self, *i);
			}
	};
	for (const ClassDef *c = this; c; c = c->parent.get())
		for (const auto &i : c->interfaces)
			if (std::ranges::find(out, i.get()) == out.end()) {
				out.push_back(i.get());
				add(add, *i);
			}
	return out;
}

void Instance::SetState(const ClassDef *native, std::shared_ptr<void> state) {
	for (auto &[c, s] : states)
		if (c == native) {
			s = std::move(state);
			return;
		}
	states.emplace_back(native, std::move(state));
}

std::string ScriptError::Format() const {
	if (!file.empty())
		return std::format("{}:{}:{}: {}", file, line, column, message);
	return file.empty() ? message : std::format("{}: {}", file, message);
}

std::string FormatValues(std::string_view fmt, std::span<const Value> args) {
	std::string out;
	size_t next = 0;
	for (size_t i = 0; i < fmt.size(); ++i) {
		if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
			out += next < args.size() ? args[next].ToString() : "{}";
			++next;
			++i;
		} else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
			out.push_back('{');
			++i;
		} else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
			out.push_back('}');
			++i;
		} else {
			out.push_back(fmt[i]);
		}
	}
	return out;
}

} // namespace script
