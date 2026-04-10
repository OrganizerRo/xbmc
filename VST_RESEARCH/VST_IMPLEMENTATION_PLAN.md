# VST Plugin Chain for Kodi (VST2 + VST3) — Implementation Plan

**Date:** 2026-04-09  
**Branch:** Krypton  
**Feasibility:** YES — straightforward path exists using existing Kodi APIs  
**Research basis:** `audio_pipeline.md`, `addon_system.md`, `vst_hosting.md`, `vst2_legal.md`, `vst2_technical.md`

---

## 1. Executive Summary

Kodi has a **dedicated Audio DSP (ADSP) binary addon system** that was designed precisely for this use case. Both VST2 and VST3 plugins can be hosted in an `xbmc.audiodsp` binary addon **without modifying any Kodi core code**.

- **VST3**: Use the official Steinberg VST3 SDK, which was re-licensed to **MIT in October 2025** (v3.8.0). No licensing obstacles.
- **VST2**: Use the `aeffectx.h` / `vestige.h` clean-room header, licensed **GPLv2**, authored by Javier Serrano Polo in 2006. The same approach is used successfully by **LMMS, Ardour, Audacity, yabridge, and Carla** — 20 years of uncontested legal use. No Steinberg SDK is shipped; only the public binary ABI is used.

The audio format Kodi's ADSP API delivers — `float** array_in / array_out`, planar, one pointer per channel — is **natively identical** to both VST2's `processReplacing(float**, float**, int)` and VST3's `AudioBusBuffers::channelBuffers32`. No format conversion is needed for either format.

**Why VST2 matters:** ~30–40% of available plugins in 2026 are VST2-only (legacy hardware emulations, freeware, older synths). VST2 support is essential for real-world plugin compatibility.

**Estimated LOC:** ~3,000–4,000 lines of C++ for a full Phase 1+2 implementation (both formats).

---

## 2. VST2 Legal Approach

### The vestige / aeffectx.h Header

The VST2 SDK cannot be distributed (Steinberg discontinued it in 2018). However, the VST2 **binary ABI** is a public, documented interface — not copyrightable — and a clean-room reimplementation header exists:

| Attribute | Details |
|-----------|---------|
| Header name | `aeffectx.h` (LMMS) / `vestige.h` (Ardour) |
| Author | Javier Serrano Polo, 2006 |
| License | **GPLv2 (or later)** — same as Kodi |
| Method | Clean-room reverse engineering of binary ABI; no Steinberg SDK referenced |
| Legal track record | 20 years; used by LMMS, Ardour, Audacity, yabridge, Carla with zero successful challenges |

**Recommended source:** Copy `src/include/vestige/aeffectx.h` from [yabridge](https://github.com/robbert-vdh/yabridge) — the most complete version, including `processDoubleReplacing` and all required opcodes.

**Place it at:** `src/vst2/vestige/aeffectx.h` in the addon repo. Preserve the full GPLv2 copyright notice from Javier Serrano Polo verbatim.

### What the Header Provides

All opcodes required for audio-only hosting are present and verified:

| Opcode | Value | Present? |
|--------|-------|----------|
| `effOpen` | 0 | YES |
| `effClose` | 1 | YES |
| `effSetSampleRate` | 10 | YES (pass as `opt` float) |
| `effSetBlockSize` | 11 | YES (pass as `value`) |
| `effMainsChanged` | 12 | YES |
| `effStartProcess` | 71 | YES |
| `effStopProcess` | 72 | YES |
| `processReplacing` | struct fn ptr | YES |
| `setParameter` / `getParameter` | struct fn ptrs | YES |
| `effGetChunk` / `effSetChunk` | 23/24 | YES |

### What Is NOT Needed

- No Steinberg SDK agreement
- No Steinberg headers
- No redistribution of any Steinberg intellectual property
- Hosts loading VST2 DLLs are **not covered by the Steinberg VST2 SDK license** — that license was solely for plugin developers who needed the SDK to build plugins

### UI Trademark Note

"VST" is a Steinberg registered trademark. In Kodi's UI, use:
- **OK:** "Audio plugin support", "VST-compatible plugins", "supports .dll and .vst3 plugin files"
- **Avoid:** "VST Plugin Host" as a branded marketing term

---

## 3. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Kodi ActiveAE Audio Engine                       │
│                                                                     │
│  Media Source ──► CActiveAEStream ──► DSP Pipeline ──► WASAPI Sink  │
│                                           │                        │
│                              ┌────────────┼────────────┐           │
│                              │   audiodsp.vsthost       │           │
│                              │   (xbmc.audiodsp DLL)   │           │
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
│                              │  DSPChain               │           │
│                              │  vector<IVSTPlugin*>    │           │
│                              │    ├─ VSTPlugin2 #1 ─── │──► VST2 DLL│
│                              │    ├─ VSTPlugin3 #2 ─── │──► VST3 bundle│
│                              │    └─ VSTPlugin2 #3 ─── │──► VST2 DLL│
│                              └─────────────────────────┘           │
└─────────────────────────────────────────────────────────────────────┘
```

The addon is a **standalone DLL** (`audiodsp.vsthost.dll`) that:
1. Implements the `AudioDSP` function table from `kodi_adsp_dll.h`
2. On `StreamCreate()`: initialises the plugin chain for the stream's format
3. On `MasterProcess()`: passes Kodi's float planar buffers through the chain
4. On `StreamDestroy()`: tears down plugin instances

Both `VSTPlugin2::processReplacing(float**, float**, int)` and VST3's `AudioBusBuffers::channelBuffers32` accept `float**` planar buffers — identical to what Kodi's ADSP API provides. **No format conversion is required for either format.**

---

## 4. Polymorphic Plugin Interface

Both plugin types are accessed through a single abstract interface, allowing the `DSPChain` to hold a mixed list without caring about format:

```cpp
// src/plugin/IVSTPlugin.h
class IVSTPlugin {
public:
    virtual ~IVSTPlugin() = default;

    // Lifecycle
    virtual bool load(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void unload() = 0;
    virtual bool reinitialize(double sampleRate, int maxBlockSize, int numChannels);

    // Audio processing (audio thread only)
    virtual int process(float** in, float** out, int samples) = 0;

    // Parameters (thread-safe — queued internally)
    virtual void setParameter(int index, float value) = 0;
    virtual float getParameter(int index) const = 0;
    virtual int getParameterCount() const = 0;
    virtual std::string getParameterName(int index) const = 0;

    // Query
    virtual int getLatencySamples() const = 0;
    virtual std::string getName() const = 0;
    virtual std::string getVendorName() const = 0;
    virtual std::string getPath() const = 0;
    virtual bool isLoaded() const = 0;

    // State persistence (settings thread)
    virtual std::vector<uint8_t> saveState() const = 0;
    virtual bool loadState(const std::vector<uint8_t>& data) = 0;

    // Format discriminator
    enum class PluginFormat { VST2, VST3 };
    virtual PluginFormat getFormat() const = 0;

    // Bypass (concrete in base — chain's decision, not the plugin's)
    bool isBypassed() const { return m_bypassed; }
    void setBypassed(bool v) { m_bypassed = v; }

protected:
    bool m_bypassed = false;
};
```

`VSTPlugin2` and `VSTPlugin3` both implement this interface. `DSPChain` holds `vector<unique_ptr<IVSTPlugin>>`.

---

## 5. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| VST2 support | **Yes, via aeffectx.h** | 30–40% of plugins are VST2-only; vestige approach is legally sound GPLv2 |
| VST3 support | **Yes, via official SDK** | MIT licensed (v3.8.0); most modern plugins |
| JUCE | **No** | AGPLv3 incompatible with Kodi GPLv2 |
| VST2 SDK from Steinberg | **No** | Unavailable since 2018 |
| Core mod vs addon | **Addon (xbmc.audiodsp)** | No core changes; existing API designed for this |
| Processing stage | **MasterProcess** | One master addon controls the full chain |
| Crash isolation | **Out-of-process scanner** | Scanner crashes don't affect Kodi; plugins load in-process for playback |
| GUI hosting | **Not in Phase 1** | Audio-only; parameters via Kodi settings UI |
| Dual VST2+VST3 DLLs | **Prefer VST3** | Some DLLs export both; present VST3 interface to user; VST2 as fallback |
| Passthrough streams | **Bypass/skip** | AC3/DTS/TrueHD cannot be processed by effect plugins |

---

## 6. Project Structure

```
kodi-audiodsp-vsthost/                    ← standalone addon repo
├── addon.xml                             ← addon manifest
├── CMakeLists.txt
│
├── src/
│   ├── addon_main.cpp                    ← get_addon() export; capabilities
│   ├── addon_main.h
│   │
│   ├── plugin/
│   │   └── IVSTPlugin.h                  ← NEW: polymorphic interface (both formats)
│   │
│   ├── vst2/                             ← NEW: VST2 hosting
│   │   ├── VSTPlugin2.h/.cpp             ← VST2 loader; implements IVSTPlugin
│   │   ├── vst2_types.h                  ← VstInt32/VstIntPtr typedefs
│   │   └── vestige/
│   │       └── aeffectx.h                ← GPLv2 clean-room ABI header (from yabridge)
│   │
│   ├── vst3/                             ← VST3 hosting
│   │   ├── VSTPlugin3.h/.cpp             ← VST3 loader; implements IVSTPlugin
│   │   ├── VSTHostContext.h/.cpp         ← IHostApplication implementation
│   │   └── VSTPluginManager.h/.cpp       ← scan cache; manages both formats
│   │
│   ├── dsp/
│   │   ├── DSPChain.h/.cpp               ← vector<unique_ptr<IVSTPlugin>>; ping-pong buffers
│   │   └── DSPProcessor.h/.cpp           ← per-stream ADSP lifecycle + MasterProcess routing
│   │
│   ├── settings/
│   │   ├── PluginSettings.h/.cpp         ← load/save chain.json
│   │   └── PresetManager.h/.cpp          ← .vstpreset load/save (VST3)
│   │
│   └── util/
│       └── ParamQueue.h                  ← lock-free ring buffer (shared by both formats)
│
├── resources/
│   ├── settings.xml
│   ├── language/English/strings.po
│   └── icon.png
│
├── scanner/
│   └── vstscanner.cpp                    ← standalone EXE; scans both VST2 .dll and VST3 .vst3
│
└── deps/
    └── vst3sdk/                          ← git submodule: steinbergmedia/vst3sdk (MIT)
```

---

## 7. VST2 Technical Details

### Loading Sequence

```cpp
// 1. Load DLL (LOAD_WITH_ALTERED_SEARCH_PATH ensures plugin's own dependencies resolve)
HMODULE hDll = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

// 2. Resolve entry point
using VSTPluginMainFn = AEffect*(VSTCALLBACK*)(audioMasterCallback);
auto pluginMain = (VSTPluginMainFn)GetProcAddress(hDll, "VSTPluginMain");
if (!pluginMain) pluginMain = (VSTPluginMainFn)GetProcAddress(hDll, "main");

// 3. Instantiate (inside SEH wrapper — crashes are contained)
AEffect* effect = callPluginMainSafe(pluginMain, staticAudioMasterCallback);

// 4. Validate
if (!effect || effect->magic != kEffectMagic) { unload(); return false; }
if (!(effect->flags & effFlagsCanReplacing))   { unload(); return false; }

// 5. Store back-pointer for static callback dispatch
effect->user = this;

// 6. Setup sequence (order is mandatory)
effect->dispatcher(effect, effOpen,          0, 0,             nullptr, 0.0f);
effect->dispatcher(effect, effSetSampleRate, 0, 0,             nullptr, (float)sampleRate);
effect->dispatcher(effect, effSetBlockSize,  0, maxBlockSize,  nullptr, 0.0f);
effect->dispatcher(effect, effMainsChanged,  0, 1,             nullptr, 0.0f);
effect->dispatcher(effect, effStartProcess,  0, 0,             nullptr, 0.0f);
```

**Critical gotchas:**
- `effSetSampleRate` passes sample rate in `opt` (float), NOT `value` — common host bug
- `effMainsChanged` must precede `effStartProcess`
- Never call `setParameter` from the GUI thread — queue via `RingBuffer<ParamChange2>`
- `effGetParamName` spec says 8 bytes but allocate 64 (plugins commonly violate this)
- 64-bit Kodi cannot load 32-bit VST2 DLLs — detect via PE header and skip

### audioMasterCallback (Host → Plugin direction)

```cpp
// Minimum required responses:
case audioMasterVersion:              return 2400;  // VST 2.4
case audioMasterGetSampleRate:        return (VstIntPtr)sampleRate;
case audioMasterGetBlockSize:         return (VstIntPtr)blockSize;
case audioMasterGetCurrentProcessLevel: return inProcess ? 2 : 1;  // 2=realtime
case audioMasterGetTime:              return (VstIntPtr)&g_timeInfo;
case audioMasterIdle:                 return 0;  // audio-only: ignore
```

### VST2 State Save/Load

```
If (effect->flags & effFlagsProgramChunks):
    Use effGetChunk / effSetChunk opcodes (opaque blob)
    saveState() returns: ['C'] + chunk_bytes

Else:
    Save each of effect->numParams floats via getParameter()
    saveState() returns: ['P'] + float_array_bytes
```

### VST2 Plugin Discovery

1. Read registry: `HKLM\SOFTWARE\VST\VSTPluginsPath` and `HKCU\SOFTWARE\VST\VSTPluginsPath`
2. Fall back to: `C:\Program Files\VSTPlugins\`, `C:\Program Files\Common Files\VST2\`
3. Also: `C:\Program Files (x86)\VSTPlugins\` (32-bit plugins — will be skipped at load time)
4. Scan for `*.dll` recursively; detect VST2 by magic number, not file extension

---

## 8. VST3 Technical Details

*(Unchanged from prior research — see `vst_hosting.md` for full details)*

Key points:
- Use `VST3::Hosting::Module::create(path)` for DLL loading
- State machine: `setupProcessing → setActive(true) → setProcessing(true) → process()`
- `HostProcessData` manages buffer memory; `ParameterChanges` for lock-free automation
- Standard scan paths: `%COMMONPROGRAMFILES%\VST3\`, `%LOCALAPPDATA%\Programs\Common\VST3\`

---

## 9. Plugin Scanner (Out-of-Process)

**File:** `scanner/vstscanner.cpp` — standalone console EXE (renamed from `vst3scanner.cpp`)

The scanner handles both formats:

```
1. Scan VST3 paths → enumerate .vst3 bundles → load, read metadata
2. Scan VST2 paths → enumerate .dll files → detectPluginType() → load, read metadata
3. Output unified JSON to stdout → Kodi addon parses and caches
```

### Plugin Type Detection

```cpp
PluginType detectPluginType(const std::wstring& dllPath) {
    HMODULE h = LoadLibraryExW(dllPath.c_str(), nullptr,
                               LOAD_WITH_ALTERED_SEARCH_PATH | DONT_RESOLVE_DLL_REFERENCES);
    bool hasVST2 = GetProcAddress(h, "VSTPluginMain") || GetProcAddress(h, "main");
    bool hasVST3 = GetProcAddress(h, "GetPluginFactory");
    FreeLibrary(h);
    // Returns: VST2_ONLY, VST3_ONLY, BOTH, or NOT_A_VST
}
```

Some plugins export **both** entry points. For dual plugins: expose only VST3 to the user (prefer VST3 over VST2); record VST2 as a fallback in the cache.

### Cache Schema (`plugin_cache.json`)

```json
[
  {
    "format": "vst3",
    "path": "C:\\Program Files\\Common Files\\VST3\\MyEQ.vst3",
    "classID": "AABBCCDD-EEFF-0011-2233-445566778899",
    "name": "MyEQ",
    "vendor": "MyVendor",
    "numInputs": 2,
    "numOutputs": 2,
    "numParams": 12,
    "latency": 0
  },
  {
    "format": "vst2",
    "path": "C:\\Program Files\\VSTPlugins\\OldCompressor.dll",
    "name": "OldCompressor",
    "vendor": "SomeVendor",
    "numInputs": 2,
    "numOutputs": 2,
    "numParams": 6,
    "latency": 0,
    "usesChunk": false,
    "bitness": 64
  }
]
```

Kodi spawns the scanner EXE via `CreateProcess()`, captures stdout, and stores the result. Scanner crashes are contained — they never affect Kodi. Incremental re-scan on file modification time change.

---

## 10. `addon_main.cpp` — ADSP Glue

```cpp
// Capabilities: we claim MasterProcess stage
AE_DSP_ERROR GetAddonCapabilities(AE_DSP_ADDON_CAPABILITIES* caps) {
    memset(caps, 0, sizeof(*caps));
    caps->bSupportsMasterProcess = true;
    return AE_DSP_ERROR_NO_ERROR;
}

// Per-stream lifecycle
AE_DSP_ERROR StreamCreate(const AE_DSP_SETTINGS* settings, ..., ADDON_HANDLE handle) {
    handle->dataIdentifier = new DSPProcessor(*settings);
    return AE_DSP_ERROR_NO_ERROR;
}

// Bypass passthrough/compressed audio streams — never process AC3/DTS/TrueHD
AE_DSP_ERROR StreamIsModeSupported(const ADDON_HANDLE handle, ...) {
    auto* proc = static_cast<DSPProcessor*>(handle->dataIdentifier);
    if (proc->streamType() == AE_DSP_ASTREAM_PASSTHROUGH)
        return AE_DSP_ERROR_IGNORE_ME;
    return AE_DSP_ERROR_NO_ERROR;
}

// Main audio processing — called from ActiveAE render thread
unsigned int MasterProcess(const ADDON_HANDLE handle,
                            float** array_in, float** array_out, unsigned int samples) {
    return static_cast<DSPProcessor*>(handle->dataIdentifier)
               ->process(array_in, array_out, samples);
}

// Latency reporting — sum of all plugin latencies (used for A/V sync)
float MasterProcessGetDelay(const ADDON_HANDLE handle) {
    return (float)static_cast<DSPProcessor*>(handle->dataIdentifier)
                ->chain().getTotalLatencySamples();
}
```

---

## 11. `DSPChain::process()` — Mixed Plugin Chain

```cpp
int DSPChain::process(float** in, float** out, int samples, int numChannels)
{
    if (m_plugins.empty()) {
        for (int ch = 0; ch < numChannels; ch++)
            memcpy(out[ch], in[ch], samples * sizeof(float));
        return samples;
    }

    // Ping-pong through pre-allocated intermediate buffers (no alloc in audio thread)
    float** cur_in  = in;
    float** cur_out = m_pingPtrs.data();

    for (int i = 0; i < (int)m_plugins.size(); i++) {
        auto& p = m_plugins[i];

        if (!p->isLoaded() || p->isBypassed()) {
            for (int ch = 0; ch < numChannels; ch++)
                memcpy(cur_out[ch], cur_in[ch], samples * sizeof(float));
        } else {
            p->process(cur_in, cur_out, samples);  // VST2 and VST3 both accept float**
        }

        if (i + 1 < (int)m_plugins.size()) {
            cur_in  = cur_out;
            cur_out = (cur_out == m_pingPtrs.data()) ? m_pongPtrs.data() : m_pingPtrs.data();
        } else {
            for (int ch = 0; ch < numChannels; ch++)
                memcpy(out[ch], cur_out[ch], samples * sizeof(float));
        }
    }
    return samples;
}
```

---

## 12. Chain Config Serialization (`chain.json`)

```json
{
  "version": 2,
  "chain": [
    {
      "format": "vst3",
      "path": "C:\\Program Files\\Common Files\\VST3\\MyEQ.vst3",
      "classID": "AABBCCDD-EEFF-0011-2233-445566778899",
      "name": "MyEQ",
      "bypass": false,
      "state_encoding": "base64",
      "state": "<base64 of IVSTPlugin::saveState() bytes>"
    },
    {
      "format": "vst2",
      "path": "C:\\Program Files\\VSTPlugins\\OldComp.dll",
      "classID": null,
      "name": "OldCompressor",
      "bypass": false,
      "state_encoding": "base64",
      "state": "UAAz...  (first byte: 'C'=chunk, 'P'=params)"
    }
  ]
}
```

Loading logic: dispatch on `format` field → `make_unique<VSTPlugin2>` or `make_unique<VSTPlugin3>` → `load()` → `loadState()` → `addPlugin()`.

---

## 13. CMake Build

```cmake
cmake_minimum_required(VERSION 3.16)
project(audiodsp.vsthost)

set(VST3SDK_DIR ${CMAKE_SOURCE_DIR}/deps/vst3sdk)

# VST3 hosting subset (~15 source files)
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
    ${VST3SDK_DIR}/pluginterfaces/base/funknown.cpp
    ${VST3SDK_DIR}/pluginterfaces/base/conststringtable.cpp
)
target_include_directories(vst3_hosting PUBLIC ${VST3SDK_DIR})
target_compile_features(vst3_hosting PUBLIC cxx_std_17)

# Main addon DLL
# VST2 requires NO external library — only Windows APIs + our aeffectx.h
add_library(audiodsp.vsthost SHARED
    src/addon_main.cpp
    src/vst2/VSTPlugin2.cpp          # LoadLibrary + aeffectx.h only
    src/vst3/VSTPlugin3.cpp          # VST3 SDK hosting utilities
    src/vst3/VSTHostContext.cpp
    src/vst3/VSTPluginManager.cpp
    src/dsp/DSPChain.cpp
    src/dsp/DSPProcessor.cpp
    src/settings/PluginSettings.cpp
    src/settings/PresetManager.cpp
)
target_link_libraries(audiodsp.vsthost PRIVATE vst3_hosting)
target_include_directories(audiodsp.vsthost PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${KODI_ADSP_SDK_DIR}
)

# Combined scanner EXE (out-of-process plugin probe)
add_executable(vstscanner scanner/vstscanner.cpp)
target_link_libraries(vstscanner PRIVATE vst3_hosting)
target_include_directories(vstscanner PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

---

## 14. `addon.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<addon
  id="audiodsp.vsthost"
  version="1.0.0"
  name="VST Audio Effect Chain"
  provider-name="YourName">
  <requires>
    <import addon="kodi.adsp" version="0.1.8"/>
    <import addon="xbmc.core" version="0.1.0"/>
  </requires>
  <extension
    point="xbmc.audiodsp"
    library_windows="audiodsp.vsthost.dll"/>
  <extension point="xbmc.addon.metadata">
    <summary lang="en">VST audio effect plugin chain</summary>
    <description lang="en">Chain VST2 and VST3 audio effect plugins in Kodi's playback pipeline.</description>
    <platform>windows</platform>
    <license>GPL-2.0-or-later</license>
  </extension>
</addon>
```

---

## 15. Critical Constraints

### Must Respect (Both Formats)
1. **Audio thread** — `MasterProcess()` runs on the ActiveAE render thread. No heap allocation, no locks, no I/O.
2. **Latency reporting** — `MasterProcessGetDelay()` must return the sum of all `getLatencySamples()` values for A/V sync.
3. **Passthrough bypass** — check `AE_DSP_SETTINGS::iStreamType` in `StreamIsModeSupported`; never process compressed streams.
4. **Thread-safe parameters** — GUI thread queues changes via `RingBuffer<ParamChange>`; audio thread drains queue before each `process()` call.
5. **Ping-pong buffers** — pre-allocate in `DSPChain::reinitialize()`; never allocate inside `process()`.

### VST2-Specific
- `effSetSampleRate` passes sample rate in `opt` (float), not `value` — common host bug if confused
- `effMainsChanged(1)` must precede `effStartProcess` — strict ordering
- Use `LOAD_WITH_ALTERED_SEARCH_PATH` so plugin's own dependency DLLs resolve
- 64-bit Kodi cannot load 32-bit VST2 DLLs — detect PE machine type in scanner and skip
- SEH (`__try/__except`) for crash containment in scan; place in separate function, no C++ destructors in scope

### VST3-Specific
- Full state machine: `setupProcessing → setActive(true) → setProcessing(true) → process()`
- Teardown must be reversed: `setProcessing(false) → setActive(false) → terminate()`
- Build `m_paramIDByIndex` map during `load()` to bridge `IVSTPlugin::setParameter(int)` to VST3 `ParamID (uint32)`

---

## 16. Implementation Task Breakdown (for Subagents)

### Task 1 — VST3 SDK Submodule + CMake Skeleton
Add `steinbergmedia/vst3sdk` as git submodule at `deps/vst3sdk`. Write `CMakeLists.txt` compiling only the ~13 hosting files. Verify compiles with MSVC C++17.  
**Depends on:** nothing

### Task 2 — `aeffectx.h` Integration + `vst2_types.h`
Copy yabridge's `src/include/vestige/aeffectx.h` to `src/vst2/vestige/aeffectx.h` (preserve full GPLv2 header). Write `src/vst2/vst2_types.h` with `VstInt32`/`VstIntPtr` typedefs and the `Vst2Opcode` / `AudioMasterOpcode` enums.  
**Depends on:** nothing (parallel with Task 1)

### Task 3 — `IVSTPlugin` Interface + `ParamQueue.h`
Write `src/plugin/IVSTPlugin.h` (pure abstract base class as designed in Section 4). Write `src/util/ParamQueue.h` (lock-free ring buffer, header-only, shared by both formats).  
**Depends on:** nothing (parallel with Tasks 1–2)

### Task 4 — `VSTPlugin2` Implementation
Implement `src/vst2/VSTPlugin2.h/.cpp` using `aeffectx.h`. Full `load/unload/process/saveState/loadState`. Static-to-instance callback dispatch via `AEffect::user`. Mono plugin fold-down adapter. SEH crash containment (`callPluginMainSafe`).  
**Depends on:** Tasks 2, 3

### Task 5 — `VSTPlugin3` Implementation
Rename/adapt existing `VSTPlugin` → `VSTPlugin3.h/.cpp`; inherit `IVSTPlugin`. Add `m_paramIDByIndex` vector built at `load()` time. Adapt `setParameter(int, float)` to map through the index → ParamID table.  
**Depends on:** Tasks 1, 3

### Task 6 — Combined Scanner EXE (`vstscanner.cpp`)
Write `scanner/vstscanner.cpp`. Enumerate VST3 paths (SDK `Module::getModulePaths()`) and VST2 paths (registry + defaults). Run `detectPluginType()` on each DLL. Output unified JSON to stdout. Handle crashes via SEH. Separate function for `callPluginMainSafe`.  
**Depends on:** Tasks 1, 2

### Task 7 — `DSPChain` + `DSPProcessor`
Implement `src/dsp/DSPChain.h/.cpp` with `vector<unique_ptr<IVSTPlugin>>`, pre-allocated ping-pong buffers, and `process()` loop. Implement `src/dsp/DSPProcessor.h/.cpp` as the ADSP per-stream container that owns a `DSPChain` and handles `StreamCreate`/`StreamDestroy`/`StreamInitialize`.  
**Depends on:** Tasks 3, 4, 5

### Task 8 — Addon Entry Point + ADSP Glue
Implement `src/addon_main.cpp`: `get_addon()` export, `GetAddonCapabilities`, `StreamCreate/Destroy/Initialize/IsModeSupported`, `MasterProcess`, `MasterProcessGetDelay`, `MasterProcessGetOutChannels`.  
**Depends on:** Task 7

### Task 9 — Settings + Serialization
Implement `src/settings/PluginSettings.h/.cpp`: load/save `chain.json` with format-discriminated entries. Implement `src/settings/PresetManager.h/.cpp` for VST3 `.vstpreset` files. Write `resources/settings.xml`.  
**Depends on:** Tasks 4, 5, 6

### Task 10 — `VSTPluginManager` + Integration
Implement `src/vst3/VSTPluginManager.h/.cpp`: spawn scanner EXE, parse JSON, maintain cache, provide `loadPlugin(path, format) → unique_ptr<IVSTPlugin>`. Write `addon.xml`, resources, and a minimal integration test.  
**Depends on:** Tasks 6, 8, 9

---

## 17. Phase Roadmap

### Phase 1 — MVP (both formats)
- aeffectx.h integrated; VST2 single plugin working
- VST3 single plugin working (MIT SDK)
- Out-of-process scanner for both formats
- `MasterProcess` stage wired up
- Basic parameter set/get in Kodi settings

### Phase 2 — Chain + Settings
- Multiple plugins in series (mixed VST2+VST3)
- Per-plugin bypass toggle
- State save/restore (`chain.json` + `.vstpreset`)
- Sample rate change handling (reinitialize chain)
- Dual-format DLL detection (prefer VST3)

### Phase 3 — Optional / Future
| Feature | Effort | Notes |
|---------|--------|-------|
| Plugin GUI window | High | `IEditController::createView()` + Win32 HWND hosting |
| CLAP support | Medium | MIT license; growing ecosystem |
| 5.1/7.1 channel chains | Medium | Query `setBusArrangements`; fold stereo-only plugins |
| Linux/macOS | Low | VST3 SDK has platform-specific module loaders; VST2 needs `dlopen` |
| Preset browser | Medium | Read `%APPDATA%\VST3 Presets\` and present list in Kodi |

---

## 18. Licensing Summary

| Component | License | Compatible with Kodi GPLv2? |
|-----------|---------|----------------------------|
| Steinberg VST3 SDK (v3.8.0+) | **MIT** | Yes ✓ |
| vestige / aeffectx.h | **GPLv2 (or later)** | Yes ✓ (identical to Kodi) |
| CLAP (optional future) | MIT | Yes ✓ |
| Steinberg VST2 SDK | Proprietary (unavailable) | **Do not use** |
| JUCE | AGPLv3 / commercial | **Do not use** |
| This addon itself | GPLv2-or-later | Yes ✓ |

Preserve the MIT copyright notice from `vst3sdk/LICENSE.txt` and the GPLv2 copyright notice from `aeffectx.h` (Javier Serrano Polo, 2006) in all distributed source trees.

---

## 19. Reference Files

| Document | Location |
|----------|----------|
| Kodi ADSP type definitions | `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_types.h` |
| Kodi ADSP function table | `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_dll.h` |
| Kodi DSP pipeline manager | `xbmc/cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/ActiveAEDSPProcess.cpp` |
| Audio pipeline research | `VST_RESEARCH/audio_pipeline.md` |
| Addon system research | `VST_RESEARCH/addon_system.md` |
| VST3 hosting research | `VST_RESEARCH/vst_hosting.md` |
| VST2 legal research | `VST_RESEARCH/vst2_legal.md` |
| VST2 technical research | `VST_RESEARCH/vst2_technical.md` |
| VST3 SDK (MIT) | `https://github.com/steinbergmedia/vst3sdk` |
| aeffectx.h source | `https://github.com/robbert-vdh/yabridge/blob/master/src/include/vestige/aeffectx.h` |
| LMMS aeffectx.h (original) | `https://github.com/LMMS/lmms/blob/master/include/aeffectx.h` |
| VST3 audiohost sample | `vst3sdk/public.sdk/samples/vst-hosting/audiohost/` |
