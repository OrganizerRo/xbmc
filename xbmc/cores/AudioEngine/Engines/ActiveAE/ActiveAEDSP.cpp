/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

/*
 * ActiveAEDSP.cpp — Windows ADSP integration for ActiveAE
 *
 * Bridges the ActiveAE audio pipeline to a legacy ADSP add-on
 * (type "xbmc.audiodsp", e.g. audiodsp.vsthost).
 *
 * All real logic is inside #ifdef TARGET_WINDOWS to keep non-Windows builds
 * clean and compilation-error-free.
 */

#include "ActiveAEDSP.h"

#ifdef TARGET_WINDOWS

#include "ServiceBroker.h"
#include "addons/AddonInfo.h"
#include "addons/AddonManager.h"
#include "addons/binary-addons/AddonDll.h"
#include "filesystem/Directory.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/log.h"

// Legacy ADSP add-on API (from kodi-audiodsp-vsthost/include/kodi-legacy-adsp/)
#include "kodi_adsp_types.h"

#include <cstring>
#include <windows.h>

using namespace ADDON;

namespace ActiveAE
{

// ---------------------------------------------------------------------------
// Log bridge — forwards addon log messages into kodi.log via CLog::Log.
// Severity constants match VSTLog.h (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR).
// This is a plain C function so it can be passed as a function pointer to
// the addon's ADDON_SetLogCallback export.
// ---------------------------------------------------------------------------

static void vstLogBridge(int level, const char* msg)
{
    switch (level)
    {
    case 0:  CLog::Log(LOGDEBUG,   "{}", msg); break;
    case 1:  CLog::Log(LOGINFO,    "{}", msg); break;
    case 2:  CLog::Log(LOGWARNING, "{}", msg); break;
    case 3:  CLog::Log(LOGERROR,   "{}", msg); break;
    default: CLog::Log(LOGDEBUG,   "{}", msg); break;
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CActiveAEDSP::CActiveAEDSP()
  : m_funcs(new AudioDSP{})
  , m_handle(new ADDON_HANDLE_STRUCT{})
{
}

CActiveAEDSP::~CActiveAEDSP()
{
  Deinit();
  delete m_funcs;
  delete m_handle;
}

// ---------------------------------------------------------------------------
// Init — load and start the first enabled ADDON_AUDIODSP add-on
// ---------------------------------------------------------------------------

bool CActiveAEDSP::Init()
{
  if (m_initialized)
    return true;

  VECADDONS addons;
  if (!CServiceBroker::GetAddonMgr().GetAddons(addons, ADDON_AUDIODSP) || addons.empty())
  {
    CLog::Log(LOGINFO, "CActiveAEDSP::Init — no enabled ADDON_AUDIODSP add-on found");
    return false;
  }

  auto& addon = addons.front();

  // Downcast to CAddonDll to obtain the platform DLL path.
  auto addonDll = std::dynamic_pointer_cast<CAddonDll>(addon);
  if (!addonDll)
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — could not cast add-on to CAddonDll");
    return false;
  }

  const std::string libPath = addonDll->LibPath();
  if (libPath.empty())
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — add-on '{}' has no DLL for this platform",
              addon->ID());
    return false;
  }

  // Load the DLL directly with LoadLibraryW, bypassing DllAddon::Load() which
  // mandates ADDON_GetTypeVersion as a non-optional export.  Legacy ADSP DLLs
  // do not export ADDON_GetTypeVersion — they predate that API version check.
  // NOTE: audiodsp.vsthost makes no callbacks into Kodi (it uses no libKODI_adsp
  // host-callback stubs), so nullptr is safe as the host-callbacks argument.
  // If a future ADSP add-on needs host callbacks this approach must be revisited.
  {
    const std::wstring wlibPath(libPath.begin(), libPath.end());
    m_hDll = LoadLibraryW(wlibPath.c_str());
  }
  if (!m_hDll)
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — LoadLibraryW failed for '{}' (error {})",
              libPath, GetLastError());
    return false;
  }

  // Fill the function-pointer table via get_addon().
  using get_addon_t = void (__cdecl*)(void*);
  auto getAddonFn = reinterpret_cast<get_addon_t>(GetProcAddress(m_hDll, "get_addon"));
  if (!getAddonFn)
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — 'get_addon' not found in '{}'", libPath);
    FreeLibrary(m_hDll);
    m_hDll = nullptr;
    return false;
  }
  std::memset(m_funcs, 0, sizeof(AudioDSP));
  getAddonFn(m_funcs);

  // Register our log bridge so the addon's internal messages (VSTLog) appear
  // in kodi.log.  The addon exports ADDON_SetLogCallback as a plain extern "C"
  // function; resolve it directly from m_hDll.
  {
    // VSTLog.h is an addon-internal header and is not on Kodi's include path,
    // so we declare the callback signature locally rather than including it.
    // The signature must stay in sync with VSTLogCallback in VSTLog.h:
    //   void (*)(int level, const char* msg)
    // ADDON_SetLogCallback is a setter that *accepts* a callback pointer,
    // so its signature is: void ADDON_SetLogCallback(VSTLogCallback_t cb)
    using VSTLogCallback_t = void (*)(int level, const char* msg);
    using ADDON_SetLogCallback_t = void (*)(VSTLogCallback_t cb);
    auto setLog = reinterpret_cast<ADDON_SetLogCallback_t>(
        GetProcAddress(m_hDll, "ADDON_SetLogCallback"));

    if (setLog)
    {
      setLog(vstLogBridge);
      CLog::Log(LOGDEBUG,
                "CActiveAEDSP::Init — log callback registered; addon messages will appear in kodi.log");
    }
    else
    {
      CLog::Log(LOGWARNING,
                "CActiveAEDSP::Init — ADDON_SetLogCallback not found in '{}'; "
                "addon log messages will not appear in kodi.log",
                libPath);
    }

    // Query chain.json recovery settings from the addon.
    // ADDON_Create (called below) pre-loads chain.json, so the actual
    // GetRecoveryParams call is deferred to after Create() returns.
  }

  // Prepare user-data and add-on-path strings (must outlive ADDON_Create call).
  const std::string specialPath = "special://profile/addon_data/" + addon->ID() + "/";
  XFILE::CDirectory::Create(specialPath);
  m_userPath  = CSpecialProtocol::TranslatePath(specialPath);
  m_addonPath = addon->Path();

  AE_DSP_PROPERTIES props{};
  props.strUserPath  = m_userPath.c_str();
  props.strAddonPath = m_addonPath.c_str();

  // ADDON_Create(hdl, props) — hdl is the host-callback pointer (nullptr is
  // safe here because the add-on makes no callbacks into Kodi).
  CLog::Log(LOGINFO,
            "CActiveAEDSP::Init — calling ADDON_Create for '{}'; "
            "named pipe \\\\.\\pipe\\kodi_vsthost_editor will start",
            addon->Name());
  using ADDON_Create_t = ADDON_STATUS (__cdecl*)(void*, void*);
  auto createFn = reinterpret_cast<ADDON_Create_t>(GetProcAddress(m_hDll, "ADDON_Create"));
  if (!createFn)
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — 'ADDON_Create' not found in '{}'", libPath);
    FreeLibrary(m_hDll);
    m_hDll = nullptr;
    return false;
  }
  const ADDON_STATUS status = createFn(nullptr, &props);
  if (status != ADDON_STATUS_OK && status != ADDON_STATUS_NEED_SETTINGS)
  {
    CLog::Log(LOGERROR, "CActiveAEDSP::Init — ADDON_Create returned error {}", (int)status);
    using ADDON_Destroy_t = void (__cdecl*)();
    auto destroyFn = reinterpret_cast<ADDON_Destroy_t>(GetProcAddress(m_hDll, "ADDON_Destroy"));
    if (destroyFn)
      destroyFn();
    FreeLibrary(m_hDll);
    m_hDll = nullptr;
    return false;
  }

  // Now that ADDON_Create has run (and pre-loaded chain.json settings),
  // retrieve the recovery params from the addon via ADDON_GetRecoveryParams.
  {
    using ADDON_GetRecoveryParams_t = void (__cdecl*)(int*, int*);
    auto getParams = reinterpret_cast<ADDON_GetRecoveryParams_t>(
        GetProcAddress(m_hDll, "ADDON_GetRecoveryParams"));
    if (getParams)
    {
      getParams(&m_recoveryDelayMs, &m_maxRecoveryAttempts);
      CLog::Log(LOGINFO,
                "CActiveAEDSP::Init — recovery params from chain.json: "
                "delay={}ms, maxAttempts={}",
                m_recoveryDelayMs, m_maxRecoveryAttempts);
    }
    else
    {
      CLog::Log(LOGINFO,
                "CActiveAEDSP::Init — ADDON_GetRecoveryParams not found; "
                "using defaults: delay={}ms, maxAttempts={}",
                m_recoveryDelayMs, m_maxRecoveryAttempts);
    }
  }

  m_initialized = true;
  m_dspFailed   = false;
  CLog::Log(LOGINFO, "CActiveAEDSP::Init — loaded '{}'", addon->Name());
  return true;
}

// ---------------------------------------------------------------------------
// Deinit
// ---------------------------------------------------------------------------

void CActiveAEDSP::Deinit()
{
  if (!m_initialized)
    return;

  StreamDestroy();

  if (m_hDll)
  {
    CLog::Log(LOGINFO,
              "CActiveAEDSP::Deinit — calling ADDON_Destroy; "
              "named pipe \\\\.\\pipe\\kodi_vsthost_editor will stop");
    using ADDON_Destroy_t = void (__cdecl*)();
    auto destroyFn = reinterpret_cast<ADDON_Destroy_t>(GetProcAddress(m_hDll, "ADDON_Destroy"));
    if (destroyFn)
      destroyFn();
    FreeLibrary(m_hDll);
    m_hDll = nullptr;
  }

  std::memset(m_funcs, 0, sizeof(AudioDSP));

  m_initialized  = false;
  m_dspFailed    = false;
  // Reset recovery counters on every clean teardown so the next plugin load
  // starts with a fresh attempt budget.
  m_recoveryAttempts = 0;
  m_dspNeedsReset    = false;
  CLog::Log(LOGINFO, "CActiveAEDSP::Deinit — ADSP add-on unloaded");
}

// ---------------------------------------------------------------------------
// OnConfigure — re-initialize the DSP stream when the AE format changes
// ---------------------------------------------------------------------------

void CActiveAEDSP::OnConfigure(const AEAudioFormat& fmt)
{
  if (!m_initialized || m_dspFailed.load())
    return;

  // Skip if the format has not changed.
  if (m_streamActive &&
      m_streamFmt.m_sampleRate == fmt.m_sampleRate &&
      m_streamFmt.m_channelLayout.Count() == fmt.m_channelLayout.Count() &&
      m_streamFmt.m_frames == fmt.m_frames)
    return;

  if (m_streamActive)
    StreamDestroy();

  StreamCreate(fmt);
}

// ---------------------------------------------------------------------------
// StreamCreate — private helper
// ---------------------------------------------------------------------------

void CActiveAEDSP::StreamCreate(const AEAudioFormat& fmt)
{
  if (!m_initialized || !m_funcs->StreamCreate || m_dspFailed.load())
    return;

  const int channels  = static_cast<int>(fmt.m_channelLayout.Count());
  const int blockSize = (fmt.m_frames > 0) ? fmt.m_frames : 1024;

  AE_DSP_SETTINGS settings{};
  settings.iStreamID                = 0;
  settings.iStreamType              = AE_DSP_ASTREAM_BASIC;
  settings.iInChannels              = channels;
  // NOTE: lInChannelPresentFlags / lOutChannelPresentFlags are set to 0 (unknown)
  // because ActiveAE's AEChannelInfo does not map directly to AE_DSP_PRSNT_CH_* flags.
  // audiodsp.vsthost ignores these fields; a future improvement could translate them
  // using CAEUtil::GetAVChannelLayout if a more strict ADSP add-on requires them.
  settings.lInChannelPresentFlags   = 0;
  settings.iInFrames                = blockSize;
  settings.iInSamplerate            = fmt.m_sampleRate;
  settings.iProcessFrames           = blockSize;
  settings.iProcessSamplerate       = fmt.m_sampleRate;
  settings.iOutChannels             = channels;
  settings.lOutChannelPresentFlags  = 0;
  settings.iOutFrames               = blockSize;
  settings.iOutSamplerate           = fmt.m_sampleRate;
  settings.bInputResamplingActive   = false;
  settings.bStereoUpmix             = false;
  settings.iQualityLevel            = 0;

  std::memset(m_handle, 0, sizeof(ADDON_HANDLE_STRUCT));
  m_handle->callerAddress = this;

  const AE_DSP_ERROR err = m_funcs->StreamCreate(&settings, nullptr, m_handle);
  if (err != AE_DSP_ERROR_NO_ERROR)
  {
    CLog::Log(LOGWARNING, "CActiveAEDSP::StreamCreate — returned error {}", (int)err);
    return;
  }

  // Inform the add-on which master-process mode is active (mode id 1).
  if (m_funcs->StreamIsModeSupported)
    m_funcs->StreamIsModeSupported(m_handle, AE_DSP_MODE_TYPE_MASTER_PROCESS, 1, 0);
  if (m_funcs->MasterProcessSetMode)
    m_funcs->MasterProcessSetMode(m_handle, AE_DSP_ASTREAM_BASIC, 1, 0);
  if (m_funcs->StreamInitialize)
    m_funcs->StreamInitialize(m_handle, &settings);

  // Allocate scratch buffers used for deinterleaving interleaved PCM input.
  m_scratch.assign(channels, std::vector<float>(static_cast<size_t>(blockSize), 0.0f));
  m_scratchPtrs.resize(channels);
  for (int ch = 0; ch < channels; ++ch)
    m_scratchPtrs[ch] = m_scratch[ch].data();

  m_streamFmt    = fmt;
  m_streamActive = true;

  CLog::Log(LOGINFO,
            "CActiveAEDSP::StreamCreate — {}ch @ {}Hz, {}fr",
            channels, fmt.m_sampleRate, blockSize);
}

// ---------------------------------------------------------------------------
// StreamDestroy — private helper
// ---------------------------------------------------------------------------

void CActiveAEDSP::StreamDestroy()
{
  if (!m_streamActive)
    return;

  if (m_funcs->StreamDestroy)
    m_funcs->StreamDestroy(m_handle);

  m_streamActive = false;
  std::memset(m_handle, 0, sizeof(ADDON_HANDLE_STRUCT));
  m_scratch.clear();
  m_scratchPtrs.clear();
}

// ---------------------------------------------------------------------------
// MasterProcess — in-place audio processing on the AE render thread
// ---------------------------------------------------------------------------

void CActiveAEDSP::MasterProcess(CSampleBuffer* buf)
{
  if (!m_streamActive || m_dspFailed.load() || !m_funcs->MasterProcess)
    return;
  if (!buf || !buf->pkt || buf->pkt->nb_samples <= 0)
    return;

  const int     channels = buf->pkt->config.channels;
  const unsigned int samples = static_cast<unsigned int>(buf->pkt->nb_samples);

  if (buf->pkt->planes > 1)
  {
    // Planar float (AE_FMT_FLOATP) — process directly in-place.
    auto** planes = reinterpret_cast<float**>(buf->pkt->data);

    __try
    {
      m_funcs->MasterProcess(m_handle, planes, planes, samples);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
      CLog::Log(LOGERROR,
                "CActiveAEDSP::MasterProcess — add-on crashed (planar path); disabling DSP");
      m_dspFailed     = true;
      m_dspNeedsReset = true;
      m_failedAt      = std::chrono::steady_clock::now();
    }
  }
  else
  {
    // Interleaved float (AE_FMT_FLOAT) — deinterleave, process, reinterleave.
    if (channels <= 0 || static_cast<int>(m_scratchPtrs.size()) != channels)
      return;

    const size_t bytesPerCh = static_cast<size_t>(samples) * sizeof(float);
    if (m_scratch.empty() || static_cast<unsigned int>(m_scratch[0].size()) < samples)
    {
      // Resize scratch buffers if the block size grew (rare).
      for (auto& v : m_scratch)
        v.assign(samples, 0.0f);
    }

    // Deinterleave: interleaved[sample * ch + ch_idx] → scratch[ch][sample]
    const float* src = reinterpret_cast<const float*>(buf->pkt->data[0]);
    for (unsigned int s = 0; s < samples; ++s)
      for (int ch = 0; ch < channels; ++ch)
        m_scratchPtrs[ch][s] = src[s * channels + ch];

    __try
    {
      m_funcs->MasterProcess(m_handle,
                             m_scratchPtrs.data(),
                             m_scratchPtrs.data(),
                             samples);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
      CLog::Log(LOGERROR,
                "CActiveAEDSP::MasterProcess — add-on crashed (interleaved path); disabling DSP");
      m_dspFailed     = true;
      m_dspNeedsReset = true;
      m_failedAt      = std::chrono::steady_clock::now();
      return;
    }

    // Reinterleave: scratch[ch][sample] → interleaved[sample * ch + ch_idx]
    float* dst = reinterpret_cast<float*>(buf->pkt->data[0]);
    for (unsigned int s = 0; s < samples; ++s)
      for (int ch = 0; ch < channels; ++ch)
        dst[s * channels + ch] = m_scratchPtrs[ch][s];
  }
}

// ---------------------------------------------------------------------------
// IsActive
// ---------------------------------------------------------------------------

bool CActiveAEDSP::IsActive() const
{
  return m_initialized && !m_dspFailed.load();
}

// ---------------------------------------------------------------------------
// NeedsReset — cheap check callable from any thread
// ---------------------------------------------------------------------------

bool CActiveAEDSP::NeedsReset() const
{
  if (!m_dspNeedsReset.load())
    return false;
  // Gate behind the configured recovery delay so we don't hammer LoadLibraryW.
  const auto elapsed = std::chrono::steady_clock::now() - m_failedAt;
  return elapsed >= std::chrono::milliseconds(m_recoveryDelayMs);
}

// ---------------------------------------------------------------------------
// TryReset — called from the AE worker thread only
// ---------------------------------------------------------------------------

bool CActiveAEDSP::TryReset(const AEAudioFormat& fmt)
{
  if (!m_dspFailed.load())
    return true;  // nothing to do

  // Increment crash counter; if we have already exhausted the budget, give up.
  const int attempt = m_recoveryAttempts.fetch_add(1) + 1;
  if (attempt > m_maxRecoveryAttempts)
  {
    CLog::Log(LOGWARNING,
              "CActiveAEDSP::TryReset — max recovery attempts ({}) exhausted; "
              "DSP remains disabled for this session",
              m_maxRecoveryAttempts);
    m_dspNeedsReset = false;
    return false;
  }

  CLog::Log(LOGINFO,
            "CActiveAEDSP::TryReset — attempt {}/{}: tearing down crashed add-on",
            attempt, m_maxRecoveryAttempts);

  // Deinit() calls FreeLibrary on the crashed DLL which may itself fault.
  // Use SEH to survive a corrupt unload and force-zero state if needed.
  __try
  {
    Deinit();
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    CLog::Log(LOGERROR,
              "CActiveAEDSP::TryReset — Deinit() faulted on attempt {}; "
              "force-clearing add-on state",
              attempt);
    // Manually null out all state without invoking DLL functions.
    m_hDll         = nullptr;
    m_streamActive = false;
    m_initialized  = false;
    std::memset(m_funcs, 0, sizeof(AudioDSP));
    m_scratch.clear();
    m_scratchPtrs.clear();
    // m_recoveryAttempts and m_dspNeedsReset will be corrected below.
  }

  // Deinit() resets m_recoveryAttempts to 0 — restore the count so the cap
  // accumulates across crashes within the same session.
  m_recoveryAttempts = attempt;

  // Clear failure flags so Init() and OnConfigure() are no longer gated.
  m_dspFailed     = false;
  m_dspNeedsReset = false;

  CLog::Log(LOGINFO,
            "CActiveAEDSP::TryReset — attempt {}: reloading add-on DLL",
            attempt);

  if (!Init())
  {
    CLog::Log(LOGERROR,
              "CActiveAEDSP::TryReset — attempt {}: Init() failed; "
              "re-arming recovery timer (next attempt in {}ms)",
              attempt, m_recoveryDelayMs);
    m_dspFailed     = true;
    m_dspNeedsReset = true;
    m_failedAt      = std::chrono::steady_clock::now();
    return false;
  }

  OnConfigure(fmt);
  CLog::Log(LOGINFO,
            "CActiveAEDSP::TryReset — DSP recovered successfully on attempt {}/{}",
            attempt, m_maxRecoveryAttempts);
  return true;
}

} // namespace ActiveAE

#else // !TARGET_WINDOWS — stub implementations (no-ops)

namespace ActiveAE
{

CActiveAEDSP::CActiveAEDSP()  = default;
CActiveAEDSP::~CActiveAEDSP() = default;

bool CActiveAEDSP::Init()                          { return false; }
void CActiveAEDSP::Deinit()                        {}
void CActiveAEDSP::OnConfigure(const AEAudioFormat&) {}
void CActiveAEDSP::MasterProcess(CSampleBuffer*)   {}
bool CActiveAEDSP::IsActive() const                { return false; }
bool CActiveAEDSP::NeedsReset() const              { return false; }
bool CActiveAEDSP::TryReset(const AEAudioFormat&)  { return false; }

} // namespace ActiveAE

#endif // TARGET_WINDOWS
