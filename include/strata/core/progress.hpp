// include/strata/core/progress.hpp - issue #29: whether a request is still moving, and where it is.
//
// The engine's host loop and the GPU wait on each other through flags; a protocol bug there does not crash, it
// spins forever (the GPU "100%", one CPU core busy, no tokens).  `--serve` runs a watchdog thread over this: a
// request whose heartbeat stops for `STRATA_WATCHDOG_S` seconds (default 60; 0 = off) ends the engine with the
// stage it was stuck in, and the server starts it again instead of hanging.  Tokens, prompt chunks and verify
// windows beat; the stage is two relaxed stores per layer, which the token path does not notice.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#if defined(_WIN32)
#include <io.h>      // _commit / _fileno
#include <process.h> // _getpid
#else
#include <unistd.h>  // fsync / fileno / getpid
#endif

namespace strata::core {

struct Progress {
    std::atomic<uint64_t> beats{0};
    std::atomic<bool> busy{false};
    std::atomic<const char*> where{"idle"};
    std::atomic<int64_t> detail{-1};
    std::atomic<int64_t> chunk{-1};      ///< #251: the prompt chunk (its first position) a batched-read stage is in
    std::atomic<int64_t> since_ms{0};    ///< when `where` was set (steady clock): how long a stage has lasted
    std::atomic<uint64_t> ticks{0};      ///< layers served: a window that still moves, slowly, against one that stopped
    /// a step that blocks in one call (a file flush, a host->device transfer of a whole session) is given until this
    /// time (steady ms; 0 = none) instead of the watchdog's limit - an explicit, bounded allowance, not a beat
    std::atomic<int64_t> allow_until_ms{0};
};

inline int64_t progress_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// What the watchdog prints about a part of the engine when it fires (the expert pool, the verify window).
using DiagFn = void (*)(std::FILE*);
inline std::atomic<DiagFn>& diag_pool_fn() { static std::atomic<DiagFn> f{nullptr}; return f; }
inline std::atomic<DiagFn>& diag_verify_fn() { static std::atomic<DiagFn> f{nullptr}; return f; }
/// #267: what a path that ends the engine runs first - it releases the GPU's spin waits on host flags (the verify
/// window's), so no kernel stays resident while the process goes away (on Windows that left the GPU "lost").
inline std::atomic<DiagFn>& release_gpu_fn() { static std::atomic<DiagFn> f{nullptr}; return f; }
inline void release_gpu_waits(std::FILE* f) {
    if (auto fn = release_gpu_fn().load()) fn(f);
}
/// --pipeline-windows: the pipelined loop's windows and the drafter's chain (set while that loop runs)
inline std::atomic<DiagFn>& diag_pipeline_fn() { static std::atomic<DiagFn> f{nullptr}; return f; }

inline Progress& progress() {
    static Progress p;
    return p;
}

/// STRATA_TRACE_FILE=<path>: a trace that survives the machine going away.  The engine's stderr goes through the
/// server's reader and a hard reset takes the tail of that with it; with this set, every stage transition
/// (progress_at) and every progress_note() is appended and flushed to the disk, so the last line in the file names
/// the step the machine died in.
///
/// STRATA_TRACE_MAX_MB=<n>: rotate at that size (default 64; 0 = never).  One request writes ~2,500 lines, so an
/// unbounded file is a habit worth avoiding; the file that passed the cap is renamed <path>.1 (one generation is
/// kept) and the fresh file starts with its own HEAD line.  The newest file always holds the death point.
inline std::mutex& progress_trace_mutex() { static std::mutex m; return m; }

/// The live handle.  Opened - and created - on first use; nullptr when STRATA_TRACE_FILE is unset, which makes
/// every call below a no-op.
inline std::FILE*& progress_trace_handle() {
    static std::FILE* f = [] () -> std::FILE* {
        const char* p = std::getenv("STRATA_TRACE_FILE");
        if (p == nullptr || *p == '\0') return nullptr;
        return std::fopen(p, "a");
    }();
    return f;
}

/// The older name for the same handle: call sites outside this header (verify.cpp writes the handshake words by
/// hand) keep working, and a caller that only asks "is tracing on?" reads the same pointer.
inline std::FILE* progress_trace_file() { return progress_trace_handle(); }

/// Bytes this process has appended, starting from what the file already held (a run that appends to an old file
/// still rotates when the file as a whole passes the cap).
inline uint64_t& progress_trace_bytes() {
    static uint64_t n = [] () -> uint64_t {
        std::FILE* f = progress_trace_handle();
        if (f == nullptr) return 0;
        const long pos = std::ftell(f);
        return pos > 0 ? (uint64_t) pos : 0;
    }();
    return n;
}

inline int64_t progress_trace_max_bytes() {
    const char* v = std::getenv("STRATA_TRACE_MAX_MB");
    if (v == nullptr || *v == '\0') return (int64_t) 64 << 20;
    const double mb = std::atof(v);          // fractional values are handy when testing the rotation
    if (mb <= 0.0) return 0;
    return (int64_t) (mb * 1048576.0);
}

inline int progress_trace_heartbeat_s();   // defined below; the rotated file's header names the interval

/// Rotate when the cap is passed.  Called with the mutex held; the fresh file's HEAD line is written by hand,
/// because calling progress_trace_line from here would take the same - non-recursive - mutex again.
inline void progress_trace_rotate_locked() {
    const int64_t cap = progress_trace_max_bytes();
    if (cap <= 0 || progress_trace_bytes() < (uint64_t) cap) return;
    const char* path = std::getenv("STRATA_TRACE_FILE");
    if (path == nullptr || *path == '\0') return;
    std::FILE* old = progress_trace_handle();
    if (old == nullptr) return;
    std::fclose(old);
    progress_trace_handle() = nullptr;
    char next[4096];
    std::snprintf(next, sizeof next, "%s.1", path);
    std::remove(next);                       // Windows' rename refuses an existing destination
    std::rename(path, next);
    std::FILE* fresh = std::fopen(path, "a");
    progress_trace_handle() = fresh;
    progress_trace_bytes() = 0;
    if (fresh == nullptr) return;
#if defined(_WIN32)
    const long pid = (long) _getpid();
#else
    const long pid = (long) getpid();
#endif
    const int n = std::fprintf(fresh, "%lld HEAD rotate %ld %d %lld\n", (long long) progress_now_ms(), pid,
                              progress_trace_heartbeat_s(), (long long) progress_now_ms());
    if (n > 0) progress_trace_bytes() += (uint64_t) n;
}

inline void progress_trace_line(const char* kind, const char* what, int64_t a, int64_t b, int64_t c,
                                int64_t d = INT64_MIN, int64_t e = INT64_MIN, int64_t f = INT64_MIN) {
    if (progress_trace_handle() == nullptr) return;
    std::lock_guard<std::mutex> lk(progress_trace_mutex());
    progress_trace_rotate_locked();
    std::FILE* fp = progress_trace_handle();
    if (fp == nullptr) return;
    // Variable width: the numbers given are the numbers printed, so a caller that has more than three values (the
    // verifier's handshake words) appends them instead of packing them.  Readers must take the numbers from the
    // END of the line: `what` itself may contain spaces.
    int n = std::fprintf(fp, "%lld %s %s %lld %lld %lld", (long long) progress_now_ms(), kind, what,
                         (long long) a, (long long) b, (long long) c);
    if (d != INT64_MIN) n += std::fprintf(fp, " %lld", (long long) d);
    if (e != INT64_MIN) n += std::fprintf(fp, " %lld", (long long) e);
    if (f != INT64_MIN) n += std::fprintf(fp, " %lld", (long long) f);
    n += std::fputc('\n', fp);
    if (n > 0) progress_trace_bytes() += (uint64_t) n;
    std::fflush(fp);
#if defined(_WIN32)
    _commit(_fileno(fp));
#else
    ::fsync(::fileno(fp));
#endif
}

/// Set by the atexit hook below, so the heartbeat never writes while the C runtime is closing its streams.
inline std::atomic<bool>& progress_trace_stopping() {
    static std::atomic<bool> b{false};
    return b;
}

/// The run's header + heartbeat.  Call it once, early, whenever STRATA_TRACE_FILE is set: it *creates* the file at
/// once (so an empty file means "the engine never reached main", not "no request yet") and then writes one HEART
/// line every STRATA_TRACE_HEARTBEAT_S seconds (default 5; 0 = off) carrying the current stage, the beat/tick
/// counters and how long that stage has lasted.  The file therefore shows the engine is alive and recording, and
/// after a hard reset its last line says both where the engine was and for how long it had been there.
inline int progress_trace_heartbeat_s() {
    const char* v = std::getenv("STRATA_TRACE_HEARTBEAT_S");
    if (v == nullptr || *v == '\0') return 5;
    const int s = std::atoi(v);
    return s <= 0 ? 0 : (s > 3600 ? 3600 : s);
}

inline void progress_trace_begin(const char* role) {
    if (progress_trace_handle() == nullptr) return;   // opens (and creates) the file when STRATA_TRACE_FILE is set
    if (progress_trace_stopping().load()) return;
    const int hb = progress_trace_heartbeat_s();
#if defined(_WIN32)
    const long pid = (long) _getpid();
#else
    const long pid = (long) getpid();
#endif
    progress_trace_line("HEAD", role, (int64_t) pid, (int64_t) hb, (int64_t) progress_now_ms());
    // ONE once_flag for both jobs: two call_once guards on the same flag would have the first one win and the
    // heartbeat would never start (measured).  The interval is the first call's; later calls only refresh HEAD.
    static std::once_flag once;
    std::call_once(once, [hb]() {
        std::atexit([]() { progress_trace_stopping().store(true); });
        if (hb <= 0) return;
        std::thread([hb]() {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(hb));
                if (progress_trace_stopping().load()) return;
                const Progress& p = progress();
                progress_trace_line("HEART", p.where.load(std::memory_order_relaxed), (int64_t) p.beats.load(),
                                    (int64_t) p.ticks.load(), progress_now_ms() - p.since_ms.load());
            }
        }).detach();
    });
}

/// A trace line that is not a stage change (a CUDA call about to run, a value): file only, plus stderr under
/// STRATA_TRACE=1.  Put it *before* the call: the last line then names the operation that never returned.
inline void progress_note(const char* what, int64_t a = -1, int64_t b = -1, int64_t c = -1) {
    progress_trace_line("NOTE", what, a, b, c);
    if (std::getenv("STRATA_TRACE") != nullptr) {
        std::fprintf(stderr, "strata trace: %s %lld %lld %lld\n", what, (long long) a, (long long) b, (long long) c);
        std::fflush(stderr);
    }
}

inline void progress_at(const char* where, int64_t detail = -1, int64_t chunk = -1) {
    Progress& p = progress();
    p.where.store(where, std::memory_order_relaxed);
    p.detail.store(detail, std::memory_order_relaxed);
    p.chunk.store(chunk, std::memory_order_relaxed);
    p.since_ms.store(progress_now_ms(), std::memory_order_relaxed);
    progress_trace_line("STAGE", where, detail, chunk, -1);
}

inline void progress_beat() { progress().beats.fetch_add(1, std::memory_order_relaxed); }
inline void progress_tick() { progress().ticks.fetch_add(1, std::memory_order_relaxed); }
/// A beat that also sets (seconds > 0) or clears (0) the allowance of a blocking step: the watchdog does not fire
/// before the allowance ends, and fires at its end if nothing beat since.
inline void progress_allow(int64_t seconds) {
    progress().allow_until_ms.store(seconds > 0 ? progress_now_ms() + seconds * 1000 : 0, std::memory_order_relaxed);
    progress_beat();
}

}  // namespace strata::core
