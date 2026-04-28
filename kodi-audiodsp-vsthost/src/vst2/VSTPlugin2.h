#pragma once
/*
 * VSTPlugin2.h — VST2 plugin host wrapper
 * Implements IVSTPlugin for VST2 (.dll) plugins on Windows.
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 */

#include "../plugin/IVSTPlugin.h"
#include "vestige/aeffectx.h"
#include "../util/ParamQueue.h"

#include <string>
#include <vector>
#include <windows.h>

// Function pointer type for the VST2 plugin entry point
typedef AEffect* (VSTCALLBACK* VSTENTRYPROC)(audioMasterCallback hostCallback);

class VSTPlugin2 : public IVSTPlugin
{
public:
    /// Construct with the absolute path to the .dll file.
    explicit VSTPlugin2(const std::string& path);
    ~VSTPlugin2() override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Lifecycle
    // -------------------------------------------------------------------------
    bool load(double sampleRate, int maxBlockSize, int numChannels) override;
    void unload() override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Audio processing
    // -------------------------------------------------------------------------
    int process(float** in, float** out, int samples) override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Parameters
    // -------------------------------------------------------------------------
    void        setParameter(int index, float value) override;
    float       getParameter(int index) const override;
    int         getParameterCount() const override;
    std::string getParameterName(int index) const override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Query
    // -------------------------------------------------------------------------
    int         getLatencySamples() const override;
    std::string getName()       const override { return m_name; }
    std::string getVendorName() const override { return m_vendor; }
    std::string getPath()       const override { return m_path; }
    bool        isLoaded()      const override { return m_loaded; }

    // -------------------------------------------------------------------------
    // IVSTPlugin — State persistence
    // -------------------------------------------------------------------------
    std::vector<uint8_t> saveState() const override;
    bool                 loadState(const std::vector<uint8_t>& data) override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Editor
    // -------------------------------------------------------------------------
    bool hasEditor()                              const override;
    bool openEditor(void* parentWindow)                 override;
    void closeEditor()                                  override;
    bool getEditorSize(int& width, int& height)   const override;
    void idleEditor()                                   override;

    // -------------------------------------------------------------------------
    // IVSTPlugin — Format discriminator
    // -------------------------------------------------------------------------
    PluginFormat getFormat() const override { return PluginFormat::VST2; }

private:
    // -------------------------------------------------------------------------
    // Host callback — static trampoline + instance dispatcher
    // -------------------------------------------------------------------------
    static VstIntPtr VSTCALLBACK staticAudioMaster(
        AEffect*  effect,
        VstInt32  opcode,
        VstInt32  index,
        VstIntPtr value,
        void*     ptr,
        float     opt);

    VstIntPtr audioMaster(
        AEffect*  effect,
        VstInt32  opcode,
        VstInt32  index,
        VstIntPtr value,
        void*     ptr,
        float     opt);

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /// Call the plugin entry point inside an SEH frame so a crashing plugin
    /// does not take down the host.  Must NOT have C++ objects with destructors
    /// in the same scope — see implementation comment.
    AEffect* callPluginMainSafe(VSTENTRYPROC proc);

    /// Call effEditOpen inside an SEH frame.  Returns the dispatcher result on
    /// success, or -1 if the plugin threw a structured exception.  Must NOT
    /// have C++ objects with destructors in the same scope (MSVC C4509).
    static VstIntPtr callEditOpenSafe(AEffect* effect, void* parentWindow);

    /// Call effEditGetRect inside an SEH frame. Returns true if a rect pointer
    /// was returned without throwing; false otherwise.
    static bool callEditGetRectSafe(AEffect* effect, ERect** rectOut);

    /// Drain the parameter change ring buffer into the plugin (audio thread).
    void drainParamQueue();

    /// Allocate / resize m_inputBufs, m_outputBufs, m_inputPtrs, m_outputPtrs
    /// to match m_numChannels × m_blockSize.
    void allocateScratchBuffers();

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    HMODULE  m_hModule    = nullptr;
    AEffect* m_effect     = nullptr;

    std::string m_path;
    std::string m_name;
    std::string m_vendor;

    double m_sampleRate  = 44100.0;
    int    m_blockSize   = 1024;
    int    m_numChannels = 2;

    bool m_loaded = false;
    bool m_active = false;

    RingBuffer<ParamChange2, 256> m_paramQueue;

    // Time info — returned to plugins that call audioMasterGetTime.
    // samplePos is incremented each process() call; other fields set in load().
    VstTimeInfo m_timeInfo{};

    // HWND of the host window that was passed to openEditor(); used to forward
    // audioMasterSizeWindow resize requests back to EditorBridge via PostMessage.
    HWND m_editorHwnd = nullptr;

    // Scratch buffers — allocated once in load(), reused every process() call
    std::vector<std::vector<float>> m_inputBufs;
    std::vector<std::vector<float>> m_outputBufs;
    std::vector<float*>             m_inputPtrs;
    std::vector<float*>             m_outputPtrs;
};
