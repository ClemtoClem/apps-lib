#pragma once
/**
 * @file jobs.hpp
 * @brief Exécution parallèle sur la bibliothèque standard C++23.
 *
 * - `ThreadPool` : N `std::jthread`, files par priorité (Haute → chunks proches du
 *   joueur, Normale, Basse → sauvegardes), tâches `std::move_only_function`, arrêt
 *   coopératif par `std::stop_token`.
 * - `ParallelFor` : découpe une plage en lots, le thread appelant participe, attente
 *   par compteur atomique (`std::atomic::wait`) : aucune allocation par lot.
 * - `WaitGroup` : attendre un ensemble de tâches.
 * - `Channel<T>` : file MPSC ; le consommateur vide tout le lot d'un coup (swap).
 * - `AsyncValue<T>` : valeur calculée en arrière-plan (chargement d'un panneau sans figer
 *   l'interface) ; seule la dernière demande compte, les précédentes sont annulées.
 */
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <thread>
#include <utility>
#include <vector>

namespace jobs {

enum class Priority : uint8_t { High = 0, Normal = 1, Low = 2 };

/// Compteur d'attente : `Add(n)` avant de soumettre, `Done()` dans chaque tâche.
class WaitGroup {
	std::atomic<int64_t> m_count{0};

public:
	void Add(int64_t n = 1) noexcept { m_count.fetch_add(n, std::memory_order_relaxed); }
	void Done() noexcept {
		if (m_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
			m_count.notify_all();
	}
	void Wait() const noexcept {
		for (int64_t v = m_count.load(std::memory_order_acquire); v != 0; v = m_count.load(std::memory_order_acquire))
			m_count.wait(v, std::memory_order_acquire);
	}
	[[nodiscard]] int64_t Pending() const noexcept { return m_count.load(std::memory_order_acquire); }
};

class ThreadPool {
public:
	using Task = std::move_only_function<void()>;

	/// `threads == 0` : `hardware_concurrency() - 1` (au moins 1).
	explicit ThreadPool(unsigned threads = 0);
	~ThreadPool();
	ThreadPool(const ThreadPool &) = delete;
	ThreadPool &operator=(const ThreadPool &) = delete;

	void Submit(Task task, Priority priority = Priority::Normal);
	/// Soumet un lot sous un seul verrou.
	void SubmitBatch(std::vector<Task> &tasks, Priority priority = Priority::Normal);

	/// Exécute des tâches en attente sur le thread appelant (aide pendant une attente).
	bool RunOne();
	/// Attend que toutes les files soient vides et les workers inactifs.
	void WaitIdle();

	[[nodiscard]] unsigned WorkerCount() const noexcept { return static_cast<unsigned>(m_workers.size()); }
	[[nodiscard]] size_t PendingCount() const;

	/// Pool partagé par défaut du processus (créé au premier appel).
	static ThreadPool &Global();

private:
	void WorkerLoop(std::stop_token stop);
	std::optional<Task> PopLocked();

	mutable std::mutex m_mutex;
	std::condition_variable_any m_cv;
	std::condition_variable m_idleCv;
	std::deque<Task> m_queues[3];
	size_t m_active = 0;
	std::vector<std::jthread> m_workers;
};

/**
 * Exécute `fn(i)` pour i ∈ [begin, end) en lots de `grain` éléments.
 * `fn` peut aussi accepter `(size_t first, size_t last)` pour traiter un lot entier.
 */
template <typename Fn> void ParallelFor(ThreadPool &pool, size_t begin, size_t end, size_t grain, Fn &&fn) {
	if (end <= begin)
		return;
	if (grain == 0)
		grain = 1;
	const size_t count = end - begin;
	const size_t batches = (count + grain - 1) / grain;
	auto runBatch = [&](size_t b) {
		const size_t first = begin + b * grain;
		const size_t last = std::min(end, first + grain);
		if constexpr (std::is_invocable_v<Fn &, size_t, size_t>) {
			fn(first, last);
		} else {
			for (size_t i = first; i < last; ++i)
				fn(i);
		}
	};
	if (batches == 1 || pool.WorkerCount() == 0) {
		for (size_t b = 0; b < batches; ++b)
			runBatch(b);
		return;
	}
	// Distribution dynamique : chaque participant pioche le prochain lot (équilibrage).
	std::atomic<size_t> next{0};
	WaitGroup wg;
	auto worker = [&] {
		for (size_t b; (b = next.fetch_add(1, std::memory_order_relaxed)) < batches;)
			runBatch(b);
		wg.Done();
	};
	const size_t helpers = std::min<size_t>(pool.WorkerCount(), batches - 1);
	wg.Add(static_cast<int64_t>(helpers));
	for (size_t h = 0; h < helpers; ++h)
		pool.Submit([&worker] { worker(); }, Priority::High);
	for (size_t b; (b = next.fetch_add(1, std::memory_order_relaxed)) < batches;)
		runBatch(b);
	// Les helpers peuvent ne pas avoir démarré : on aide à vider la file pendant l'attente.
	while (wg.Pending() > 0) {
		if (!pool.RunOne())
			wg.Wait();
	}
}

template <typename Fn> void ParallelFor(size_t begin, size_t end, size_t grain, Fn &&fn) {
	ParallelFor(ThreadPool::Global(), begin, end, grain, std::forward<Fn>(fn));
}

/// File MPSC : `Push` depuis n'importe quel thread, `Drain` (un consommateur) récupère tout.
template <typename T> class Channel {
	mutable std::mutex m_mutex;
	std::vector<T> m_items;

public:
	void Push(T item) {
		std::scoped_lock lock(m_mutex);
		m_items.push_back(std::move(item));
	}
	/// Échange le contenu avec `out` (vidé au préalable) : O(1) sous verrou.
	void Drain(std::vector<T> &out) {
		out.clear();
		std::scoped_lock lock(m_mutex);
		std::swap(out, m_items);
	}
	[[nodiscard]] size_t Size() const {
		std::scoped_lock lock(m_mutex);
		return m_items.size();
	}
};

/**
 * Valeur produite par une tâche de fond, consommée par un thread (l'interface).
 *
 * - `Request(pool, job)` : soumet `job(stop_token) -> T` ; une demande plus récente annule la
 *   précédente (son jeton passe à « arrêt demandé » et son résultat est ignoré).
 * - `Poll()` (thread consommateur) : récupère le dernier résultat prêt ; `Get()` reste valide
 *   jusqu'au prochain résultat (on affiche l'ancienne valeur pendant un rafraîchissement).
 * - `Loading()` : une demande est en cours ; `Get() == nullptr && Loading()` → indicateur
 *   de chargement.
 * - L'état partagé survit à l'objet : une tâche encore en vol après sa destruction écrit
 *   dans un état orphelin, sans danger. Les données qu'elle *lit* doivent en revanche rester
 *   valides : `ThreadPool::WaitIdle()` avant de les libérer.
 */
template <typename T> class AsyncValue {
	struct State {
		std::mutex mutex;
		std::optional<T> ready;
		uint64_t latest = 0;
		std::stop_source stop;
		std::atomic<int> inFlight{0};
	};
	std::shared_ptr<State> m_state = std::make_shared<State>();
	std::optional<T> m_value;
	uint64_t m_generation = 0; // génération de la valeur courante

public:
	using Job = std::move_only_function<T(std::stop_token)>;

	void Request(ThreadPool &pool, Job job, Priority priority = Priority::Normal) {
		std::stop_source source;
		uint64_t generation;
		{
			std::scoped_lock lock(m_state->mutex);
			m_state->stop.request_stop();
			m_state->stop = source;
			generation = ++m_state->latest;
		}
		m_state->inFlight.fetch_add(1, std::memory_order_relaxed);
		pool.Submit(
			[state = m_state, generation, job = std::move(job), token = source.get_token()]() mutable {
				if (!token.stop_requested()) {
					T value = job(token);
					std::scoped_lock lock(state->mutex);
					if (generation == state->latest && !token.stop_requested())
						state->ready = std::move(value);
				}
				state->inFlight.fetch_sub(1, std::memory_order_release);
			},
			priority);
	}

	/// Transfère le résultat prêt ; vrai si la valeur a changé.
	bool Poll() {
		std::scoped_lock lock(m_state->mutex);
		if (!m_state->ready)
			return false;
		m_value = std::move(*m_state->ready);
		m_state->ready.reset();
		m_generation = m_state->latest;
		return true;
	}
	[[nodiscard]] const T *Get() const noexcept { return m_value ? &*m_value : nullptr; }
	[[nodiscard]] T *Get() noexcept { return m_value ? &*m_value : nullptr; }
	/// Une tâche est encore en file ou en cours (même annulée).
	[[nodiscard]] bool Loading() const noexcept { return m_state->inFlight.load(std::memory_order_acquire) > 0; }
	/// Annule la demande en cours (son résultat sera ignoré).
	void Cancel() {
		std::scoped_lock lock(m_state->mutex);
		m_state->stop.request_stop();
		++m_state->latest;
		m_state->ready.reset();
	}
	/// Annule et oublie la valeur courante (l'indicateur de chargement réapparaît).
	void Clear() {
		Cancel();
		m_value.reset();
	}
};

} // namespace jobs
