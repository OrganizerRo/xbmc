#pragma once
/*
 * VSTLog.h — Lightweight logging utility for audiodsp.vsthost
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 *
 * When running inside Kodi (audiodsp.vsthost.dll loaded in-process),
 * CActiveAEDSP registers a callback via ADDON_SetLogCallback so that
 * all addon log messages appear in kodi.log alongside the rest of Kodi's
 * output.  Before the callback is registered, or in stand-alone tools,
 * messages fall back to fprintf(stderr).
 *
 * Severity levels are chosen to match Kodi's LOGDEBUG / LOGINFO /
 * LOGWARNING / LOGERROR values so that the bridge in ActiveAEDSP.cpp
 * can pass them straight through to CLog::Log without translation.
 */

#include <cstdarg>

// ---------------------------------------------------------------------------
// Severity constants
// ---------------------------------------------------------------------------

constexpr int VSTLOG_DEBUG   = 0;
constexpr int VSTLOG_INFO    = 1;
constexpr int VSTLOG_WARNING = 2;
constexpr int VSTLOG_ERROR   = 3;

// ---------------------------------------------------------------------------
// Callback type
// ---------------------------------------------------------------------------

/// Called by VSTLog() with a severity level and a fully-formatted message.
/// The callback MUST be thread-safe: it is invoked from the pipe thread,
/// the UI thread, and the audio render thread simultaneously.
using VSTLogCallback = void (*)(int level, const char* msg);

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Install (or clear) the logging callback.
/// Called once from CActiveAEDSP::Init() immediately after loading the DLL.
/// Pass nullptr to revert to fprintf(stderr).
void VSTLog_SetCallback(VSTLogCallback cb);

/// Format and emit a log message at the given severity level.
void VSTLog(int level, const char* fmt, ...);
