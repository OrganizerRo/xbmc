# VST2 Technical Hosting Reference — Kodi AudioDSP Addon

**Date:** 2026-04-09
**Context:** Extension to `VST_IMPLEMENTATION_PLAN.md` — adds VST2 support alongside VST3  
**Basis:** Existing research in `vst_hosting.md`, `audio_pipeline.md`, `addon_system.md`

> **Licensing note:** The VST2 SDK has not been distributed by Steinberg since ~2018.
> No legal SDK is obtainable today. This document describes how to *host already-installed
> VST2 plugins* (which the end user owns) using only the minimal struct definitions needed
> to call into them — definitions that are independently reconstructable from public ABI
> documentation without reproducing the original SDK verbatim. The Kodi addon itself does
> not ship VST2 SDK headers; it reconstructs only the AEffect struct and opcode enum values
> that are publicly documented and necessary for interoperability. This is the same approach
> taken by open-source projects such as Ardour and Carla. Legal review is still recommended
> before shipping.

---

## Table of Contents

1. [VST2 DLL Loading on Windows](#1-vst2-dll-loading-on-windows)
2. [The AEffect Struct — Fields Needed for Audio Hosting](#2-the-aeffect-struct--fields-needed-for-audio-hosting)
3. [Plugin Opcodes Required](#3-plugin-opcodes-required)
4. [audioMasterCallback — Host-Side Opcodes](#4-audiomastercallback--host-side-opcodes)
5. [VST2 Audio Processing](#5-vst2-audio-processing)
6. [VST2 Parameter Handling](#6-vst2-parameter-handling)
7. [VST2 State Persistence](#7-vst2-state-persistence)
8. [VST2 Plugin Discovery on Windows](#8-vst2-plugin-discovery-on-windows)
9. [Plugin Type Detection](#9-plugin-type-detection)
10. [IVSTPlugin — Polymorphic Interface Design](#10-ivstplugin--polymorphic-interface-design)
11. [VSTPlugin2 — Loader Class Skeleton](#11-vstplugin2--loader-class-skeleton)
12. [VSTPlugin3 — Adapted Wrapper Skeleton](#12-vstplugin3--adapted-wrapper-skeleton)
13. [Scanner Updates for VST2](#13-scanner-updates-for-vst2)
14. [DSPChain with Mixed VST2+VST3](#14-dspchain-with-mixed-vst2vst3)
15. [JSON Serialization for Mixed Chain Config](#15-json-serialization-for-mixed-chain-config)
16. [Threading Model for VST2](#16-threading-model-for-vst2)
17. [Project Structure Changes](#17-project-structure-changes)
18. [CMake Changes](#18-cmake-changes)
19. [Windows-Specific Gotchas](#19-windows-specific-gotchas)
20. [Summary of Differences from VST3](#20-summary-of-differences-from-vst3)

---

## 1. VST2 DLL Loading on Windows

VST2 plugins are single `.dll` files. No bundle directory, no metadata sidecar.

### 1.1 Entry Point

```cpp
// The canonical entry point symbol name
typedef AEffect* (VSTCALLBACK* VSTPluginMainFn)(audioMasterCallback hostCallback);

HMODULE hDll = LoadLibraryW(L"C:\\path\\to\\plugin.dll");
if (!hDll) {
    // GetLastError() will give the Win32 error code
    return false;
}

// Primary entry point (VST 2.1+)
auto pluginMain = reinterpret_cast<VSTPluginMainFn>(
    GetProcAddress(hDll, "VSTPluginMain"));

// Fallback: some older plugins (VST 2.0 era) export "main" instead
// This is especially common with plugins compiled for Mac OS 9 then ported to Windows
if (!pluginMain)
    pluginMain = reinterpret_cast<VSTPluginMainFn>(
        GetProcAddress(hDll, "main"));

if (!pluginMain) {
    // DLL has neither entry point — it is not a VST2 plugin
    FreeLibrary(hDll);
    return false;
}
```

### 1.2 Instantiation and Magic Number Check

```cpp
AEffect* effect = pluginMain(hostCallback);

// CRITICAL: Validate before using any other fields
// kEffectMagic = 0x56737450 = 'VstP' in little-endian
if (!effect || effect->magic != kEffectMagic) {
    // Either null return or magic mismatch — not a valid VST2 effect
    FreeLibrary(hDll);
    return false;
}
```

The magic check is the *only* reliable way to distinguish a VST2 plugin DLL from an
arbitrary DLL that happens to export `VSTPluginMain`. Always check it before reading any
other fields in the struct.

### 1.3 VSTCALLBACK Convention

On Windows, VST2 functions use `__cdecl`. `VSTCALLBACK` is `#define VSTCALLBACK __cdecl`
on MSVC. This must match the calling convention of the function pointer typedefs.
Mismatches (e.g., calling a `__cdecl` function through a `__stdcall` pointer) will corrupt
the stack silently on x86-32 and crash immediately on x86-64.

For modern 64-bit Windows builds there is only one calling convention (`__cdecl` is the
default for x64), so this is only a hazard if supporting a 32-bit build. Flag it clearly
in your own code:

```cpp
#ifndef VSTCALLBACK
  #if defined(_WIN32)
    #define VSTCALLBACK __cdecl
  #else
    #define VSTCALLBACK
  #endif
#endif
```

---

## 2. The AEffect Struct — Fields Needed for Audio Hosting

The full AEffect struct has many historical fields. For audio-only hosting these are the
ones you must read or call:

```cpp
// Minimal AEffect struct definition for hosting — no SDK required
// Values are stable ABI; documented in numerous public sources
struct AEffect {
    VstInt32 magic;              // Must be 0x56737450 ('VstP'). Check first.

    // Function pointers set by the plugin at instantiation time
    // dispatcher: plugin←host opcode call (effOpen, effClose, effSetSampleRate, etc.)
    VstIntPtr (VSTCALLBACK* dispatcher)(AEffect*, VstInt32 opcode, VstInt32 index,
                                        VstIntPtr value, void* ptr, float opt);

    void* process;               // Deprecated accumulating process — do NOT use
    // setParameter/getParameter: normalized float 0.0..1.0
    void (VSTCALLBACK* setParameter)(AEffect*, VstInt32 index, float parameter);
    float (VSTCALLBACK* getParameter)(AEffect*, VstInt32 index);

    VstInt32 numPrograms;        // Number of preset programs
    VstInt32 numParams;          // Number of automatable parameters — use for enumeration
    VstInt32 numInputs;          // Number of audio input channels (1=mono, 2=stereo, etc.)
    VstInt32 numOutputs;         // Number of audio output channels

    VstInt32 flags;              // Bitmask — check effFlagsHasEditor, effFlagsCanReplacing
    VstIntPtr resvd1;
    VstIntPtr resvd2;
    VstInt32 initialDelay;       // Processing latency in samples — use for A/V sync reporting
    VstInt32 _realQualities;     // Unused
    VstInt32 _offQualities;      // Unused
    float    _ioRatio;           // Unused
    void*    object;             // Plugin-private (do not touch)
    void*    user;               // Host-private (you may use this to store context pointer)
    VstInt32 uniqueID;           // 4-char plugin identifier
    VstInt32 version;            // Plugin version (not API version)

    // processReplacing: the main audio callback — always prefer this
    void (VSTCALLBACK* processReplacing)(AEffect*, float** inputs,
                                         float** outputs, VstInt32 sampleFrames);

    // processDoubleReplacing: 64-bit version (VST 2.4+); may be null
    void (VSTCALLBACK* processDoubleReplacing)(AEffect*, double** inputs,
                                               double** outputs, VstInt32 sampleFrames);

    char future[56];             // Reserved — do not use
};
```

### Key Flags (effFlags bitmask)

| Flag | Value | Meaning |
|------|-------|---------|
| `effFlagsHasEditor` | 1 | Plugin has a GUI editor (not needed for audio-only) |
| `effFlagsCanReplacing` | 16 | processReplacing is available — must be set for all modern plugins |
| `effFlagsCanDoubleReplacing` | 4096 | processDoubleReplacing is available |
| `effFlagsProgramChunks` | 32 | Uses chunk-based preset save/load (effGetChunk/effSetChunk) |
| `effFlagsIsSynth` | 256 | Instrument plugin (generates audio) — skip for DSP-only chains |

Always check `effFlagsCanReplacing` before calling `processReplacing`. If this flag is not
set the function pointer may be null or undefined behavior. (In practice all post-2000
plugins set this flag, but skip any that do not.)

### VstInt32 / VstIntPtr Typedefs

```cpp
typedef int         VstInt32;
typedef intptr_t    VstIntPtr;   // 32-bit on x86, 64-bit on x64
```

Define these locally in your own `vst2_types.h` to avoid needing the original SDK.

---

## 3. Plugin Opcodes Required

Opcodes are passed as the `opcode` argument to `effect->dispatcher()`.

```cpp
// Opcode values — stable VST2 ABI constants
enum Vst2Opcode {
    effOpen             = 0,  // Called once after instantiation
    effClose            = 1,  // Called before unloading — plugin frees resources
    effSetSampleRate    = 10, // opt = (float)sampleRate
    effSetBlockSize     = 11, // value = (VstIntPtr)maxBlockSize
    effMainsChanged     = 12, // value = 1 to activate, 0 to deactivate
    effGetEffectName    = 45, // ptr = char[32]; plugin writes its name
    effGetVendorString  = 47, // ptr = char[64]; plugin writes vendor name
    effGetProductString = 48, // ptr = char[64]; plugin writes product name
    effGetParamName     = 8,  // index = paramIndex; ptr = char[8] (max 8 chars in VST2!)
    effGetNumMidiInputChannels  = 78,  // not needed — audio only; ignore
    effGetNumMidiOutputChannels = 79,  // not needed — audio only; ignore
    effStartProcess     = 71, // Called before first processReplacing (VST 2.4)
    effStopProcess      = 72, // Called after last processReplacing (VST 2.4)
    effGetChunk         = 23, // ptr = void**; plugin allocates and fills preset blob
    effSetChunk         = 24, // ptr = void*; value = size; plugin restores from blob
    effCanDo            = 51, // ptr = const char* feature; returns 1 if supported
};
```

### Setup Call Sequence (must follow this exact order)

```cpp
// 1. Open plugin (allocates internal state)
effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);

// 2. Configure format
effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, (float)sampleRate);
effect->dispatcher(effect, effSetBlockSize,  0, (VstIntPtr)maxBlockSize, nullptr, 0.0f);

// 3. Query plugin name (optional but good for logging/UI)
char effectName[32] = {}, vendorName[64] = {}, productName[64] = {};
effect->dispatcher(effect, effGetEffectName,    0, 0, effectName,   0.0f);
effect->dispatcher(effect, effGetVendorString,  0, 0, vendorName,   0.0f);
effect->dispatcher(effect, effGetProductString, 0, 0, productName,  0.0f);

// 4. Activate (turn power on)
effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);

// 5. Start processing (VST 2.4 hosts should call this; older plugins ignore it)
effect->dispatcher(effect, effStartProcess, 0, 0, nullptr, 0.0f);
```

### Teardown Call Sequence

```cpp
// Reverse of setup
effect->dispatcher(effect, effStopProcess,  0, 0, nullptr, 0.0f);
effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f); // value=0 = deactivate
effect->dispatcher(effect, effClose,        0, 0, nullptr, 0.0f);
FreeLibrary(hDll);
// Do NOT delete effect — the plugin owns the AEffect struct memory
```

---

## 4. audioMasterCallback — Host-Side Opcodes

The plugin calls back into the host via the callback you supply at instantiation. Your
callback function has this signature:

```cpp
VstIntPtr VSTCALLBACK audioMasterCallback(AEffect* effect, VstInt32 opcode,
                                           VstInt32 index, VstIntPtr value,
                                           void* ptr, float opt);
```

`effect` may be `nullptr` during initial plugin construction (before AEffect* is returned),
so never dereference it unconditionally.

### Opcodes a Host Must Handle

```cpp
// Opcode values for the audioMasterCallback direction (plugin → host)
enum AudioMasterOpcode {
    audioMasterVersion              = 1,   // Return 2400 (VST 2.4)
    audioMasterGetSampleRate        = 16,  // Return current sample rate as VstIntPtr
    audioMasterGetBlockSize         = 17,  // Return current max block size as VstIntPtr
    audioMasterGetCurrentProcessLevel = 23, // Return process level (see below)
    audioMasterGetTime              = 7,   // Return pointer to VstTimeInfo
    audioMasterIdle                 = 13,  // Plugin wants to pump GUI loop; return 0
    audioMasterIOChanged            = 18,  // Plugin changed numInputs/numOutputs; return 1
    audioMasterGetVendorString      = 32,  // ptr = char[64]; write your host name
    audioMasterGetProductString     = 33,  // ptr = char[64]; write host product name
    audioMasterGetVendorVersion     = 34,  // Return host version integer
    audioMasterCanDo                = 37,  // ptr = const char* feature; return 1 if supported
    audioMasterSizeWindow           = 15,  // Plugin wants to resize its window; index=w, value=h
    audioMasterUpdateDisplay        = 42,  // Plugin wants host to refresh UI; return 0
    audioMasterBeginEdit            = 43,  // Plugin starting parameter automation recording
    audioMasterEndEdit              = 44,  // Plugin ending parameter automation recording
};
```

### Process Level Constants

```cpp
// Returned by audioMasterGetCurrentProcessLevel
enum VstProcessLevel {
    kVstProcessLevelUnknown  = 0,
    kVstProcessLevelUser     = 1,  // User/GUI thread
    kVstProcessLevelRealtime = 2,  // Audio thread — return this inside processReplacing
    kVstProcessLevelPrefetch = 3,  // Offline prefetch
    kVstProcessLevelOffline  = 4,  // Offline rendering
};
```

### Minimal Correct Callback Implementation

```cpp
// Per-plugin (or global) host state needed by the callback
struct VST2HostState {
    double   sampleRate  = 48000.0;
    VstInt32 blockSize   = 1024;
    bool     inProcess   = false;   // Set true during processReplacing calls
};

// Thread-local pointer to current state (or use a global map keyed on AEffect*)
thread_local VST2HostState* g_currentHostState = nullptr;

VstIntPtr VSTCALLBACK audioMasterCallback(AEffect* effect, VstInt32 opcode,
                                           VstInt32 index, VstIntPtr value,
                                           void* ptr, float opt)
{
    VST2HostState* state = g_currentHostState;  // see section 11 for better approach

    switch (opcode)
    {
        // --- Mandatory ---
        case audioMasterVersion:
            return 2400;  // We implement VST 2.4

        case audioMasterGetSampleRate:
            return state ? (VstIntPtr)state->sampleRate : 48000;

        case audioMasterGetBlockSize:
            return state ? state->blockSize : 1024;

        case audioMasterGetCurrentProcessLevel:
            return (state && state->inProcess)
                ? kVstProcessLevelRealtime
                : kVstProcessLevelUser;

        case audioMasterGetTime: {
            // A minimal VstTimeInfo; static is safe because processReplacing is
            // always called from a single consistent thread per stream
            static VstTimeInfo ti{};
            ti.sampleRate  = state ? state->sampleRate : 48000.0;
            ti.samplePos   = 0.0;  // could track position if needed
            ti.flags       = 1 << 1;  // kVstTransportPlaying = 0x2
            return reinterpret_cast<VstIntPtr>(&ti);
        }

        case audioMasterIdle:
            // Plugin wants to pump its GUI event loop — audio-only host: ignore
            return 0;

        case audioMasterIOChanged:
            // Plugin changed its channel count — must reinitialize chain
            // Queue a reinit on the owning VSTPlugin2 object
            // (Cannot do it here — may be called from audio thread)
            return 1;

        // --- Informational (nice to have) ---
        case audioMasterGetVendorString:
            if (ptr) strncpy_s(static_cast<char*>(ptr), 64, "KodiVSTHost", 12);
            return 1;

        case audioMasterGetProductString:
            if (ptr) strncpy_s(static_cast<char*>(ptr), 64, "audiodsp.vsthost", 17);
            return 1;

        case audioMasterGetVendorVersion:
            return 1000;  // 1.0.0.0

        case audioMasterCanDo:
            // Return 1 for features you support, -1 for features you don't
            return 0;

        default:
            return 0;  // Safe default: return 0 for unknown opcodes
    }
}
```

---

## 5. VST2 Audio Processing

### 5.1 processReplacing Signature

```cpp
void (VSTCALLBACK* processReplacing)(AEffect* effect,
                                      float**   inputs,
                                      float**   outputs,
                                      VstInt32  sampleFrames);
```

- `inputs`  — array of `effect->numInputs` pointers, each pointing to `sampleFrames` floats
- `outputs` — array of `effect->numOutputs` pointers, each pointing to `sampleFrames` floats
- "Replacing" means the plugin *writes* (replaces) the output buffers rather than
  accumulating into them. This is the only modern form. The old `process` (accumulating)
  function pointer must never be called.

### 5.2 Buffer Format Compatibility with Kodi ADSP

Kodi's `MasterProcess()` receives `float** array_in, float** array_out` — one pointer per
channel, `samples` floats each. VST2's `processReplacing` takes `float** inputs,
float** outputs, VstInt32 sampleFrames` — identical layout. **No conversion is needed.**
This is the same happy coincidence as VST3 (Section 2, `VST_IMPLEMENTATION_PLAN.md`).

### 5.3 Channel Count Handling

```cpp
// numInputs and numOutputs from AEffect tell you the plugin's native channel count.
// Do not assume stereo. Validate at load time:

int pluginInputs  = effect->numInputs;
int pluginOutputs = effect->numOutputs;
int kodiChannels  = settings.iInChannels;   // from AE_DSP_SETTINGS

if (pluginOutputs != kodiChannels) {
    // Mismatch — must adapt (see section 5.4)
}
```

### 5.4 Mono Plugin in a Stereo Chain

A mono-in / mono-out plugin (`numInputs=1, numOutputs=1`) in a stereo chain must be
handled explicitly. Strategy: process both channels independently through the plugin in two
passes, *or* fold to mono before the plugin and back to stereo after.

```cpp
void processMonoPlugin(AEffect* effect, float** in, float** out,
                        int numChannels, int numSamples)
{
    // Approach A: Run the plugin once per channel (only valid if the plugin is
    // truly stateless / memoryless between channels, which is not guaranteed).
    // Only safe for simple EQ/gain plugins — do NOT use for reverb/delay/compressor.

    // Approach B: Sum to mono, process, distribute back to each channel
    // (introduces loudness change; correct approach for most mono FX)
    static std::vector<float> monoIn, monoOut;
    monoIn.resize(numSamples);
    monoOut.resize(numSamples);

    // Mix down to mono (average)
    for (int i = 0; i < numSamples; i++) {
        monoIn[i] = 0.0f;
        for (int ch = 0; ch < numChannels; ch++)
            monoIn[i] += in[ch][i];
        monoIn[i] /= numChannels;
    }

    float* inPtr  = monoIn.data();
    float* outPtr = monoOut.data();
    effect->processReplacing(effect, &inPtr, &outPtr, numSamples);

    // Distribute mono output to all channels
    for (int ch = 0; ch < numChannels; ch++)
        memcpy(out[ch], monoOut.data(), numSamples * sizeof(float));
}
```

If a mono plugin is found in the chain, log a warning and apply approach B by default.
Expose a per-plugin setting for "mono mode: fold / dual-mono" in the UI.

### 5.5 mismatched numOutputs > numInputs (e.g., upmixer)

If `numOutputs > numInputs` (an unusual case for DSP effects): allocate the extra output
channel buffers internally and discard the surplus channels. The Kodi chain always has a
fixed channel count.

---

## 6. VST2 Parameter Handling

### 6.1 Set/Get Parameter

```cpp
// Normalized float in range [0.0, 1.0]
effect->setParameter(effect, paramIndex, normalizedValue);
float currentValue = effect->getParameter(effect, paramIndex);
```

### 6.2 Parameter Names

**Important:** The VST2 `effGetParamName` opcode only allows **8 characters** (including
null terminator — so 7 usable characters). This is a known VST2 limitation. Some plugins
violate this and write up to 32 bytes, so allocate generously:

```cpp
char paramName[64] = {};  // allocate more than 8 for buggy plugins
effect->dispatcher(effect, effGetParamName, paramIndex, 0, paramName, 0.0f);
// Null-terminate explicitly since some plugins don't
paramName[63] = '\0';
```

### 6.3 Parameter Count

```cpp
int numParams = effect->numParams;
// Enumerate: for (int i = 0; i < numParams; i++) { getParameter(i); getParamName(i); }
```

### 6.4 Thread Safety

VST2 has no defined thread-safe parameter mechanism. Use the same lock-free ring buffer
approach as VST3 (see `src/util/ParamQueue.h` from the existing plan):

```cpp
// Main/GUI thread:
m_paramQueue.push({paramIndex, normalizedValue});

// Audio thread (before calling processReplacing):
ParamChange2 pc;
while (m_paramQueue.pop(pc)) {
    effect->setParameter(effect, pc.index, pc.value);
}
```

Do not call `setParameter` from any thread other than the audio thread unless the specific
plugin documents that it is safe — assume it is not.

---

## 7. VST2 State Persistence

### 7.1 Chunk-Based Save/Load (preferred)

If `effect->flags & effFlagsProgramChunks` is set, the plugin uses opaque chunk blobs:

```cpp
// SAVE: plugin allocates a buffer and returns its pointer via ptr argument
void* chunkData  = nullptr;
VstInt32 chunkSize = (VstInt32)effect->dispatcher(
    effect, effGetChunk,
    0,           // index: 0 = current bank, 1 = current program
    0, &chunkData, 0.0f);
// chunkData now points to chunkSize bytes owned by the plugin — copy it before unloading

std::vector<uint8_t> savedChunk(
    static_cast<uint8_t*>(chunkData),
    static_cast<uint8_t*>(chunkData) + chunkSize);

// LOAD: pass data back to plugin
effect->dispatcher(effect, effSetChunk,
    0,           // index: 0 = bank, 1 = program
    (VstIntPtr)savedChunk.size(),
    savedChunk.data(), 0.0f);
```

### 7.2 Parameter-Based Fallback

If `effFlagsProgramChunks` is not set, save individual parameter values:

```cpp
// SAVE
std::vector<float> params(effect->numParams);
for (int i = 0; i < effect->numParams; i++)
    params[i] = effect->getParameter(effect, i);
// Serialize params as JSON array of floats

// LOAD
for (int i = 0; i < effect->numParams; i++)
    effect->setParameter(effect, i, params[i]);
```

### 7.3 Strategy Selection

```cpp
bool usesChunk = (effect->flags & effFlagsProgramChunks) != 0;
if (usesChunk)
    saveWithChunk();
else
    saveWithParams();
```

Always save both the strategy flag and the data so the loader knows which path to take.

---

## 8. VST2 Plugin Discovery on Windows

### 8.1 Registry Keys

VST2 installation paths are stored in the Windows Registry. Read them at scan time:

```cpp
#include <windows.h>
#include <string>
#include <vector>

std::vector<std::wstring> getVST2RegistryPaths()
{
    std::vector<std::wstring> paths;
    const wchar_t* keys[] = {
        L"SOFTWARE\\VST",                    // Standard 32/64-bit path
        L"SOFTWARE\\WOW6432Node\\VST",       // 32-bit app on 64-bit Windows
    };
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

    for (HKEY root : roots) {
        for (const wchar_t* key : keys) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(root, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                continue;

            wchar_t buf[MAX_PATH] = {};
            DWORD bufSize = sizeof(buf);
            if (RegQueryValueExW(hKey, L"VSTPluginsPath", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(buf), &bufSize) == ERROR_SUCCESS) {
                paths.push_back(buf);
            }
            RegCloseKey(hKey);
        }
    }
    return paths;
}
```

### 8.2 Common Default Paths

When registry keys are absent, fall back to these hardcoded defaults:

```cpp
std::vector<std::wstring> getVST2DefaultPaths()
{
    std::vector<std::wstring> paths;

    // 64-bit VST2 plugins
    paths.push_back(L"C:\\Program Files\\VSTPlugins");
    paths.push_back(L"C:\\Program Files\\Steinberg\\VSTPlugins");
    paths.push_back(L"C:\\Program Files\\Common Files\\VST2");
    paths.push_back(L"C:\\Program Files\\Common Files\\Steinberg\\VST2");

    // 32-bit VST2 plugins on 64-bit system
    paths.push_back(L"C:\\Program Files (x86)\\VSTPlugins");
    paths.push_back(L"C:\\Program Files (x86)\\Steinberg\\VSTPlugins");

    return paths;
}
```

### 8.3 File Extension

VST2 plugins use `.dll` with no bundle directory — one DLL is the entire plugin. Scan for
`*.dll` recursively within the configured paths.

### 8.4 Distinguishing VST2 from Arbitrary DLLs

A raw `.dll` file scan will pick up many non-VST DLLs. The identification sequence is:

1. `LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH | DONT_RESOLVE_DLL_REFERENCES)`
   — loads without executing DllMain (reduces crash risk during scan)
2. Look for `VSTPluginMain` or `main` via `GetProcAddress`
3. If found, load fully with `LoadLibraryW` and call the entry point
4. Check `effect->magic == kEffectMagic` (0x56737450)
5. Check `effect->flags & effFlagsCanReplacing` — must be set for processing

Use SEH (`__try/__except`) around steps 3–4 in the scanner process to catch crashes:

```cpp
AEffect* effect = nullptr;
__try {
    effect = pluginMain(audioMasterCallback);
} __except (EXCEPTION_EXECUTE_HANDLER) {
    // Crashed during init — add to blocklist
    return ScanResult::CRASHED;
}
```

---

## 9. Plugin Type Detection

### 9.1 Can a DLL Be Both VST2 and VST3?

**Yes.** Some plugin vendors ship a single DLL that exports both `VSTPluginMain` (VST2
entry) and `GetPluginFactory` (VST3 entry). This is common with plugins that wrap a common
internal engine in both formats as a compatibility measure.

Examples: iZotope plugins, some Waves plugins, various smaller vendors.

### 9.2 Detection Strategy

```cpp
enum class PluginType {
    UNKNOWN,
    VST2_ONLY,
    VST3_ONLY,
    BOTH_VST2_AND_VST3,
    NOT_A_VST
};

PluginType detectPluginType(const std::wstring& dllPath)
{
    HMODULE h = LoadLibraryExW(dllPath.c_str(), nullptr,
                               LOAD_WITH_ALTERED_SEARCH_PATH |
                               DONT_RESOLVE_DLL_REFERENCES);
    if (!h) return PluginType::NOT_A_VST;

    bool hasVST2 = (GetProcAddress(h, "VSTPluginMain") != nullptr ||
                    GetProcAddress(h, "main")           != nullptr);
    bool hasVST3 = (GetProcAddress(h, "GetPluginFactory") != nullptr);

    FreeLibrary(h);

    if (hasVST2 && hasVST3) return PluginType::BOTH_VST2_AND_VST3;
    if (hasVST2)             return PluginType::VST2_ONLY;
    if (hasVST3)             return PluginType::VST3_ONLY;
    return PluginType::NOT_A_VST;
}
```

Note: VST3 plugins installed as `.vst3` bundles will never overlap with VST2 `.dll` files
at the path level. Overlap only occurs when a single DLL exports both entry points.

### 9.3 Scan Strategy for Both Types

In the combined scanner:

1. Scan VST3 standard paths (`%COMMONPROGRAMFILES%\VST3\`) for `.vst3` bundles
   — these are always VST3 only (bundle format carries no `VSTPluginMain`).
2. Scan VST2 paths (registry + defaults) for `.dll` files.
3. For each `.dll`, run `detectPluginType()`:
   - `VST2_ONLY` → add to VST2 cache
   - `VST3_ONLY` → could also appear in VST3 paths; add to VST3 cache
   - `BOTH` → add to *both* caches with a flag `"dual": true`
   - `NOT_A_VST` → ignore
4. Deduplicate: if the same VST3 DLL appears both as a `.vst3` bundle member and as a
   loose `.dll` in a VST2 path, prefer the bundle version.
5. For dual plugins, expose only the VST3 interface in the chain UI (prefer VST3; VST2 is
   the fallback for VST2-only plugins).

---

## 10. IVSTPlugin — Polymorphic Interface Design

This is the core abstraction that allows `DSPChain` to hold a mixed list of VST2 and VST3
plugins without caring which type each one is.

```cpp
// src/plugin/IVSTPlugin.h
#pragma once
#include <string>
#include <cstdint>

class IVSTPlugin {
public:
    virtual ~IVSTPlugin() = default;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Load and initialize the plugin for audio processing.
    /// @param sampleRate    Processing sample rate in Hz
    /// @param maxBlockSize  Maximum number of samples per process() call
    /// @param numChannels   Host-side channel count (plugin adapts if needed)
    /// @return true on success
    virtual bool load(double sampleRate, int maxBlockSize, int numChannels) = 0;

    /// Tear down and release all plugin resources. Safe to call if not loaded.
    virtual void unload() = 0;

    /// Reinitialize after a sample rate or block size change.
    /// Default implementation: unload() then load() — override for efficiency.
    virtual bool reinitialize(double newSampleRate, int newMaxBlockSize, int numChannels) {
        unload();
        return load(newSampleRate, newMaxBlockSize, numChannels);
    }

    // -----------------------------------------------------------------------
    // Audio Processing (called from audio thread only)
    // -----------------------------------------------------------------------

    /// Process one block of audio.
    /// @param in       Input channel buffers (one pointer per channel)
    /// @param out      Output channel buffers (one pointer per channel)
    /// @param samples  Number of sample frames in this block (≤ maxBlockSize)
    /// @return Number of samples actually processed (should == samples)
    virtual int process(float** in, float** out, int samples) = 0;

    // -----------------------------------------------------------------------
    // Parameters (may be called from any thread; implementation must queue)
    // -----------------------------------------------------------------------

    /// Set a parameter by its integer index.
    /// @param index  0-based parameter index
    /// @param value  Normalized value in range [0.0, 1.0]
    virtual void setParameter(int index, float value) = 0;

    /// Get a parameter's current value.
    /// @return Normalized value in [0.0, 1.0], or 0.0 if index out of range
    virtual float getParameter(int index) const = 0;

    /// Return the number of parameters this plugin exposes.
    virtual int getParameterCount() const = 0;

    /// Return a human-readable name for a parameter.
    virtual std::string getParameterName(int index) const = 0;

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    /// Return plugin latency in samples (used for A/V sync compensation).
    virtual int getLatencySamples() const = 0;

    /// Human-readable plugin name.
    virtual std::string getName() const = 0;

    /// Human-readable vendor/manufacturer name.
    virtual std::string getVendorName() const = 0;

    /// Full path to the plugin file on disk.
    virtual std::string getPath() const = 0;

    /// Whether the plugin is currently loaded and ready to process.
    virtual bool isLoaded() const = 0;

    /// Whether the plugin is in bypassed mode (process() call is skipped).
    bool isBypassed() const { return m_bypassed; }
    void setBypassed(bool bypass) { m_bypassed = bypass; }

    /// VST format type — for serialization and display
    enum class PluginFormat { VST2, VST3 };
    virtual PluginFormat getFormat() const = 0;

    // -----------------------------------------------------------------------
    // State Persistence (called from GUI/settings thread, not audio thread)
    // -----------------------------------------------------------------------

    /// Serialize plugin state to an opaque byte vector.
    virtual std::vector<uint8_t> saveState() const = 0;

    /// Restore plugin state from a previously saved byte vector.
    virtual bool loadState(const std::vector<uint8_t>& data) = 0;

protected:
    bool m_bypassed = false;
};
```

### Design Notes

- `setParameter` uses integer index, not Steinberg `ParamID` (uint32), because VST2 only
  has integer indices. The VST3 wrapper can maintain a mapping from index to ParamID.
- `saveState`/`loadState` use `std::vector<uint8_t>` — VST3 uses `IBStream`,
  VST2 uses opaque chunks or param arrays. Both formats serialize to bytes here.
- The `m_bypassed` flag is concrete in the base class because the bypass decision is always
  made by the chain orchestrator, not the plugin itself.
- No GUID/ClassID field in the interface — `getPath()` + `getFormat()` is sufficient for
  reloading. The VST3 ClassID is only needed internally by `VSTPlugin3`.

---

## 11. VSTPlugin2 — Loader Class Skeleton

```cpp
// src/vst2/VSTPlugin2.h
#pragma once
#include "plugin/IVSTPlugin.h"
#include "util/ParamQueue.h"   // existing lock-free ring buffer from VST3 plan
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

// Forward declarations of VST2 types (no SDK needed — defined in vst2_types.h)
struct AEffect;

class VSTPlugin2 : public IVSTPlugin {
public:
    explicit VSTPlugin2(const std::string& dllPath);
    ~VSTPlugin2() override;

    // IVSTPlugin
    bool        load(double sampleRate, int maxBlockSize, int numChannels) override;
    void        unload() override;
    int         process(float** in, float** out, int samples) override;
    void        setParameter(int index, float value) override;
    float       getParameter(int index) const override;
    int         getParameterCount() const override;
    std::string getParameterName(int index) const override;
    int         getLatencySamples() const override;
    std::string getName() const override;
    std::string getVendorName() const override;
    std::string getPath() const override { return m_path; }
    bool        isLoaded() const override { return m_effect != nullptr; }
    PluginFormat getFormat() const override { return PluginFormat::VST2; }
    std::vector<uint8_t> saveState() const override;
    bool                 loadState(const std::vector<uint8_t>& data) override;

private:
    // --- Internals ---

    // Called as the static audioMasterCallback; dispatches to instance method
    static VstIntPtr VSTCALLBACK staticAudioMasterCallback(
        AEffect* effect, VstInt32 opcode,
        VstInt32 index, VstIntPtr value, void* ptr, float opt);

    VstIntPtr handleHostCallback(AEffect* effect, VstInt32 opcode,
                                  VstInt32 index, VstIntPtr value,
                                  void* ptr, float opt);

    // Mono plugin adapter (numInputs/numOutputs == 1, host channels > 1)
    int processMono(float** in, float** out, int samples);

    // Internal buffer for mono mix-down/fold-up
    std::vector<float> m_monoInBuf;
    std::vector<float> m_monoOutBuf;

    // --- State ---
    std::string     m_path;
    HMODULE         m_hDll    = nullptr;
    AEffect*        m_effect  = nullptr;
    double          m_sampleRate  = 48000.0;
    int             m_maxBlockSize = 1024;
    int             m_numChannels  = 2;
    std::string     m_name;
    std::string     m_vendorName;

    // Processing state flag (set true while inside processReplacing)
    // Used by hostCallback to return correct process level
    bool            m_inProcess = false;

    // Lock-free parameter change queue (GUI thread → audio thread)
    struct ParamChange2 { int index; float value; };
    RingBuffer<ParamChange2, 256> m_paramQueue;

    // Chunk-based state flag (set after load() based on effFlagsProgramChunks)
    bool m_usesChunk = false;
};
```

```cpp
// src/vst2/VSTPlugin2.cpp  — key method implementations

bool VSTPlugin2::load(double sampleRate, int maxBlockSize, int numChannels)
{
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_numChannels  = numChannels;

    // 1. Load DLL
    m_hDll = LoadLibraryW(std::wstring(m_path.begin(), m_path.end()).c_str());
    if (!m_hDll) return false;

    // 2. Resolve entry point
    using VSTPluginMainFn = AEffect*(VSTCALLBACK*)(audioMasterCallback);
    auto pluginMain = reinterpret_cast<VSTPluginMainFn>(
        GetProcAddress(m_hDll, "VSTPluginMain"));
    if (!pluginMain)
        pluginMain = reinterpret_cast<VSTPluginMainFn>(
            GetProcAddress(m_hDll, "main"));
    if (!pluginMain) { unload(); return false; }

    // 3. Instantiate — catch crashes with SEH
    __try {
        m_effect = pluginMain(staticAudioMasterCallback);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        m_effect = nullptr;
    }
    if (!m_effect || m_effect->magic != kEffectMagic) { unload(); return false; }

    // 4. Store back-pointer so static callback can reach this instance
    m_effect->user = this;

    // 5. Check required flags
    if (!(m_effect->flags & effFlagsCanReplacing)) { unload(); return false; }

    // 6. Setup sequence
    m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);
    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr, (float)m_sampleRate);
    m_effect->dispatcher(m_effect, effSetBlockSize, 0, (VstIntPtr)m_maxBlockSize, nullptr, 0.0f);

    // 7. Read metadata
    char buf[128] = {};
    m_effect->dispatcher(m_effect, effGetEffectName, 0, 0, buf, 0.0f);
    buf[127] = '\0';
    m_name = buf;

    memset(buf, 0, sizeof(buf));
    m_effect->dispatcher(m_effect, effGetVendorString, 0, 0, buf, 0.0f);
    buf[127] = '\0';
    m_vendorName = buf;

    // 8. Note state save strategy
    m_usesChunk = (m_effect->flags & effFlagsProgramChunks) != 0;

    // 9. Allocate mono buffers if needed
    if (m_effect->numInputs == 1 || m_effect->numOutputs == 1) {
        m_monoInBuf.resize(m_maxBlockSize);
        m_monoOutBuf.resize(m_maxBlockSize);
    }

    // 10. Activate
    m_effect->dispatcher(m_effect, effMainsChanged, 0, 1, nullptr, 0.0f);
    m_effect->dispatcher(m_effect, effStartProcess,  0, 0, nullptr, 0.0f);

    return true;
}

void VSTPlugin2::unload()
{
    if (m_effect) {
        m_effect->dispatcher(m_effect, effStopProcess,  0, 0, nullptr, 0.0f);
        m_effect->dispatcher(m_effect, effMainsChanged, 0, 0, nullptr, 0.0f);
        m_effect->dispatcher(m_effect, effClose,        0, 0, nullptr, 0.0f);
        m_effect = nullptr;
    }
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
    }
}

int VSTPlugin2::process(float** in, float** out, int samples)
{
    if (!m_effect || !m_effect->processReplacing) return 0;

    // Drain parameter queue before processing
    ParamChange2 pc;
    while (m_paramQueue.pop(pc)) {
        if (pc.index >= 0 && pc.index < m_effect->numParams)
            m_effect->setParameter(m_effect, pc.index, pc.value);
    }

    // Handle mono plugin
    if (m_effect->numInputs == 1 && m_effect->numOutputs == 1)
        return processMono(in, out, samples);

    // Normal case: plugin channel count matches host channel count
    m_inProcess = true;
    m_effect->processReplacing(m_effect, in, out, (VstInt32)samples);
    m_inProcess = false;
    return samples;
}

void VSTPlugin2::setParameter(int index, float value)
{
    // Called from GUI thread — queue for audio thread
    m_paramQueue.push({index, value});
}

float VSTPlugin2::getParameter(int index) const
{
    if (!m_effect || index < 0 || index >= m_effect->numParams) return 0.0f;
    // Safe to call from GUI thread for read-only queries in practice,
    // but formally this is not thread-safe. Acceptable for display purposes.
    return m_effect->getParameter(m_effect, index);
}

VstIntPtr VSTCALLBACK VSTPlugin2::staticAudioMasterCallback(
    AEffect* effect, VstInt32 opcode,
    VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    // effect->user is set to 'this' during load()
    // Guard against calls before user pointer is set (during pluginMain itself)
    if (effect && effect->user) {
        auto* self = static_cast<VSTPlugin2*>(effect->user);
        return self->handleHostCallback(effect, opcode, index, value, ptr, opt);
    }
    // Minimal fallback for calls before user pointer is set
    if (opcode == 1 /*audioMasterVersion*/) return 2400;
    return 0;
}

std::vector<uint8_t> VSTPlugin2::saveState() const
{
    if (!m_effect) return {};

    if (m_usesChunk) {
        void* chunkData = nullptr;
        VstInt32 size = (VstInt32)m_effect->dispatcher(
            m_effect, effGetChunk, 0, 0, &chunkData, 0.0f);
        if (chunkData && size > 0) {
            // Prefix with 'C' marker to distinguish from param-based saves
            std::vector<uint8_t> result(1 + size);
            result[0] = 'C';
            memcpy(result.data() + 1, chunkData, size);
            return result;
        }
    }

    // Fallback: save numParams floats
    std::vector<uint8_t> result(1 + m_effect->numParams * sizeof(float));
    result[0] = 'P';  // 'P' = param-based save
    float* params = reinterpret_cast<float*>(result.data() + 1);
    for (int i = 0; i < m_effect->numParams; i++)
        params[i] = m_effect->getParameter(m_effect, i);
    return result;
}

bool VSTPlugin2::loadState(const std::vector<uint8_t>& data)
{
    if (!m_effect || data.empty()) return false;
    if (data[0] == 'C') {
        // Chunk restore
        m_effect->dispatcher(m_effect, effSetChunk,
            0, (VstIntPtr)(data.size() - 1),
            const_cast<uint8_t*>(data.data() + 1), 0.0f);
        return true;
    }
    if (data[0] == 'P') {
        // Param restore
        int count = (int)((data.size() - 1) / sizeof(float));
        const float* params = reinterpret_cast<const float*>(data.data() + 1);
        for (int i = 0; i < std::min(count, m_effect->numParams); i++)
            m_effect->setParameter(m_effect, i, params[i]);
        return true;
    }
    return false;
}
```

---

## 12. VSTPlugin3 — Adapted Wrapper Skeleton

The existing `VSTPlugin` class from `VST_IMPLEMENTATION_PLAN.md` must be adapted to
implement `IVSTPlugin`. The changes are straightforward:

```cpp
// src/vst3/VSTPlugin3.h  — rename VSTPlugin → VSTPlugin3, inherit IVSTPlugin
class VSTPlugin3 : public IVSTPlugin {
public:
    explicit VSTPlugin3(const std::string& path);
    // ...all IVSTPlugin overrides...

    // VST3-specific: set parameter by ParamID (uint32) rather than index
    void setParameterByID(uint32_t paramID, double normalizedValue);

    PluginFormat getFormat() const override { return PluginFormat::VST3; }

    // VST3 ClassID — needed for saving chain config to JSON
    std::string getClassID() const;

private:
    // ... existing VST3 SDK objects ...

    // Index-to-ParamID mapping (built during load)
    std::vector<uint32_t> m_paramIDByIndex;

    // IVSTPlugin::setParameter(index, float) maps via m_paramIDByIndex
};
```

### Mapping Integer Index to VST3 ParamID

VST3 parameters are addressed by `uint32_t` ParamID, not index. To implement
`IVSTPlugin::setParameter(int index, float value)`:

```cpp
void VSTPlugin3::setParameter(int index, float value)
{
    if (index < 0 || index >= (int)m_paramIDByIndex.size()) return;
    uint32_t paramID = m_paramIDByIndex[index];
    // Queue {paramID, (double)value} for audio thread (existing RingBuffer)
    m_paramQueue.push({paramID, (double)value});
}
```

Build `m_paramIDByIndex` during `load()` by iterating `IEditController::getParameterCount()`
and `getParameterInfo(i, info)`, storing `info.id` at position `i`.

---

## 13. Scanner Updates for VST2

The existing `scanner/vst3scanner.cpp` (standalone EXE) must be extended to also scan
VST2 DLLs. Options:

### Option A: Combined Scanner (recommended)

Extend the existing scanner EXE to handle both formats. The scanner outputs a unified JSON
array for both types.

```
scanner/vstscanner.cpp         (rename from vst3scanner.cpp)
```

The scanner's `main()` flow:

```
1. Scan VST3 paths  → enumerate .vst3 bundles → load each, read metadata
2. Scan VST2 paths  → enumerate .dll files    → detect type → load, read metadata
3. Output combined JSON to stdout
```

VST2 scan code in the scanner (runs in isolated process — crashes are safe):

```cpp
struct PluginInfo {
    std::string path;
    std::string name;
    std::string vendor;
    std::string format;       // "vst2" or "vst3"
    std::string classID;      // VST3 only
    int         numInputs;
    int         numOutputs;
    int         numParams;
    int         latency;
    bool        usesChunk;
};

PluginInfo scanVST2(const std::wstring& dllPath)
{
    PluginInfo info;
    info.format = "vst2";
    info.path   = wstringToUtf8(dllPath);

    HMODULE h = LoadLibraryW(dllPath.c_str());
    if (!h) { info.name = "LOAD_FAILED"; return info; }

    using MainFn = AEffect*(VSTCALLBACK*)(audioMasterCallback);
    auto fn = (MainFn)GetProcAddress(h, "VSTPluginMain");
    if (!fn) fn = (MainFn)GetProcAddress(h, "main");
    if (!fn) { FreeLibrary(h); info.name = "NOT_VST2"; return info; }

    AEffect* fx = nullptr;
    __try { fx = fn(scannerAudioMaster); }
    __except(EXCEPTION_EXECUTE_HANDLER) { }

    if (!fx || fx->magic != kEffectMagic) {
        FreeLibrary(h); info.name = "BAD_MAGIC"; return info;
    }

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0.0f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, 512, nullptr, 0.0f);

    char buf[128] = {};
    fx->dispatcher(fx, effGetEffectName, 0, 0, buf, 0.0f);
    info.name = buf[0] ? buf : "Unknown";

    memset(buf, 0, sizeof(buf));
    fx->dispatcher(fx, effGetVendorString, 0, 0, buf, 0.0f);
    info.vendor    = buf;
    info.numInputs  = fx->numInputs;
    info.numOutputs = fx->numOutputs;
    info.numParams  = fx->numParams;
    info.latency    = fx->initialDelay;
    info.usesChunk  = (fx->flags & effFlagsProgramChunks) != 0;

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.0f);
    FreeLibrary(h);
    return info;
}
```

### Option B: Separate Scanner EXE

Keep `vst3scanner.cpp` as-is and add `vst2scanner.cpp`. The addon runs both, merges their
JSON output. Slightly more process overhead but cleaner separation. Recommended only if
the VST3 scanner is already shipping and cannot be changed.

---

## 14. DSPChain with Mixed VST2+VST3

`DSPChain` holds a `vector<unique_ptr<IVSTPlugin>>`. Both VST2 and VST3 plugins satisfy
this interface.

```cpp
// src/dsp/DSPChain.h
#pragma once
#include "plugin/IVSTPlugin.h"
#include <vector>
#include <memory>

class DSPChain {
public:
    void addPlugin(std::unique_ptr<IVSTPlugin> plugin);
    void removePlugin(int index);
    void reorder(int fromIndex, int toIndex);
    void clear();

    int pluginCount() const { return (int)m_plugins.size(); }
    IVSTPlugin* getPlugin(int index) const;

    // Called from audio thread — chains all plugins in order
    int process(float** in, float** out, int samples, int numChannels);

    // Sum of all plugin latencies (for MasterProcessGetDelay)
    int getTotalLatencySamples() const;

    // Called when stream format changes
    bool reinitialize(double sampleRate, int maxBlockSize, int numChannels);

private:
    std::vector<std::unique_ptr<IVSTPlugin>> m_plugins;

    // Ping-pong intermediate buffers
    // Pre-allocated at reinitialize() time — no runtime allocation in process()
    std::vector<std::vector<float>> m_pingBuf;  // [channel][sample]
    std::vector<std::vector<float>> m_pongBuf;
    std::vector<float*>             m_pingPtrs; // [channel] -> m_pingBuf[ch].data()
    std::vector<float*>             m_pongPtrs;
    int m_numChannels = 0;
    int m_maxBlockSize = 0;
};
```

```cpp
// src/dsp/DSPChain.cpp — process implementation
int DSPChain::process(float** in, float** out, int samples, int numChannels)
{
    if (m_plugins.empty()) {
        // No plugins: copy in → out
        for (int ch = 0; ch < numChannels; ch++)
            memcpy(out[ch], in[ch], samples * sizeof(float));
        return samples;
    }

    float** current_in  = in;
    float** current_out = m_pingPtrs.data();  // first intermediate = ping buffer

    for (int i = 0; i < (int)m_plugins.size(); i++) {
        auto& p = m_plugins[i];

        if (!p->isLoaded() || p->isBypassed()) {
            for (int ch = 0; ch < numChannels; ch++)
                memcpy(current_out[ch], current_in[ch], samples * sizeof(float));
        } else {
            p->process(current_in, current_out, samples);
        }

        // Set up buffers for next plugin
        if (i + 1 < (int)m_plugins.size()) {
            // Alternate between ping and pong
            current_in  = current_out;
            current_out = (current_out == m_pingPtrs.data())
                        ? m_pongPtrs.data()
                        : m_pingPtrs.data();
        } else {
            // Last plugin: output goes directly to out
            // But we already processed into current_out (ping or pong)
            // Copy to final out
            for (int ch = 0; ch < numChannels; ch++)
                memcpy(out[ch], current_out[ch], samples * sizeof(float));
        }
    }

    return samples;
}
```

### Important Note on Intermediate Buffers

The ping-pong approach above copies the final result into `out` for the last plugin,
which is always correct regardless of how many plugins are in the chain. This is slightly
less efficient than the swap-pointer trick in the original VST3-only plan (which requires
a final extra copy if the chain length is even) but is simpler and less error-prone.

For maximum efficiency, use the swap-pointer approach but ensure the final result always
ends up in `out` by tracking which buffer is "current_out" at the end.

---

## 15. JSON Serialization for Mixed Chain Config

The chain config file (`chain.json`) must handle both VST2 and VST3 entries:

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
      "state": "AAEC...(base64 of saveState() bytes)...",
      "parameters": {}
    },
    {
      "format": "vst2",
      "path": "C:\\Program Files\\VSTPlugins\\OldCompressor.dll",
      "classID": null,
      "name": "OldCompressor",
      "bypass": false,
      "state_encoding": "base64",
      "state": "AAEC...(base64 of saveState() bytes)...",
      "parameters": {
        "0": 0.75,
        "1": 0.40,
        "2": 0.60
      }
    }
  ]
}
```

### Serialization Fields

| Field | Purpose |
|-------|---------|
| `format` | `"vst2"` or `"vst3"` — selects which factory to use when loading |
| `path` | Absolute path to the plugin file |
| `classID` | VST3 only — required to re-find the correct plugin in a multi-plugin .vst3 bundle |
| `name` | Human-readable name — for display, not used to identify the plugin |
| `bypass` | Whether the plugin is currently bypassed |
| `state_encoding` | Always `"base64"` for safety with binary data |
| `state` | Base64-encoded result of `IVSTPlugin::saveState()`. For VST2: first byte is `'C'` (chunk) or `'P'` (params). For VST3: the IBStream bytes. |
| `parameters` | Optional overrides for individual parameters (string key = index). Used for simple knob tweaks without full state save. Applied after `loadState()`. |

### Loading Logic

```cpp
// In PluginSettings::loadChain()
for (auto& entry : chainJson["chain"]) {
    std::string fmt = entry["format"];
    std::unique_ptr<IVSTPlugin> plugin;

    if (fmt == "vst2") {
        plugin = std::make_unique<VSTPlugin2>(entry["path"]);
    } else if (fmt == "vst3") {
        plugin = std::make_unique<VSTPlugin3>(entry["path"], entry["classID"]);
    } else {
        // Unknown format — skip with warning
        continue;
    }

    plugin->setBypassed(entry.value("bypass", false));

    if (plugin->load(m_sampleRate, m_maxBlockSize, m_numChannels)) {
        // Restore state
        if (entry.contains("state") && !entry["state"].get<std::string>().empty()) {
            auto stateBytes = base64Decode(entry["state"]);
            plugin->loadState(stateBytes);
        }
        // Apply individual parameter overrides
        if (entry.contains("parameters")) {
            for (auto& [key, val] : entry["parameters"].items()) {
                plugin->setParameter(std::stoi(key), val.get<float>());
            }
        }
        m_chain.addPlugin(std::move(plugin));
    } else {
        // Plugin failed to load — add a placeholder in bypass mode
        plugin->setBypassed(true);
        m_chain.addPlugin(std::move(plugin));
    }
}
```

---

## 16. Threading Model for VST2

### Summary of Thread Rules

| Operation | Thread | Notes |
|-----------|--------|-------|
| `load()` / `unload()` | Main/settings thread | Never call while audio thread is processing |
| `processReplacing` | Audio thread only | Single thread, consistent across calls |
| `setParameter` (IVSTPlugin) | Any thread | Enqueues to lock-free ring buffer |
| `getParameter` | Any thread | Reads current value — formally not thread-safe but acceptable for display |
| `saveState` / `loadState` | Main/settings thread | Must not run concurrently with process |

### Comparing VST2 and VST3 Threading

- **VST3** has an *explicitly defined* threading model: `IParameterChanges` queue, separate
  `IEditController` for UI thread, strict rules about which calls cross threads.
- **VST2** has *no defined threading model.* The entire burden is on the host.
- The `RingBuffer<ParamChange2>` approach (same as VST3) satisfies VST2 requirements as
  long as `setParameter` on the `AEffect` struct is only called from the audio thread.

### Reinitialize Safety

If the audio format changes mid-session (sample rate change, channel count change):

```cpp
// Called from the settings/stream thread (not audio thread)
// The Kodi ADSP system guarantees StreamInitialize is not called while
// MasterProcess is running — so this is safe without additional locking
bool DSPProcessor::reinitialize(const AE_DSP_SETTINGS& settings)
{
    m_chain.reinitialize(
        settings.iProcessSamplerate,
        m_maxBlockSize,
        settings.iInChannels);
    return true;
}
```

The `DSPChain::reinitialize()` calls `unload()` then `load()` on each plugin, which is
safe because Kodi serializes `StreamInitialize` and `MasterProcess`.

---

## 17. Project Structure Changes

Add these files alongside the existing VST3 structure from `VST_IMPLEMENTATION_PLAN.md`:

```
kodi-audiodsp-vst3/
├── src/
│   ├── plugin/
│   │   └── IVSTPlugin.h           ← NEW: polymorphic interface (section 10)
│   │
│   ├── vst2/                      ← NEW directory
│   │   ├── VSTPlugin2.h           ← NEW: VST2 loader (section 11)
│   │   ├── VSTPlugin2.cpp
│   │   └── vst2_types.h           ← NEW: minimal AEffect struct, opcode enums
│   │
│   ├── vst3/
│   │   ├── VSTPlugin3.h           ← RENAMED from VSTPlugin.h; now inherits IVSTPlugin
│   │   ├── VSTPlugin3.cpp         ← RENAMED; adapted to IVSTPlugin (section 12)
│   │   ├── VSTHostContext.h/.cpp  ← unchanged
│   │   └── VSTPluginManager.h/.cpp ← UPDATED: manages both VST2 and VST3
│   │
│   ├── dsp/
│   │   ├── DSPChain.h/.cpp        ← UPDATED: holds vector<unique_ptr<IVSTPlugin>>
│   │   └── DSPProcessor.h/.cpp    ← UPDATED: uses IVSTPlugin interface
│   │
│   └── util/
│       └── ParamQueue.h           ← SHARED by both VSTPlugin2 and VSTPlugin3
│
└── scanner/
    └── vstscanner.cpp             ← RENAMED (was vst3scanner); scans both formats
```

### Files That Are Renamed or Significantly Changed

| Old Name | New Name / Change |
|----------|-------------------|
| `src/vst3/VSTPlugin.h/.cpp` | Renamed to `VSTPlugin3.h/.cpp`; inherits `IVSTPlugin` |
| `src/dsp/DSPChain.h/.cpp` | Chain element type changes from `VSTPlugin*` to `IVSTPlugin*` |
| `scanner/vst3scanner.cpp` | Renamed to `vstscanner.cpp`; handles both formats |

---

## 18. CMake Changes

```cmake
# In CMakeLists.txt — additions for VST2 support

# VST2 does not require any external SDK — only vst2_types.h (internal)
# The VSTPlugin2 implementation uses only Windows APIs (LoadLibrary, etc.)

add_library(audiodsp.vsthost SHARED
    src/addon_main.cpp
    src/dsp/DSPProcessor.cpp
    src/dsp/DSPChain.cpp

    # VST2 — no external library needed
    src/vst2/VSTPlugin2.cpp

    # VST3 — requires vst3_hosting library (unchanged from original plan)
    src/vst3/VSTPlugin3.cpp         # renamed from VSTPlugin.cpp
    src/vst3/VSTHostContext.cpp
    src/vst3/VSTPluginManager.cpp

    src/settings/PluginSettings.cpp
    src/settings/PresetManager.cpp
)

target_link_libraries(audiodsp.vsthost PRIVATE
    vst3_hosting  # unchanged from original plan
    # No additional library for VST2
)

target_include_directories(audiodsp.vsthost PRIVATE
    ${CMAKE_SOURCE_DIR}/src        # for plugin/IVSTPlugin.h
    ${KODI_ADSP_SDK_DIR}
)

# Combined scanner (was vst3scanner.cpp)
add_executable(vstscanner scanner/vstscanner.cpp)
target_link_libraries(vstscanner PRIVATE vst3_hosting)
target_include_directories(vstscanner PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

Note: The addon name changes from `audiodsp.vst3host` to `audiodsp.vsthost` to reflect
support for both formats. Update `addon.xml` accordingly.

---

## 19. Windows-Specific Gotchas

### 19.1 DLL Search Path

When `LoadLibraryW` loads a VST2 plugin DLL, Windows will search for that DLL's
dependencies starting from the *DLL's own directory*, then the standard system paths.
Many VST2 plugins bundle extra DLLs (MSVC runtime, vendor library, protection DLL) in the
same folder. If the plugin's folder is not on the DLL search path, the load will fail.

Fix: Use `LoadLibraryExW` with `LOAD_WITH_ALTERED_SEARCH_PATH` flag, which makes Windows
treat the DLL's own directory as the first search location:

```cpp
m_hDll = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
```

### 19.2 32-bit vs 64-bit Plugin Mismatch

A 64-bit Kodi process cannot load a 32-bit VST2 DLL directly. The scan should detect and
skip 32-bit DLLs. Use `GetBinaryTypeW()` or read the PE header:

```cpp
bool is64BitDll(const std::wstring& path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    IMAGE_DOS_HEADER dos = {};
    DWORD read = 0;
    ReadFile(hFile, &dos, sizeof(dos), &read, nullptr);

    LONG ntOffset = dos.e_lfanew;
    SetFilePointer(hFile, ntOffset, nullptr, FILE_BEGIN);

    IMAGE_NT_HEADERS nt = {};
    ReadFile(hFile, &nt, sizeof(nt), &read, nullptr);
    CloseHandle(hFile);

    return nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
}
```

Add a `"bitness": 32` or `"bitness": 64` field to the scanner's JSON output. The addon
loader skips 32-bit plugins when running as a 64-bit process.

### 19.3 Plugin DLL Keeps File Handles Open

After `FreeLibrary`, the DLL is unloaded but the Windows loader may keep certain handles
open briefly. If you try to move or delete the DLL file immediately after `FreeLibrary`,
it may fail. This matters for plugin update scenarios but not normal operation.

### 19.4 Structured Exception Handling vs C++ Exceptions

`__try/__except` (SEH) and C++ `try/catch` cannot be mixed in the same function on MSVC
when SEH is active. Put all `__try/__except` code in dedicated functions (like
`VSTPlugin2::load()` calling a helper `callPluginMain()`). Do not put `__try` in a function
that has C++ objects with destructors — this triggers MSVC warning C4703 and can cause
undefined behavior.

```cpp
// Separate helper — no C++ destructors in scope
static AEffect* callPluginMainSafe(VSTPluginMainFn fn, audioMasterCallback cb)
{
    AEffect* result = nullptr;
    __try {
        result = fn(cb);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        result = nullptr;
    }
    return result;
}
```

### 19.5 VST2 and COM Apartments

Unlike VST3, VST2 has no opinion on COM initialization. However, a small number of VST2
plugins use COM internally (for licensing, copy protection, or UI). If a plugin fails to
load with mysterious errors, try calling `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`
before loading it. Do not rely on this being unnecessary.

### 19.6 effGetParamName 8-Byte Limit

The VST2 specification limits `effGetParamName` string results to 8 bytes (7 chars + null).
However, many real-world plugins write up to 32 bytes. Always allocate at least 64 bytes
for the buffer to be safe. Never pass an 8-byte buffer — stack corruption will result with
buggy plugins.

### 19.7 Plugin Version of effSetSampleRate Uses `opt` Not `value`

The sample rate is passed in the `opt` parameter (a `float`) of the dispatcher call, not
in `value`. This is a VST2 quirk:

```cpp
// Correct:
effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, (float)sampleRate);
//                                                    ^value=0  ^opt=sampleRate

// Block size uses value (correct):
effect->dispatcher(effect, effSetBlockSize, 0, (VstIntPtr)blockSize, nullptr, 0.0f);
//                                              ^value=blockSize
```

Mixing these up is a common host bug that causes plugins to run at the wrong sample rate
(usually defaulting to whatever the plugin initializes `opt` to at startup, typically 0.0).

### 19.8 effMainsChanged Must Be Called Before effStartProcess

The VST2 spec is strict: `effStartProcess`/`effStopProcess` (opcodes 71/72) are only valid
after `effMainsChanged` (opcode 12) has been called with `value=1`. Calling `effStartProcess`
first (or without calling `effMainsChanged`) is undefined behavior and will crash some
plugins. Always follow the sequence in Section 3.

---

## 20. Summary of Differences from VST3

| Aspect | VST3 (existing plan) | VST2 (this document) |
|--------|----------------------|----------------------|
| DLL loading | VST3 SDK `Module::create()` | Raw `LoadLibraryW` + `GetProcAddress` |
| Entry point | `GetPluginFactory()` | `VSTPluginMain()` or `main()` |
| Identification | Magic number in struct | Magic number `kEffectMagic` = 0x56737450 |
| Process call | `IAudioProcessor::process(ProcessData)` | `AEffect::processReplacing(float**, float**, VstInt32)` |
| Buffer format | `float**` via `AudioBusBuffers` | `float**` direct — identical to Kodi |
| Parameters | `uint32_t` ParamID, `IEditController` | `int` index, direct function pointer |
| Thread safety | Defined by SDK spec | Host's responsibility entirely |
| State save | `IBStream` via `IComponent` | Opaque chunk or per-param floats |
| Latency | `IAudioProcessor::getLatencySamples()` | `AEffect::initialDelay` field |
| Vendor/name | IEditController metadata | `effGetVendorString` / `effGetEffectName` opcodes |
| Discovery paths | `%COMMONPROGRAMFILES%\VST3\` | Registry + `%PROGRAMFILES%\VSTPlugins\` |
| File extension | `.vst3` (bundle directory) | `.dll` (single file) |
| SDK needed | Steinberg VST3 SDK (MIT) | None — struct/opcode ABI is public |
| License risk | None (MIT SDK) | Low if only using public ABI constants |
| Multi-plugin DLL | VST3 bundle has multiple classes | VST2: one AEffect per DLL |

---

*Document completed 2026-04-09. Covers full VST2 hosting ABI, polymorphic interface design,
scanner strategy, mixed-chain architecture, and Windows-specific implementation hazards.*
