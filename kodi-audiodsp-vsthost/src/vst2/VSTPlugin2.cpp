/*
 * VSTPlugin2.cpp — VST2 plugin host wrapper implementation
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 *
 * Key implementation notes:
 *  - effSetSampleRate passes the rate through `opt` (float), NOT `value`.
 *  - getParameterName allocates a 64-byte buffer even though the VST2 spec
 *    says 8 bytes; almost every real plugin overruns the 8-byte limit.
 *  - callPluginMainSafe() uses SEH (__try/__except) and is kept as its own
 *    function with no C++ objects in scope to avoid MSVC C4509 warnings
 *    ("nonstandard extension used: SEH and C++ destructors").
 */

#include "VSTPlugin2.h"
#include "../util/VSTLog.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

VSTPlugin2::VSTPlugin2(const std::string& path)
    : m_path(path)
{
}

VSTPlugin2::~VSTPlugin2()
{
    unload();
}

// ---------------------------------------------------------------------------
// callPluginMainSafe — SEH wrapper
//
// This MUST be a standalone function (not a lambda, not inlined into load())
// because MSVC prohibits mixing C++ object destructors and __try/__except in
// the same function scope.  By isolating the SEH here we keep the rest of
// load() clean.
// ---------------------------------------------------------------------------

AEffect* VSTPlugin2::callPluginMainSafe(VSTENTRYPROC proc)
{
    AEffect* result = nullptr;
    __try {
        result = proc(staticAudioMaster);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = nullptr;
    }
    return result;
}

// ---------------------------------------------------------------------------
// callEditOpenSafe — SEH wrapper for effEditOpen
//
// Same rationale as callPluginMainSafe: isolated to avoid MSVC C4509.
//
// outException is set to true when the plugin threw a structured exception.
// The return value is whatever the plugin's dispatcher returned (which some
// plugins legitimately set to 0 or even -1 to indicate success — the old
// Arakula reference host intentionally removed the "if (l > 0)" guard for
// this reason).  Callers must only fail on outException, not on return value.
// ---------------------------------------------------------------------------

VstIntPtr VSTPlugin2::callEditOpenSafe(AEffect* effect, void* parentWindow,
                                        bool& outException)
{
    VstIntPtr result = 0;
    BOOL threw = FALSE;              // POD — safe inside __try/__except scope
    __try {
        result = effect->dispatcher(effect, effEditOpen, 0, 0, parentWindow, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        threw = TRUE;
    }
    outException = (threw != FALSE);
    return result;
}

bool VSTPlugin2::callEditGetRectSafe(AEffect* effect, ERect** rectOut)
{
    if (!effect || !rectOut)
        return false;
    *rectOut = nullptr;
    __try {
        effect->dispatcher(effect, effEditGetRect, 0, 0, rectOut, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *rectOut = nullptr;
        return false;
    }
    return *rectOut != nullptr;
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

bool VSTPlugin2::load(double sampleRate, int maxBlockSize, int numChannels)
{
    if (m_loaded)
        unload();

    m_sampleRate  = sampleRate;
    m_blockSize   = maxBlockSize;
    m_numChannels = numChannels;

    // --- 1. Load the DLL -------------------------------------------------------
    // Convert UTF-8 path to wide string for LoadLibraryW
    int wlen = MultiByteToWideChar(CP_UTF8, 0, m_path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, m_path.c_str(), -1, wpath.data(), wlen);

    m_hModule = LoadLibraryW(wpath.c_str());
    if (!m_hModule)
    {
        VSTLOG(VSTLOG_ERROR, "[VSTPlugin2] LoadLibraryW failed for '%s': error %lu",
               m_path.c_str(), GetLastError());
        return false;
    }

    // --- 2. Locate entry point -------------------------------------------------
    VSTENTRYPROC proc = reinterpret_cast<VSTENTRYPROC>(
        GetProcAddress(m_hModule, "VSTPluginMain"));
    if (!proc)
        proc = reinterpret_cast<VSTENTRYPROC>(
            GetProcAddress(m_hModule, "main"));

    if (!proc) {
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] No VSTPluginMain or main entry point found in '%s'",
               m_path.c_str());
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        return false;
    }

    // --- 3. Instantiate the plugin (SEH-guarded) --------------------------------
    m_effect = callPluginMainSafe(proc);
    if (!m_effect) {
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] Plugin entry point returned null (crash or bad plugin): '%s'",
               m_path.c_str());
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        return false;
    }

    // --- 4. Validate magic number -----------------------------------------------
    if (m_effect->magic != kEffectMagic) {
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] Invalid VST2 magic 0x%08X in '%s' (expected 0x%08X)",
               m_effect->magic, m_path.c_str(), kEffectMagic);
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
        m_effect  = nullptr;
        return false;
    }

    // --- 5. Store host context so audioMaster callback can recover this instance
    m_effect->user = this;

    // --- 6. Open the plugin -----------------------------------------------------
    m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);

    // --- 7. Set sample rate — CRITICAL: pass via `opt` (float), NOT `value`! ----
    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr,
                         static_cast<float>(m_sampleRate));

    // --- 8. Set block size — pass via `value` ------------------------------------
    m_effect->dispatcher(m_effect, effSetBlockSize, 0,
                         static_cast<VstIntPtr>(m_blockSize), nullptr, 0.0f);

    // --- 9. Resume (mains active) ------------------------------------------------
    m_effect->dispatcher(m_effect, effMainsChanged, 0, 1, nullptr, 0.0f);

    // --- 10. Retrieve plugin name -------------------------------------------------
    {
        char nameBuf[64] = {};
        m_effect->dispatcher(m_effect, effGetEffectName, 0, 0, nameBuf, 0.0f);
        if (nameBuf[0] != '\0') {
            m_name = nameBuf;
        } else {
            // Fall back to the filename without extension
            try {
                m_name = std::filesystem::path(m_path).stem().string();
            } catch (...) {
                m_name = m_path;
            }
        }
    }

    // --- 10b. Cache plugin directory for audioMasterGetDirectory -----------------
    try {
        m_pluginDir = std::filesystem::path(m_path).parent_path().string();
    } catch (...) {
        m_pluginDir = m_path;
    }

    // --- 11. Retrieve vendor string ----------------------------------------------
    {
        char vendorBuf[64] = {};
        m_effect->dispatcher(m_effect, effGetVendorString, 0, 0, vendorBuf, 0.0f);
        m_vendor = vendorBuf;
    }

    // --- 12. Allocate scratch buffers --------------------------------------------
    allocateScratchBuffers();

    // --- 13. Initialise VstTimeInfo so audioMasterGetTime returns valid data ------
    m_timeInfo = {};
    m_timeInfo.sampleRate         = m_sampleRate;
    m_timeInfo.tempo              = 120.0;
    m_timeInfo.timeSigNumerator   = 4;
    m_timeInfo.timeSigDenominator = 4;
    m_timeInfo.flags              = kVstTempoValid | kVstTimeSigValid;

    // --- 14. Mark as loaded and active ------------------------------------------
    m_loaded = true;
    m_active = true;

    VSTLOG(VSTLOG_INFO,
           "[VSTPlugin2] Loaded '%s' — vendor: '%s', params: %d, in: %d ch, out: %d ch",
           m_name.c_str(), m_vendor.c_str(),
           m_effect->numParams, m_effect->numInputs, m_effect->numOutputs);

    return true;
}

// ---------------------------------------------------------------------------
// unload()
// ---------------------------------------------------------------------------

void VSTPlugin2::unload()
{
    if (m_effect) {
        if (m_active) {
            m_effect->dispatcher(m_effect, effMainsChanged, 0, 0, nullptr, 0.0f);
            m_active = false;
        }
        m_effect->dispatcher(m_effect, effClose, 0, 0, nullptr, 0.0f);
        m_effect = nullptr;
    }

    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }

    m_loaded = false;

    // Release scratch memory
    m_inputBufs.clear();
    m_outputBufs.clear();
    m_inputPtrs.clear();
    m_outputPtrs.clear();
}

// ---------------------------------------------------------------------------
// allocateScratchBuffers()
// ---------------------------------------------------------------------------

void VSTPlugin2::allocateScratchBuffers()
{
    m_inputBufs.assign(m_numChannels, std::vector<float>(m_blockSize, 0.0f));
    m_outputBufs.assign(m_numChannels, std::vector<float>(m_blockSize, 0.0f));

    m_inputPtrs.resize(m_numChannels);
    m_outputPtrs.resize(m_numChannels);

    for (int i = 0; i < m_numChannels; ++i) {
        m_inputPtrs[i]  = m_inputBufs[i].data();
        m_outputPtrs[i] = m_outputBufs[i].data();
    }
}

// ---------------------------------------------------------------------------
// drainParamQueue() — called at the top of process() on the audio thread
// ---------------------------------------------------------------------------

void VSTPlugin2::drainParamQueue()
{
    ParamChange2 change;
    while (m_paramQueue.pop(change)) {
        // VST2 parameter set is a direct function-pointer call, NOT a
        // dispatcher opcode.  effSetParameter does not exist in the ABI.
        if (m_effect)
            m_effect->setParameter(m_effect, change.index, change.value);
    }
}

// ---------------------------------------------------------------------------
// process()
// ---------------------------------------------------------------------------

int VSTPlugin2::process(float** in, float** out, int samples)
{
    // Apply queued parameter changes before touching audio
    drainParamQueue();

    // Advance time position (used by audioMasterGetTime for plugin UIs)
    m_timeInfo.samplePos += static_cast<double>(samples);

    // Bypass: pass input straight through
    if (!m_loaded || m_bypassed) {
        for (int ch = 0; ch < m_numChannels; ++ch)
            std::copy(in[ch], in[ch] + samples, out[ch]);
        return samples;
    }

    // Determine actual I/O channel counts reported by the plugin
    const int pluginIns  = m_effect->numInputs;
    const int pluginOuts = m_effect->numOutputs;

    // Fill input scratch buffers from caller's input pointers
    const int inChannels = std::min(pluginIns, m_numChannels);
    for (int ch = 0; ch < inChannels; ++ch)
        std::copy(in[ch], in[ch] + samples, m_inputPtrs[ch]);

    // Zero any extra plugin input channels that the host doesn't supply
    for (int ch = inChannels; ch < pluginIns && ch < m_numChannels; ++ch)
        std::fill(m_inputPtrs[ch], m_inputPtrs[ch] + samples, 0.0f);

    // Clear output scratch buffers
    for (int ch = 0; ch < m_numChannels; ++ch)
        std::fill(m_outputPtrs[ch], m_outputPtrs[ch] + samples, 0.0f);

    // Run the plugin (guard against legacy VST2 plugins with null processReplacing)
    if (m_effect->processReplacing)
    {
        m_effect->processReplacing(m_effect,
                                   m_inputPtrs.data(),
                                   m_outputPtrs.data(),
                                   samples);
    }
    else
    {
        // Very old VST2 plugin — no processReplacing; passthrough.
        for (int ch = 0; ch < m_numChannels; ++ch)
            std::copy(m_inputPtrs[ch], m_inputPtrs[ch] + samples, m_outputPtrs[ch]);
    }

    // Copy plugin outputs to caller's output buffers
    const int outChannels = std::min(pluginOuts, m_numChannels);
    for (int ch = 0; ch < outChannels; ++ch)
        std::copy(m_outputPtrs[ch], m_outputPtrs[ch] + samples, out[ch]);

    // Mono fold-down: if plugin has 1 output but host expects 2 channels,
    // duplicate the mono output to the second channel
    if (pluginOuts == 1 && m_numChannels >= 2)
        std::copy(out[0], out[0] + samples, out[1]);

    return samples;
}

// ---------------------------------------------------------------------------
// setParameter() — thread-safe; queues change for audio thread delivery
// ---------------------------------------------------------------------------

void VSTPlugin2::setParameter(int index, float value)
{
    m_paramQueue.push(ParamChange2{index, value});
}

// ---------------------------------------------------------------------------
// getParameter() — approximately thread-safe (read-only, display purposes)
// ---------------------------------------------------------------------------

float VSTPlugin2::getParameter(int index) const
{
    if (!m_loaded || !m_effect)
        return 0.0f;
    return m_effect->getParameter(m_effect, index);
}

// ---------------------------------------------------------------------------
// getParameterCount()
// ---------------------------------------------------------------------------

int VSTPlugin2::getParameterCount() const
{
    return m_effect ? m_effect->numParams : 0;
}

// ---------------------------------------------------------------------------
// getParameterName()
//
// The VST2 spec mandates only 8 bytes, but virtually every plugin writes
// more.  We allocate 64 bytes to avoid buffer overruns.
// ---------------------------------------------------------------------------

std::string VSTPlugin2::getParameterName(int index) const
{
    if (!m_loaded || !m_effect)
        return {};
    char buf[64] = {};
    m_effect->dispatcher(m_effect, effGetParamName, index, 0, buf, 0.0f);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// getLatencySamples()
// ---------------------------------------------------------------------------

int VSTPlugin2::getLatencySamples() const
{
    return m_effect ? m_effect->initialDelay : 0;
}

// ---------------------------------------------------------------------------
// saveState()
//
// Format (matches task3_interface.md contract):
//   First byte 'C' + chunk blob  — if plugin supports effFlagsProgramChunks
//   First byte 'P' + raw floats  — otherwise
// ---------------------------------------------------------------------------

std::vector<uint8_t> VSTPlugin2::saveState() const
{
    if (!m_loaded || !m_effect)
        return {};

    if (m_effect->flags & effFlagsProgramChunks) {
        // Plugin supports chunk-based preset storage
        void* chunkData = nullptr;
        VstIntPtr chunkSize = m_effect->dispatcher(
            m_effect, effGetChunk,
            0,           // 0 = bank chunk
            0, &chunkData, 0.0f);

        if (chunkData && chunkSize > 0) {
            std::vector<uint8_t> result;
            result.reserve(1 + static_cast<size_t>(chunkSize));
            result.push_back('C');
            const uint8_t* src = static_cast<const uint8_t*>(chunkData);
            result.insert(result.end(), src, src + chunkSize);
            return result;
        }
    }

    // Fall back to saving all parameters as raw floats
    const int count = m_effect->numParams;
    std::vector<uint8_t> result;
    result.reserve(1 + static_cast<size_t>(count) * sizeof(float));
    result.push_back('P');

    for (int i = 0; i < count; ++i) {
        float v = m_effect->getParameter(m_effect, i);
        uint8_t bytes[sizeof(float)];
        std::memcpy(bytes, &v, sizeof(float));
        result.insert(result.end(), bytes, bytes + sizeof(float));
    }

    return result;
}

// ---------------------------------------------------------------------------
// loadState()
// ---------------------------------------------------------------------------

bool VSTPlugin2::loadState(const std::vector<uint8_t>& data)
{
    if (!m_loaded || !m_effect || data.empty())
        return false;

    const uint8_t tag = data[0];

    if (tag == 'C') {
        // Chunk mode
        if (data.size() < 2)
            return false;
        const size_t chunkSize = data.size() - 1;
        // We need a non-const copy because dispatcher takes void* (not const void*)
        std::vector<uint8_t> blob(data.begin() + 1, data.end());
        m_effect->dispatcher(m_effect, effSetChunk,
                             0,   // 0 = bank chunk
                             static_cast<VstIntPtr>(chunkSize),
                             blob.data(), 0.0f);
        return true;
    }

    if (tag == 'P') {
        // Parameter mode
        const size_t bodySize = data.size() - 1;
        if (bodySize % sizeof(float) != 0)
            return false;
        const int count = static_cast<int>(bodySize / sizeof(float));
        for (int i = 0; i < count && i < m_effect->numParams; ++i) {
            float v;
            std::memcpy(&v, data.data() + 1 + i * sizeof(float), sizeof(float));
            // VST2 parameter set is a direct function-pointer call.
            m_effect->setParameter(m_effect, i, v);
        }
        return true;
    }

    return false;  // Unknown format tag
}

// ---------------------------------------------------------------------------
// Editor support — VST2 native editor window
// ---------------------------------------------------------------------------

bool VSTPlugin2::hasEditor() const
{
    if (!m_loaded || !m_effect)
        return false;

    if ((m_effect->flags & effFlagsHasEditor) != 0)
        return true;

    ERect* rect = nullptr;
    return callEditGetRectSafe(m_effect, &rect);
}

bool VSTPlugin2::openEditor(void* parentWindow)
{
    if (!m_loaded || !m_effect || !hasEditor())
        return false;

    // Validate the parent window handle before passing it to the plugin.
    // A null or already-destroyed HWND would cause the plugin to embed into
    // an invalid window, producing no visible UI and potentially crashing.
    if (!parentWindow)
    {
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] openEditor — null parent HWND for '%s'",
               m_name.c_str());
        return false;
    }
    if (!IsWindow(static_cast<HWND>(parentWindow)))
    {
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] openEditor — parent HWND (%p) is not a valid window for '%s'",
               parentWindow, m_name.c_str());
        return false;
    }

    VSTLOG(VSTLOG_DEBUG, "[VSTPlugin2] openEditor — calling effEditOpen for '%s'", m_name.c_str());

    // Store the HWND *before* calling effEditOpen so that the legacy
    // audioMasterOpenWindow callback (fired during effEditOpen by VST 1 /
    // VST 2.0/2.1 era plugins) can reference the host window and embed the
    // plugin's UI directly into it rather than creating a stray floating window.
    m_editorHwnd = static_cast<HWND>(parentWindow);

    // Signal that we are inside effEditOpen so audioMasterOpenWindow knows
    // whether it is handling a primary-editor request (reuse host HWND) or
    // a secondary/idle-triggered window request (create a new window).
    m_inEffEditOpen = true;

    bool exceptionOccurred = false;
    VstIntPtr result = callEditOpenSafe(m_effect, parentWindow, exceptionOccurred);

    m_inEffEditOpen = false;

    if (exceptionOccurred)
    {
        // Structured exception inside the plugin — abort cleanly.
        m_editorHwnd = nullptr;
        VSTLOG(VSTLOG_ERROR,
               "[VSTPlugin2] openEditor — effEditOpen threw a structured exception in '%s'",
               m_name.c_str());
        return false;
    }

    // Do NOT fail based on the dispatcher return value alone.  The VST 2.0/2.1
    // spec is loose here: some plugins return 0 even on success, and others
    // return -1 as a non-error value.  Arakula's reference host intentionally
    // removed the "if (l > 0)" guard for this reason.
    VSTLOG(VSTLOG_DEBUG, "[VSTPlugin2] openEditor — effEditOpen returned %lld for '%s'",
           static_cast<long long>(result), m_name.c_str());

    // Probe for a child window — diagnostic only; some plugins embed via
    // SetParent() which we cannot observe directly at this point.
    HWND child = GetWindow(static_cast<HWND>(parentWindow), GW_CHILD);
    if (!child)
    {
        VSTLOG(VSTLOG_WARN,
               "[VSTPlugin2] openEditor — no child window in host HWND for '%s'; "
               "plugin may use legacy audioMasterOpenWindow or deferred embedding",
               m_name.c_str());
    }

    return true;
}

void VSTPlugin2::closeEditor()
{
    // Destroy any auxiliary windows opened via the legacy audioMasterOpenWindow
    // callback (VST 1 / VST 2.0/2.1 era plugins that use this for secondary UI).
    for (HWND hwnd : m_auxWindows)
    {
        if (IsWindow(hwnd))
            DestroyWindow(hwnd);
    }
    m_auxWindows.clear();

    if (m_loaded && m_effect)
        m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
    m_editorHwnd = nullptr;
}

bool VSTPlugin2::getEditorSize(int& width, int& height) const
{
    if (!m_loaded || !m_effect)
        return false;

    ERect* rect = nullptr;
    if (!callEditGetRectSafe(m_effect, &rect))
        return false;

    width  = static_cast<int>(rect->right  - rect->left);
    height = static_cast<int>(rect->bottom - rect->top);
    return (width > 0 && height > 0);
}

void VSTPlugin2::idleEditor()
{
    if (m_loaded && m_effect)
        m_effect->dispatcher(m_effect, effEditIdle, 0, 0, nullptr, 0.0f);
}

// ---------------------------------------------------------------------------
// staticAudioMaster — trampoline; recovers VSTPlugin2* from AEffect::user
// ---------------------------------------------------------------------------

VstIntPtr VSTCALLBACK VSTPlugin2::staticAudioMaster(
    AEffect*  effect,
    VstInt32  opcode,
    VstInt32  index,
    VstIntPtr value,
    void*     ptr,
    float     opt)
{
    if (effect && effect->user)
        return static_cast<VSTPlugin2*>(effect->user)->audioMaster(
            effect, opcode, index, value, ptr, opt);

    // Handle opcodes that arrive before the effect is fully constructed
    // (e.g. audioMasterVersion queried inside VSTPluginMain itself)
    if (opcode == audioMasterVersion)
        return 2400;

    return 0;
}

// ---------------------------------------------------------------------------
// audioMaster() — per-instance host callback dispatcher
// ---------------------------------------------------------------------------

VstIntPtr VSTPlugin2::audioMaster(
    AEffect*  /*effect*/,
    VstInt32  opcode,
    VstInt32  index,
    VstIntPtr value,
    void*     ptr,
    float     /*opt*/)
{
    switch (opcode)
    {
    case audioMasterVersion:
        return 2400;

    case audioMasterIdle:
        // Plugin requests idle time — nothing to do in a realtime host.
        return 0;

    case audioMasterNeedIdle:
        // Deprecated (VST 2.4) — plugin wants idle calls (effIdle / effEditIdle).
        // We already call effEditIdle on a timer; acknowledge to satisfy the plugin.
        return 1;

    case audioMasterGetTime:
        // Return a pointer to our VstTimeInfo struct.  The plugin must not
        // free or cache this pointer across calls; it is always valid for the
        // lifetime of the plugin instance.
        return reinterpret_cast<VstIntPtr>(&m_timeInfo);

    case audioMasterSizeWindow:
        // Plugin requests the host to resize its editor window.
        // index = new width, value = new height.
        // Forward to the UI thread via PostMessage (WM_USER+103 = WM_VSTBRIDGE_RESIZE
        // as defined in EditorBridge.h).  This is fire-and-forget; the UI thread
        // will call SetWindowPos when it processes the message.
        if (m_editorHwnd && index > 0 && value > 0)
        {
            PostMessage(m_editorHwnd,
                        WM_USER + 103,
                        static_cast<WPARAM>(index),
                        static_cast<LPARAM>(value));
        }
        return 1;

    case audioMasterGetSampleRate:
        return static_cast<VstIntPtr>(m_sampleRate);

    case audioMasterGetBlockSize:
        return static_cast<VstIntPtr>(m_blockSize);

    case audioMasterGetCurrentProcessLevel:
        return kVstProcessLevelRealtime;  // 2

    case audioMasterGetVendorString:
        if (ptr)
            strcpy_s(static_cast<char*>(ptr), 64, "Kodi VST Host");
        return 1;

    case audioMasterGetProductString:
        if (ptr)
            strcpy_s(static_cast<char*>(ptr), 64, "Kodi AudioDSP");
        return 1;

    case audioMasterGetVendorVersion:
        return 1000;

    case audioMasterGetLanguage:
        return kVstLangEnglish;  // 1

    case audioMasterCanDo:
    {
        const char* feature = static_cast<const char*>(ptr);
        if (!feature)
            return 0;
        if (std::strcmp(feature, "sizeWindow") == 0 ||
            std::strcmp(feature, "sendVstEvents") == 0 ||
            std::strcmp(feature, "sendVstMidiEvent") == 0 ||
            std::strcmp(feature, "receiveVstEvents") == 0 ||
            std::strcmp(feature, "receiveVstMidiEvent") == 0 ||
            std::strcmp(feature, "sendVstTimeInfo") == 0 ||
            std::strcmp(feature, "receiveVstTimeInfo") == 0 ||
            std::strcmp(feature, "conformsToWindowRules") == 0 ||
            std::strcmp(feature, "supplyIdle") == 0 ||    // legacy idle support
            std::strcmp(feature, "openWindow") == 0 ||    // legacy audioMasterOpenWindow
            std::strcmp(feature, "closeWindow") == 0)     // legacy audioMasterCloseWindow
        {
            return 1;
        }
        return 0;
    }

    case audioMasterUpdateDisplay:
        // Plugin requests host to refresh parameter display.
        // Acknowledge; the Python side will re-query params on next poll.
        return 1;

    // -------------------------------------------------------------------------
    // Legacy VST 2.0/2.1 / VST 1 window management callbacks
    // -------------------------------------------------------------------------

    case audioMasterOpenWindow:
    {
        // Deprecated since VST 2.4.  VST 1 and early VST 2.x plugins use this
        // to request a host-provided window rather than using the parent HWND
        // that was passed to effEditOpen.
        //
        // Strategy: if we are currently inside effEditOpen (m_inEffEditOpen is
        // true), reuse the host editor window (m_editorHwnd) and resize it to
        // match the plugin's requested dimensions.  This embeds the plugin's UI
        // directly into the host window with no blank/extra window.
        //
        // If called at idle time (secondary window request — e.g. a preset
        // browser triggered by a button click inside the plugin's UI), create a
        // new standalone window and track it in m_auxWindows so it is destroyed
        // together with the editor.

        VstWindow* vw = static_cast<VstWindow*>(ptr);
        if (!vw)
            return 0;

        if (m_inEffEditOpen && m_editorHwnd && IsWindow(m_editorHwnd))
        {
            // Primary-editor request during effEditOpen — reuse host HWND.
            if (vw->width > 0 && vw->height > 0)
            {
                // Resize the host window to fit the plugin's requested client area.
                RECT wr = {0, 0, vw->width, vw->height};
                AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
                SetWindowPos(m_editorHwnd, nullptr, 0, 0,
                             wr.right - wr.left, wr.bottom - wr.top,
                             SWP_NOMOVE | SWP_NOZORDER);
            }
            if (vw->title[0] != '\0')
            {
                // Update the window title from the plugin's request.
                wchar_t wtitle[128] = {};
                MultiByteToWideChar(CP_ACP, 0, vw->title, -1, wtitle, 128);
                SetWindowTextW(m_editorHwnd, wtitle);
            }
            vw->winHandle = m_editorHwnd;
            VSTLOG(VSTLOG_INFO,
                   "[VSTPlugin2] audioMasterOpenWindow — reusing host HWND %p for '%s'",
                   m_editorHwnd, m_name.c_str());
            return reinterpret_cast<VstIntPtr>(m_editorHwnd);
        }

        // Secondary / idle-time window request — create a new floating window.
        // Register the auxiliary window class lazily on first use.
        // Called on the UI thread (effEditIdle / idle path), so no locking needed.
        static ATOM s_auxClass = 0;
        if (!s_auxClass)
        {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = DefWindowProcW;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"KodiVSTAux";
            wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            s_auxClass = RegisterClassExW(&wc);
            if (!s_auxClass && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
                s_auxClass = static_cast<ATOM>(1);  // already registered
        }

        const int w = (vw->width  > 0) ? vw->width  : 640;
        const int h = (vw->height > 0) ? vw->height : 480;

        wchar_t wtitle[128] = L"VST Editor";
        if (vw->title[0] != '\0')
            MultiByteToWideChar(CP_ACP, 0, vw->title, -1, wtitle, 128);

        RECT wr = {0, 0, w, h};
        AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);

        HWND auxHwnd = CreateWindowExW(
            0, L"KodiVSTAux", wtitle, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            wr.right - wr.left, wr.bottom - wr.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (!auxHwnd)
        {
            VSTLOG(VSTLOG_ERROR,
                   "[VSTPlugin2] audioMasterOpenWindow — CreateWindowExW failed "
                   "for '%s': error %lu", m_name.c_str(), GetLastError());
            return 0;
        }

        ShowWindow(auxHwnd, SW_SHOW);
        UpdateWindow(auxHwnd);

        vw->winHandle = auxHwnd;
        m_auxWindows.push_back(auxHwnd);

        VSTLOG(VSTLOG_INFO,
               "[VSTPlugin2] audioMasterOpenWindow — created aux window %p (%dx%d) for '%s'",
               auxHwnd, w, h, m_name.c_str());
        return reinterpret_cast<VstIntPtr>(auxHwnd);
    }

    case audioMasterCloseWindow:
    {
        // Deprecated since VST 2.4.  Destroy the window the plugin previously
        // obtained via audioMasterOpenWindow.
        VstWindow* vw = static_cast<VstWindow*>(ptr);
        if (!vw || !vw->winHandle)
            return 0;

        HWND hwnd = static_cast<HWND>(vw->winHandle);

        // Never destroy the main editor HWND on the plugin's request.
        if (hwnd == m_editorHwnd)
        {
            VSTLOG(VSTLOG_WARN,
                   "[VSTPlugin2] audioMasterCloseWindow — plugin tried to close "
                   "the main editor HWND; ignoring for '%s'", m_name.c_str());
            vw->winHandle = nullptr;
            return 1;
        }

        // Remove from aux window tracking.
        auto it = std::find(m_auxWindows.begin(), m_auxWindows.end(), hwnd);
        if (it != m_auxWindows.end())
            m_auxWindows.erase(it);

        if (IsWindow(hwnd))
            DestroyWindow(hwnd);

        vw->winHandle = nullptr;
        VSTLOG(VSTLOG_INFO,
               "[VSTPlugin2] audioMasterCloseWindow — closed window %p for '%s'",
               hwnd, m_name.c_str());
        return 1;
    }

    case audioMasterGetDirectory:
        // Return a pointer to the null-terminated ANSI directory path.
        // The string is owned by this VSTPlugin2 instance and valid for its
        // entire lifetime.
        return reinterpret_cast<VstIntPtr>(m_pluginDir.c_str());

    default:
        VSTLOG(VSTLOG_DEBUG,
               "[VSTPlugin2] audioMaster — unhandled opcode %d (index=%d, value=%lld) in '%s'",
               static_cast<int>(opcode), static_cast<int>(index),
               static_cast<long long>(value), m_name.c_str());
        return 0;
    }
}
