#include "jobs/jobs.hpp"

#include <algorithm>

namespace jobs {

ThreadPool::ThreadPool(unsigned threads) {
	if (threads == 0) {
		const unsigned hw = std::thread::hardware_concurrency();
		threads = hw > 1 ? hw - 1 : 1;
	}
	m_workers.reserve(threads);
	for (unsigned i = 0; i < threads; ++i)
		m_workers.emplace_back([this](std::stop_token st) { WorkerLoop(st); });
}

ThreadPool::~ThreadPool() {
	for (auto &w : m_workers)
		w.request_stop();
	m_cv.notify_all();
	m_workers.clear(); // join
}

ThreadPool &ThreadPool::Global() {
	static ThreadPool pool;
	return pool;
}

void ThreadPool::Submit(Task task, Priority priority) {
	{
		std::scoped_lock lock(m_mutex);
		m_queues[static_cast<size_t>(priority)].push_back(std::move(task));
	}
	m_cv.notify_one();
}

void ThreadPool::SubmitBatch(std::vector<Task> &tasks, Priority priority) {
	if (tasks.empty())
		return;
	{
		std::scoped_lock lock(m_mutex);
		auto &q = m_queues[static_cast<size_t>(priority)];
		for (auto &t : tasks)
			q.push_back(std::move(t));
	}
	tasks.clear();
	m_cv.notify_all();
}

std::optional<ThreadPool::Task> ThreadPool::PopLocked() {
	for (auto &q : m_queues) {
		if (!q.empty()) {
			Task t = std::move(q.front());
			q.pop_front();
			return t;
		}
	}
	return std::nullopt;
}

bool ThreadPool::RunOne() {
	std::optional<Task> task;
	{
		std::scoped_lock lock(m_mutex);
		task = PopLocked();
		if (!task)
			return false;
		++m_active;
	}
	(*task)();
	{
		std::scoped_lock lock(m_mutex);
		--m_active;
	}
	m_idleCv.notify_all();
	return true;
}

void ThreadPool::WorkerLoop(std::stop_token stop) {
	for (;;) {
		std::optional<Task> task;
		{
			std::unique_lock lock(m_mutex);
			m_cv.wait(lock, stop, [this] {
				return !m_queues[0].empty() || !m_queues[1].empty() || !m_queues[2].empty();
			});
			if (stop.stop_requested())
				return;
			task = PopLocked();
			if (!task)
				continue;
			++m_active;
		}
		(*task)();
		{
			std::scoped_lock lock(m_mutex);
			--m_active;
		}
		m_idleCv.notify_all();
	}
}

void ThreadPool::WaitIdle() {
	while (RunOne()) {
	}
	std::unique_lock lock(m_mutex);
	m_idleCv.wait(lock, [this] {
		return m_active == 0 && m_queues[0].empty() && m_queues[1].empty() && m_queues[2].empty();
	});
}

size_t ThreadPool::PendingCount() const {
	std::scoped_lock lock(m_mutex);
	return m_queues[0].size() + m_queues[1].size() + m_queues[2].size();
}

} // namespace jobs
