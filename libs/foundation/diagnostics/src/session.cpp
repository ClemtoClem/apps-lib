#include "diagnostics/session.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>

#include <core/log.hpp>

#include "diagnostics/trace.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define DIAG_HAVE_BACKTRACE 1
#endif
#endif

namespace diagnostics::session {
namespace {

constexpr std::string_view kAddr2lineHint = "\n(adresses : addr2line -f -C -e <binaire> <décalage>, binaire compilé avec -g)\n";

struct State {
	std::mutex mutex; // session (début, fin, options) ; jamais pris par le puits du journal
	std::mutex logMutex;
	Options options;
	std::string base;
	bool active = false, ownsTrace = false, stopping = false;
	std::FILE *log = nullptr;
	core::log::Sink previousSink;
	std::thread watchdog;
	std::condition_variable wake;
#if defined(__linux__)
	pthread_t mainThread{};
#endif
};

State &GetState() {
	static State s;
	return s;
}

// Copies lisibles sans allocation par les gestionnaires de signaux.
char gCrashPath[4096] = {};
char gAppName[128] = {};
std::atomic<int64_t> gStartNs{0};

#if defined(__linux__)
std::atomic<int> gReportFd{-1};		 // rapport ouvert pendant le relevé de la pile du fil principal
std::atomic<bool> gStackDone{false}; // le fil principal a écrit sa pile

void WriteAll(int fd, std::string_view text) {
	while (!text.empty()) {
		const ssize_t n = ::write(fd, text.data(), text.size());
		if (n <= 0)
			return;
		text.remove_prefix(size_t(n));
	}
}

void WriteBacktrace(int fd) {
#ifdef DIAG_HAVE_BACKTRACE
	void *frames[64];
	const int n = ::backtrace(frames, 64);
	::backtrace_symbols_fd(frames, n, fd);
#else
	WriteAll(fd, "(pile d'appels indisponible sur cette plateforme)\n");
#endif
}

const char *SignalName(int sig) {
	switch (sig) {
	case SIGSEGV: return "SIGSEGV (accès mémoire invalide)";
	case SIGABRT: return "SIGABRT (abandon : assertion, std::terminate, corruption du tas…)";
	case SIGFPE: return "SIGFPE (erreur arithmétique)";
	case SIGILL: return "SIGILL (instruction illégale)";
	case SIGBUS: return "SIGBUS (erreur de bus)";
	default: return "signal inattendu";
	}
}

/// En-tête d'un rapport : nature, instant, images, mémoire.
std::string Header(std::string_view kind) {
	const int64_t now = trace::NowNs(), last = trace::LastFrameNs();
	std::string h = std::format("\n══ {} — {} à {:.1f} s de session ══\nimages : {}", gAppName, kind, double(now - gStartNs.load()) / 1e9, trace::FrameCount());
	if (last != 0)
		h += std::format(", la dernière il y a {:.2f} s", double(now - last) / 1e9);
	h += std::format("\nmémoire résidente : {:.0f} Mo\n", ResidentMemoryMb());
	return h;
}

/// Plantage : rapport, dernière trace, puis traitement par défaut du signal (SA_RESETHAND).
/// Au mieux : le processus est peut-être déjà corrompu (allocations, verrous essayés seulement).
extern "C" void OnFatal(int sig, siginfo_t *info, void *) {
	const int fd = ::open(gCrashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		WriteAll(fd, Header("plantage"));
		WriteAll(fd, std::format("signal : {}\n", SignalName(sig)));
		if (info && sig != SIGABRT)
			WriteAll(fd, std::format("adresse fautive : {}\n", info->si_addr));
		WriteAll(fd, "\n── État des fils ──\n");
		WriteAll(fd, trace::DescribeState(false));
		WriteAll(fd, "\n── Pile d'appels du fil fautif ──\n");
		WriteBacktrace(fd);
		WriteAll(fd, kAddr2lineHint);
		::close(fd);
	}
	(void)trace::Checkpoint(false);
	::raise(sig);
}

/// Relevé demandé par le fil de garde : le fil principal écrit sa propre pile.
extern "C" void OnStackRequest(int) {
	const int fd = gReportFd.load();
	if (fd >= 0)
		WriteBacktrace(fd);
	gStackDone.store(true);
}

int StackSignal() { return SIGRTMIN + 4; }

void InstallHandlers() {
	static char altStack[1 << 16]; // débordement de pile : le gestionnaire a besoin de place
	stack_t ss{};
	ss.ss_sp = altStack;
	ss.ss_size = sizeof altStack;
	::sigaltstack(&ss, nullptr);
	struct sigaction sa{};
	sa.sa_sigaction = OnFatal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = static_cast<int>(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND); // SA_RESETHAND : bit de poids fort
	for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
		::sigaction(sig, &sa, nullptr);
	struct sigaction st{};
	st.sa_handler = OnStackRequest;
	sigemptyset(&st.sa_mask);
	st.sa_flags = SA_RESTART;
	::sigaction(StackSignal(), &st, nullptr);
#ifdef DIAG_HAVE_BACKTRACE
	void *warm[2];
	(void)::backtrace(warm, 2); // charge libgcc maintenant, pas dans un gestionnaire
#endif
}

void RemoveHandlers() {
	for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
		std::signal(sig, SIG_DFL);
	std::signal(StackSignal(), SIG_DFL);
}

/// Blocage : état des fils et pile du fil principal là où il est arrêté.
void HangReport(State &s, double frozenSeconds) {
	const int fd = ::open(gCrashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	WriteAll(fd, Header("blocage"));
	WriteAll(fd, std::format("aucune image depuis {:.1f} s\n\n── État des fils ──\n", frozenSeconds));
	WriteAll(fd, trace::DescribeState(false));
	WriteAll(fd, "\n── Pile d'appels du fil principal ──\n");
	gStackDone.store(false);
	gReportFd.store(fd);
	if (::pthread_kill(s.mainThread, StackSignal()) == 0)
		for (int i = 0; i < 100 && !gStackDone.load(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	gReportFd.store(-1);
	if (!gStackDone.load())
		WriteAll(fd, "(le fil principal n'a pas répondu au relevé)\n");
	WriteAll(fd, kAddr2lineHint);
	::close(fd);
}

void AppendNote(std::string_view text) {
	const int fd = ::open(gCrashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		WriteAll(fd, text);
		::close(fd);
	}
}
#endif

/// Fil de garde : trace réécrite à intervalle régulier, détection des blocages.
void Watchdog() {
	State &s = GetState();
	int64_t lastCheckpoint = trace::NowNs();
	bool hanging = false;
	uint64_t hangFrames = 0;
	std::unique_lock lock(s.mutex);
	while (!s.stopping) {
		s.wake.wait_for(lock, std::chrono::milliseconds(250));
		if (s.stopping)
			break;
		const Options o = s.options;
		lock.unlock();
		const int64_t now = trace::NowNs();
		if (o.checkpointSeconds > 0.0 && double(now - lastCheckpoint) > o.checkpointSeconds * 1e9) {
			(void)trace::Checkpoint();
			lastCheckpoint = now;
		}
		const int64_t last = trace::LastFrameNs();
		const uint64_t frames = trace::FrameCount();
		const double frozen = last != 0 ? double(now - last) / 1e9 : 0.0;
#if defined(__linux__)
		if (o.hangSeconds > 0.0 && !hanging && last != 0 && frozen > o.hangSeconds) {
			hanging = true;
			hangFrames = frames;
			HangReport(s, frozen);
			(void)trace::Checkpoint(false);
			core::log::Error("diagnostics : aucune image depuis {:.1f} s, rapport dans {}", frozen, gCrashPath);
		} else if (hanging && frames != hangFrames) {
			hanging = false;
			AppendNote(std::format("reprise : les images ont repris ({} images à {:.1f} s de session)\n", frames, double(now - gStartNs.load()) / 1e9));
			core::log::Warn("diagnostics : les images ont repris après le blocage");
		}
#else
		(void)frozen;
		(void)hanging;
		(void)hangFrames;
#endif
		lock.lock();
	}
}

} // namespace

std::string DefaultDirectory() {
	namespace fs = std::filesystem;
	if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
		return (fs::path(xdg) / "apps-lib" / "diagnostics").string();
	const char *home = std::getenv("HOME");
	return (fs::path(home && *home ? home : ".") / ".local" / "share" / "apps-lib" / "diagnostics").string();
}

std::string Start(const Options &options) {
	namespace fs = std::filesystem;
	State &s = GetState();
	std::scoped_lock lock(s.mutex);
	if (s.active)
		return s.base;
	const fs::path dir = options.directory.empty() ? fs::path(DefaultDirectory()) : fs::path(options.directory);
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		core::log::Error("diagnostics : dossier {} : {}", dir.string(), ec.message());
		return {};
	}
	const std::time_t t = std::time(nullptr);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &t);
	const long pid = 0;
#else
	localtime_r(&t, &tm);
	const long pid = long(::getpid());
#endif
	char stamp[32];
	std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
	const std::string app = options.app.empty() ? "app" : options.app;
	s.base = (dir / std::format("{}_{}_{}", app, stamp, pid)).string();
	s.options = options;
	std::snprintf(gCrashPath, sizeof gCrashPath, "%s.crash.txt", s.base.c_str());
	std::snprintf(gAppName, sizeof gAppName, "%s", app.c_str());
	gStartNs.store(trace::NowNs());

	s.ownsTrace = !trace::Enabled(); // `profiler record` a déjà choisi le fichier : on le garde
	if (s.ownsTrace)
		trace::Start(s.base + ".vcap");
	if (options.logFile && (s.log = std::fopen((s.base + ".log").c_str(), "w")) != nullptr)
		s.previousSink = core::log::SetSink([](core::log::Level level, std::string_view message) {
			State &st = GetState();
			{
				std::scoped_lock l(st.logMutex);
				if (st.log) {
					const double at = double(trace::NowNs() - gStartNs.load()) / 1e9;
					std::fprintf(st.log, "[%9.3f] [%s] %.*s\n", at, core::log::LevelName(level).data(), int(message.size()), message.data());
					std::fflush(st.log); // un plantage ne perd aucune ligne
				}
			}
			if (st.previousSink)
				st.previousSink(level, message);
			else
				std::fprintf(stderr, "[%s] %.*s\n", core::log::LevelName(level).data(), int(message.size()), message.data());
		});
#if defined(__linux__)
	s.mainThread = ::pthread_self();
	InstallHandlers();
#endif
	s.stopping = false;
	s.watchdog = std::thread(Watchdog);
	s.active = true;
	static const bool registered = (std::atexit(Stop), true);
	(void)registered;
	return s.base;
}

void Stop() {
	State &s = GetState();
	std::thread watchdog;
	{
		std::scoped_lock lock(s.mutex);
		if (!s.active)
			return;
		s.active = false;
		s.stopping = true;
		watchdog = std::move(s.watchdog);
	}
	s.wake.notify_all();
	if (watchdog.joinable())
		watchdog.join();
#if defined(__linux__)
	RemoveHandlers();
#endif
	if (s.ownsTrace)
		(void)trace::Stop();
	if (s.log) {
		(void)core::log::SetSink(std::move(s.previousSink));
		std::scoped_lock l(s.logMutex);
		std::fclose(s.log);
		s.log = nullptr;
	}
}

std::string BasePath() {
	State &s = GetState();
	std::scoped_lock lock(s.mutex);
	return s.active ? s.base : std::string();
}

double ResidentMemoryMb() noexcept {
#if defined(__linux__)
	std::FILE *f = std::fopen("/proc/self/statm", "r");
	if (!f)
		return 0.0;
	long size = 0, resident = 0;
	const int read = std::fscanf(f, "%ld %ld", &size, &resident);
	std::fclose(f);
	return read == 2 ? double(resident) * double(::sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0) : 0.0;
#else
	return 0.0;
#endif
}

} // namespace diagnostics::session
