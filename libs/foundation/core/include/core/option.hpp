#pragma once
/**
 * @file option.hpp
 * @brief `Option<T>` — valeur optionnelle au vocabulaire explicite (`Some`, `NONE`)
 *        construite directement sur `std::optional<T>` (aucun stockage maison).
 */
#include <concepts>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

template <typename T, typename E> class Result;

struct NoneType {
	constexpr NoneType() noexcept = default;
};
inline constexpr NoneType NONE{};

/// Porteur de valeur pour construire un `Option` engagé : `return Some(v);`
template <typename T> struct Some {
	T value;
	constexpr explicit Some(T v) : value(std::move(v)) {}
	template <typename... Args>
	constexpr explicit Some(std::in_place_t, Args &&...args) : value(std::forward<Args>(args)...) {}
};
template <typename T> Some(T) -> Some<T>;

template <typename T> class Option {
	static_assert(!std::is_reference_v<T>, "Option<T&> interdit : utilisez Option<std::reference_wrapper<T>>");
	std::optional<T> m_value;

public:
	using ValueType = T;

	constexpr Option() noexcept = default;
	constexpr Option(NoneType) noexcept {}
	constexpr Option(std::nullopt_t) noexcept {}
	constexpr Option(std::optional<T> v) noexcept(std::is_nothrow_move_constructible_v<T>) : m_value(std::move(v)) {}

	template <typename U>
		requires std::constructible_from<T, U &&>
	constexpr Option(Some<U> &&s) : m_value(std::in_place, std::move(s.value)) {}
	template <typename U>
		requires std::constructible_from<T, const U &>
	constexpr Option(const Some<U> &s) : m_value(std::in_place, s.value) {}

	template <typename... Args>
	constexpr explicit Option(std::in_place_t, Args &&...args) : m_value(std::in_place, std::forward<Args>(args)...) {}

	[[nodiscard]] constexpr bool IsSome() const noexcept { return m_value.has_value(); }
	[[nodiscard]] constexpr bool IsNone() const noexcept { return !m_value.has_value(); }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_value.has_value(); }

	[[nodiscard]] constexpr const T &Value() const & noexcept { return *m_value; }
	[[nodiscard]] constexpr T &Value() & noexcept { return *m_value; }
	[[nodiscard]] constexpr T &&Value() && noexcept { return std::move(*m_value); }
	[[nodiscard]] constexpr const T &ValueUnchecked() const noexcept { return *m_value; }
	[[nodiscard]] constexpr T &ValueUnchecked() noexcept { return *m_value; }

	[[nodiscard]] constexpr const T &operator*() const & noexcept { return *m_value; }
	[[nodiscard]] constexpr T &operator*() & noexcept { return *m_value; }
	[[nodiscard]] constexpr const T *operator->() const noexcept { return &*m_value; }
	[[nodiscard]] constexpr T *operator->() noexcept { return &*m_value; }

	[[nodiscard]] constexpr T Unwrap() && { return std::move(*m_value); }
	[[nodiscard]] constexpr T Unwrap() const & { return *m_value; }
	[[nodiscard]] constexpr T UnwrapOr(T fallback) const & { return m_value ? *m_value : std::move(fallback); }
	[[nodiscard]] constexpr T UnwrapOr(T fallback) && { return m_value ? std::move(*m_value) : std::move(fallback); }
	template <typename Fn> [[nodiscard]] constexpr T UnwrapOrElse(Fn &&fn) const & {
		return m_value ? *m_value : std::invoke(std::forward<Fn>(fn));
	}

	template <typename Fn> [[nodiscard]] constexpr auto Map(Fn &&fn) const & -> Option<std::invoke_result_t<Fn, const T &>> {
		if (m_value)
			return Option<std::invoke_result_t<Fn, const T &>>(std::in_place, std::invoke(std::forward<Fn>(fn), *m_value));
		return NONE;
	}
	template <typename Fn> [[nodiscard]] constexpr auto AndThen(Fn &&fn) const & -> std::invoke_result_t<Fn, const T &> {
		if (m_value)
			return std::invoke(std::forward<Fn>(fn), *m_value);
		return NONE;
	}
	template <typename Pred> [[nodiscard]] constexpr Option Filter(Pred &&pred) const & {
		return (m_value && std::invoke(std::forward<Pred>(pred), *m_value)) ? *this : Option{};
	}
	template <typename SomeFn, typename NoneFn> [[nodiscard]] constexpr auto Match(SomeFn &&s, NoneFn &&n) const & {
		return m_value ? std::invoke(std::forward<SomeFn>(s), *m_value) : std::invoke(std::forward<NoneFn>(n));
	}

	template <typename E> [[nodiscard]] Result<T, E> OkOr(E err) const &;

	template <typename... Args> constexpr T &Emplace(Args &&...args) { return m_value.emplace(std::forward<Args>(args)...); }
	constexpr void Reset() noexcept { m_value.reset(); }

	/// Accès direct au `std::optional` sous-jacent (interop std).
	[[nodiscard]] constexpr const std::optional<T> &Std() const noexcept { return m_value; }
	[[nodiscard]] constexpr std::optional<T> &Std() noexcept { return m_value; }

	[[nodiscard]] constexpr bool operator==(const Option &o) const
		requires std::equality_comparable<T>
	{
		return m_value == o.m_value;
	}
	[[nodiscard]] constexpr bool operator==(NoneType) const noexcept { return IsNone(); }
};

template <typename T> [[nodiscard]] constexpr Option<std::decay_t<T>> MakeSome(T &&v) {
	return Option<std::decay_t<T>>(std::in_place, std::forward<T>(v));
}
