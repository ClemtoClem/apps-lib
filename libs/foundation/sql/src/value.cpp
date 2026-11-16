#include "sql/value.hpp"

#include <charconv>
#include <format>

namespace sql {

int64_t Value::AsInt(int64_t def) const noexcept {
	switch (m_v.index()) {
	case 1: return std::get<int64_t>(m_v);
	case 2: return static_cast<int64_t>(std::get<double>(m_v));
	case 3: {
		const std::string &s = std::get<std::string>(m_v);
		int64_t v = def;
		std::from_chars(s.data(), s.data() + s.size(), v);
		return v;
	}
	default: return def;
	}
}

double Value::AsReal(double def) const noexcept {
	switch (m_v.index()) {
	case 1: return static_cast<double>(std::get<int64_t>(m_v));
	case 2: return std::get<double>(m_v);
	case 3: {
		const std::string &s = std::get<std::string>(m_v);
		double v = def;
		std::from_chars(s.data(), s.data() + s.size(), v);
		return v;
	}
	default: return def;
	}
}

std::string_view Value::AsText() const noexcept {
	if (const auto *s = std::get_if<std::string>(&m_v))
		return *s;
	return {};
}

std::span<const uint8_t> Value::AsBlob() const noexcept {
	if (const auto *b = std::get_if<Blob>(&m_v))
		return *b;
	return {};
}

std::string Value::ToString() const {
	switch (m_v.index()) {
	case 0: return "NULL";
	case 1: return std::format("{}", std::get<int64_t>(m_v));
	case 2: return std::format("{}", std::get<double>(m_v));
	case 3: return std::get<std::string>(m_v);
	default: return std::format("<blob {} o>", std::get<Blob>(m_v).size());
	}
}

std::weak_ordering Value::Compare(const Value &o) const noexcept {
	const auto rank = [](const Value &v) {
		switch (v.m_v.index()) {
		case 0: return 0;
		case 1:
		case 2: return 1;
		case 3: return 2;
		default: return 3;
		}
	};
	const int ra = rank(*this), rb = rank(o);
	if (ra != rb)
		return ra <=> rb;
	switch (ra) {
	case 0: return std::weak_ordering::equivalent;
	case 1:
		if (m_v.index() == 1 && o.m_v.index() == 1)
			return std::get<int64_t>(m_v) <=> std::get<int64_t>(o.m_v);
		{
			const double a = AsReal(), b = o.AsReal();
			return a < b ? std::weak_ordering::less : (a > b ? std::weak_ordering::greater : std::weak_ordering::equivalent);
		}
	case 2: return std::get<std::string>(m_v).compare(std::get<std::string>(o.m_v)) <=> 0;
	default: {
		const Blob &a = std::get<Blob>(m_v), &b = std::get<Blob>(o.m_v);
		return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
	}
	}
}

std::string_view TypeName(ColumnType t) noexcept {
	switch (t) {
	case ColumnType::Null: return "NULL";
	case ColumnType::Int: return "INT";
	case ColumnType::Real: return "REAL";
	case ColumnType::Text: return "TEXT";
	case ColumnType::Blob: return "BLOB";
	}
	return "?";
}

} // namespace sql
