# Windows Build Migration: VS 2019 → VS 2022

## Research Date
2026-04-09

## Summary
Migration from `windows-2019` GitHub Actions runner + Visual Studio 16 (2019) to
`windows-2022` runner + Visual Studio 17 (2022).

---

## Files Requiring Changes

### CRITICAL — Must change for build to work

| File | Line | Current | Change To |
|------|------|---------|-----------|
| `.github/workflows/build-windows.yaml` | 15 | `runs-on: windows-2019` | `runs-on: windows-2022` |
| `.github/workflows/build-windows.yaml` | 154 | `cmake -G "Visual Studio 16 2019" -A Win32` | `cmake -G "Visual Studio 17 2022" -A Win32` |

### INFORMATIONAL — Other VS references found (not blocking)

| File | Notes |
|------|-------|
| `appveyor.yml` | References VS 2015 / v140 — not related to this migration |
| `project/Win32BuildSetup/BuildSetup.bat` | Legacy local build script, uses VS 14 (2015) |
| `project/cmake/README.md` | Docs reference VS 14 (2015); could be updated for accuracy |
| `tools/buildsteps/win32/run-tests.bat` | Registry lookup for VS 2015 toolset |
| Various `.vcxproj` files | Use v140 toolset; CMake manages these and they remain compatible |

---

## GitHub Actions Runner Comparison

| Aspect | windows-2019 | windows-2022 |
|--------|-------------|-------------|
| Visual Studio | VS 2019 (v16, MSVC v142) | VS 2022 (v17, MSVC v143) |
| CMake generator | `"Visual Studio 16 2019"` | `"Visual Studio 17 2022"` |
| CMake version | Older 3.x | 3.31.6 |
| MSYS2 path | `C:\msys64` | `C:\msys64` (unchanged) |
| MinGW/MSYS2 usage | pacman 5.x | pacman 6.1.0 |
| MinGW gcc | older | 14.2.0 |
| Availability | **REMOVED from GA** | Current supported |

---

## Architecture Flag Notes
- VS 2022 on a 64-bit runner defaults to **x64** if `-A` is omitted
- Current workflow uses `-A Win32` for 32-bit build — this flag must be **kept**
- If migrating to x64 in future: change to `-A x64` and update artifact name

---

## MSYS2 / MinGW Compatibility
All existing workflow steps are fully compatible with windows-2022:
- `C:\msys64\usr\bin` and `C:\msys64\bin` PATH additions: **unchanged**
- `pacman` invocations: **unchanged**
- `mklink /j C:\msys64\xbmc %GITHUB_WORKSPACE%`: **unchanged**
- `MSYSTEM=MINGW32` + `bash --login`: **unchanged**
- MinGW32 packages (`mingw-w64-i686-gcc`, etc.): **unchanged**

---

## VS 2019 Toolset Compatibility (if needed)
If VS 2022 breaks compilation due to stricter conformance, the v142 toolset is
available on windows-2022 via `-T v142` in the CMake command. This is a fallback
option; try without it first.

---

## Sources
- GitHub Actions runner-images README (official)
- Windows Server 2022 runner image manifest v20260329.98.1
- CMake docs: Visual Studio 17 2022 generator
