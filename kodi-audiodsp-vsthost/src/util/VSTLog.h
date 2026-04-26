#pragma once
/*
 * VSTLog.h — Lightweight log callback for audiodsp.vsthost
 *
 * Provides a VSTLOG(level, fmt, ...) macro that writes to the Kodi log via a
 * registered callback, falling back to std::fprintf(stderr, ...) when no
 * callback has been registered (e.g. standalone / unit-test contexts).
 *
 * Level values match the Kodi-side vstLogBridge() in ActiveAEDSP.cpp:
 *   VSTLOG_DEBUG = 0, VSTLOG_INFO = 1, VSTLOG_WARN = 2, VSTLOG_ERROR = 3
 *
 * Registration:
 *   CActiveAEDSP::Init() calls ADDON_SetLogCallback(vstLogBridge) via
 *   GetProcAddress after loading the addon DLL.  That stores the pointer in
 *   VSTLog::g_callback so every subsequent VSTLOG() call is routed into
 *   kodi.log.
 *
 * Thread-safety:
 *   g_callback is an atomic pointer.  Registration must happen once before
 *   log calls, but concurrent VSTLOG() calls on any thread are safe.
 */

#include <atomic>
#include <cstdio>

// Callback type — must stay in sync with the declaration in ActiveAEDSP.cpp:
//   using VSTLogCallback_t = void (*)(int level, const char* msg);
using VSTLogCallback_t = void (*)(int level, const char* msg);

// ---------------------------------------------------------------------------
// Log level constants
// ---------------------------------------------------------------------------
static constexpr int VSTLOG_DEBUG = 0;
static constexpr int VSTLOG_INFO  = 1;
static constexpr int VSTLOG_WARN  = 2;
static constexpr int VSTLOG_ERROR = 3;

// Internal buffer capacity for VSTLOG().  Large enough for typical log lines;
// messages exceeding this length are truncated with "..." appended.
static constexpr int VSTLOG_BUFFER_SIZE = 512;

namespace VSTLog
{

// Atomic callback pointer — set once by ADDON_SetLogCallback(), read on hot paths.
inline std::atomic<VSTLogCallback_t> g_callback{nullptr};

inline void write(int level, const char* msg)
{
    VSTLogCallback_t cb = g_callback.load(std::memory_order_relaxed);
    if (cb)
        cb(level, msg);
    else
        std::fprintf(stderr, "[VSTHost] %s\n", msg);
}

} // namespace VSTLog

// ---------------------------------------------------------------------------
// VSTLOG(level, fmt, ...) — format into a stack buffer and forward to
// VSTLog::write().  Messages longer than VSTLOG_BUFFER_SIZE are truncated
// and the truncation is marked with "..." at the end.
// ---------------------------------------------------------------------------
#define VSTLOG(level, fmt, ...)                                                     \
    do {                                                                            \
        char _vstlog_buf[VSTLOG_BUFFER_SIZE];                                       \
        int _vstlog_ret = std::snprintf(_vstlog_buf, sizeof(_vstlog_buf),           \
                                        fmt, ##__VA_ARGS__);                        \
        if (_vstlog_ret >= static_cast<int>(sizeof(_vstlog_buf)))                   \
        {                                                                           \
            /* message was truncated — append "..." to make it visible */           \
            _vstlog_buf[sizeof(_vstlog_buf) - 4] = '.';                             \
            _vstlog_buf[sizeof(_vstlog_buf) - 3] = '.';                             \
            _vstlog_buf[sizeof(_vstlog_buf) - 2] = '.';                             \
            _vstlog_buf[sizeof(_vstlog_buf) - 1] = '\0';                            \
        }                                                                           \
        VSTLog::write((level), _vstlog_buf);                                        \
    } while (0)
