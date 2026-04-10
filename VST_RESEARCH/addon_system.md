# Kodi Binary Addon System Research

## 1. Binary Addon Structure and Directory Layout

**API Definition Locations:**
- `xbmc/addons/kodi-addon-dev-kit/include/kodi/` — Main addon SDK headers
  - `kodi_adsp_types.h` — Audio DSP type definitions
  - `kodi_adsp_dll.h` — Audio DSP function declarations that addons must implement
  - `libKODI_adsp.h` — Helper library for addons to communicate back to Kodi

**Addon Directory Structure:**
- `addons/` — Built-in addon manifests
- `lib/addons/` — Addon support libraries
- `xbmc/addons/` — Addon loading and management system

**Required Files for a Binary Addon:**
1. `addon.xml` — Addon manifest with metadata
2. Compiled binary library (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS)
3. `CMakeLists.txt` for building
4. Resource files (icons, language strings, etc.)

---

## 2. The Audio DSP Addon API

Kodi has a **dedicated Audio DSP (ADSP) addon type** specifically for audio processing. This is ideal for VST integration.

**Audio DSP API Version:** 0.1.8 (defined in `kodi_adsp_types.h`)

### Core API Structure (`AudioDSP` struct in `kodi_adsp_types.h`)

**Initialization functions:**
- `GetAudioDSPAPIVersion()` — Required
- `GetMinimumAudioDSPAPIVersion()` — Required
- `GetAddonCapabilities()` — Returns supported features
- `GetDSPName()` / `GetDSPVersion()` — Metadata

**Stream lifecycle:**
- `StreamCreate(AE_DSP_SETTINGS*, AE_DSP_STREAM_PROPERTIES*, handle)` — Create processing for a stream
- `StreamDestroy(handle)` — Cleanup
- `StreamInitialize(handle, AE_DSP_SETTINGS*)` — Initialize processing
- `StreamIsModeSupported(handle, type, mode_id, unique_db_mode_id)` — Query capabilities

**Processing pipeline stages (all optional, enabled via capabilities):**

| Stage | Description | Max addons |
|-------|-------------|------------|
| Input Processing | Direct stream modification | Unlimited |
| Input Resampling | Sample rate conversion at input | 1 |
| Pre-Processing | Effects before master | Unlimited |
| **Master Processing** | **Main signal chain — use for VST** | **1** |
| Post-Processing | Effects after master | Unlimited |
| Output Resampling | Sample rate conversion at output | 1 |

---

## 3. Audio Stream Format (`AE_DSP_SETTINGS`)

```c
iStreamID                  // Unique stream identifier
iStreamType                // Music, Movie, Game, etc.
iInChannels                // Input channel count
lInChannelPresentFlags     // Channel layout (L/R/C/LFE/etc)
iInSamplerate              // Input sample rate (Hz)
iProcessSamplerate         // Processing sample rate (after input resample)
iOutChannels               // Output channel count
iOutSamplerate             // Output sample rate
iQualityLevel              // LOW/MID/HIGH/REALLYHIGH
```

---

## 4. Processing Function Signature Pattern

```c
unsigned int ProcessFunction(
    const ADDON_HANDLE handle,   // Stream identification
    float **array_in,            // Input sample arrays (one ptr per channel)
    float **array_out,           // Output sample arrays
    unsigned int samples         // Number of samples to process
);
// Returns: number of samples actually processed
```

---

## 5. Addon Capabilities (`AE_DSP_ADDON_CAPABILITIES`)

```c
bSupportsInputProcess
bSupportsInputResample
bSupportsPreProcess
bSupportsMasterProcess     // ← USE THIS for VST chain
bSupportsPostProcess
bSupportsOutputResample
```

---

## 6. Addon Loading and Initialization Flow

1. DLL is dynamically loaded via `LoadLibraryA()` on Windows
2. `get_addon()` exported function called to populate the `AudioDSP` struct
3. `Create()` called on the addon instance
4. Helper interfaces provided for callbacks
5. Addon registers modes with `RegisterMode()` callback

**Runtime per-stream flow:**
1. Audio plays → `StreamCreate()` called with format info
2. User selects DSP mode → `MasterProcessSetMode()` called
3. Audio data flows: `InputProcess` → `InputResample` → `PreProcess` → `MasterProcess` → `PostProcess` → `OutputResample`
4. Playback stops → `StreamDestroy()` called

---

## 7. Threading Model

- **All addon processing functions are called from the ActiveAE audio rendering thread**
- Kodi calls functions sequentially per stream
- **No explicit threading needed** — pipeline is serialized by Kodi
- Each stream has its own independent handle/context
- Addons should NOT spawn threads for audio processing

**For VST integration:**
- Use `std::map<AE_DSP_STREAM_ID, VSTInstance*>` to manage per-stream VST contexts
- No mutex needed within `process()` — guaranteed single-threaded access per stream
- Settings changes (GUI thread) require thread-safe callbacks

---

## 8. Required DLL Exports

```cpp
// Mandatory — called by Kodi to populate function table
extern "C" __declspec(dllexport) void get_addon(struct AudioDSP* pDSP);

// Then implement all C-style functions:
const char* GetAudioDSPAPIVersion(void);
const char* GetMinimumAudioDSPAPIVersion(void);
AE_DSP_ERROR GetAddonCapabilities(AE_DSP_ADDON_CAPABILITIES*);
const char* GetDSPName(void);
const char* GetDSPVersion(void);
AE_DSP_ERROR StreamCreate(const AE_DSP_SETTINGS*, const AE_DSP_STREAM_PROPERTIES*, ADDON_HANDLE);
AE_DSP_ERROR StreamDestroy(const ADDON_HANDLE);
AE_DSP_ERROR StreamInitialize(const ADDON_HANDLE, const AE_DSP_SETTINGS*);
// ... etc
```

---

## 9. Addon Descriptor (`addon.xml`)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<addon
  id="audiodsp.vst.host"
  version="1.0.0"
  name="VST Audio DSP Host"
  provider-name="YourName">
  <requires>
    <import addon="kodi.adsp" version="0.1.8"/>
    <import addon="xbmc.core" version="0.1.0"/>
  </requires>
  <extension
    point="xbmc.audiodsp"
    library_windows="VST_addon.dll"/>
  <extension point="xbmc.addon.metadata">
    <summary lang="en">VST Audio DSP Processing</summary>
    <description lang="en">Host VST plugins in Kodi's audio processing chain</description>
    <platform>windows</platform>
    <license>GPL-2.0-or-later</license>
  </extension>
</addon>
```

---

## 10. Suggested Addon Project Structure

```
kodi-vst-addon/
├── addon.xml
├── CMakeLists.txt
├── src/
│   ├── addon_main.cpp       # get_addon() export, capabilities
│   ├── vst_host.h/cpp       # VST plugin loading and hosting
│   ├── dsp_processor.h/cpp  # StreamCreate/Destroy/Process implementations
│   └── vst_chain.h/cpp      # Multi-VST chain management
├── resources/
│   ├── language/
│   │   └── English/strings.po
│   └── icon.png
└── project/cmake/modules/
```

---

## 11. Key Reference Files in Codebase

| File | Role |
|------|------|
| `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_types.h` | All ADSP types and constants |
| `xbmc/addons/kodi-addon-dev-kit/include/kodi/kodi_adsp_dll.h` | Function table definition |
| `xbmc/addons/kodi-addon-dev-kit/include/kodi/libKODI_adsp.h` | Callback helper class |
| `xbmc/cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/ActiveAEDSPAddon.h` | Kodi-side addon wrapper |
| `xbmc/cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/ActiveAEDSPProcess.h` | Per-stream pipeline manager |
| `xbmc/addons/AddonManager.cpp` | Addon discovery and lifecycle |
| `xbmc/addons/AddonDll.h` | Template for DLL instantiation |

---

## 12. Summary

| Aspect | Details |
|--------|---------|
| API Type | Audio DSP (ADSP) — dedicated audio effect system |
| Best processing stage | Master Processing |
| Sample format | 32-bit float arrays, multi-channel |
| Threading | Single-threaded per stream (called from audio thread) |
| Multiple instances | Yes — one per audio stream |
| Channel info | Full layout available |
| Delays | Must report processing delay for A/V sync |
| Dynamic loading | DLL loaded via LoadLibrary at runtime |
| Build system | CMake with Kodi addon macros |
