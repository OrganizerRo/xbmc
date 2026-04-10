# VST3 Plugin Chain for Kodi — Implementation Plan

**Date:** 2026-04-09  
**Branch:** Krypton  
**Feasibility:** YES — straightforward path exists using existing Kodi APIs  
**Research basis:** See `audio_pipeline.md`, `addon_system.md`, `vst_hosting.md` in this directory

---

## 1. Executive Summary

Kodi has a **dedicated Audio DSP (ADSP) binary addon system** that was designed precisely for this use case. A VST3 plugin chain can be implemented as an `xbmc.audiodsp` binary addon without modifying any Kodi core code. The Steinberg VST3 SDK was re-licensed to **MIT in 2024/2025**, eliminating all prior licensing obstacles. The audio format Kodi passes to DSP addons (`float**, planar, one pointer per channel`) is natively compatible with VST3's `AudioBusBuffers::channelBuffers32` — **no interleaved/planar conversion is needed**.

**Do not implement VST2.** The VST2 SDK was discontinued by Steinberg in 2018 and cannot be legally redistributed. VST3 covers all modern use cases.

**Estimated LOC:** ~2,000–3,000 lines of C++ for a full Phase 1+2 implementation.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Kodi ActiveAE Audio Engine                       │
│                                                                     │
│  Media Source ──► CActiveAEStream ──► DSP Pipeline ──► WASAPI Sink  │
│                                           │                        │
│                              ┌────────────┼────────────┐           │
│                              │     ADSP Plugin Chain   │           │
│                              │  (xbmc.audiodsp binary) │           │
│                              │                         │           │
│                              │  StreamCreate()         │           │
│                              │    ├─ sample rate       │           │
│                              │    ├─ channel count     │           │
│                              │    └─ stream type       │           │
│                              │                         │           │
│                              │  MasterProcess()        │           │
│                              │  float** in / out       │           │
│                              │    │                    │           │
│                              │    ▼                    │           │
│                              │  [VST3Chain]            │           │
│                              │    ├─ VSTPlugin #1      │           │
│                              │    ├─ VSTPlugin #2      │           │
│                              │    └─ VSTPlugin #N      │           │
│                              └─────────────────────────┘           │
└─────────────────────────────────────────────────────────────────────┘
```

The addon is a **standalone DLL** (`audiodsp.vst3host.dll`) that:
1. Implements the `AudioDSP` function table (from `kodi_adsp_dll.h`)
2. On `StreamCreate()`: initialises the VST3 plugin chain for the stream's format
3. On `MasterProcess()`: passes Kodi's float planar buffers directly through the VST3 chain
4. On `StreamDestroy()`: tears down plugin instances

Because Kodi already provides audio as `float** array_in / array_out` (planar, one pointer per channel), and VST3 expects `AudioBusBuffers::channelBuffers32` (also `float**`, planar), **no format conversion is required**. The buffers can be wired directly.

---

## 3. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| VST2 vs VST3 | **VST3 only** | VST2 SDK legally unavailable; VST3 MIT licensed |
| JUCE vs VST3 SDK | **VST3 SDK directly** | JUCE is AGPLv3, incompatible with Kodi GPLv2 |
| Core mod vs addon | **Addon (`xbmc.audiodsp`)** | No core changes; clean separation; existing API |
| Processing stage | **MasterProcess** | One master addon can control full signal chain |
| Crash isolation | **Out-of-process scanner** | Separate scanner process; in-process for playback |
| GUI hosting | **Not in Phase 1** | Audio-only; plugin parameters via Kodi settings UI |
| Multi-channel | **Stereo first; multi-ch Phase 2** | Most VST3 plugins are stereo-only |
| Passthrough streams | **Bypass/skip** | Cannot process AC3/DTS/TrueHD — must not touch |

---

## 4. Project Structure

```
kodi-audiodsp-vst3/                       ← separate repo / Kodi addon directory
├── addon.xml                             ← addon manifest
├── CMakeLists.txt                        ← build entry point
├── README.md
│
├── src/
│   ├── addon_main.cpp                    ← get_addon() export; capabilities
│   ├── addon_main.h
│   │
│   ├── dsp/
│   │   ├── DSPProcessor.h/.cpp           ← StreamCreate/Destroy/Process; per-stream state
│   │   └── DSPChain.h/.cpp               ← ordered list of VSTPlugin instances
│   │
│   ├── vst3/
│   │   ├── VSTHostContext.h/.cpp         ← IHostApplication implementation
│   │   ├── VSTPlugin.h/.cpp              ← single VST3 plugin instance wrapper
│   │   ├── VSTPluginManager.h/.cpp       ← scan, cache, load/unload plugins
│   │   └── VSTScanner.h/.cpp             ← out-of-process scanner helper
│   │
│   ├── settings/
│   │   ├── PluginSettings.h/.cpp         ← load/save chain config from Kodi settings
│   │   └── PresetManager.h/.cpp          ← .vstpreset load/save
│   │
│   └── util/
│       └── ParamQueue.h                  ← lock-free parameter change queue
│
├── resources/
│   ├── settings.xml                      ← Kodi settings UI definition
│   ├── language/English/strings.po
│   └── icon.png
│
├── scanner/
│   └── vst3scanner.cpp                   ← standalone EXE for safe plugin scanning
│
└── deps/
    └── vst3sdk/                          ← git submodule: steinbergmedia/vst3sdk
```

---

## 5. Phase 1 — Minimal Viable Addon

### 5.1 VST3 SDK Integration (CMake)

```cmake
# In CMakeLists.txt:
cmake_minimum_required(VERSION 3.16)
project(audiodsp.vst3host)

set(VST3SDK_DIR ${CMAKE_SOURCE_DIR}/deps/vst3sdk)

# Only compile the hosting subset — not vstgui, not samples
add_library(vst3_hosting STATIC
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/module.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/processdata.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${VST3SDK_DIR}/base/source/fstring.cpp
    ${VST3SDK_DIR}/base/source/updatehandler.cpp
    ${VST3SDK_DIR}/base/source/classfactoryhelpers.cpp
    ${VST3SDK_DIR}/pluginterfaces/base/conststringtable.cpp
    ${VST3SDK_DIR}/pluginterfaces/base/funknown.cpp
)

target_include_directories(vst3_hosting PUBLIC
    ${VST3SDK_DIR}
    ${VST3SDK_DIR}/pluginterfaces
)

target_compile_features(vst3_hosting PUBLIC cxx_std_17)

# Main addon DLL
add_library(audiodsp.vst3host SHARED
    src/addon_main.cpp
    src/dsp/DSPProcessor.cpp
    src/dsp/DSPChain.cpp
    src/vst3/VSTHostContext.cpp
    src/vst3/VSTPlugin.cpp
    src/vst3/VSTPluginManager.cpp
    src/settings/PluginSettings.cpp
    src/settings/PresetManager.cpp
)

target_link_libraries(audiodsp.vst3host PRIVATE vst3_hosting)
target_include_directories(audiodsp.vst3host PRIVATE
    ${KODI_ADSP_SDK_DIR}   # path to kodi_adsp_dll.h, kodi_adsp_types.h
)
```

### 5.2 `addon.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<addon
  id="audiodsp.vst3host"
  version="1.0.0"
  name="VST3 Audio Effect Chain"
  provider-name="YourName">
  <requires>
    <import addon="kodi.adsp" version="0.1.8"/>
    <import addon="xbmc.core" version="0.1.0"/>
  </requires>
  <extension
    point="xbmc.audiodsp"
    library_windows="audiodsp.vst3host.dll"/>
  <extension point="xbmc.addon.metadata">
    <summary lang="en">VST3 plugin chain for Kodi audio</summary>
    <description lang="en">Load and chain VST3 audio effect plugins in Kodi's playback pipeline.</description>
    <platform>windows</platform>
    <license>GPL-2.0-or-later</license>
  </extension>
</addon>
```

### 5.3 `addon_main.cpp` — Addon Entry Point

```cpp
#include "kodi/kodi_adsp_dll.h"
#include "kodi/kodi_adsp_types.h"
#include "dsp/DSPProcessor.h"

// Global DSP processor registry (one per active stream)
static std::map<AE_DSP_STREAM_ID, DSPProcessor*> g_streams;

extern "C" {

void __declspec(dllexport) get_addon(struct AudioDSP* pDSP)
{
    pDSP->GetAudioDSPAPIVersion        = GetAudioDSPAPIVersion;
    pDSP->GetMinimumAudioDSPAPIVersion = GetMinimumAudioDSPAPIVersion;
    pDSP->GetAddonCapabilities         = GetAddonCapabilities;
    pDSP->GetDSPName                   = GetDSPName;
    pDSP->GetDSPVersion                = GetDSPVersion;
    pDSP->StreamCreate                 = StreamCreate;
    pDSP->StreamDestroy                = StreamDestroy;
    pDSP->StreamInitialize             = StreamInitialize;
    pDSP->StreamIsModeSupported        = StreamIsModeSupported;
    pDSP->MasterProcess                = MasterProcess;
    pDSP->MasterProcessSetMode         = MasterProcessSetMode;
    pDSP->MasterProcessGetDelay        = MasterProcessGetDelay;
    pDSP->MasterProcessGetOutChannels  = MasterProcessGetOutChannels;
    // ... fill remaining nulls for unused stages
}

AE_DSP_ERROR GetAddonCapabilities(AE_DSP_ADDON_CAPABILITIES* caps)
{
    memset(caps, 0, sizeof(*caps));
    caps->bSupportsMasterProcess = true;
    return AE_DSP_ERROR_NO_ERROR;
}

AE_DSP_ERROR StreamCreate(const AE_DSP_SETTINGS* settings,
                           const AE_DSP_STREAM_PROPERTIES* props,
                           ADDON_HANDLE handle)
{
    auto* proc = new DSPProcessor(*settings);
    handle->dataIdentifier = proc;
    g_streams[settings->iStreamID] = proc;
    return AE_DSP_ERROR_NO_ERROR;
}

AE_DSP_ERROR StreamDestroy(const ADDON_HANDLE handle)
{
    auto* proc = static_cast<DSPProcessor*>(handle->dataIdentifier);
    g_streams.erase(proc->streamID());
    delete proc;
    return AE_DSP_ERROR_NO_ERROR;
}

unsigned int MasterProcess(const ADDON_HANDLE handle,
                            float** array_in, float** array_out,
                            unsigned int samples)
{
    auto* proc = static_cast<DSPProcessor*>(handle->dataIdentifier);
    return proc->process(array_in, array_out, samples);
}

} // extern "C"
```

### 5.4 `VSTPlugin.h/.cpp` — Single Plugin Wrapper

```cpp
class VSTPlugin {
public:
    bool load(const std::string& path, double sampleRate,
              int maxBlockSize, int numInputs, int numOutputs);
    void unload();

    // Called from audio thread only
    unsigned int process(float** in, float** out, int samples);

    // Thread-safe — queues change for next process() call
    void setParameter(uint32_t paramID, double normalizedValue);

    // Query
    int getLatencySamples() const;
    bool isLoaded()         const { return m_processor != nullptr; }
    const std::string& name() const { return m_name; }

private:
    // VST3 SDK objects
    std::shared_ptr<VST3::Hosting::Module>   m_module;
    Steinberg::IPtr<Steinberg::Vst::IComponent>    m_component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> m_processor;
    Steinberg::Vst::HostProcessData          m_processData;
    Steinberg::Vst::ParameterChanges         m_paramChanges;

    // Lock-free parameter queue (main→audio thread)
    struct ParamChange { uint32_t id; double value; };
    RingBuffer<ParamChange, 256>             m_paramQueue;

    std::string m_name;
    double      m_sampleRate = 0;
    int         m_maxBlockSize = 0;
};
```

### 5.5 `DSPProcessor.cpp` — Per-Stream Chain Orchestration

```cpp
unsigned int DSPProcessor::process(float** array_in, float** array_out,
                                    unsigned int samples)
{
    // NOTE: array_in / array_out are already float** planar from Kodi.
    // VST3 expects float** planar. No conversion needed.

    float** current_in  = array_in;
    float** current_out = array_out;

    for (auto& plugin : m_chain)
    {
        if (!plugin->isLoaded() || plugin->isBypassed())
        {
            // Pass through unchanged
            for (int ch = 0; ch < m_numChannels; ch++)
                memcpy(current_out[ch], current_in[ch], samples * sizeof(float));
        }
        else
        {
            plugin->process(current_in, current_out, samples);
        }

        // Output of this plugin feeds next plugin's input
        std::swap(current_in, current_out);
    }

    // Ensure final output is in array_out (swap back if odd number of plugins)
    if (m_chain.size() % 2 == 1)
    {
        for (int ch = 0; ch < m_numChannels; ch++)
            memcpy(array_out[ch], current_in[ch], samples * sizeof(float));
    }

    return samples;
}
```

### 5.6 Plugin Scanner (Out-of-Process)

```
scanner/vst3scanner.cpp  — standalone console EXE
```

The scanner EXE:
1. Calls `VST3::Hosting::Module::getModulePaths()` to enumerate standard VST3 locations
2. For each `.vst3` bundle, loads it and reads plugin metadata
3. Outputs JSON to stdout: `[{path, name, classID, numInputs, numOutputs, latency}, ...]`
4. Crashes are contained — Kodi spawns this EXE and parses its stdout

Kodi addon calls the scanner with `CreateProcess()`, captures stdout, parses JSON, stores result in `%APPDATA%\Kodi\userdata\addon_data\audiodsp.vst3host\plugin_cache.json`.

Cache invalidation: compare file modification timestamps on re-scan.

---

## 6. Phase 2 — Chain Management and Settings UI

### 6.1 Kodi Settings UI (`resources/settings.xml`)

```xml
<settings>
  <category label="VST3 Plugins">
    <group>
      <setting id="enabled" type="bool" label="Enable VST3 processing" default="true"/>
      <setting id="chain" type="string" label="Plugin chain (JSON)" default="[]"/>
      <setting id="scan_paths" type="string" label="Additional scan paths" default=""/>
      <setting id="rescan" type="action" label="Re-scan plugins" action="RunScript(...)"/>
    </group>
  </category>
</settings>
```

For a richer per-plugin UI (add/remove/reorder/parameters), a dedicated settings dialog using `libKODI_gui` callbacks is needed. This is the most significant non-trivial UI work.

### 6.2 Chain State Serialization

```json
{
  "chain": [
    {
      "path": "C:\\Program Files\\Common Files\\VST3\\MyEQ.vst3",
      "classID": "AABBCCDD-EEFF-0011-2233-445566778899",
      "bypass": false,
      "preset": "%APPDATA%\\VST3 Presets\\Vendor\\MyEQ\\Default.vstpreset",
      "parameters": {
        "1": 0.75,
        "2": 0.5
      }
    }
  ]
}
```

### 6.3 Sample Rate Change Handling

When Kodi calls `StreamInitialize()` with a new sample rate (different from `StreamCreate()`), the DSP chain must be torn down and rebuilt:

```cpp
AE_DSP_ERROR StreamInitialize(const ADDON_HANDLE handle,
                               const AE_DSP_SETTINGS* settings)
{
    auto* proc = static_cast<DSPProcessor*>(handle->dataIdentifier);
    if (settings->iProcessSamplerate != proc->currentSampleRate())
        proc->reinitialize(*settings);
    return AE_DSP_ERROR_NO_ERROR;
}
```

### 6.4 Passthrough/Bypass for Non-PCM Streams

```cpp
AE_DSP_ERROR StreamIsModeSupported(const ADDON_HANDLE handle,
                                    AE_DSP_MODE_TYPE type,
                                    unsigned int mode_id,
                                    int unique_db_mode_id)
{
    auto* proc = static_cast<DSPProcessor*>(handle->dataIdentifier);

    // Only process PCM streams — never touch passthrough (AC3, DTS, TrueHD, etc.)
    if (proc->streamType() == AE_DSP_ASTREAM_PASSTHROUGH)
        return AE_DSP_ERROR_IGNORE_ME;

    return AE_DSP_ERROR_NO_ERROR;
}
```

---

## 7. Phase 3 — Optional / Future

| Feature | Effort | Notes |
|---------|--------|-------|
| Plugin GUI window | High | Requires `IEditController::createView()` + Win32 HWND hosting; separate Win32 popup window |
| CLAP plugin support | Medium | MIT license; clean C API; growing ecosystem |
| 5.1/7.1 channel chains | Medium | Query `setBusArrangements`; fold to stereo if plugin is stereo-only |
| Linux/macOS | Low | VST3 SDK has `module_linux.cpp` / `module_mac.cpp`; same addon code |
| Per-stream-type chains | Low | Different chain for Music vs Movies (Kodi reports stream type in AE_DSP_SETTINGS) |
| Preset browser UI | Medium | Read `%APPDATA%\VST3 Presets\` and present list in Kodi settings |

---

## 8. Critical Constraints

### Must Respect
1. **Audio thread** — `MasterProcess()` runs on the ActiveAE render thread. No allocations, no locks, no I/O.
2. **Latency reporting** — `MasterProcessGetDelay()` must return the sum of all plugin `getLatencySamples()` values. Kodi uses this for video sync.
3. **Passthrough bypass** — never process compressed audio (AC3/DTS/TrueHD/ATMOS). Check `AE_DSP_SETTINGS::iStreamType` in `StreamIsModeSupported`.
4. **Thread-safe settings** — plugin parameters are set from the GUI thread and consumed in the audio thread via the lock-free `RingBuffer<ParamChange>`.
5. **VST3 state machine** — must call `setProcessing(false)` → `setActive(false)` → `terminate()` in that exact order on teardown.

### Format Compatibility
- Input: **32-bit float, planar** (Kodi ADSP API) — natively compatible with VST3 `channelBuffers32`
- Kodi provides channel count at `StreamCreate()` via `AE_DSP_SETTINGS::iInChannels`
- Match `ProcessSetup.sampleRate` to `AE_DSP_SETTINGS::iProcessSamplerate`
- Match `ProcessSetup.maxSamplesPerBlock` to what Kodi typically passes (probe `samples` on first call; pre-configure to 4096 safely)

---

## 9. Implementation Task Breakdown (for Subagents)

The following tasks are ordered with dependencies noted. Tasks with no dependency can run in parallel.

### Task 1 — VST3 SDK Submodule Setup
- Add `steinbergmedia/vst3sdk` as a git submodule at `deps/vst3sdk`
- Write `CMakeLists.txt` that compiles only the hosting subset (~15 source files)
- Verify it compiles with MSVC 2019+ (C++17 required)
- **Depends on:** nothing
- **Output:** `CMakeLists.txt`, `deps/vst3sdk/` submodule

### Task 2 — Plugin Scanner EXE
- Implement `scanner/vst3scanner.cpp`: enumerate `Module::getModulePaths()`, load each `.vst3`, output JSON to stdout
- Must handle plugin crashes gracefully (structured exception handling on Windows)
- **Depends on:** Task 1 (SDK available)
- **Output:** `scanner/vst3scanner.cpp`, `scanner/CMakeLists.txt`

### Task 3 — VSTPlugin Wrapper
- Implement `src/vst3/VSTPlugin.h/.cpp`
- Load a single VST3 plugin, call `setupProcessing`, run the `process()` loop
- Implement lock-free `RingBuffer<ParamChange>` for thread-safe parameter delivery
- **Depends on:** Task 1
- **Output:** `src/vst3/VSTPlugin.h`, `src/vst3/VSTPlugin.cpp`, `src/util/ParamQueue.h`

### Task 4 — VSTPluginManager
- Implement `src/vst3/VSTPluginManager.h/.cpp`
- Spawn scanner EXE, parse JSON output, maintain `plugin_cache.json`
- Provide `loadPlugin(path) -> VSTPlugin*`, `getAvailablePlugins() -> vector`
- **Depends on:** Task 2, Task 3
- **Output:** `src/vst3/VSTPluginManager.h`, `src/vst3/VSTPluginManager.cpp`

### Task 5 — DSPChain and DSPProcessor
- Implement `src/dsp/DSPChain.h/.cpp` (ordered vector of `VSTPlugin*`)
- Implement `src/dsp/DSPProcessor.h/.cpp` (per-stream ADSP lifecycle + MasterProcess routing)
- Implement ping-pong buffer logic for chained processing
- **Depends on:** Task 3
- **Output:** `src/dsp/DSPChain.h/.cpp`, `src/dsp/DSPProcessor.h/.cpp`

### Task 6 — Addon Entry Point and ADSP Glue
- Implement `src/addon_main.cpp`
- Export `get_addon()`, implement all required `AudioDSP` function table entries
- Wire `StreamCreate/Destroy/Initialize/IsModeSupported/MasterProcess` to `DSPProcessor`
- Implement `GetAddonCapabilities` returning `bSupportsMasterProcess = true`
- **Depends on:** Task 5
- **Output:** `src/addon_main.cpp`, `src/addon_main.h`

### Task 7 — Settings and Serialization
- Implement `src/settings/PluginSettings.h/.cpp`
- Load/save chain config from `addon_data/audiodsp.vst3host/chain.json`
- Implement `src/settings/PresetManager.h/.cpp` for `.vstpreset` load/save via `IBStream`
- Write `resources/settings.xml`
- **Depends on:** Task 4
- **Output:** settings source files, `resources/settings.xml`

### Task 8 — addon.xml, Resources, Integration Test
- Write `addon.xml`, `resources/icon.png`, `resources/language/English/strings.po`
- Write a minimal integration test: load a known VST3 plugin, process a buffer of silence, verify output is silence (or known processed signal)
- **Depends on:** Task 6, Task 7
- **Output:** `addon.xml`, resources, test

---

## 10. File Map — What to Create vs What Already Exists

### Already Exists (Kodi core — read-only reference)
```
xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_types.h
xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_dll.h
xbmc/addons/kodi-addon-dev-kit/include/kodi/libKODI_adsp.h
xbmc/cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/  (DSP orchestration)
xbmc/cores/AudioEngine/Sinks/AESinkWASAPI.cpp             (WASAPI output)
```

### To Create (addon repo)
```
addon.xml
CMakeLists.txt
deps/vst3sdk/                   (git submodule)
scanner/vst3scanner.cpp
src/addon_main.h/.cpp
src/dsp/DSPChain.h/.cpp
src/dsp/DSPProcessor.h/.cpp
src/vst3/VSTHostContext.h/.cpp
src/vst3/VSTPlugin.h/.cpp
src/vst3/VSTPluginManager.h/.cpp
src/util/ParamQueue.h           (lock-free ring buffer, header-only)
src/settings/PluginSettings.h/.cpp
src/settings/PresetManager.h/.cpp
resources/settings.xml
resources/language/English/strings.po
resources/icon.png
```

---

## 11. Licensing Summary

| Component | License | Compatible with Kodi GPLv2? |
|-----------|---------|----------------------------|
| Steinberg VST3 SDK | **MIT** | Yes ✓ |
| CLAP (optional future) | MIT | Yes ✓ |
| VST2 SDK | Proprietary (unavailable) | No — do not use |
| JUCE | AGPLv3 / commercial | No — do not use |
| This addon itself | GPLv2-or-later | Yes ✓ |

Preserve the MIT copyright notice from `vst3sdk/LICENSE.txt` in source tree.  
Do not use the "VST" logo without Steinberg permission.

---

## 12. References

| Document | Location |
|----------|----------|
| Kodi ADSP addon API types | `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_types.h` |
| Kodi ADSP function table | `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_dll.h` |
| Kodi DSP pipeline manager | `xbmc/cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/ActiveAEDSPProcess.cpp` |
| Audio pipeline details | `VST_RESEARCH/audio_pipeline.md` |
| Addon system details | `VST_RESEARCH/addon_system.md` |
| VST3 hosting details | `VST_RESEARCH/vst_hosting.md` |
| VST3 SDK | `https://github.com/steinbergmedia/vst3sdk` |
| VST3 audiohost sample | `vst3sdk/public.sdk/samples/vst-hosting/audiohost/` |
| VST3 interface docs | `https://steinbergmedia.github.io/vst3_doc/vstinterfaces/` |
