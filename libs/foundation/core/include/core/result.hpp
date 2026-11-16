#pragma once
/**
 * @file result.hpp
 * @brief `Result<T, E>` — résultat faillible au vocabulaire `Ok(...)` / `Err(...)`,
 *        stocké dans un `std::expected<T, E>` (C++23). Aucune exception.
 */
#include <concepts>
#include <expected>
#include <functional>
#include <type_traits>
#include <utility>

#include "option.hpp"

template <typename... Ts> struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

template <typename T> struct Ok {
	T value;
	constexpr explicit Ok(T v) : value(std::move(v)) {}
	template <typename... Args>
	constexpr explicit Ok(std::in_place_t, Args &&...args) : value(std::forward<Args>(args)...) {}
};
template <typename T> Ok(T) -> Ok<T>;

template <typename E> struct Err {
	E error;
	constexpr explicit Err(E e) : error(std::move(e)) {}
	template <typename... Args>
	constexpr explicit Err(std::in_place_t, Args &&...args) : error(std::forward<Args>(args)...) {}
};
template <typename E> Err(E) -> Err<E>;

template <typename T, typename E> class Result {
	static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>, "Result<T&,...> interdit");
	std::expected<T, E> m_data;

public:
	using ValueType = T;
	using ErrorType = E;

	template <typename U>
		requires std::constructible_from<T, U &&>
	constexpr Result(::Ok<U> &&ok) : m_data(std::in_place, std::move(ok.value)) {}
	template <typename U>
		requires std::constructible_from<T, const U &>
	constexpr Result(const ::Ok<U> &ok) : m_data(std::in_place, ok.value) {}
	template <typename F>
		requires std::constructible_from<E, F &&>
	constexpr Result(Err<F> &&err) : m_data(std::unexpect, std::move(err.error)) {}
	template <typename F>
		requires std::constructible_from<E, const F &>
	constexpr Result(const Err<F> &err) : m_data(std::unexpect, err.error) {}
	constexpr Result(std::expected<T, E> e) : m_data(std::move(e)) {}

	template <typename... Args> [[nodiscard]] static constexpr Result MakeOk(Args &&...args) {
		return Result(std::expected<T, E>(std::in_place, std::forward<Args>(args)...));
	}
	template <typename... Args> [[nodiscard]] static constexpr Result MakeErr(Args &&...args) {
		return Result(std::expected<T, E>(std::unexpect, std::forward<Args>(args)...));
	}

	[[nodiscard]] constexpr bool IsOk() const noexcept { return m_data.has_value(); }
	[[nodiscard]] constexpr bool IsError() const noexcept { return !m_data.has_value(); }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_data.has_value(); }

	[[nodiscard]] constexpr const T &Value() const & noexcept { return *m_data; }
	[[nodiscard]] constexpr T &Value() & noexcept { return *m_data; }
	[[nodiscard]] constexpr T &&Value() && noexcept { return std::move(*m_data); }
	[[nodiscard]] constexpr const E &Error() const & noexcept { return m_data.error(); }
	[[nodiscard]] constexpr E &Error() & noexcept { return m_data.error(); }

	[[nodiscard]] constexpr const T &operator*() const & noexcept { return *m_data; }
	[[nodiscard]] constexpr T &operator*() & noexcept { return *m_data; }
	[[nodiscard]] constexpr const T *operator->() const noexcept { return &*m_data; }
	[[nodiscard]] constexpr T *operator->() noexcept { return &*m_data; }

	[[nodiscard]] constexpr T Unwrap() const & { return *m_data; }
	[[nodiscard]] constexpr T Unwrap() && { return std::move(*m_data); }
	[[nodiscard]] constexpr T UnwrapOr(T fallback) const & { return m_data ? *m_data : std::move(fallback); }
	[[nodiscard]] constexpr T UnwrapOr(T fallback) && { return m_data ? std::move(*m_data) : std::move(fallback); }
	[[nodiscard]] constexpr E UnwrapError() && { return std::move(m_data.error()); }
	[[nodiscard]] constexpr E UnwrapError() const & { return m_data.error(); }

	template <typename Fn> [[nodiscard]] constexpr auto Map(Fn &&fn) const & -> Result<std::invoke_result_t<Fn, const T &>, E> {
		if (m_data)
			return ::Ok(std::invoke(std::forward<Fn>(fn), *m_data));
		return Err<E>(m_data.error());
	}
	template <typename Fn> [[nodiscard]] constexpr auto MapErr(Fn &&fn) const & -> Result<T, std::invoke_result_t<Fn, const E &>> {
		if (m_data)
			return ::Ok<T>(*m_data);
		return Err(std::invoke(std::forward<Fn>(fn), m_data.error()));
	}
	template <typename Fn> [[nodiscard]] constexpr auto AndThen(Fn &&fn) const & -> std::invoke_result_t<Fn, const T &> {
		if (m_data)
			return std::invoke(std::forward<Fn>(fn), *m_data);
		return Err<E>(m_data.error());
	}
	template <typename OkFn, typename ErrFn> [[nodiscard]] constexpr auto Match(OkFn &&ok, ErrFn &&err) const & {
		return m_data ? std::invoke(std::forward<OkFn>(ok), *m_data) : std::invoke(std::forward<ErrFn>(err), m_data.error());
	}

	[[nodiscard]] constexpr Option<T> Ok() const & {
		if (m_data)
			return Some<T>(*m_data);
		return NONE;
	}
	[[nodiscard]] constexpr Option<E> ErrorOption() const & {
		if (!m_data)
			return Some<E>(m_data.error());
		return NONE;
	}

	[[nodiscard]] constexpr const std::expected<T, E> &Std() const noexcept { return m_data; }
	[[nodiscard]] constexpr std::expected<T, E> &Std() noexcept { return m_data; }
};

template <typename T> template <typename E> Result<T, E> Option<T>::OkOr(E err) const & {
	if (IsSome())
		return ::Ok<T>(Value());
	return Err<E>(std::move(err));
}
