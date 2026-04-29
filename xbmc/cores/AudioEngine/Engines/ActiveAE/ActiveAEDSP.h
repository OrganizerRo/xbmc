/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*
 * ActiveAEDSP.h — Windows ADSP (audio DSP) integration for ActiveAE
 *
 * Bridges Kodi's ActiveAE audio pipeline to legacy ADSP add-ons
 * (add-on type "xbmc.audiodsp", e.g. audiodsp.vsthost).
 *
 * All logic is inside #ifdef TARGET_WINDOWS because:
 *   - The legacy ADSP add-on API uses __cdecl / __declspec(dllexport)
 *   - VST plugins and the named-pipe UI bridge are Windows-only
 *   - SEH (__try/__except) is required for crash isolation
 *
 * Thread model
 * ────────────
 *   Init() / Deinit()    — AE setup thread (CActiveAE::Start / Dispose)
 *   OnConfigure()        — AE worker thread (CActiveAE::Configure)
 *   MasterProcess()      — AE render thread (CActiveAE::RunStages)
 */

#include "ActiveAEBuffer.h"
#include "cores/AudioEngine/Utils/AEAudioFormat.h"

#include <atomic>
#include <chrono>
#include <vector>

#ifdef TARGET_WINDOWS
#include <windows.h>
#endif

// Forward-declare legacy ADSP types so we avoid pulling in the full legacy
// ADSP headers in files that only include this header.
struct AudioDSP;
struct ADDON_HANDLE_STRUCT;

namespace ActiveAE
{

class CActiveAEDSP
{
public:
  CActiveAEDSP();
  ~CActiveAEDSP();

  // -------------------------------------------------------------------------
  // Lifecycle — called from the AE setup thread
  // -------------------------------------------------------------------------

  /// Load and start the first enabled ADDON_AUDIODSP add-on.
  /// @return true on success; false if no add-on is available or loading fails.
  bool Init();

  /// Unload the add-on and release all resources.
  void Deinit();

  // -------------------------------------------------------------------------
  // Stream lifecycle — called from the AE worker thread
  // -------------------------------------------------------------------------

  /// Called whenever the output audio format changes.
  /// Re-initializes the DSP stream with the new parameters if necessary.
  void OnConfigure(const AEAudioFormat& fmt);

  // -------------------------------------------------------------------------
  // Audio processing — called from the AE render thread
  // -------------------------------------------------------------------------

  /// Process one output audio buffer in-place through the VST chain.
  /// Handles both planar (AE_FMT_FLOATP) and interleaved (AE_FMT_FLOAT) buffers.
  /// Silently skips processing if the add-on is not active or has crashed.
  void MasterProcess(CSampleBuffer* buf);

  // -------------------------------------------------------------------------
  // Status
  // -------------------------------------------------------------------------

  /// Returns true if the add-on was loaded successfully and has not crashed.
  bool IsActive() const;

  // -------------------------------------------------------------------------
  // Crash recovery — Layer 3 of §8
  // -------------------------------------------------------------------------

  /// Returns true when a DSP crash has been detected and the recovery delay
  /// (read from chain.json "settings".recovery_delay_ms) has elapsed.
  /// Cheap: only atomics + a steady_clock read.  Safe on any thread.
  bool NeedsReset() const;

  /// Tear down and reload the add-on DLL, then re-initialize the stream.
  /// Must only be called from the AE worker thread (same thread as Init/Configure).
  /// @param fmt  The current internal audio format — passed to OnConfigure() on success.
  /// @return true if recovery succeeded; false if max attempts exceeded or reload failed.
  bool TryReset(const AEAudioFormat& fmt);

private:
#ifdef TARGET_WINDOWS
  void StreamCreate(const AEAudioFormat& fmt);
  void StreamDestroy();

  HMODULE              m_hDll   = nullptr;
  AudioDSP*            m_funcs  = nullptr;   ///< heap-alloc'd to avoid large struct in header
  ADDON_HANDLE_STRUCT* m_handle = nullptr;   ///< per-stream handle

  AEAudioFormat    m_streamFmt{};
  bool             m_streamActive = false;
  bool             m_initialized  = false;
  std::atomic<bool> m_dspFailed{false};

  // -------------------------------------------------------------------------
  // Recovery state (§8 Layer 3)
  // m_dspNeedsReset and m_failedAt are written by the render thread and read
  // by the AE worker thread; both use only atomics / steady_clock, no locks.
  // m_recoveryAttempts, m_maxRecoveryAttempts, m_recoveryDelayMs are only
  // touched on the AE worker thread.
  // -------------------------------------------------------------------------
  std::atomic<bool>  m_dspNeedsReset{false};     ///< crash flagged; worker thread should TryReset
  std::atomic<int>   m_recoveryAttempts{0};       ///< crash counter for this DLL load
  std::chrono::steady_clock::time_point m_failedAt{};  ///< when m_dspFailed was first set
  int m_maxRecoveryAttempts = 3;   ///< cap on reload attempts; overridden by chain.json
  int m_recoveryDelayMs     = 30000; ///< ms before first reload attempt; overridden by chain.json

  // Scratch buffers for deinterleaving interleaved float input
  // (allocated once in StreamCreate, sized to [channels][frames])
  std::vector<std::vector<float>> m_scratch;
  std::vector<float*>             m_scratchPtrs;

  // String storage for AE_DSP_PROPERTIES — must outlive the addon DLL
  std::string m_userPath;
  std::string m_addonPath;
#endif // TARGET_WINDOWS
};

} // namespace ActiveAE
