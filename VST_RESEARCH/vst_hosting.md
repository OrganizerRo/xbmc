# VST Plugin Hosting Research for Kodi (XBMC)

**Date:** 2026-04-09
**Purpose:** Evaluate adding VST plugin hosting to Kodi's audio processing pipeline
**Scope:** VST2 and VST3 on Windows, audio-only (no GUI), C++ implementation

---

## Table of Contents

1. [Executive Summary and Recommendation](#1-executive-summary-and-recommendation)
2. [VST2 — Technical Overview](#2-vst2--technical-overview)
3. [VST3 — Technical Overview](#3-vst3--technical-overview)
4. [VST2 vs VST3 Comparison for Kodi](#4-vst2-vs-vst3-comparison-for-kodi)
5. [Existing Libraries and Frameworks](#5-existing-libraries-and-frameworks)
6. [Minimum Code Needed to Host a VST3 Plugin](#6-minimum-code-needed-to-host-a-vst3-plugin)
7. [Threading and Performance Considerations](#7-threading-and-performance-considerations)
8. [Plugin Discovery on Windows](#8-plugin-discovery-on-windows)
9. [Licensing Analysis](#9-licensing-analysis)
10. [Risks and Challenges](#10-risks-and-challenges)
11. [Recommended Integration Approach for Kodi](#11-recommended-integration-approach-for-kodi)
12. [References](#12-references)

---

## 1. Executive Summary and Recommendation

**Recommendation: Use VST3 only, via the Steinberg VST3 SDK (MIT license), with no dependency on JUCE.**

Key findings:

- The **VST3 SDK** was re-licensed from a dual GPLv3/proprietary model to **MIT** (as of 2024/2025). This is confirmed in the repository's `LICENSE.txt`. This eliminates the previously significant open source obstacle.
- **VST2 is a dead end.** Steinberg stopped distributing the VST2 SDK around 2018. There is no legal way to obtain it without a prior commercial agreement. Redistributing VST2 header definitions in Kodi's source is legally risky.
- **JUCE** is dual-licensed AGPLv3/commercial. AGPLv3 is incompatible with Kodi's GPLv2 codebase (not GPLv2-or-later in all parts). JUCE also brings significant compile-time and binary-size overhead for what is a narrow use case.
- The **Steinberg VST3 SDK itself** contains hosting helper classes (`VST3::Hosting::Module`, `PlugProvider`, `HostProcessData`, etc.) that cover everything needed for audio-only hosting without GUI.
- **CLAP** (Clever Audio Plugin) is an emerging open alternative (MIT license) worth tracking, but plugin availability is not yet broad enough to be a primary target.

**Minimum viable implementation:** ~600–1000 lines of C++ using only the VST3 SDK headers and helpers, no third-party dependencies.

---

## 2. VST2 — Technical Overview

### 2.1 API Basics

VST2 is a C API built around a single struct `AEffect`:

```c
// Simplified AEffect structure
struct AEffect {
    VstInt32 magic;           // Must be 0x56737450 ('VstP')
    AEffectDispatcherProc dispatcher;    // host->plugin opcodes
    AEffectProcessProc process;          // deprecated (use processReplacing)
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;           // kPluginIsSynth, kPluginHasEditor, etc.
    VstIntPtr resvd1, resvd2;
    VstInt32 initialDelay;    // latency in samples
    VstInt32 uniqueID;        // 4-char identifier
    float version;
    AEffectProcessProc processReplacing;   // the main audio callback
    AEffectProcessDoubleProc processDoubleReplacing;
    char future[56];
};
```

### 2.2 Loading a VST2 DLL on Windows

```cpp
// 1. Load the DLL
HMODULE hDll = LoadLibraryW(L"C:\\path\\to\\plugin.dll");

// 2. Get the entry point (note: older plugins used "main" on some platforms)
typedef AEffect* (VSTCALLBACK* VSTPluginMain)(audioMasterCallback hostCallback);
VSTPluginMain pluginMain = (VSTPluginMain)GetProcAddress(hDll, "VSTPluginMain");
if (!pluginMain)
    pluginMain = (VSTPluginMain)GetProcAddress(hDll, "main"); // fallback

// 3. Instantiate the plugin
AEffect* effect = pluginMain(hostCallback);
// effect->magic must equal kEffectMagic (0x56737450)
```

### 2.3 The Host Callback Function

The plugin calls the host for services via a callback that you provide at instantiation:

```cpp
VstIntPtr VSTCALLBACK hostCallback(AEffect* effect, VstInt32 opcode,
                                    VstInt32 index, VstIntPtr value,
                                    void* ptr, float opt) {
    switch (opcode) {
        case audioMasterVersion:
            return 2400; // VST 2.4

        case audioMasterGetSampleRate:
            return (VstIntPtr)sampleRate;

        case audioMasterGetBlockSize:
            return (VstIntPtr)blockSize;

        case audioMasterGetCurrentProcessLevel:
            return kVstProcessLevelRealtime; // 2 = realtime audio thread

        case audioMasterGetTime: {
            // Return pointer to VstTimeInfo struct
            static VstTimeInfo timeInfo;
            timeInfo.sampleRate = sampleRate;
            timeInfo.samplePos = currentSample;
            timeInfo.flags = kVstTransportPlaying;
            return (VstIntPtr)&timeInfo;
        }

        case audioMasterIdle:
            // Some plugins call this to pump the GUI message loop - ignore
            return 0;

        // Many opcodes exist; most can return 0 safely for audio-only hosting
        default:
            return 0;
    }
}
```

### 2.4 Setup Sequence

```cpp
// After getting the AEffect* pointer:

// 1. Open the plugin
effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);

// 2. Set sample rate and block size
effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, (float)sampleRate);
effect->dispatcher(effect, effSetBlockSize, 0, blockSize, nullptr, 0.0f);

// 3. Activate
effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);

// 4. Start processing
effect->dispatcher(effect, effStartProcess, 0, 0, nullptr, 0.0f);
```

### 2.5 Audio Processing (processReplacing)

```cpp
// Float buffers: float*[numInputs] and float*[numOutputs]
// Each channel buffer has numSamples floats

float* inputBuffers[2];   // stereo input
float* outputBuffers[2];  // stereo output
int numSamples = 512;

// allocate actual sample storage elsewhere...
inputBuffers[0] = leftInputData;
inputBuffers[1] = rightInputData;
outputBuffers[0] = leftOutputData;
outputBuffers[1] = rightOutputData;

// The core call — effect processes in-place or replaces output buffers
effect->processReplacing(effect, inputBuffers, outputBuffers, numSamples);
```

### 2.6 Parameter Automation

```cpp
// Set a parameter (index 0..numParams-1, value 0.0..1.0 normalized)
effect->setParameter(effect, paramIndex, normalizedValue);

// Get a parameter
float val = effect->getParameter(effect, paramIndex);

// Get parameter name/label (result in ptr)
char name[64];
effect->dispatcher(effect, effGetParamName, paramIndex, 0, name, 0.0f);
```

### 2.7 VST2 Threading Requirements

- VST2 has **no explicit threading model** in the spec.
- In practice: the `processReplacing` call should come from a single, consistent audio thread.
- Parameter changes from a UI thread require thread-safe queuing (VST2 does not define a safe mechanism — the burden is entirely on the host).
- `audioMasterIdle` was called by plugins to pump their GUI message loop; since Kodi is audio-only, simply return 0 safely.

---

## 3. VST3 — Technical Overview

### 3.1 Architecture Overview

VST3 is a full COM-style C++ API. A plugin exposes a factory (`IPluginFactory`) from which the host creates components. Audio processing and UI editing are cleanly separated:

- **`IComponent`** — base component: bus configuration, state save/load, activate/deactivate
- **`IAudioProcessor`** — extends IComponent: processing setup and the `process()` call
- **`IEditController`** — parameter management, UI creation (optional for audio-only hosting)

The plugin DLL on Windows exports a single function:

```cpp
extern "C" SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory();
```

### 3.2 Key Interfaces

#### IComponent (mandatory)

```cpp
class IComponent : public IPluginBase {
public:
    virtual tresult getBusCount(MediaType type, BusDirection dir) = 0;
    virtual tresult getBusInfo(MediaType type, BusDirection dir,
                               int32 index, BusInfo& bus) = 0;
    virtual tresult activateBus(MediaType type, BusDirection dir,
                                int32 index, TBool state) = 0;
    virtual tresult setActive(TBool state) = 0;
    virtual tresult setState(IBStream* state) = 0;
    virtual tresult getState(IBStream* state) = 0;
};
```

#### IAudioProcessor (mandatory)

```cpp
class IAudioProcessor : public FUnknown {
public:
    virtual tresult setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                       SpeakerArrangement* outputs, int32 numOuts) = 0;
    virtual tresult setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult setProcessing(TBool state) = 0;
    virtual tresult process(ProcessData& data) = 0;
    virtual uint32  getLatencySamples() = 0;
    virtual uint32  getTailSamples() = 0;
};
```

#### ProcessSetup (setup before processing begins)

```cpp
struct ProcessSetup {
    int32      processMode;       // kRealtime, kPrefetch, kOffline
    int32      symbolicSampleSize; // kSample32 or kSample64
    int32      maxSamplesPerBlock; // maximum block size
    SampleRate sampleRate;
};
```

#### ProcessData (passed on every process() call)

```cpp
struct ProcessData {
    int32             processMode;
    int32             symbolicSampleSize;
    int32             numSamples;          // actual samples this block
    int32             numInputs;
    int32             numOutputs;
    AudioBusBuffers*  inputs;              // array of input bus buffers
    AudioBusBuffers*  outputs;             // array of output bus buffers
    IParameterChanges* inputParameterChanges;  // automation
    IParameterChanges* outputParameterChanges; // plugin-generated automation
    IEventList*       inputEvents;         // MIDI notes, etc.
    IEventList*       outputEvents;
    ProcessContext*   processContext;      // tempo, time signature, etc.
};

struct AudioBusBuffers {
    int32    numChannels;
    uint64   silenceFlags;   // bitmask of which channels are silent
    union {
        Sample32** channelBuffers32;  // float** for 32-bit
        Sample64** channelBuffers64;  // double** for 64-bit
    };
};
```

### 3.3 VST3 Bundle Structure on Windows

VST3 plugins are packaged as **bundles** (directories with a `.vst3` extension):

```
MyPlugin.vst3/
  Contents/
    x86_64-win/
      MyPlugin.vst3    (the actual DLL, same name as bundle)
    Resources/
      moduleinfo.json  (optional metadata)
      Snapshots/       (UI preview images)
```

The module loader in `module_win32.cpp` handles both bundle format and legacy single-DLL format automatically.

### 3.4 VST3 Processing — State Machine

The required sequence before calling `process()`:

```
Module::create(path)
  -> factory.classInfos()   [enumerate plugins in DLL]
  -> factory.createInstance<IComponent>(classID)
  -> component->initialize(hostContext)
  -> component->queryInterface(IAudioProcessor::iid, &processor)
  -> processor->setBusArrangements(...)
  -> component->activateBus(kAudio, kInput, 0, true)
  -> component->activateBus(kAudio, kOutput, 0, true)
  -> processor->setupProcessing(processSetup)
  -> component->setActive(true)
  -> processor->setProcessing(true)
  -> [AUDIO LOOP] processor->process(processData)
  -> processor->setProcessing(false)
  -> component->setActive(false)
  -> component->terminate()
```

### 3.5 VST3 Host Context

Every plugin needs a host application context object implementing `IHostApplication`:

```cpp
// Minimal host application — the SDK provides HostApplication in hosting/hostclasses.h
// It just needs a name and the ability to create IMessage/IAttributeList objects.
FUnknown* gStandardPluginContext = new Vst::HostApplication();
// Pass as the 'context' argument to IComponent::initialize()
```

### 3.6 Parameter Automation in VST3

VST3 uses a thread-safe parameter change queue model:

```cpp
// ParameterChanges implements IParameterChanges
// Add a parameter change for the next process() block:
ParameterChanges paramChanges;
int32 queueIndex;
IParamValueQueue* queue = paramChanges.addParameterData(paramID, queueIndex);
queue->addPoint(sampleOffset, normalizedValue, queueIndex);

// Then assign to processData:
processData.inputParameterChanges = &paramChanges;
```

The SDK's `ParameterChanges` and `ParamValueQueue` classes in `hosting/parameterchanges.h` are ready to use.

### 3.7 VST3 Threading Model

VST3 defines explicit threading rules:

- **`process()`** — called only from the audio thread; must never block.
- **Controller methods** (`setParamNormalized`, etc.) — called from the UI/main thread.
- **`IComponent::setState()`/`getState()`** — called from UI thread.
- Parameter changes cross from UI → audio thread via the `IParameterChanges` queue (lock-free).
- The VST3 SDK's `ParameterTransferrer` helper class handles the lock-free transfer safely.

---

## 4. VST2 vs VST3 Comparison for Kodi

| Factor | VST2 | VST3 |
|--------|------|------|
| **SDK Availability** | Discontinued ~2018; legally unavailable | Actively maintained by Steinberg; MIT license |
| **License** | Was proprietary; SDK no longer distributed | MIT (as of 2024/2025) |
| **Plugin Availability** | Enormous legacy library | Growing; most new plugins are VST3 only |
| **API Complexity** | Simple C struct-based | More complex COM-style C++ |
| **Audio-only Hosting** | Straightforward | Straightforward with SDK helpers |
| **Thread Safety** | Ad-hoc; no defined model | Explicit, well-defined model |
| **Parameter System** | Simple float 0..1 | Normalized values + dedicated change queues |
| **State Save/Load** | Via dispatcher opcodes | Via IBStream interface |
| **64-bit Audio** | Via `processDoubleReplacing` | Built-in via `kSample64` |
| **Multi-bus** | Limited (main in/out only) | Full multi-bus support |
| **Legal Risk** | High (no valid SDK source) | None (MIT) |
| **Future Support** | None — dead spec | Active; SDK updated regularly |

**Verdict: VST3 only.** Do not implement VST2. The legal risk alone is disqualifying, and Steinberg's own DAW products (Nuendo, Cubase) dropped VST2 loading support after 2020. Most plugin vendors now ship VST3-only. The small set of VST2-only legacy plugins a Kodi user might want to use is shrinking daily.

---

## 5. Existing Libraries and Frameworks

### 5.1 Steinberg VST3 SDK (RECOMMENDED)

- **Repo:** `https://github.com/steinbergmedia/vst3sdk` (with submodules)
- **License:** MIT (confirmed in LICENSE.txt)
- **Hosting utilities:** `public.sdk/source/vst/hosting/` — production-ready classes
  - `Module` — loads a VST3 DLL/bundle (Win32, macOS, Linux implementations)
  - `PluginFactory` — wraps `IPluginFactory`, enumerates classes
  - `PlugProvider` — instantiates and connects `IComponent` + `IEditController`
  - `HostProcessData` — manages audio buffer memory
  - `ParameterChanges` / `ParamValueQueue` — lock-free parameter automation
  - `EventList` — MIDI event list
  - `HostApplication` — minimal `IHostApplication` implementation
- **Samples:** `public.sdk/samples/vst-hosting/audiohost/` — a working audio-only host (uses JACK on Linux but shows the full VST3 hosting pattern)
- **Size:** The hosting subset compiles to ~50 source files; does not require the full SDK

### 5.2 JUCE Framework

- **Repo:** `https://github.com/juce-framework/JUCE`
- **License:** AGPLv3 **or** commercial (paid)
- **VST hosting:** `juce_audio_processors` module; `AudioPluginFormatManager`, `AudioPluginFormat`, `AudioPluginInstance`
- **VST2 support:** JUCE still supports VST2 if the host provides a VST2 SDK (but see licensing issue above)
- **VST3 support:** Full, using the Steinberg VST3 SDK internally
- **Verdict for Kodi:** **Not suitable.** AGPLv3 is incompatible with Kodi's GPLv2-only codebase. Commercial license requires per-seat fees. JUCE brings enormous compile complexity (GUI, audio I/O, etc.) for only a plugin hosting use case.

### 5.3 CLAP (Clever Audio Plugin)

- **Repo:** `https://github.com/free-audio/clap`
- **License:** MIT
- **Status:** Version 1.x; growing ecosystem. Bitwig Studio, Reaper, Ableton 12 support it.
- **Plugin availability:** Much smaller library than VST3 today (2026)
- **API:** Clean C API; easier to host than VST3
- **Verdict for Kodi:** Monitor for the future. Not recommended as a primary target today due to limited plugin library, but worth a parallel implementation later.

### 5.4 LV2

- **License:** LGPL / ISC
- **Platform:** Linux-native; Windows support exists but is uncommon
- **Verdict for Kodi:** Linux-only consideration; skip for this use case.

### 5.5 clap-wrapper

- **Repo:** `https://github.com/free-audio/clap-wrapper`
- Allows VST3 plugins to run in CLAP hosts and vice versa
- Not applicable for this research (we are the host, not the plugin)

### 5.6 VST3 SDK's Own "audiohost" Sample

The best reference for audio-only VST3 hosting. Located in `vst3_public_sdk`:
- `samples/vst-hosting/audiohost/source/audiohost.cpp` — top-level orchestration
- `samples/vst-hosting/audiohost/source/media/audioclient.cpp` — full process loop

---

## 6. Minimum Code Needed to Host a VST3 Plugin

### 6.1 Dependencies

```cmake
# CMakeLists.txt additions (minimal set)
# Clone vst3sdk submodules: base, pluginterfaces, public.sdk

add_library(kodi_vst3host STATIC
    # SDK hosting utilities (all ~10 files)
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/module.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/processdata.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp
    ${VST3SDK_DIR}/public.sdk/source/vst/hosting/connectionproxy.cpp
    # Base SDK
    ${VST3SDK_DIR}/base/source/fstring.cpp
    ${VST3SDK_DIR}/base/source/updatehandler.cpp
    # ... (a handful more base files)
)
```

### 6.2 Complete Minimal Host — Step by Step

```cpp
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/base/funknownimpl.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

// === Step 1: Host context (one global instance) ===
FUnknown* gPluginContext = new HostApplication();

// === Step 2: Load the module ===
std::string errorDesc;
auto module = Module::create("/path/to/MyPlugin.vst3", errorDesc);
if (!module) {
    fprintf(stderr, "Failed to load plugin: %s\n", errorDesc.c_str());
    return false;
}

// === Step 3: Find the audio effect class ===
auto& factory = module->getFactory();
PlugProvider* plugProvider = nullptr;

for (const auto& classInfo : factory.classInfos()) {
    if (classInfo.category() == kVstAudioEffectClass) {
        plugProvider = new PlugProvider(factory, classInfo, true);
        break;
    }
}
if (!plugProvider) {
    fprintf(stderr, "No audio effect found in plugin\n");
    return false;
}

// === Step 4: Get component and processor interfaces ===
IPtr<IComponent> component = plugProvider->getComponent();
FUnknownPtr<IAudioProcessor> processor(component);

// === Step 5: Configure bus arrangements ===
// For stereo in/stereo out:
SpeakerArrangement stereo = SpeakerArr::kStereo;
processor->setBusArrangements(&stereo, 1, &stereo, 1);

// Activate the buses we need
component->activateBus(kAudio, kInput,  0, true);
component->activateBus(kAudio, kOutput, 0, true);

// === Step 6: Setup processing ===
ProcessSetup setup;
setup.processMode       = kRealtime;
setup.symbolicSampleSize = kSample32;
setup.maxSamplesPerBlock = 512;
setup.sampleRate         = 48000.0;

if (processor->setupProcessing(setup) != kResultOk) {
    fprintf(stderr, "setupProcessing failed\n");
    return false;
}

// === Step 7: Activate and start ===
component->setActive(true);
processor->setProcessing(true);

// === Step 8: Allocate process data ===
HostProcessData processData;
processData.prepare(*component, 512, kSample32);

ParameterChanges inputParamChanges;
processData.inputParameterChanges = &inputParamChanges;
processData.processContext = nullptr; // optional

// === Step 9: AUDIO PROCESS LOOP ===
// (called from your audio thread at regular intervals)
void processBlock(float** inputChannels, float** outputChannels,
                  int numSamples) {
    // Wire your buffers into the process data
    processData.numSamples = numSamples;

    // Input bus 0, channels 0 and 1
    processData.inputs[0].channelBuffers32[0] = inputChannels[0];
    processData.inputs[0].channelBuffers32[1] = inputChannels[1];

    // Output bus 0, channels 0 and 1
    processData.outputs[0].channelBuffers32[0] = outputChannels[0];
    processData.outputs[0].channelBuffers32[1] = outputChannels[1];

    // Clear silence flags
    processData.inputs[0].silenceFlags  = 0;
    processData.outputs[0].silenceFlags = 0;

    // Process
    processor->process(processData);

    // Clear parameter changes after each block
    inputParamChanges.clearQueue();
}

// === Step 10: Cleanup ===
void shutdown() {
    processor->setProcessing(false);
    component->setActive(false);
    component->terminate();
    delete plugProvider;
    // module is shared_ptr; will free automatically
}
```

### 6.3 Setting Parameters from the UI/Main Thread

```cpp
// Thread-safe parameter automation from any non-audio thread:
// Use ParameterTransferrer (from public.sdk/source/vst/utility/sampleaccurate.h)
// or use a simple atomic ring buffer approach.

// Simplest approach for non-realtime parameter setting:
// Queue the change; apply it in processBlock() before calling process()

// In main thread:
paramQueue.push({paramID, normalizedValue, 0 /*sample offset*/});

// In audio thread (inside processBlock, before processor->process):
ParamChange change;
while (paramQueue.pop(change)) {
    int32 idx;
    auto* queue = inputParamChanges.addParameterData(change.id, idx);
    if (queue) queue->addPoint(change.offset, change.value, idx);
}
```

### 6.4 Preset Save/Load

```cpp
// Save plugin state to a byte buffer:
class MemoryStream : public IBStream { /* implement Read/Write/Seek/Tell */ };

MemoryStream stateStream;
component->getState(&stateStream); // serializes state

// Load preset:
stateStream.seek(0, IBStream::kIBSeekSet, nullptr);
component->setState(&stateStream);

// VST3 presets are stored in platform-specific locations:
// Windows: %APPDATA%\VST3 Presets\VendorName\PluginName\*.vstpreset
```

---

## 7. Threading and Performance Considerations

### 7.1 Audio Thread Requirements

The VST3 `process()` call and the Kodi audio pipeline must agree on:

- **Block size:** Kodi's audio engine typically uses 512–4096 samples per block. Match `ProcessSetup.maxSamplesPerBlock` to what Kodi provides. If Kodi's blocks vary in size, pass the actual count in `ProcessData.numSamples` as long as it stays ≤ maxSamplesPerBlock.
- **Sample rate:** Must be set accurately. Common rates: 44100, 48000, 96000, 192000 Hz.
- **Buffer format:** VST3 uses **non-interleaved** (`float**`) buffers — one pointer per channel. Kodi's audio engine may use interleaved formats; conversion is necessary.

### 7.2 Interleaved ↔ Non-Interleaved Conversion

Kodi's audio pipeline uses interleaved samples (LRLRLR for stereo). VST3 uses planar/non-interleaved (LLLL... then RRRR...). Conversion needed:

```cpp
// Deinterleave: interleaved → planar (before calling process)
void deinterleave(const float* interleaved, float* left, float* right, int n) {
    for (int i = 0; i < n; i++) {
        left[i]  = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }
}

// Reinterleave: planar → interleaved (after calling process)
void reinterleave(const float* left, const float* right, float* interleaved, int n) {
    for (int i = 0; i < n; i++) {
        interleaved[i * 2]     = left[i];
        interleaved[i * 2 + 1] = right[i];
    }
}
```

Use SIMD intrinsics (SSE2/AVX) for production performance. A simple scalar implementation will work for initial development.

### 7.3 Latency

- VST3 plugins report `processor->getLatencySamples()` — the plugin's own processing latency.
- For real-time playback in Kodi this introduces an output delay that may be audible (common for convolution reverbs, linear-phase EQs, etc.).
- For media playback (post-processing audio before output), latency compensation may be needed: delay video by the same number of samples.

### 7.4 Tail Samples

- `processor->getTailSamples()` returns how many samples of "tail" the plugin produces after input stops (e.g., reverb decay).
- For Kodi's use case (stream playback), tail handling matters at track end/transition.

### 7.5 COM Threading (Windows)

VST3 on Windows uses COM for its `queryInterface`/`addRef`/`release` COM-like mechanism. However:

- **VST3 does NOT require COM apartment initialization.** It uses its own COM-compatible binary protocol without requiring `CoInitializeEx()`.
- The Steinberg `Win32Module` loader does call `OleInitialize()` in older code paths (for resolving `.lnk` shortcuts), but this is optional and wrapped in `#if USE_OLE`.
- Safe practice: call `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the audio thread if loading in a multithreaded environment.

### 7.6 Plugin Crash Isolation

VST3 plugins run in-process by default. A misbehaving plugin can crash Kodi. Mitigation options:
- Load plugins in a separate process and communicate over shared memory or pipes (complex; done by some professional DAWs).
- A simpler approach: load/validate plugins at scan time in a separate process, then load in-process for playback. Scan crashes don't affect playback.

---

## 8. Plugin Discovery on Windows

### 8.1 Standard VST3 Installation Paths

From `module_win32.cpp`, `Module::getModulePaths()` searches:

1. `%LOCALAPPDATA%\Programs\Common\VST3\` — per-user installations (`FOLDERID_UserProgramFilesCommon`)
2. `%COMMONPROGRAMFILES%\VST3\` — system-wide installations (`FOLDERID_ProgramFilesCommon`)
3. `<application_directory>\VST3\` — plugins bundled with the host application

```
Typical paths:
C:\Program Files\Common Files\VST3\
C:\Program Files (x86)\Common Files\VST3\
C:\Users\<User>\AppData\Local\Programs\Common\VST3\
```

### 8.2 VST2 Paths (for reference, if reading registry)

VST2 paths are stored in the Windows Registry:
- `HKEY_LOCAL_MACHINE\SOFTWARE\VST\VSTPluginsPath`
- `HKEY_CURRENT_USER\SOFTWARE\VST\VSTPluginsPath`
- Common default: `C:\Program Files\VSTPlugins\` or `C:\VSTPlugins\`

### 8.3 Plugin Scanning Strategy for Kodi

Since plugin loading is slow and some plugins can crash during scan:

1. **Initial scan:** Use `Module::getModulePaths()` to find all `.vst3` bundles.
2. **Background scan:** Load each plugin in a **separate scanning process** to prevent crashes affecting Kodi.
3. **Cache results:** Store discovered plugins (name, path, class ID, category, bus counts) in a JSON or SQLite cache file.
4. **Incremental scan:** On subsequent starts, only re-scan files whose modification time changed.
5. **Blocklist:** Record plugins that caused scan crashes; skip them on future scans.

---

## 9. Licensing Analysis

### 9.1 VST3 SDK License

**License: MIT** (as of approximately 2024)

The `LICENSE.txt` in `https://github.com/steinbergmedia/vst3sdk` reads:

> MIT License
> Copyright (c) 2025, Steinberg Media Technologies GmbH
> Permission is hereby granted, free of charge, to any person obtaining a copy...

**Impact on Kodi:** MIT is fully compatible with Kodi's GPLv2 license. The VST3 SDK headers and helper source files can be included in Kodi's source tree (with the MIT copyright notice preserved) or linked as a submodule. **No licensing obstacles remain for VST3.**

**Note:** Prior to approximately 2024, the VST3 SDK used a dual GPLv3/commercial license. The MIT relicensing is a significant change. Always verify the current LICENSE.txt before committing to a Kodi release.

### 9.2 VST2 SDK License

The VST2 SDK was distributed under a proprietary Steinberg license. Steinberg stopped distributing it around 2018. Key implications:

- **No new VST2 SDK licenses are available** from Steinberg.
- Existing VST2 SDK headers circulate on the internet but **cannot be legally redistributed** in open source code.
- Several projects (JUCE, others) previously shipped "VST2 compatibility headers" derived from the original SDK, but this practice is now legally questionable.
- **Conclusion: Kodi must not include any VST2 SDK headers.** Do not implement VST2 support.

### 9.3 JUCE License

**License: AGPLv3 or commercial**

- AGPLv3 requires that any user of network-facing software linked with JUCE must have access to the full source. While Kodi is already open source, AGPLv3 is generally considered incompatible with software under GPLv2 (only), which describes significant portions of Kodi.
- The commercial license costs money (tiered pricing; roughly $50–$1000+/year depending on revenue).
- **Conclusion: Do not use JUCE in Kodi.**

### 9.4 CLAP License

**License: MIT** — fully compatible with Kodi's GPLv2. Worth implementing alongside VST3 in a future iteration.

### 9.5 VST Trademark

The term "VST" and the VST logo are registered trademarks of Steinberg. Kodi's UI should avoid using the VST logo without permission, and should identify the feature as "VST3 Plugin Support" (factual description, not trademark use).

---

## 10. Risks and Challenges

### 10.1 Plugin Stability

**Risk: High.** VST3 plugins vary enormously in quality. Common failure modes:
- Crashes during initialization (common with old plugins)
- Memory leaks
- UI thread assumptions (calling `MessageBox` from the audio thread)
- Long-running operations in `process()` (e.g., license checks)

**Mitigation:** Out-of-process scanning; try/catch wrappers around init; watchdog timer on `process()` calls.

### 10.2 Interoperability

**Risk: Medium.** Many VST3 plugins shipped before 2020 have bugs:
- Incorrect `kResultOk` returns from `setupProcessing` even on failure
- Not handling `setProcessing(false)` before parameter changes
- Hardcoded assumptions about block size (must gracefully handle variable-size blocks)

**Mitigation:** Follow the Steinberg "VST3 Usage Guidelines" PDF (in the SDK). The `hostchecker` sample plugin is useful for validating your host implementation.

### 10.3 Sample Rate Conversion

**Risk: Low–Medium.** VST3 plugins generally operate at a fixed sample rate set at `setupProcessing` time. If Kodi's audio pipeline changes sample rate mid-stream, the plugin must be torn down and re-initialized. This introduces a gap/click in audio.

**Mitigation:** Re-initialize the plugin when Kodi detects a sample rate change. Buffer a few hundred milliseconds of audio to hide the gap during reinit (already standard practice in Kodi's audio engine).

### 10.4 Build System Integration

**Risk: Medium.** The VST3 SDK uses CMake and requires multiple submodules:
- `steinbergmedia/vst3_base`
- `steinbergmedia/vst3_pluginterfaces`
- `steinbergmedia/vst3_public_sdk`

Kodi uses CMake for Windows builds (via MSVC). The VST3 SDK compiles cleanly with MSVC. However:
- The SDK uses C++17 features (`std::filesystem`, if-constexpr, etc.)
- Kodi's minimum compiler version must support C++17
- The SDK does not support C++14 fallback in newer versions

**Mitigation:** Wrap the VST3 SDK in an optional CMake feature flag (`KODI_VST3_SUPPORT`). Only compile it when opted in.

### 10.5 Audio Format Mismatch

Kodi supports many audio formats (S16LE, S32LE, Float32, etc.) and channel layouts (stereo, 5.1, 7.1, Atmos passthrough). VST3 plugins mostly process stereo float32.

**Mitigation:**
- Apply VST3 effects only to PCM stereo or multi-channel float streams.
- Skip/bypass VST3 for passthrough (AC3, DTS, TrueHD, DTS-MA, Atmos) — these cannot be processed by audio effect plugins.
- Perform float32 conversion before feeding into VST3.

### 10.6 GUI/Editor

VST3 plugins can provide a UI via `IEditController::createView()`. Kodi's current architecture does not support embedding arbitrary Win32 HWND or Direct2D plugin UIs.

**Mitigation for now:** Implement parameter control via Kodi's own UI (parameter list with sliders) using `IEditController`. A full plugin GUI can be added later as a separate feature (requires significant windowing work). Since the requirement says "audio only," skip GUI hosting for the initial implementation.

### 10.7 Multi-Channel Support

Many VST3 plugins are stereo-only. Supporting 5.1/7.1 requires plugins that expose multi-channel bus configurations. Call `processor->setBusArrangements()` with the target arrangement and check the return value — if `kResultFalse`, the plugin cannot handle it and should be run in stereo with fold-down/fold-up.

---

## 11. Recommended Integration Approach for Kodi

### 11.1 Architecture

```
Kodi Audio Engine (CActiveAE / AESink)
         |
         v
[KodiVST3PluginChain]  -- optional, feature-flagged
         |
         +-- [KodiVST3Plugin] x N  (one per loaded plugin)
         |       |
         |       +-- VST3::Hosting::Module (DLL loader)
         |       +-- Steinberg::Vst::PlugProvider (IComponent/IAudioProcessor)
         |       +-- HostProcessData (buffer management)
         |       +-- ParameterChanges (automation queue)
         |
         v
[AudioFormatConverter]  -- interleaved <-> planar, format conversion
```

### 11.2 New Files to Create

```
xbmc/cores/AudioEngine/Engines/ActiveAE/
    VSTPluginManager.h/.cpp     -- scan, cache, load/unload plugins
    VSTPlugin.h/.cpp            -- wraps a single VST3 plugin instance
    VSTPluginChain.h/.cpp       -- ordered chain of VSTPlugin instances
    VSTHostContext.h/.cpp       -- IHostApplication implementation
```

### 11.3 CMake Integration

```cmake
# In xbmc/cores/AudioEngine/CMakeLists.txt:
option(KODI_VST3_SUPPORT "Enable VST3 plugin hosting" OFF)

if(KODI_VST3_SUPPORT)
    find_package(VST3SDK REQUIRED)
    target_compile_definitions(kodi_ae PRIVATE HAVE_VST3_SUPPORT)
    target_link_libraries(kodi_ae PRIVATE kodi_vst3_hosting)
endif()
```

### 11.4 VST3 SDK Integration — Recommended Approach

Do **not** vendor the entire VST3 SDK. Instead:
1. Add `steinbergmedia/vst3sdk` as a git submodule at `tools/depends/target/vst3sdk/`.
2. Use CMake's `FetchContent` or `ExternalProject_Add` to pull it at configure time.
3. Only compile the files needed for hosting (not samples, not vstgui4, not tutorial code).

### 11.5 Implementation Phases

**Phase 1 (MVP):**
- Plugin scanning and caching (background process)
- Load a single stereo plugin on the default audio stream
- Basic parameter set/get from Kodi settings UI
- Preset load from `.vstpreset` files

**Phase 2:**
- Plugin chain (multiple plugins in series)
- Per-plugin enable/disable bypass
- Parameter automation (from Kodi scripting/keymaps)
- Sample rate change handling

**Phase 3 (optional):**
- Plugin GUI window (separate Win32 window hosted alongside Kodi)
- CLAP plugin support (MIT; simpler API)
- User preset creation

---

## 12. References

### Steinberg Official Documentation

- VST3 SDK GitHub: `https://github.com/steinbergmedia/vst3sdk`
- VST3 Public SDK (hosting utilities): `https://github.com/steinbergmedia/vst3_public_sdk`
- VST3 Interface documentation: `https://steinbergmedia.github.io/vst3_doc/vstinterfaces/`
- VST3 Usage Guidelines (PDF): `vst3sdk/VST3_Usage_Guidelines.pdf`

### Key SDK Files Studied

- `public.sdk/source/vst/hosting/module.h` — Module loading abstraction
- `public.sdk/source/vst/hosting/module_win32.cpp` — Windows DLL loader, standard paths
- `public.sdk/source/vst/hosting/plugprovider.h/.cpp` — IComponent/IEditController instantiation
- `public.sdk/source/vst/hosting/hostclasses.h/.cpp` — IHostApplication implementation
- `public.sdk/source/vst/hosting/processdata.h/.cpp` — HostProcessData buffer management
- `public.sdk/source/vst/hosting/parameterchanges.h/.cpp` — Lock-free parameter queues
- `public.sdk/source/vst/hosting/eventlist.h/.cpp` — MIDI event lists
- `samples/vst-hosting/audiohost/source/audiohost.cpp` — Reference audio-only host
- `samples/vst-hosting/audiohost/source/media/audioclient.cpp` — Full process loop example

### Interface Documentation (directly verified)

- `IAudioProcessor` at `steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html`
- `IComponent` at `steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponent.html`
- `IEditController` at `steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IEditController.html`
- `ProcessData` struct at `steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessData.html`
- `ProcessSetup` struct at `steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessSetup.html`
- `IPluginFactory` / `GetPluginFactory()` at `steinbergmedia.github.io/vst3_doc/base/group__pluginBase.html`

### Licenses Verified

- VST3 SDK: `https://api.github.com/repos/steinbergmedia/vst3sdk/contents/LICENSE.txt` → **MIT**
- JUCE: `https://api.github.com/repos/juce-framework/JUCE/contents/LICENSE.md` → **AGPLv3 or commercial**
- CLAP: `https://api.github.com/repos/free-audio/clap/contents/LICENSE` → **MIT**

### JUCE Plugin Hosting Reference

- `AudioPluginFormatManager` at `https://docs.juce.com/master/classAudioPluginFormatManager.html`
- `AudioPluginInstance` at `https://docs.juce.com/master/classAudioPluginInstance.html`
- `AudioPluginFormat` at `https://docs.juce.com/master/classAudioPluginFormat.html`

---

*Research completed 2026-04-09. VST3 SDK MIT license status confirmed directly from the repository.*
