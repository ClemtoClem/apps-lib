#pragma once
/**
 * @file ref.hpp
 * @brief Emprunts non propriétaires explicites : `Ref<T>` (lecture) et `RefMut<T>` (écriture).
 *        Un emprunt ne se construit jamais depuis un temporaire.
 */
#include <concepts>
#include <type_traits>
#include <utility>

template <typename T> class Ref {
	static_assert(!std::is_pointer_v<T>, "Ref<T*> est ambigu : utilisez Ref<T>");
	const T *m_ptr;

public:
	constexpr Ref(const T &v) noexcept : m_ptr(&v) {}
	Ref(const T &&) = delete;

	[[nodiscard]] constexpr const T *Get() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr const T &operator*() const noexcept { return *m_ptr; }
	[[nodiscard]] constexpr const T *operator->() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr operator const T &() const noexcept { return *m_ptr; }

	[[nodiscard]] constexpr bool operator==(const Ref &o) const noexcept
		requires std::equality_comparable<T>
	{
		return *m_ptr == *o.m_ptr;
	}
	[[nodiscard]] constexpr bool operator==(const T &o) const noexcept
		requires std::equality_comparable<T>
	{
		return *m_ptr == o;
	}
};

template <typename T> class RefMut {
	static_assert(!std::is_const_v<T>, "RefMut<const T> : utilisez Ref<T>");
	static_assert(!std::is_pointer_v<T>, "RefMut<T*> est ambigu : utilisez RefMut<T>");
	T *m_ptr;

public:
	constexpr explicit RefMut(T &v) noexcept : m_ptr(&v) {}
	RefMut(T &&) = delete;

	[[nodiscard]] constexpr T *Get() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr T &operator*() const noexcept { return *m_ptr; }
	[[nodiscard]] constexpr T *operator->() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr operator T &() const noexcept { return *m_ptr; }
	[[nodiscard]] constexpr operator Ref<T>() const noexcept { return Ref<T>(*m_ptr); }

	[[nodiscard]] T Replace(T v) const { return std::exchange(*m_ptr, std::move(v)); }
	void Swap(T &other) const noexcept {
		using std::swap;
		swap(*m_ptr, other);
	}
	[[nodiscard]] constexpr bool operator==(const RefMut &o) const noexcept
		requires std::equality_comparable<T>
	{
		return *m_ptr == *o.m_ptr;
	}
};

template <typename T> [[nodiscard]] constexpr Ref<T> MakeRef(const T &v) noexcept { return Ref<T>(v); }
template <typename T> [[nodiscard]] constexpr RefMut<T> MakeRefMut(T &v) noexcept { return RefMut<T>(v); }
