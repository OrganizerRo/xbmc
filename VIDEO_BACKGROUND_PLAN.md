# XBMC Video Background Implementation Plan

## Executive Summary

This document outlines how to replace the GIF-based background system in XBMC (Krypton branch) with one that supports MP4, MKV, and any video format. It also answers whether a drop-in DLL replacement is feasible for an already-built Windows executable.

---

## Drop-In DLL Feasibility: Short Answer

**No — a simple drop-in DLL is not possible.** The guilib code (TextureManager, GUITexture, FFmpegImage) is compiled into a **static library** (`libkodi`) that is statically linked directly into `kodi.exe`. There is no `guilib.dll` to replace.

The only way to make this change is to **recompile `kodi.exe`** with modified source files. The good news is that the change is narrow: only 2–3 source files need modification, and FFmpeg (already a runtime DLL dependency) handles all the heavy video decoding. You do **not** need to rebuild the entire project from scratch — you can rebuild just the changed files and relink the executable.

See [Minimal Rebuild Strategy](#minimal-rebuild-strategy) below for how to do this efficiently.

---

## Current GIF Background Architecture

### Rendering Pipeline

```
Skin XML (Home.xml)
  └─ <control type="multiimage"> with background="true"
       └─ CGUIMultiImage / CGUIImage
            └─ CGUITextureBase::UpdateAnimFrame()   [GUITexture.cpp:485]
                 └─ CTextureArray (frame array + delays)
                      └─ Loaded by CTextureManager::Load()  [TextureManager.cpp:379]
                           └─ CFFmpegImage::ReadFrame()      [FFmpegImage.cpp:705]
                                └─ FFmpeg (avcodec/avformat DLLs)
```

### Key Limitations (GIF)

| Constraint | Root Cause | Location |
|---|---|---|
| ~9 seconds of animation | Memory hard-cap: `91,238,400` bytes (~12 full-HD frames) | `TextureManager.cpp:403` |
| Frames pre-loaded into RAM | All frames decoded at load time, stored as textures | `TextureManager.cpp:379–435` |
| GIF/APNG only | Extension check: `.gif` hard-coded | `TextureManager.cpp:345, 379` |
| "12 fps" perception | GIF format stores delays in 10ms units; encoders often round to ~83ms (12fps) — not an XBMC limit | `FFmpegImage.cpp:711` |

---

## Proposed Implementation

The strategy is to add a **streaming video texture** path alongside the existing GIF path. For video files, instead of pre-loading all frames into memory, we decode frames on-the-fly using FFmpeg and upload each decoded frame to the same GPU texture used by the existing animation system.

### New Class: `CVideoBackgroundDecoder`

**File:** `xbmc/guilib/VideoBackgroundDecoder.h` / `VideoBackgroundDecoder.cpp`

This class wraps FFmpeg and provides a streaming frame interface:

```cpp
class CVideoBackgroundDecoder
{
public:
  bool Open(const std::string& filename);
  void Close();

  // Returns next decoded RGBA frame, or nullptr if not yet ready.
  // Caller does NOT own the buffer — it is valid until next call.
  const uint8_t* GetCurrentFrame(int& width, int& height);

  // Call each render tick; advances to next frame when delay has elapsed.
  // Returns true if frame changed (texture needs re-upload).
  bool Update(unsigned int currentTimeMs);

  bool IsOpen() const;

private:
  AVFormatContext* m_fmtCtx    = nullptr;
  AVCodecContext*  m_codecCtx  = nullptr;
  SwsContext*      m_swsCtx    = nullptr;
  AVFrame*         m_avFrame   = nullptr;
  AVFrame*         m_rgbFrame  = nullptr;
  uint8_t*         m_buffer    = nullptr;
  int              m_videoStream = -1;
  int              m_width      = 0;
  int              m_height     = 0;
  double           m_timeBase   = 0.0;       // seconds per tick
  unsigned int     m_nextFrameTimeMs = 0;    // when to show next frame
  bool             m_looping    = true;

  bool DecodeNextFrame();     // reads packets until a video frame is decoded
  void SeekToStart();         // seeks back to beginning for looping
};
```

**Key FFmpeg calls used** (all already available as runtime DLLs):
- `avformat_open_input` / `avformat_find_stream_info` — open any container (MP4, MKV, AVI, etc.)
- `avcodec_find_decoder` / `avcodec_open2` — find and open the video codec
- `av_read_frame` / `avcodec_decode_video2` — decode packets to frames
- `sws_scale` — convert decoded YUV/etc. to RGBA for GPU upload
- `av_seek_frame` — seek back to start for looping

Memory footprint: **one frame buffer** (~8 MB for 1080p RGBA) instead of 12× (96 MB for GIF).

---

### Modified File 1: `TextureManager.cpp`

**Change:** Add a video extension check **before** the GIF path. For video files, skip frame pre-loading entirely and return a sentinel that tells `CGUITextureBase` to use the streaming path.

```cpp
// In CTextureManager::Load(), after the .gif check (around line 379):

static const std::vector<std::string> VIDEO_EXTS = {
  ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".ts", ".webm"
};

bool isVideo = false;
for (const auto& ext : VIDEO_EXTS)
  if (StringUtils::EndsWithNoCase(strPath, ext)) { isVideo = true; break; }

if (isVideo)
{
  // Create a placeholder single-frame texture map.
  // The real decoding happens in CGUITextureBase via CVideoBackgroundDecoder.
  CTextureMap* pMap = new CTextureMap(strTextureName, 0, 0, 0);
  pMap->SetVideoPath(strPath);   // <-- new field, see below
  m_textures.insert(pMap);
  return pMap;
}
```

**`CTextureMap` additions needed** (`TextureManager.h`):
```cpp
std::string m_videoPath;          // non-empty if this is a video background
void SetVideoPath(const std::string& p) { m_videoPath = p; }
const std::string& GetVideoPath() const { return m_videoPath; }
bool IsVideo() const { return !m_videoPath.empty(); }
```

---

### Modified File 2: `GUITexture.cpp`

**Change:** In `CGUITextureBase`, detect when the loaded texture is a video background and route through `CVideoBackgroundDecoder` instead of the frame-array animation.

**Add members to `CGUITextureBase`** (`GUITexture.h`):
```cpp
#include "VideoBackgroundDecoder.h"

CVideoBackgroundDecoder* m_videoDecoder = nullptr;  // non-null for video backgrounds
CTexture*                m_videoTexture = nullptr;  // single reusable GPU texture
```

**Modify `SetInfo()` or texture allocation** (where `m_texture` is first assigned):
```cpp
if (m_texture.IsVideo())
{
  m_videoDecoder = new CVideoBackgroundDecoder();
  m_videoDecoder->Open(m_texture.GetVideoPath());
  m_videoTexture = new CTexture();
  // Allocate GPU texture at video native resolution
  int w, h;
  m_videoDecoder->GetCurrentFrame(w, h);
  m_videoTexture->Allocate(w, h, XB_FMT_A8R8G8B8);
}
```

**Modify `UpdateAnimFrame()`**:
```cpp
bool CGUITextureBase::UpdateAnimFrame(unsigned int currentTime)
{
  // NEW: streaming video path
  if (m_videoDecoder)
  {
    if (m_videoDecoder->Update(currentTime))
    {
      int w, h;
      const uint8_t* pixels = m_videoDecoder->GetCurrentFrame(w, h);
      if (pixels)
        m_videoTexture->LoadFromMemory(w, h, w * 4, XB_FMT_A8R8G8B8, true, pixels);
      return true;  // texture changed — mark dirty
    }
    return false;
  }

  // EXISTING GIF path unchanged below...
  bool changed = false;
  unsigned int delay = m_texture.m_delays[m_currentFrame];
  // ... (existing code) ...
}
```

**Modify `Render()`**: When `m_videoDecoder` is active, bind `m_videoTexture` instead of `m_texture.m_textures[m_currentFrame]`.

---

### Skin XML: No Changes Required

The existing skin entry in `Home.xml` already works:

```xml
<control type="multiimage">
  <imagepath background="true" colordiffuse="bg_overlay">$VAR[HomeFanartVar]</imagepath>
</control>
```

`HomeFanartVar` is set by the skin to a file path. If that path ends in `.mp4` / `.mkv` / etc., the new code path activates automatically. No skin changes are needed.

To use a video background, users simply point their skin's background path to a video file — exactly as they would today with a `.gif`.

---

## Files to Create / Modify

| Action | File | Scope of Change |
|---|---|---|
| **Create** | `xbmc/guilib/VideoBackgroundDecoder.h` | New file (~60 lines) |
| **Create** | `xbmc/guilib/VideoBackgroundDecoder.cpp` | New file (~200 lines) |
| **Modify** | `xbmc/guilib/TextureManager.cpp` | ~20 lines added |
| **Modify** | `xbmc/guilib/TextureManager.h` | ~5 lines added to CTextureMap |
| **Modify** | `xbmc/guilib/GUITexture.cpp` | ~40 lines added/changed |
| **Modify** | `xbmc/guilib/GUITexture.h` | ~5 lines added |
| **Modify** | `xbmc/guilib/CMakeLists.txt` | 2 lines (add new .cpp/.h) |

**Total: ~330 lines of new/changed code.**

---

## Minimal Rebuild Strategy

Since guilib is statically linked into `kodi.exe`, you must recompile the executable. However, you do **not** need a full clean build.

### Prerequisites

- Visual Studio 2015 (or the version used for your existing build)
- CMake (to regenerate project files if needed)
- The same FFmpeg headers/libs used in the original build (already in your tree under `tools/depends/` or `lib/ffmpeg/`)
- Your existing compiled `.obj` files / build cache

### Steps

1. **Add new files** to `xbmc/guilib/CMakeLists.txt`:
   ```cmake
   list(APPEND SOURCES VideoBackgroundDecoder.cpp)
   list(APPEND HEADERS VideoBackgroundDecoder.h)
   ```

2. **Regenerate VS project** (only needed if you want IDE support):
   ```bash
   cd build
   cmake --build . --target kodi -- /p:BuildProjectReferences=false
   ```

3. **Incremental build** — only the changed `.cpp` files recompile:
   - `VideoBackgroundDecoder.cpp` (new)
   - `TextureManager.cpp` (modified)
   - `GUITexture.cpp` (modified)
   - Then `kodi.exe` relinks

   Typical incremental build time on a modern machine: **1–3 minutes** vs 30–60 minutes for a full rebuild.

4. **Output**: Replace `kodi.exe` in your installed XBMC directory. No other files change.

### FFmpeg DLLs

`CVideoBackgroundDecoder` uses only FFmpeg APIs already called elsewhere in XBMC. The same FFmpeg DLLs shipped with your build (`avformat-57.dll`, `avcodec-57.dll`, `avutil-55.dll`, `swscale-4.dll`) already support MP4 and MKV. No new DLLs are needed.

---

## Why a Detour/Hook DLL Won't Work Well

A DLL-injection approach (using Microsoft Detours or MinHook to patch `kodi.exe` in memory) is theoretically possible but is not recommended:

- `TextureManager::Load()` and `CGUITextureBase::UpdateAnimFrame()` are non-virtual internal methods — finding their addresses requires either debug symbols or manual pattern scanning, which breaks on every build.
- The new `CVideoBackgroundDecoder` object needs to be allocated and tracked per-texture-instance; there is no clean way to do this from outside the class without modifying the object layout.
- A hook DLL would need to be injected via a launcher or `AppInit_DLLs` registry key, which is fragile and antivirus-unfriendly.

**Conclusion:** The recompile path is far simpler and more maintainable.

---

## Implementation Notes

### Video Looping
Set `m_looping = true` in `CVideoBackgroundDecoder`. On `av_read_frame` returning `AVERROR_EOF`, call `av_seek_frame(m_fmtCtx, m_videoStream, 0, AVSEEK_FLAG_BACKWARD)` and flush the codec context.

### Threading
FFmpeg frame decoding is fast enough to run on the render thread for typical background video (720p/1080p at 24–30fps). If you experience frame drops, offload decoding to a background thread with a 2-frame ring buffer.

### Audio
Do **not** open an audio stream in `CVideoBackgroundDecoder`. Read only video stream packets (check `pkt.stream_index == m_videoStream`). Background loops should be silent.

### Format Support
Any container/codec that FFmpeg supports in your build works automatically: MP4 (H.264, H.265), MKV (VP9, AV1, H.264), AVI, MOV, WebM, MPEG-TS, and more.

### Transparent Overlays
The existing skin `colordiffuse="bg_overlay"` and `bg_alpha` constants still apply — video frames render through the same RGBA texture path, so skin-level dimming and color correction work without changes.

### Memory Usage
- **GIF path (current):** Up to 91 MB RAM (all frames pre-loaded)
- **Video path (new):** ~8 MB RAM (single 1080p RGBA frame) + ~2 MB for FFmpeg decode buffers

---

## Testing Checklist

- [ ] MP4 file (H.264) displays as background and loops
- [ ] MKV file (H.264/H.265) displays and loops
- [ ] GIF files still work exactly as before (existing code path untouched)
- [ ] Background fades correctly when navigating away (existing `CGUIMultiImage` fade logic)
- [ ] No audio plays from background video
- [ ] Memory usage stays flat over time (no leak in frame buffer)
- [ ] Seeking back to start on loop doesn't cause visual glitch
- [ ] Works when XBMC is minimized / focus lost (decoder should pause or skip frames)
