# Kodi Audio Pipeline Architecture Research

## 1. Pipeline Overview (ASCII)

```
Media Source (File/Stream)
         |
         v
    IAEStream Interface
         |
         +---> Input Buffer Pool (CSampleBuffer)
         |
         v
    Stream Processing Stage
    +----------+-----------+----------+
    |          |           |          |
    v          v           v          v
  DSP       Resampling  Volume    Limiter
  Addons    Processing  Mixing   (CAELimiter)
    |          |           |          |
    +----------+-----------+----------+
         |
         v
    Output Buffer Pool
         |
         v
    Sink Selection & Format Conversion
    (WASAPI on Windows)
         |
    +----+----+----+
    |    |    |    |
    v    v    v    v
   PCM  RAW  DTS  EAC3
    |
    +---> Audio Device (Speakers/HDMI/etc.)
```

---

## 2. Key Components and Files

### Core Audio Engine (`xbmc/cores/AudioEngine/`)

| File | Role |
|------|------|
| `AEFactory.h/cpp` | Factory for engine creation/lifecycle; `CAEFactory::GetEngine()` entry point |
| `Interfaces/AE.h` | Abstract engine interface (IAE): MakeStream, FreeStream, SetVolume, HasDSP |
| `Engines/ActiveAE/ActiveAE.h/cpp` | Main audio engine thread (CActiveAE); processing loop, mixing, format management |
| `Engines/ActiveAE/ActiveAEStream.h/cpp` | Per-stream abstraction and buffering (CActiveAEStream) |
| `Engines/ActiveAE/ActiveAESink.h/cpp` | Sink thread management; enumerates devices, sends data to device |
| `Engines/ActiveAE/ActiveAEBuffer.h/cpp` | CSampleBuffer, buffer pools, CActiveAEBufferPoolResample |
| `Sinks/AESinkWASAPI.h/cpp` | Windows WASAPI audio device implementation |
| `AudioDSPAddons/ActiveAEDSP.h/cpp` | DSP system manager (CActiveAEDSP) |
| `AudioDSPAddons/ActiveAEDSPProcess.h/cpp` | Per-stream DSP chain (CActiveAEDSPProcess) |
| `AudioDSPAddons/ActiveAEDSPAddon.h/cpp` | Individual DSP addon DLL wrapper |
| `Utils/AEAudioFormat.h` | AEAudioFormat struct definition |
| `Utils/AEChannelData.h` | Channel layout and format enums |
| `Interfaces/IAudioCallback.h` | Visualization callback interface |

---

## 3. Audio Format Management

### `AEAudioFormat` struct
```cpp
struct AEAudioFormat {
  AEDataFormat    m_dataFormat;      // Sample format
  unsigned int    m_sampleRate;      // 44100, 48000, 96000, etc.
  CAEChannelInfo  m_channelLayout;   // Channel layout (stereo, 5.1, 7.1, etc.)
  unsigned int    m_frames;          // Samples per buffer
  unsigned int    m_frameSize;       // Bytes per frame
  CAEStreamInfo   m_streamInfo;      // Raw/passthrough info
};
```

### Supported Sample Formats
```
AE_FMT_U8           8-bit unsigned
AE_FMT_S16LE/BE/NE  16-bit signed
AE_FMT_S32LE/BE/NE  32-bit signed
AE_FMT_S24BE4/LE4   24-bit in 32-bit container
AE_FMT_FLOAT        32-bit float  ← preferred for DSP processing
AE_FMT_FLOATP       32-bit float planar
```

### Supported Channel Layouts
Mono, Stereo, 2.1, 3.0, 3.1, 4.0, 4.1, 5.0, 5.1, 7.0, 7.1
Individual channels: FL, FR, FC, LFE, SL, SR, BL, BR, TFL, TFR, etc.

---

## 4. DSP Processing Pipeline (Primary Extension Point)

### Stages (in order)
1. **Input Resampling** — sample rate conversion before DSP (max 1 addon)
2. **Input Processing** — read-only stream observation (unlimited addons)
3. **Pre-Processing** — normalization, pre-filtering (unlimited addons)
4. **Master Processing** — primary DSP effect: EQ, surround, etc. **(max 1 addon) ← VST goes here**
5. **Post-Processing** — loudness, final filter (unlimited addons)
6. **Output Resampling** — sample rate conversion after DSP (max 1 addon)

### Key DSP Classes
- **`CActiveAEDSPProcess`** — manages the full DSP chain for one stream; calls `InputProcess()`, `MasterProcess()`, etc.
- **`CActiveAEDSP`** — central coordinator; creates/destroys `CActiveAEDSPProcess` instances per stream
- **`CActiveAEDSPAddon`** — wraps a single addon DLL; maps capabilities to modes

---

## 5. Windows WASAPI Sink (`CAESinkWASAPI`)

- Uses Windows Audio Session API (WASAPI)
- Format probe order: `Float32 → S24NE4MSB → S32NE → S24NE3 → S16NE`
- Supports exclusive mode (bit-perfect) and shared mode
- Handles IEC61937 passthrough for S/PDIF/HDMI
- Sample rates: 11025 – 384000 Hz
- Device types: Speakers, Headphones, HDMI, S/PDIF, USB

---

## 6. Existing Extension / Hook Points

| Hook Point | Interface | Writable? | Notes |
|------------|-----------|-----------|-------|
| DSP addon pipeline | `kodi_adsp_dll.h` | Yes | Full float processing; multi-stage |
| Audio visualization callback | `IAudioCallback` | No | Read-only float samples |
| A/V sync clock callback | `IAEClockCallback` | No | Timing only |
| Per-stream volume/resample | `CActiveAEStream` | Partial | Volume and resample ratio only |

---

## 7. Where VST Processing Should Be Inserted

### Option 1: DSP Addon (Recommended)
- **Point**: Master Processing stage in `CActiveAEDSPProcess`
- **Format**: 32-bit float, planar multi-channel
- **Pros**: Full UI integration, mode selection, format info available, no core changes
- **Cons**: One master process active at a time; must implement full ADSP addon interface

### Option 2: Post-Sink Intercept (Advanced)
- **Point**: `CActiveAESink::OutputSamples()` before WASAPI write
- **Format**: Final device format (may be S16/S24/S32)
- **Pros**: Access to bit-exact output; can run in WASAPI exclusive mode
- **Cons**: No DSP UI integration; must handle all format variants

### Option 3: Stream Callback (Limited)
- **Point**: `IAudioCallback::OnAudioData()` on each stream
- **Format**: Float only
- **Pros**: Minimal changes; already supported interface
- **Cons**: Read-only — cannot modify samples

---

## 8. Constraints and Limitations

| Constraint | Details |
|------------|---------|
| Float requirement | DSP processing is hardcoded to float; no raw S24/S32 PCM |
| Single master DSP | Only one active master processing mode at a time (but a VST chain addon counts as one) |
| Buffer-oriented | No per-sample access; audio processed in frames (typically 1024 samples) |
| No sidechain | DSP addons cannot observe multiple streams simultaneously |
| Real-time thread | All DSP processing runs on audio thread; must avoid allocations and blocking |
| Latency reporting | Addon must accurately report processing delay for A/V lip-sync |
| WASAPI exclusive | In exclusive mode, sample rate and bit depth fixed to device capability |

---

## 9. Summary

The **DSP Addon system** (Master Processing stage) is the correct and designed integration point for a VST host. The pipeline delivers audio as **32-bit float planar arrays** — exactly what VST plugins expect. The threading model is single-threaded per stream, which matches VST's own threading requirements. Format metadata (sample rate, channel count, layout) is fully available at `StreamCreate()` time.
