# Kodi Nexus — Windows Build Plan

> **Agent memory / orchestrator file** — This document is the master plan
> produced by the build-workflow research agent.  It records every decision
> made, lists the deliverables, and serves as the checklist for the
> implementation agent.

---

## Problem Statement

The `OrganizerRo/xbmc` repository contains the **Kodi 20 Nexus** source tree.
The Leia (18.x) branch already has working GitHub Actions workflows that
build `kodi.exe` for Windows x86 and publish GitHub Releases.  The Nexus
branch has **no CI at all**.

Goal: create `build-windows.yaml` that builds Kodi Nexus for Windows (x86)
on GitHub-hosted runners, uploads the artifact, and publishes a GitHub Release.

---

## Research Summary

### Reference documents
| Document | Purpose |
|----------|---------|
| `docs/build/leia-vs-nexus-comparison.md` | Full diff of Leia/Nexus build systems |
| `docs/build/nexus-cmake-windows.md` | Per-file cmake analysis; confirms no source changes needed |
| **This file** | Master plan + implementation checklist |

### Key finding: no cmake or script changes required
Every relevant cmake file and shell/bat script in the Nexus tree is already
correct.  The **only deliverable that creates work** is the new workflow file.

---

## Architecture Decision: Win32 (x86)

The Leia workflows successfully target `Win32`.  The Nexus pre-built package
list (`0_package.target-win32.list`) exists and is fully populated.  The
`cmake/scripts/windows/ArchSetup.cmake` detects Win32 from `CMAKE_SIZEOF_VOID_P`.

**Decision**: **Win32 (x86)** for the initial Nexus workflow, matching Leia.
An x64 workflow can be added later by cloning the file and changing `-A Win32`
to `-A x64` and passing `--build64=yes` to `make-mingwlibs.sh`.

---

## Workflow Design

### Build order (critical — order matters)

```
1. Checkout (fetch-depth: 0 for tag-based versioning)
2. Restore MinGW cache (key: FFMPEG-VERSION + buildscript hashes)
3. Add MSYS2 to PATH
4. Create junctions:  C:\msys64\xbmc → workspace
                      workspace\project\BuildDependencies\msys64 → C:\msys64
5. Write MSYS2 fstab mounts (/build, /local32, /depends/win32, …)
6. Download pre-built Win32 deps  ◀── MUST be before FFmpeg build
   (openssl-*.7z and dav1d-*.7z provide libs for buildffmpeg.sh)
7. [cache miss] pacman -Sy + install mingw-w64-i686-{gcc,binutils,pkg-config,yasm}
8. [cache miss] Write /local32/etc/profile.local
9. [cache miss] vcvars32.bat + bash --login make-mingwlibs.sh --build32=yes
10.[cache miss] Validate FFmpeg .lib files
11. Prepare system/ directory
12. cmake -G "Visual Studio 17 2022" -A Win32
13. cmake --build . --target ALL_BUILD --config RelWithDebInfo --parallel
14. Verify kodi.exe exists
15. vcvars32.bat + bootstrap-addons.bat
16. Disable visualization.milkdrop* (write platforms.txt = !windows)
17. cmake addons -G "NMake Makefiles" (configure + download sources)
18. Set CL=/D_CRT_DECLARE_NONSTDC_NAMES=1  (MSVC 14.44+ compat)
19. vcvars32.bat + build each addon individually (with per-addon logs)
20. Upload addon logs (always, for debugging)
21. Stage: kodi-build/RelWithDebInfo/ + addon output → staging/
22. Validate: kodi.exe, DLLs, system/, addons/
23. Upload artifact: kodi-nexus-windows-x86
24. [push|workflow_dispatch] Compute next nexus-v*.*.* tag
25. [push|workflow_dispatch] Generate changelog
26. [push|workflow_dispatch] Compress-Archive staging/ → kodi-nexus-windows-x86.zip
27. [push|workflow_dispatch] gh release create
```

### Why deps must be downloaded before FFmpeg
`buildffmpeg.sh` resolves OpenSSL and dav1d from `/depends/win32/lib`:
```bash
extra_ldflags="-LIBPATH:\"/depends/$TRIPLET/lib\""
extra_cflags="-I/depends/$TRIPLET/include"
```
`/depends/win32` is fstab-mounted to `project/BuildDependencies/win32/`
which is populated by `download-dependencies.bat`.

### OpenSSL: NO separate installer step needed
In Leia the workflow installs OpenSSL via `Win32OpenSSL.exe` from slproweb.com.
In Nexus the pre-built package `openssl-1.1.1q-win10-win32-v142-20221017.7z`
(in `0_package.target-win32.list`) provides the same MSVC libs at
`project/BuildDependencies/win32/lib/libssl.lib` → `/depends/win32/lib/libssl.lib`.

### Addon build: continue-on-error
Binary addons are built with `continue-on-error: true` — some addons may fail
due to unavailable dependencies or MSVC incompatibilities.  The workflow
records successes/failures in `.success` / `.failure` files and uploads logs
as an artifact for debugging.  The main kodi.exe artifact is uploaded
regardless of addon failures.

---

## Deliverables

| # | File | Status |
|---|------|--------|
| 1 | `docs/build/leia-vs-nexus-comparison.md` | ✅ Created |
| 2 | `docs/build/nexus-cmake-windows.md` | ✅ Created |
| 3 | `docs/build/nexus-windows-build-plan.md` | ✅ This file |
| 4 | `.github/workflows/build-windows.yaml` | ✅ Created |

---

## Implementation Checklist

- [x] Research Leia build system (subagent: leia-research)
- [x] Research Nexus build system (subagent: nexus-research)
- [x] Produce leia-vs-nexus-comparison.md
- [x] Produce nexus-cmake-windows.md (confirms no cmake changes needed)
- [x] Produce this master plan
- [x] Create `.github/workflows/build-windows.yaml`

---

## Decision Log

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture | Win32 (x86) | Matches Leia; all pre-built packages are win32 |
| CMake generator | Visual Studio 17 2022 | On windows-2022 runner, VS 2022 is pre-installed |
| FFmpeg build | make-mingwlibs.sh `--build32=yes` | Produces MSVC .lib via `--toolchain=msvc` |
| OpenSSL for FFmpeg | From mirrors.kodi.tv pre-built | No separate installer; available at /depends/win32 |
| libDVD | Skip | Nexus make-mingwlibs.sh does not build libDVD |
| Cache key | FFMPEG-VERSION + 5 build-script hashes | Invalidate on any FFmpeg version or script change |
| Build config | RelWithDebInfo | PDB symbols + optimised binary |
| Release tag prefix | `nexus-v*.*.*` | Disambiguates from Leia `v*.*.*` tags |
| Addon failures | continue-on-error | Partial addon failures must not block kodi.exe upload |

---

## Required GitHub Secrets

| Secret | Purpose | Default fallback |
|--------|---------|-----------------|
| `ORGANIZERRO_PAT` | `gh release create` as OrganizerRo + push tags | `github.token` (limited permissions) |

The `ORGANIZERRO_PAT` is the same PAT already used by the Leia workflows.
If it is not set, the workflow falls back to `github.token`; this may
prevent tag deletion/re-creation but the build and artifact upload succeed.

---

## Potential Issues & Mitigations

| Issue | Mitigation |
|-------|-----------|
| mirrors.kodi.tv flaky | `wget --tries=5 --retry-connrefused` already in `get_formed.cmd` |
| FFmpeg build >60 min | `timeout-minutes: 60` on the build step; cache prevents re-runs |
| MSVC 14.44 PTRDIFF_MAX | `CL=/D_CRT_DECLARE_NONSTDC_NAMES=1` env var before addon build |
| visualization.milkdrop C7552 | Write `!windows` platforms.txt after bootstrap |
| vswhere.exe detection | Use system `%ProgramFiles(x86)%\…\vswhere.exe` (always present) |
| Windows SDK too old | Min SDK 10.0.14393.0; runner has 10.0.20348.0 → OK |
| dav1d configure failure | Ensured by pre-FFmpeg dep download; available at /depends/win32 |
| Shallow clone (no tags) | `fetch-depth: 0` on checkout |

---

## Future Work (out of scope for this PR)

1. **x64 variant** — duplicate workflow with `-A x64` + `--build64=yes`
2. **Caching pre-built deps** — cache `project/BuildDependencies/win32/`
   keyed on `0_package.target-win32.list` hash to speed up re-runs
3. **NSIS installer** — add NSIS step to produce a `.exe` setup package
4. **Test step** — `run-tests.bat` can be added after the main build
