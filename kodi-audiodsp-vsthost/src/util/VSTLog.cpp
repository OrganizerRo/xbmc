/*
 * VSTLog.cpp — Lightweight logging utility for audiodsp.vsthost
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 */

#include "VSTLog.h"

#include <atomic>
#include <cstdio>
#include <cstdarg>

// ---------------------------------------------------------------------------
// Global callback — stored as a lock-free atomic so that VSTLog() can be
// called concurrently from the pipe thread, UI thread, and audio thread
// without taking a mutex.  Plain function pointers are trivially copyable
// and therefore valid as std::atomic template arguments.
// ---------------------------------------------------------------------------

static std::atomic<VSTLogCallback> g_logCallback{nullptr};

/// Maximum length of a formatted log message (including null terminator).
static constexpr size_t VSTLOG_BUFFER_SIZE = 2048;

void VSTLog_SetCallback(VSTLogCallback cb)
{
    g_logCallback.store(cb, std::memory_order_release);
}

void VSTLog(int level, const char* fmt, ...)
{
    char buf[VSTLOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    VSTLogCallback cb = g_logCallback.load(std::memory_order_acquire);
    if (cb)
    {
        cb(level, buf);
        return;
    }

    // Fallback: write to stderr (useful during early startup before the
    // callback is registered and in stand-alone diagnostic tools).
    const char* prefix = "LOG";
    switch (level)
    {
    case VSTLOG_DEBUG:   prefix = "DEBUG"; break;
    case VSTLOG_INFO:    prefix = "INFO";  break;
    case VSTLOG_WARNING: prefix = "WARN";  break;
    case VSTLOG_ERROR:   prefix = "ERROR"; break;
    default: break;
    }
    std::fprintf(stderr, "[VSTHost][%s] %s\n", prefix, buf);
}
