#pragma once
/**
 * @file wrapper.hpp
 * @brief Propriété RAII de handles C (SDL, …) :
 *  - `Wrapper<T, Deleter>`        : handle possédé, deleter `void(T*)` ;
 *  - `DeviceWrapper<D, T, Deleter>` : deleter `void(D*, T*)` (ressources GPU) ;
 *  - `Borrowed<T>`                : vue non possédante.
 *  Le handle reste un pointeur brut `m_handle` accessible aux classes dérivées
 *  (zéro indirection, taille d'un pointeur) ; déplacements par `std::exchange`.
 */
#include <span>
#include <utility>
#include <vector>

#include "option.hpp"
#include "ref.hpp"

template <typename T, auto Deleter> class Wrapper {
protected:
	T *mHandle = nullptr;

public:
	constexpr Wrapper() noexcept = default;
	constexpr explicit Wrapper(T *handle) noexcept : mHandle(handle) {}
	constexpr explicit Wrapper(RefMut<T> ref) noexcept : mHandle(ref.Get()) {}
	Wrapper(const Wrapper &) = delete;
	Wrapper &operator=(const Wrapper &) = delete;
	constexpr Wrapper(Wrapper &&o) noexcept : mHandle(std::exchange(o.mHandle, nullptr)) {}
	constexpr Wrapper &operator=(Wrapper &&o) noexcept {
		if (this != &o)
			Reset(std::exchange(o.mHandle, nullptr));
		return *this;
	}
	~Wrapper() { Reset(); }

	[[nodiscard]] Option<Ref<T>> AsRef() const noexcept {
		if (mHandle)
			return Some(MakeRef(*mHandle));
		return NONE;
	}
	void Reset(T *h = nullptr) noexcept {
		if (mHandle)
			Deleter(mHandle);
		mHandle = h;
	}
	[[nodiscard]] T *Release() noexcept { return std::exchange(mHandle, nullptr); }
	[[nodiscard]] T *Get() const noexcept { return mHandle; }
	[[nodiscard]] explicit operator bool() const noexcept { return mHandle != nullptr; }
	[[nodiscard]] operator T *() const noexcept { return mHandle; }
};

template <typename D, typename T, auto Deleter> class DeviceWrapper {
protected:
	D *mDevice = nullptr;
	T *mHandle = nullptr;

public:
	constexpr DeviceWrapper() noexcept = default;
	constexpr DeviceWrapper(D *device, T *handle) noexcept : mDevice(device), mHandle(handle) {}
	constexpr DeviceWrapper(D *device, RefMut<T> ref) noexcept : mDevice(device), mHandle(ref.Get()) {}
	DeviceWrapper(const DeviceWrapper &) = delete;
	DeviceWrapper &operator=(const DeviceWrapper &) = delete;
	constexpr DeviceWrapper(DeviceWrapper &&o) noexcept
		: mDevice(o.mDevice), mHandle(std::exchange(o.mHandle, nullptr)) {}
	constexpr DeviceWrapper &operator=(DeviceWrapper &&o) noexcept {
		if (this != &o) {
			Reset();
			mDevice = o.mDevice;
			mHandle = std::exchange(o.mHandle, nullptr);
		}
		return *this;
	}
	~DeviceWrapper() { Reset(); }

	[[nodiscard]] Option<Ref<T>> AsRef() const noexcept {
		if (mDevice && mHandle)
			return Some(MakeRef(*mHandle));
		return NONE;
	}
	void Reset(T *h = nullptr) noexcept {
		if (mDevice && mHandle)
			Deleter(mDevice, mHandle);
		mHandle = h;
	}
	[[nodiscard]] T *Release() noexcept { return std::exchange(mHandle, nullptr); }
	[[nodiscard]] D *GetDevice() const noexcept { return mDevice; }
	[[nodiscard]] T *Get() const noexcept { return mHandle; }
	[[nodiscard]] explicit operator bool() const noexcept { return mDevice && mHandle; }
	[[nodiscard]] operator T *() const noexcept { return mHandle; }
};

template <typename T> class Borrowed {
protected:
	T *mHandle = nullptr;

public:
	constexpr Borrowed() noexcept = default;
	constexpr explicit Borrowed(T *h) noexcept : mHandle(h) {}
	[[nodiscard]] T *Get() const noexcept { return mHandle; }
	[[nodiscard]] explicit operator bool() const noexcept { return mHandle != nullptr; }
	[[nodiscard]] operator T *() const noexcept { return mHandle; }
};

namespace detail {
/// Convertit une plage d'emprunts de wrappers en tableau de handles bruts pour l'API C.
template <typename Wrapped> [[nodiscard]] inline auto ToRawHandles(std::span<const Ref<Wrapped>> refs) {
	std::vector<decltype(std::declval<const Wrapped &>().Get())> out;
	out.reserve(refs.size());
	for (const Ref<Wrapped> &r : refs)
		out.push_back(r->Get());
	return out;
}
} // namespace detail
