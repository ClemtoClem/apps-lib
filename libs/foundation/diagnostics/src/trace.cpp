#include "diagnostics/trace.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <core/log.hpp>
#include <core/robin_map.hpp>
#include <dataio/binary.hpp>

#include "diagnostics/capture.hpp"

namespace diagnostics::trace {
namespace {

constexpr size_t kBlockEvents = 1 << 16;

struct ThreadBuffer {
	std::mutex mutex; // jamais disputé, sauf pendant Stop()
	uint64_t id = 0;
	std::string name;
	std::vector<std::unique_ptr<Event[]>> blocks;
	size_t used = kBlockEvents; // force l'allocation du premier bloc
	size_t count = 0;
};

struct Registry {
	std::mutex mutex;
	std::vector<std::shared_ptr<ThreadBuffer>> threads;
	std::vector<std::string> names;
	core::RobinMap<std::string, uint32_t, core::Hasher<std::string>> nameIds;
	std::string path;
	int64_t startNs = 0;
	uint64_t nextThreadId = 1;
};

Registry &GetRegistry() {
	static Registry r;
	return r;
}

std::atomic<bool> gEnabled{false};
std::atomic<bool> gDumpRequested{false};
std::atomic<int64_t> gStopAtNs{0}; // DIAG_TRACE_DURATION : arrêt automatique
std::atomic<int64_t> gLastFrameNs{0};
std::atomic<uint64_t> gFrames{0};

int64_t Now() noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

ThreadBuffer &LocalBuffer() {
	thread_local std::shared_ptr<ThreadBuffer> local = [] {
		auto b = std::make_shared<ThreadBuffer>();
		Registry &r = GetRegistry();
		std::scoped_lock lock(r.mutex);
		b->id = r.nextThreadId++;
		b->name = b->id == 1 ? "main" : std::format("thread {}", b->id);
		r.threads.push_back(b);
		return b;
	}();
	return *local;
}

extern "C" void OnSigUsr2(int) { gDumpRequested.store(true, std::memory_order_relaxed); }

/// Écrit la capture (registre verrouillé par l'appelant). Les événements de chaque fil sont
/// recopiés sous son verrou puis sérialisés hors verrou : le fil instrumenté n'attend que la
/// copie. `clear` : vide les tampons (fin de capture). `wait` faux : un tampon verrouillé est
/// écrit vide. Le fichier est remplacé d'un bloc (écriture à côté puis renommage).
bool WriteCapture(Registry &r, bool clear, bool wait) {
	dataio::ByteWriter w(1 << 20);
	w.Put<uint32_t>(capture::kMagic);
	w.Put<uint32_t>(capture::kVersion);
	w.Put<int64_t>(r.startNs);
	w.PutVarUint(r.names.size());
	for (const auto &n : r.names)
		w.PutString(n);
	w.PutVarUint(r.threads.size());
	std::vector<Event> events;
	for (const auto &t : r.threads) {
		std::string name;
		events.clear();
		{
			std::unique_lock lock(t->mutex, std::defer_lock);
			if (wait)
				lock.lock();
			if (lock.owns_lock() || lock.try_lock()) {
				name = t->name;
				events.reserve(t->count);
				for (size_t i = 0; i < t->count; ++i)
					events.push_back(t->blocks[i / kBlockEvents][i % kBlockEvents]);
				if (clear) {
					t->blocks.clear();
					t->used = kBlockEvents;
					t->count = 0;
				}
			} else
				name = std::format("thread {} (verrouillé)", t->id);
		}
		w.PutVarUint(t->id);
		w.PutString(name);
		w.PutVarUint(events.size());
		int64_t prev = r.startNs;
		for (const Event &e : events) {
			w.PutU8(static_cast<uint8_t>(e.type));
			w.PutVarUint(e.name);
			w.PutVarInt(e.timeNs - prev);
			prev = e.timeNs;
			if (e.type == EventType::Counter)
				w.Put<double>(e.value);
		}
	}
	w.Put<uint32_t>(dataio::Crc32(w.Bytes()));
	const std::string tmp = r.path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		f.write(reinterpret_cast<const char *>(w.Bytes().data()), static_cast<std::streamsize>(w.Size()));
		if (!f)
			return false;
	}
	return std::rename(tmp.c_str(), r.path.c_str()) == 0;
}

void AtExit() { Stop(); }

[[maybe_unused]] const bool kAutoInit = (InitFromEnvironment(), true);

} // namespace

bool Enabled() noexcept { return gEnabled.load(std::memory_order_relaxed); }

void InitFromEnvironment() {
	static std::once_flag once;
	std::call_once(once, [] {
		if (const char *path = std::getenv("DIAG_TRACE_FILE"); path && *path) {
			Start(path);
			std::atexit(AtExit);
			if (const char *d = std::getenv("DIAG_TRACE_DURATION"); d && *d)
				gStopAtNs.store(Now() + static_cast<int64_t>(std::atof(d) * 1e9));
		}
		std::signal(SIGUSR2, OnSigUsr2);
	});
}

bool Start(const std::string &path) {
	Registry &r = GetRegistry();
	{
		std::scoped_lock lock(r.mutex);
		r.path = path;
		r.startNs = Now();
		for (auto &t : r.threads) {
			std::scoped_lock tl(t->mutex);
			t->blocks.clear();
			t->used = kBlockEvents;
			t->count = 0;
		}
	}
	gEnabled.store(true, std::memory_order_release);
	return true;
}

bool Stop() {
	if (!gEnabled.exchange(false))
		return false;
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	const bool ok = WriteCapture(r, true, true);
	if (ok)
		core::log::Info("trace : capture écrite dans {}", r.path);
	else
		core::log::Error("trace : écriture impossible de {}", r.path);
	return ok;
}

bool Checkpoint(bool waitForLocks) {
	if (!Enabled())
		return false;
	Registry &r = GetRegistry();
	std::unique_lock lock(r.mutex, std::defer_lock);
	if (waitForLocks)
		lock.lock();
	if (!lock.owns_lock() && !lock.try_lock())
		return false;
	return WriteCapture(r, false, waitForLocks);
}

std::string CurrentPath() {
	if (!Enabled())
		return {};
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	return r.path;
}

int64_t NowNs() noexcept { return Now(); }
int64_t LastFrameNs() noexcept { return gLastFrameNs.load(std::memory_order_relaxed); }
uint64_t FrameCount() noexcept { return gFrames.load(std::memory_order_relaxed); }

std::string DescribeState(bool waitForLocks) {
	Registry &r = GetRegistry();
	std::unique_lock lock(r.mutex, std::defer_lock);
	if (waitForLocks)
		lock.lock();
	if (!lock.owns_lock() && !lock.try_lock())
		return "registre du traceur verrouillé : état indisponible\n";
	const auto name = [&](uint32_t id) { return id < r.names.size() ? r.names[id] : std::format("#{}", id); };
	std::string out;
	for (const auto &t : r.threads) {
		std::unique_lock tl(t->mutex, std::defer_lock);
		if (waitForLocks)
			tl.lock();
		if (!tl.owns_lock() && !tl.try_lock()) {
			out += std::format("fil {} : tampon verrouillé (en cours d'écriture)\n", t->id);
			continue;
		}
		std::vector<uint32_t> open;
		std::map<uint32_t, double> counters;
		for (size_t i = 0; i < t->count; ++i) {
			const Event &e = t->blocks[i / kBlockEvents][i % kBlockEvents];
			if (e.type == EventType::Begin)
				open.push_back(e.name);
			else if (e.type == EventType::End && !open.empty())
				open.pop_back();
			else if (e.type == EventType::Counter)
				counters[e.name] = e.value;
		}
		out += std::format("fil « {} » ({}) : {} événements\n  zones ouvertes : ", t->name, t->id, t->count);
		if (open.empty())
			out += "aucune";
		for (size_t i = 0; i < open.size(); ++i)
			out += (i ? " > " : "") + name(open[i]);
		out += "\n";
		if (!counters.empty()) {
			out += "  compteurs :";
			for (const auto &[id, v] : counters)
				out += std::format(" {}={:.6g}", name(id), v);
			out += "\n";
		}
	}
	return out;
}

void Frame() {
	gLastFrameNs.store(Now(), std::memory_order_relaxed);
	gFrames.fetch_add(1, std::memory_order_relaxed);
	if (!Enabled())
		return;
	static const uint32_t frameId = Intern("frame");
	Record(EventType::Frame, frameId);
	const int64_t stopAt = gStopAtNs.load(std::memory_order_relaxed);
	if (gDumpRequested.exchange(false, std::memory_order_relaxed) || (stopAt != 0 && Now() >= stopAt))
		Stop();
}

uint32_t Intern(std::string_view name) {
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	if (const uint32_t *id = r.nameIds.Find(name))
		return *id;
	const auto id = static_cast<uint32_t>(r.names.size());
	r.names.emplace_back(name);
	r.nameIds.InsertOrAssign(std::string(name), id);
	return id;
}

void SetThreadName(std::string_view name) {
	ThreadBuffer &b = LocalBuffer();
	std::scoped_lock lock(b.mutex);
	b.name = std::string(name);
}

void Record(EventType type, uint32_t name, double value) noexcept {
	ThreadBuffer &b = LocalBuffer();
	const int64_t t = Now();
	std::scoped_lock lock(b.mutex);
	if (b.used == kBlockEvents) {
		b.blocks.push_back(std::make_unique_for_overwrite<Event[]>(kBlockEvents));
		b.used = 0;
	}
	b.blocks.back()[b.used++] = Event{t, name, type, value};
	++b.count;
}

} // namespace diagnostics::trace
