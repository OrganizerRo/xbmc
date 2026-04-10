# Win32 Build Scripts — Fix Pass Summary

**Date:** 2026-04-10  
**Branch:** Krypton  
**Orchestrator:** tools/buildsteps/win32/analysis/00-orchestrator.md

---

## Overview

| Priority | Bugs Fixed | Files Changed |
|----------|------------|---------------|
| P0       | 4          | buildhelpers.sh, make-mingwlibs.sh, make-mingwlibs.bat, download-msys.bat |
| P1       | 8          | buildffmpeg.sh, buildlibdvd.sh, bootstrap-addons.bat, make-addons.bat, run-tests.bat |
| P2       | 7          | fmpeg_options.txt, make-mingwlibs.sh, buildffmpeg.sh, prepare-env.bat, download-msys.bat |
| P3       | 4          | make-mingwlibs.bat, make-addons.bat, run-tests.bat, prepare-env.bat |
| **Total**| **23**     | **11 modified, 1 new** |

---

## Changes by File

### tools/buildsteps/win32/buildhelpers.sh

- **BUG-01** — Shell comparison operator: replaced `>` with `-gt` in `[ ]` numeric comparisons for `NUMBER_OF_PROCESSORS`; `>` performs stream redirection, not comparison. Added `${NUMBER_OF_PROCESSORS:-0}` safe default.
- **BUG-03** — Removed `--no-check-certificate` from both `wget` invocations in `do_wget`; disabling TLS certificate validation is a security risk.

### tools/buildsteps/win32/fmpeg_options.txt

- **BUG-01** — Removed `--enable-memalign-hack`; option was dropped from FFmpeg and causes `configure` to abort.
- **BUG-02** — Removed `--disable-crystalhd`; removed from FFmpeg 4.x+, also causes `configure` to abort.
- **BUG-03/04** — Removed `--enable-shared` and `--disable-static`; both are set explicitly in `buildffmpeg.sh` and duplicating them in the options file caused confusion.

### tools/buildsteps/win32/make-mingwlibs.sh

- **BUG-01** — Shell comparison operator: replaced `if [ $NUMBER_OF_PROCESSORS > 1 ]` with `if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]`.
- **BUG-02** — MAKEFLAGS arithmetic: replaced backtick `expr` with `$((...))` arithmetic expansion; backtick `expr` is POSIX-obsolete.
- **BUG-03** — Hardcoded FFmpeg DLL names: replaced `checkfiles avcodec-57.dll avformat-57.dll ...` with a version-agnostic glob loop over the seven library prefixes so the check does not break on every FFmpeg update.
- **BUG-04** — `$(PWD)` corrected to `$PWD`; `$(PWD)` attempted to run `PWD` as a subprocess command rather than reading the shell variable.

### tools/buildsteps/win32/buildffmpeg.sh

- **BUG-01** — HTTP upgraded to HTTPS for the gnutls download URL (`http://mirrors.xbmc.org/...` → `https://mirrors.xbmc.org/...`); HTTP downloads are vulnerable to MITM attacks.
- **BUG-03** — VS2015 path fallback: added vswhere-based detection for VS 2017+ when `VS140COMNTOOLS` is unset (see Cross-Cutting Changes).
- **BUG-06** — Redundant bgprocess cleanup block removed; the `trap` handler at the top of the script already removes `$BGPROCESSFILE` on exit.

### tools/buildsteps/win32/buildlibdvd.sh

- **BUG-01** — Added `MAKEFLAGS` default via `nproc` (`MAKEFLAGS="${MAKEFLAGS:--j$(( $(nproc) / 2 + 1 ))}"`); without it, builds ran single-threaded.
- **BUG-02** — Added object file glob guard before the gcc link step; a missing `.o` file previously caused a silently broken output library.
- **BUG-03** — Added `LIBDVDPREFIX` guard with an explicit error message and `exit 1` if the variable is unset; previously the script ran silently into broken paths.

### tools/buildsteps/win32/make-mingwlibs.bat

- **BUG-01** — VS detection migrated from `VS140COMNTOOLS` to `find-vs.bat` / vswhere (see Cross-Cutting Changes).
- **BUG-03** — Added `start /wait` before the `mintty.exe` invocation; without it the batch file returned immediately, making exit-code checking unreliable.

### tools/buildsteps/win32/download-msys.bat

- **BUG-04** — Install path corrected from `msys` to `msys64`; the old path did not match the directory expected by `make-mingwlibs.bat`.
- **BUG-01** — VS detection in the generated `msys.bat` patch migrated from `VS140COMNTOOLS\vcvars32.bat` to a vswhere inline `FOR /F` query (see Cross-Cutting Changes).
- **BUG-03** — fstab dedup guards: wrapped both `/mingw` and `/xbmc` append lines with `FINDSTR` checks so repeated script runs do not accumulate duplicate mount entries.

### tools/buildsteps/win32/bootstrap-addons.bat

- **BUG-01** — VS detection migrated to `find-vs.bat` / vswhere (see Cross-Cutting Changes).
- **BUG-02** — nmake failure propagation: the `IF ERRORLEVEL 1` block previously only echoed the error; added `GOTO ERROR` so build failures abort the script.

### tools/buildsteps/win32/make-addons.bat

- **BUG-01** — VS detection migrated to `find-vs.bat` / vswhere (see Cross-Cutting Changes).
- **BUG-03** — Addon failure file check added before `GOTO END`; a non-empty failure file was previously not detected and the script exited with code 0 even when individual addons failed.
- **BUG-05** — Error log writes changed from `>` (overwrite) to `>>` (append) on both nmake error lines so all per-addon errors accumulate in the log instead of the last one overwriting earlier ones.

### tools/buildsteps/win32/run-tests.bat

- **BUG-01** — MSBuild location migrated from a `REG QUERY` against the MSBuild 14.0 registry key to a `vswhere -find MSBuild` query; added a guard that `GOTO DIE` if MSBuild is not found.
- **BUG-02** — All `VS2010Express` solution and output paths updated to `cmake`-based equivalents (`project/cmake/kodi.sln`, `project/cmake/kodi/%buildconfig%/...`); the `VS2010Express` directory does not exist in this branch. `VCTargetsPath` property also removed as it is not needed with modern VS/vswhere.
- **BUG-04** — exitcode initialization: already present at line 22 (`SET exitcode=0`). No change made.

### tools/buildsteps/win32/prepare-env.bat

- **BUG-04** — Added `WORKSPACE` guard at line 2; script now fails fast with an explicit error message if `WORKSPACE` is not set.
- **BUG-02** — Added `rmdir /S /Q` for `msys64`; the existing cleanup only removed `msys`, leaving stale MSYS2 directories on clean runs.
- **BUG-03** — Removed three dead `rmdir` lines targeting `VS2010Express\XBMC`, `VS2010Express\objs`, and `VS2010Express\libs`; the directory was verified not to exist anywhere in the repository.

---

## New Files Created

| File | Purpose |
|------|---------|
| `tools/buildsteps/win32/find-vs.bat` | Shared vswhere helper called by all `.bat` scripts that need a Visual Studio environment. Queries `vswhere.exe -latest` and exports `VSINSTALLDIR` and `VSMSBUILD`. Replaces the VS 2015-only `VS140COMNTOOLS` pattern across the build system. |

---

## Cross-Cutting Changes

### VS Detection Migration: VS140COMNTOOLS → vswhere (6 files)

The original scripts located Visual Studio exclusively through the `VS140COMNTOOLS` environment variable, which is only populated by VS 2015 installers. VS 2017 and later do not set this variable, silently breaking every script that called `vcvarsall.bat` or `vcvars32.bat` through it.

The fix introduces `find-vs.bat`, a single shared helper that runs `vswhere.exe -latest` to discover the current VS installation and exports the result as `VSINSTALLDIR`. Scripts that previously referenced `VS140COMNTOOLS` now call `find-vs.bat` and then invoke `vcvarsall.bat` through `%VSINSTALLDIR%`.

Files affected:

1. **find-vs.bat** (new) — Central vswhere helper; sets `VSINSTALLDIR` and `VSMSBUILD`.
2. **buildffmpeg.sh** — Added vswhere fallback in bash when `VS140COMNTOOLS` is absent.
3. **make-mingwlibs.bat** — Replaced `VS140COMNTOOLS` call with `find-vs.bat` + `%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat`.
4. **download-msys.bat** — Replaced `VS140COMNTOOLS` reference in the generated `msys.bat` with an inline `FOR /F vswhere` query.
5. **bootstrap-addons.bat** — Replaced `VS140COMNTOOLS` call with `find-vs.bat` + `vcvarsall.bat x86`.
6. **make-addons.bat** — Replaced `VS140COMNTOOLS` call with `find-vs.bat` + `vcvarsall.bat %vcarch%`.
7. **run-tests.bat** — Replaced `REG QUERY MSBuild 14.0` lookup with `vswhere -find MSBuild`.

---

## Notes & Caveats

1. **run-tests.bat BUG-04 (exitcode init):** The orchestrator spec listed this as requiring a fix, but `SET exitcode=0` was already present at line 22. No change was made.

2. **run-tests.bat BUG-02 (VS2010Express paths):** The `project/VS2010Express` directory was verified not to exist anywhere in the repository. Rather than guessing a plausible path, all solution and binary references were updated to the `project/cmake/` paths that the branch's CMake-based build system actually produces.

3. **prepare-env.bat BUG-03 (VS2010Express rmdirs):** Same finding as above — the three `rmdir` lines were dead code and were removed entirely rather than updated to a different path.

4. **download-msys.bat VS detection:** Uses an inline vswhere `FOR /F` loop rather than calling `find-vs.bat`, because this script generates a separate `msys.bat` file and must embed the detection logic as literal text written to that file.

5. **buildffmpeg.sh BUG-06 (bgprocess cleanup):** The explicit cleanup block was removed rather than repaired because the `trap` handler at script entry already handles the same cleanup on any exit path, making the block genuinely redundant.

6. **fmpeg_options.txt BUG-03/04:** The `--enable-shared` and `--disable-static` entries were removed as redundant rather than erroneous; both flags are already passed explicitly by `buildffmpeg.sh` and the options file should not duplicate them.
