# Kodi Leia (18.x) vs Nexus (20.x) — Windows Build Comparison

> **Agent memory file** — This document was produced by the build-workflow planning
> subagent and is the authoritative reference for differences between the Leia and
> Nexus Windows build systems used to design `build-windows.yaml` for Nexus.

---

## 1. High-level Summary

| Dimension | Leia (18.x) | Nexus (20.x) |
|-----------|-------------|--------------|
| Python | 2.7 (`python27.dll`) | 3.8 (`python38.dll`) |
| OpenSSL (pre-built) | 1.0.2g – vc140 | 1.1.1q – v142 |
| FFmpeg source | 4.0.x-Leia | 4.4.1-Nexus-Alpha1 |
| libDVD (MinGW) | libdvdnav + libdvdcss | **Removed** — not built by Nexus |
| CMake generator | Visual Studio 17 2022 | Visual Studio 17 2022 (identical) |
| Architecture | Win32 (x86) | Win32 (x86) *or* x64; Leia used Win32 |
| Min Windows SDK | 10.0.14393.0 | 10.0.14393.0 (same in `ArchSetup.cmake`) |
| MSVC runtime ABI | vc140 (VS 2015) | v141–v143 (VS 2017–2022) |
| Build tools dir | `tools/win32/` (Leia) / `tools/buildsteps/win32/` | `tools/buildsteps/windows/` |
| Addon build system | NMake Makefiles | NMake Makefiles (identical) |
| GHA workflows (upstream) | None | None |
| GHA workflows (OrganizerRo) | 4 Leia-targeted | **0 Nexus** — gap this plan fills |

---

## 2. Dependency Package Format Changes

### Leia package naming
```
libname-version-arch-vc140[-vN].7z
examples:
  python-2.7.13-win32-vc140-v3.7z
  openssl-1.0.2g-win32-vc140-v2.7z
  sqlite-3.10.2-win32-vc140.7z
```

### Nexus package naming
```
libname-version-arch-vNNN-YYYYMMDD.7z   ← date-stamped revision
examples:
  python-3.8.15-win32-v142-20221017.7z
  openssl-1.1.1q-win10-win32-v142-20221017.7z
  sqlite-3300100-win32-v141-20200105.7z
```

Key change: **date-stamp** (`YYYYMMDD`) replaces `-vN` numeric suffix.

### Packages added in Nexus that were absent in Leia
| Package | Purpose |
|---------|---------|
| `harfbuzz-2.8.0-win32-v142-20210602.7z` | HarfBuzz text shaping |
| `libudfread-1.1.0-win32-v142-20200706.7z` | UDF disc reading |
| `libbdplus-0.1.2-win32-v141-20200105.7z` | BD+ decryption |
| `detours-64ec13-win32-v141-20200105.7z` | API detours |
| `GoogleTest-1.10.0-win32-v141-20200410.7z` | Unit testing |
| `dav1d-0.8.2-win32-v142-20210314.7z` | AV1 decoder |

### Packages removed in Nexus (were in Leia)
- `cpluff` (C Plugin Framework)
- `libssh` (SSH2 library)
- `libyajl` (JSON library; replaced by internal)
- `swig` (moved to native tools list)
- `jsonschemabuilder` (moved to native tools list)

---

## 3. MinGW Build Layer Comparison

### Leia `make-mingwlibs.sh` built:
1. FFmpeg (via buildffmpeg.sh → `--toolchain=msvc`)
2. libdvdnav
3. libdvdcss
4. libdvdread

Output: `project/BuildDependencies/mingwlibs/win32/{lib,include,bin}`

### Nexus `make-mingwlibs.sh` builds:
1. **FFmpeg only** (via buildffmpeg.sh → `--toolchain=msvc`)

Output: `project/BuildDependencies/mingwlibs/win32/{lib,include,bin}`

> **Impact on workflow**: No libDVD copying / validation step is needed in Nexus.
> The libdvd family is now sourced from the pre-built dependency packages or is
> statically linked via CMake ExternalProject at the Kodi build step.

### FFmpeg configure differences (Nexus vs Leia)
| Option | Leia | Nexus |
|--------|------|-------|
| OpenSSL | External installer (slproweb.com) | From `openssl-1.1.1q-win32-v142-20221017.7z` pre-built package |
| gnutls | Disabled at MSYS2 (`mingw-w64-i686-gnutls` package) | Not installed |
| dav1d | No | `--enable-libdav1d` (from pre-built `dav1d-*.7z`) |
| libdvd | Built before FFmpeg | N/A |
| Toolchain | `--toolchain=msvc` | `--toolchain=msvc` (identical) |

---

## 4. CMake Build System Comparison

### Root CMakeLists.txt
Both versions use the same structure. No Nexus-specific root changes needed.

### `cmake/scripts/windows/ArchSetup.cmake`
| Setting | Leia | Nexus (this repo) |
|---------|------|-------------------|
| `VS_MINIMUM_SDK_VERSION` | 10.0.14393.0 | **10.0.14393.0** (same) |
| Architecture detection | `CMAKE_SIZEOF_VOID_P` | `CMAKE_SIZEOF_VOID_P` (same) |
| `DEPS_FOLDER_RELATIVE` | `project/BuildDependencies` | `project/BuildDependencies` (same) |
| `MINGW_LIBS_DIR` | `${DEPS_FOLDER_RELATIVE}/mingwlibs/${ARCH}` | Same |

**Conclusion: No ArchSetup.cmake changes required for Nexus.**

### Addon build (`cmake/addons/`)
Both use `NMake Makefiles` generator with identical flag structure.
The `CFlagOverrides.cmake` / `CXXFlagOverrides.cmake` files exist in both.

---

## 5. Workflow Script Path Changes

| Task | Leia path | Nexus path |
|------|-----------|------------|
| Bootstrap addons | `tools/buildsteps/win32/bootstrap-addons.bat` | `tools/buildsteps/windows/bootstrap-addons.bat` |
| Make addons | `tools/buildsteps/win32/make-addons.bat` | `tools/buildsteps/windows/make-addons.bat` |
| Download deps | `tools/buildsteps/win32/download-dependencies.bat` | `tools/buildsteps/windows/download-dependencies.bat` |
| Make mingwlibs | `tools/buildsteps/win32/make-mingwlibs.bat` | `tools/buildsteps/windows/make-mingwlibs.bat` |
| Patch viz addons | `tools/buildsteps/windows/patch-visualization-addons.ps1` | Same (exists in Nexus) |

---

## 6. Known MSVC Compatibility Issues

### Issue 1: `PTRDIFF_MAX` not declared (MSVC 14.44+)
Affects: `imagedecoder.heif` (libde265/xlocnum) — `C2065` error.  
Fix: Set `CL=/D_CRT_DECLARE_NONSTDC_NAMES=1` in the environment before the addon build.  
Status: Applied in Leia workflow; **same fix needed in Nexus workflow**.

### Issue 2: `visualization.milkdrop` / `visualization.milkdrop2`
Affects: Uses `__floor`/`__ceil` as function pointers; MSVC 14.44+ treats them as pure intrinsics.  
Fix: Write `!windows` to `platforms.txt` to exclude these addons on Windows.  
Status: Applied in Leia workflow; **may or may not be present in Nexus addon set** — apply defensively.

---

## 7. GitHub Actions Runner Compatibility

| Runner | Pre-installed components relevant to Nexus build |
|--------|--------------------------------------------------|
| `windows-2022` | VS 2022 (MSVC 14.x), Windows SDK 10.0.20348.0, MSYS2 at `C:\msys64`, Python 3.x, Git, gh CLI |
| Min SDK in code | 10.0.14393.0 → **Runner has 10.0.20348.0 → OK** |
| vswhere.exe | `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` → always present |
| MSYS2 | Pre-installed at `C:\msys64` → junction `/xbmc → $GITHUB_WORKSPACE` needed |

---

## 8. Artifact / Release Strategy Comparison

| Aspect | Leia workflow | Nexus workflow |
|--------|--------------|----------------|
| Artifact name | `kodi-windows-x86` | `kodi-nexus-windows-x86` |
| Release tag prefix | `v*.*.*` | `nexus-v*.*.*` |
| Release zip | `kodi-windows-x86.zip` | `kodi-nexus-windows-x86.zip` |
| Release trigger | push + workflow_dispatch | push + workflow_dispatch (same) |
| Token | `ORGANIZERRO_PAT` ‖ `github.token` | Same |
