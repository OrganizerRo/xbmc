# Win32 Build Scripts — Error Fix Orchestrator

This document is the master guide for a subagent-based fix pass over all Win32 build scripts.
Each section defines one fix task: which file to edit, which bugs to fix (referencing the analysis MD), and the exact changes required.

**Source analysis files:** `tools/buildsteps/win32/analysis/01-*.md` through `12-*.md`
**Working directory assumption:** `/xbmc` (MSYS2 path) or `C:/src/xbmc` (Windows path)

---

## Priority Legend

| Priority | Meaning |
|----------|---------|
| P0 | Build-breaking; must fix before anything will compile |
| P1 | High-impact; likely to break specific configurations |
| P2 | Medium-impact; incorrect behavior in some conditions |
| P3 | Low-impact; cleanup / robustness |

---

## Fix Task 1 — buildhelpers.sh: Shell comparison operator bug

**File:** `tools/buildsteps/win32/buildhelpers.sh`
**Analysis:** `01-buildhelpers-analysis.md` BUG-01
**Priority:** P0
**Agent instructions:**
- Read `buildhelpers.sh`
- On lines 8 and 9, replace both instances of `[ $NUMBER_OF_PROCESSORS > 1 ]` and `[ $NUMBER_OF_PROCESSORS > 4 ]` with proper arithmetic comparisons using `-gt`, quoting the variable:
  - Line 8: `if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then`
  - Line 9: `if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 4 ]; then`
- Also address BUG-03: remove `--no-check-certificate` from the `do_wget` function (lines 37–40) or replace with `--ca-certificate=/etc/ssl/certs/ca-bundle.crt`

---

## Fix Task 2 — make-mingwlibs.sh: Shell comparison + MAKEFLAGS + hardcoded DLL names

**File:** `tools/buildsteps/win32/make-mingwlibs.sh`
**Analysis:** `02-make-mingwlibs-sh-analysis.md` BUG-01, BUG-02, BUG-03, BUG-04
**Priority:** P0 (BUG-01), P1 (BUG-03), P2 (BUG-02, BUG-04)
**Agent instructions:**
- Read `make-mingwlibs.sh`
- **BUG-01** (line 149): Change `if [ $NUMBER_OF_PROCESSORS > 1 ]; then` to `if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then`
- **BUG-02** (line 150): Replace the backtick `expr` with `$((…))`:
  ```bash
  MAKEFLAGS="-j$(( ${NUMBER_OF_PROCESSORS:-2} + ${NUMBER_OF_PROCESSORS:-2} / 2 ))"
  ```
- **BUG-03** (line 89): Remove the hardcoded FFmpeg DLL version numbers. Replace the `checkfiles` call with a wildcard check, or derive version numbers from the `FFMPEG-VERSION` file. Simplest safe fix — replace with a version-agnostic check that at least confirms 7 DLLs were produced:
  ```bash
  # Check that all 7 expected FFmpeg DLLs are present (version-agnostic names)
  setfilepath /xbmc/system
  for pattern in avcodec avformat avutil postproc swscale avfilter swresample; do
    if ! ls "$FILEPATH/${pattern}-"*.dll 1>/dev/null 2>&1; then
      throwerror "$FILEPATH/${pattern}-*.dll"
      exit 1
    fi
  done
  ```
- **BUG-04** (line 54): Fix `$(PWD)` → `$PWD` in the echo inside `runBackgroundProcess`

---

## Fix Task 3 — fmpeg_options.txt: Remove obsolete FFmpeg configure flags

**File:** `tools/buildsteps/win32/fmpeg_options.txt`
**Analysis:** `05-fmpeg-options-analysis.md` BUG-01, BUG-02
**Priority:** P0 (BUG-01 and BUG-02 both terminate FFmpeg configure)
**Agent instructions:**
- Read `fmpeg_options.txt`
- **BUG-01**: Remove the line `--enable-memalign-hack` entirely (this option was removed from FFmpeg and causes configure to abort)
- **BUG-02**: Remove the line `--disable-crystalhd` (removed from FFmpeg 4.x+; causes configure to abort on modern versions)
- **BUG-03/BUG-04**: Remove `--enable-shared` and `--disable-static` from this file (already set explicitly in `buildffmpeg.sh`; having them here is harmless but confusing)
- Final file should retain: `--enable-memalign-hack` removed, `--disable-crystalhd` removed, `--enable-shared` removed, `--disable-static` removed

---

## Fix Task 4 — buildffmpeg.sh: HTTPS for download + VS detection

**File:** `tools/buildsteps/win32/buildffmpeg.sh`
**Analysis:** `03-buildffmpeg-analysis.md` BUG-01, BUG-03
**Priority:** P1 (BUG-01 security), P1 (BUG-03 VS path)
**Agent instructions:**
- Read `buildffmpeg.sh`
- **BUG-01** (line 119): Change `http://` to `https://` in the gnutls download URL
- **BUG-03** (lines 97–104): The MSVC path block references `VS140COMNTOOLS`. Since this script runs inside MSYS2, add a fallback that searches common VS install paths:
  ```bash
  if [[ -z "$VS140COMNTOOLS" ]]; then
    # Try vswhere for VS 2017+
    VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if [[ -f "$VSWHERE" ]]; then
      VSINSTALLDIR=$("$VSWHERE" -latest -property installationPath | cygpath -u -)
      if [[ $BITS = "64bit" ]]; then
        VCTOOLSPATH="$VSINSTALLDIR/VC/Tools/MSVC/$(ls "$VSINSTALLDIR/VC/Tools/MSVC/" | tail -1)/bin/Hostx64/x64"
      else
        VCTOOLSPATH="$VSINSTALLDIR/VC/Tools/MSVC/$(ls "$VSINSTALLDIR/VC/Tools/MSVC/" | tail -1)/bin/Hostx86/x86"
      fi
    fi
  fi
  ```
- **BUG-06** (lines 173–175): Remove the redundant bgprocess file removal block (the `trap` at the top already handles it)

---

## Fix Task 5 — buildlibdvd.sh: MAKEFLAGS + glob guards + LIBDVDPREFIX guard

**File:** `tools/buildsteps/win32/buildlibdvd.sh`
**Analysis:** `04-buildlibdvd-analysis.md` BUG-01, BUG-02, BUG-03
**Priority:** P1 (BUG-01 parallelism), P1 (BUG-02 silent bad link), P2 (BUG-03)
**Agent instructions:**
- Read `buildlibdvd.sh`
- **BUG-01**: Add at the top of the script (after the `source buildhelpers.sh` block), before the first `do_load_autoconf` call:
  ```bash
  MAKEFLAGS="${MAKEFLAGS:--j$(( $(nproc) / 2 + 1 ))}"
  ```
  This inherits `$MAKEFLAGS` from environment or sets a sensible default.
- **BUG-02** (lines 74–82): Before the final `gcc` link command add a safety check:
  ```bash
  for objdir in "libdvdread/src" "libdvdnav/src" "libdvdnav/src/vm"; do
    objs=( "$LOCALBUILDDIR/$objdir"/*.o )
    if [[ ! -e "${objs[0]}" ]]; then
      echo "ERROR: No .o files found in $objdir — aborting link"
      exit 1
    fi
  done
  ```
- **BUG-03**: Add a guard near the top (after `source buildhelpers.sh`):
  ```bash
  [[ -z "$LIBDVDPREFIX" ]] && { echo "ERROR: LIBDVDPREFIX is not set"; exit 1; }
  ```

---

## Fix Task 6 — make-mingwlibs.bat: VS detection + mintty async fix

**File:** `tools/buildsteps/win32/make-mingwlibs.bat`
**Analysis:** `06-make-mingwlibs-bat-analysis.md` BUG-01, BUG-03
**Priority:** P0 (BUG-01 — will fail on any modern VS), P1 (BUG-03 — error check always wrong)
**Agent instructions:**
- Read `make-mingwlibs.bat`
- **BUG-01** (line 28): Replace the VS2015 `vcvarsall.bat` call with a vswhere-based dynamic lookup:
  ```bat
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
  IF "%VSINSTALLDIR%"=="" (ECHO Visual Studio not found via vswhere & EXIT /B 1)
  call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" %vcarch% || exit /b 1
  ```
- **BUG-03** (lines 55–58): Add `start /wait` before the mintty invocation so the batch script blocks until the build completes:
  ```bat
  start /wait %WORKDIR%\project\BuildDependencies\%msys2%\usr\bin\mintty.exe ...
  ```

---

## Fix Task 7 — download-msys.bat: VS detection + fstab dedup + msys64 path

**File:** `tools/buildsteps/win32/download-msys.bat`
**Analysis:** `09-download-msys-analysis.md` BUG-01, BUG-03, BUG-04
**Priority:** P0 (BUG-01), P1 (BUG-03, BUG-04)
**Agent instructions:**
- Read `download-msys.bat`
- **BUG-01** (line 61): Replace the hardcoded VS2015 line in the msys.bat patch with a vswhere lookup:
  ```bat
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
  IF NOT "%VSINSTALLDIR%"=="" (
    ECHO CALL "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars32.bat">>msys.bat
  )
  ```
- **BUG-03** (lines 42–48): Wrap each `ECHO ... >>fstab` with a FINDSTR check:
  ```bat
  FINDSTR /C:"/mingw" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB% /mingw>>"%MSYS_INSTALL_PATH%\etc\fstab"
  FINDSTR /C:"/xbmc" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB2% /xbmc>>"%MSYS_INSTALL_PATH%\etc\fstab"
  ```
- **BUG-04** (line 9): Change `SET MSYS_INSTALL_PATH="%CUR_PATH%\msys"` to `SET MSYS_INSTALL_PATH="%CUR_PATH%\msys64"` to align with the path expected by `make-mingwlibs.bat`

---

## Fix Task 8 — bootstrap-addons.bat + make-addons.bat: VS detection + exit code propagation

**Files:** `tools/buildsteps/win32/bootstrap-addons.bat`, `tools/buildsteps/win32/make-addons.bat`
**Analysis:** `10-bootstrap-addons-analysis.md` BUG-01, BUG-02; `11-make-addons-analysis.md` BUG-01, BUG-02, BUG-03
**Priority:** P0 (VS detection), P1 (exit code propagation)
**Agent instructions:**

For **bootstrap-addons.bat**:
- Read the file
- **BUG-01** (line 21): Replace `call "%VS140COMNTOOLS%..\..\VC\bin\vcvars32.bat"` with vswhere-based lookup (same pattern as Fix Task 6)
- **BUG-02** (lines 84–87): Add `GOTO ERROR` on nmake failure:
  ```bat
  IF ERRORLEVEL 1 (
    ECHO nmake failed with error level: %ERRORLEVEL%
    GOTO ERROR
  )
  ```

For **make-addons.bat**:
- Read the file
- **BUG-01** (line 27): Same VS2015 → vswhere fix
- **BUG-03** (after the addon build loop, before `GOTO END`): Add a failure file size check:
  ```bat
  FOR %%F IN (%ADDONS_FAILURE_FILE%) DO IF %%~zF GTR 0 (
    SET EXITCODE=1
    ECHO Some addons failed to build. See %ADDONS_FAILURE_FILE%
  )
  ```
- **BUG-05** (line 138): Change `>` to `>>` so all error messages are accumulated:
  ```bat
  ECHO nmake %%a error level: %ERRORLEVEL% >> %ERRORFILE%
  ```

---

## Fix Task 9 — run-tests.bat: MSBuild detection + solution paths

**File:** `tools/buildsteps/win32/run-tests.bat`
**Analysis:** `12-run-tests-analysis.md` BUG-01, BUG-02, BUG-04
**Priority:** P0 (completely broken on VS 2017+)
**Agent instructions:**
- Read `run-tests.bat`
- **BUG-01** (lines 30–36): Replace the MSBuild 14.0 registry query with vswhere:
  ```bat
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET NET="%%i"
  IF NOT DEFINED NET (
    set DIETEXT=MSBuild was not found.
    goto DIE
  )
  ```
- **BUG-02** (lines 35–36, 44–45, 60–61, 69): Update all `VS2010Express` path references to the current solution structure. The correct paths need to be determined from the actual repository layout — search for `*.sln` files to find the current solution before editing:
  - Use `Glob` to find `*.sln` in `project/` and update `OPTS_EXE`, `EXE`, `PDB`, and `cd` paths accordingly
- **BUG-04**: Add `SET exitcode=0` at the very top of the script (after `@ECHO OFF`)

---

## Fix Task 10 — prepare-env.bat: workspace guard + msys64 cleanup

**File:** `tools/buildsteps/win32/prepare-env.bat`
**Analysis:** `07-prepare-env-analysis.md` BUG-02, BUG-03, BUG-04
**Priority:** P2
**Agent instructions:**
- Read `prepare-env.bat`
- **BUG-04** (line 5): Add workspace guard at top:
  ```bat
  IF "%WORKSPACE%"=="" (ECHO ERROR: WORKSPACE environment variable is not set & EXIT /B 1)
  ```
- **BUG-02** (line 21): Add a matching `rmdir` for `msys64`:
  ```bat
  IF EXIST %WORKSPACE%\project\BuildDependencies\msys64 rmdir %WORKSPACE%\project\BuildDependencies\msys64 /S /Q
  ```
- **BUG-03** (lines 23–25): Remove or update the stale `VS2010Express` rmdir blocks (investigate whether the paths are still used before deleting)

---

## Execution Order

Run fix tasks in this order to avoid dependency issues:

```
P0 tasks first (build-breaking):
  Task 3  — fmpeg_options.txt (FFmpeg configure flags)
  Task 1  — buildhelpers.sh (comparison operator)
  Task 2  — make-mingwlibs.sh (comparison operator + hardcoded DLLs)
  Task 6  — make-mingwlibs.bat (VS detection)
  Task 7  — download-msys.bat (VS detection + fstab)
  Task 8  — bootstrap-addons.bat + make-addons.bat (VS detection)
  Task 9  — run-tests.bat (MSBuild + solution paths)

P1/P2 tasks after:
  Task 4  — buildffmpeg.sh (HTTPS + VS fallback)
  Task 5  — buildlibdvd.sh (MAKEFLAGS + glob guards)
  Task 10 — prepare-env.bat (workspace guard + cleanup)
```

---

## Cross-Cutting Issue: VS2015 (`VS140COMNTOOLS`) Replacement

**Affects:** `make-mingwlibs.bat`, `download-msys.bat`, `bootstrap-addons.bat`, `make-addons.bat`, `run-tests.bat`, `buildffmpeg.sh` (MSVC path)

All six files use VS 2015's `VS140COMNTOOLS` environment variable. The replacement pattern is consistent across all of them — use **vswhere.exe** which ships with VS 2017+. A single shared vswhere-lookup snippet should be extracted into a common `find-vs.bat` helper and included by all batch files.

**Suggested shared helper (`find-vs.bat`):**
```bat
@ECHO OFF
REM find-vs.bat — sets VSINSTALLDIR and VSMSBUILD via vswhere
FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET VSMSBUILD=%%i
IF "%VSINSTALLDIR%"=="" (ECHO Visual Studio not found & EXIT /B 1)
```

Each affected `.bat` file should call this helper instead of using `VS140COMNTOOLS` directly.
