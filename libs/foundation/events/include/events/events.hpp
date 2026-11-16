#pragma once
/**
 * @file events.hpp
 * @brief Événements de jeu et de mods.
 *
 * - `Signal<Args...>` : diffusion immédiate, abonnés triés par priorité, jetons
 *   `Connection` RAII, désabonnement sûr pendant une émission, arrêt de la
 *   propagation si un abonné retourne `true` (événement « consommé »).
 * - `EventBus` : files **typées et doublement tamponnées**. `Publish<T>` (tout
 *   thread) empile dans le tampon d'écriture ; `Dispatch()` (thread principal)
 *   échange les tampons et livre les événements par lot à chaque abonné : pas
 *   d'allocation par événement, pas de type effacé dans la boucle chaude.
 * - `NamedEvents<Payload>` : événements nommés (chaînes interneés en hachage 64 bits)
 *   pour les mods : `On("world_load", fn)`, `Emit("world_load", payload)`.
 */
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <core/hash.hpp>
#include <core/robin_map.hpp>

namespace events {

/// Jeton d'abonnement : se désabonne à la destruction (sauf `Release`).
class Connection {
public:
	Connection() = default;
	explicit Connection(std::move_only_function<void()> disconnect) : m_disconnect(std::move(disconnect)) {}
	Connection(Connection &&) noexcept = default;
	Connection &operator=(Connection &&o) noexcept {
		if (this != &o) {
			Disconnect();
			m_disconnect = std::move(o.m_disconnect);
		}
		return *this;
	}
	~Connection() { Disconnect(); }
	void Disconnect() {
		if (m_disconnect) {
			auto fn = std::move(m_disconnect);
			m_disconnect = nullptr;
			fn();
		}
	}
	/// Abandonne le jeton : l'abonnement vit aussi longtemps que la source.
	void Release() noexcept { m_disconnect = nullptr; }
	[[nodiscard]] bool Connected() const noexcept { return static_cast<bool>(m_disconnect); }

private:
	std::move_only_function<void()> m_disconnect;
};

template <typename... Args> class Signal {
public:
	/// Retourner `true` consomme l'événement (les abonnés suivants ne le reçoivent pas).
	using Handler = std::function<bool(Args...)>;

	Signal() : m_state(std::make_shared<State>()) {}

	template <typename Fn> [[nodiscard]] Connection Connect(Fn &&fn, int priority = 0) {
		Handler h;
		if constexpr (std::is_same_v<std::invoke_result_t<Fn &, Args...>, bool>)
			h = std::forward<Fn>(fn);
		else
			h = [f = std::forward<Fn>(fn)](Args... a) mutable {
				f(a...);
				return false;
			};
		const uint64_t id = ++m_state->nextId;
		auto &slots = m_state->slots;
		const auto it = std::ranges::upper_bound(slots, priority, std::greater<>{}, &Slot::priority);
		slots.insert(it, Slot{id, priority, std::move(h), true});
		std::weak_ptr<State> weak = m_state;
		return Connection([weak, id] {
			if (auto s = weak.lock())
				for (auto &slot : s->slots)
					if (slot.id == id)
						slot.alive = false;
		});
	}

	/// Diffuse ; retourne vrai si un abonné a consommé l'événement.
	bool Emit(Args... args) {
		State &s = *m_state;
		++s.emitting;
		bool consumed = false;
		for (size_t i = 0; i < s.slots.size() && !consumed; ++i)
			if (s.slots[i].alive)
				consumed = s.slots[i].handler(args...);
		if (--s.emitting == 0)
			std::erase_if(s.slots, [](const Slot &sl) { return !sl.alive; });
		return consumed;
	}

	[[nodiscard]] size_t Count() const noexcept {
		return static_cast<size_t>(std::ranges::count_if(m_state->slots, &Slot::alive));
	}
	void Clear() { m_state->slots.clear(); }

private:
	struct Slot {
		uint64_t id;
		int priority;
		Handler handler;
		bool alive;
	};
	struct State {
		std::vector<Slot> slots;
		uint64_t nextId = 0;
		int emitting = 0;
	};
	std::shared_ptr<State> m_state;
};

class EventBus {
public:
	EventBus() = default;
	EventBus(const EventBus &) = delete;

	/// Empile un événement (thread-safe) ; livré au prochain `Dispatch`.
	template <typename T> void Publish(T event) {
		Channel<T> &ch = GetChannel<T>();
		std::scoped_lock lock(ch.mutex);
		ch.write.push_back(std::move(event));
	}
	/// Abonne `fn(std::span<const T>)` (traitement par lot) ou `fn(const T&)`.
	template <typename T, typename Fn> [[nodiscard]] Connection Subscribe(Fn &&fn, int priority = 0) {
		Channel<T> &ch = GetChannel<T>();
		if constexpr (std::is_invocable_v<Fn &, std::span<const T>>)
			return ch.batch.Connect(std::forward<Fn>(fn), priority);
		else
			return ch.batch.Connect(
				[f = std::forward<Fn>(fn)](std::span<const T> evs) mutable {
					for (const T &e : evs)
						f(e);
				},
				priority);
	}
	/// Livre immédiatement un événement (thread principal).
	template <typename T> void Emit(const T &event) { GetChannel<T>().batch.Emit(std::span<const T>(&event, 1)); }

	/// Livre tous les événements en attente, type par type. Retourne leur nombre.
	size_t Dispatch() {
		size_t total = 0;
		for (auto &ch : m_channels)
			total += ch->Flush();
		return total;
	}
	[[nodiscard]] size_t Pending() const {
		size_t n = 0;
		for (auto &ch : m_channels)
			n += ch->Size();
		return n;
	}

private:
	struct ChannelBase {
		virtual ~ChannelBase() = default;
		virtual size_t Flush() = 0;
		virtual size_t Size() = 0;
	};
	template <typename T> struct Channel final : ChannelBase {
		std::mutex mutex;
		std::vector<T> write, read;
		Signal<std::span<const T>> batch;
		size_t Flush() override {
			{
				std::scoped_lock lock(mutex);
				std::swap(write, read);
			}
			const size_t n = read.size();
			if (n)
				batch.Emit(std::span<const T>(read));
			read.clear(); // capacité conservée
			return n;
		}
		size_t Size() override {
			std::scoped_lock lock(mutex);
			return write.size();
		}
	};

	template <typename T> static size_t TypeIndex() {
		static const size_t index = m_sNextType.fetch_add(1);
		return index;
	}
	template <typename T> Channel<T> &GetChannel() {
		const size_t idx = TypeIndex<T>();
		std::scoped_lock lock(m_registryMutex);
		if (idx >= m_byType.size())
			m_byType.resize(idx + 1, nullptr);
		if (!m_byType[idx]) {
			auto ch = std::make_unique<Channel<T>>();
			m_byType[idx] = ch.get();
			m_channels.push_back(std::move(ch));
		}
		return *static_cast<Channel<T> *>(m_byType[idx]);
	}

	static inline std::atomic<size_t> m_sNextType{0};
	std::mutex m_registryMutex;
	std::vector<ChannelBase *> m_byType;
	std::vector<std::unique_ptr<ChannelBase>> m_channels;
};

/// Identifiant d'événement nommé (hachage FNV-1a 64 bits, calculable à la compilation).
using EventId = uint64_t;
[[nodiscard]] constexpr EventId MakeEventId(std::string_view name) noexcept { return core::Fnv1a(name); }

template <typename Payload> class NamedEvents {
public:
	using Handler = std::function<bool(const Payload &)>;

	template <typename Fn> [[nodiscard]] Connection On(std::string_view name, Fn &&fn, int priority = 0) {
		Entry &e = m_signals[MakeEventId(name)];
		if (e.name.empty())
			e.name = std::string(name);
		return e.signal.Connect(std::forward<Fn>(fn), priority);
	}
	bool Emit(std::string_view name, const Payload &p) { return Emit(MakeEventId(name), p); }
	bool Emit(EventId id, const Payload &p) {
		Entry *e = m_signals.Find(id);
		++m_emitted;
		return e ? e->signal.Emit(p) : false;
	}
	[[nodiscard]] size_t Listeners(std::string_view name) const {
		const Entry *e = m_signals.Find(MakeEventId(name));
		return e ? e->signal.Count() : 0;
	}
	[[nodiscard]] std::vector<std::string> Names() const {
		std::vector<std::string> out;
		for (const auto &e : m_signals)
			out.push_back(e.value.name);
		return out;
	}
	[[nodiscard]] uint64_t EmittedCount() const noexcept { return m_emitted; }
	void Clear() { m_signals.Clear(); }

private:
	struct Entry {
		std::string name;
		Signal<const Payload &> signal;
	};
	core::RobinMap<EventId, Entry> m_signals;
	uint64_t m_emitted = 0;
};

} // namespace events
