# Fix Summary: bootstrap-addons.bat + make-addons.bat + run-tests.bat + prepare-env.bat

## bootstrap-addons.bat

### BUG-01 — VS2015 → vswhere detection (~line 21)
- **Before:** `call "%VS140COMNTOOLS%..\..\VC\bin\vcvars32.bat"`
- **After:**
  ```bat
  CALL "%~dp0find-vs.bat" || EXIT /B 1
  call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 || exit /b 1
  ```
- **Why:** VS140COMNTOOLS is not set on VS 2017+ builds. The find-vs.bat helper uses vswhere to locate the current VS installation.

### BUG-02 — nmake failure propagation (~lines 84-87)
- **Before:** `IF ERRORLEVEL 1` block only echoed the error but did not `GOTO ERROR`
- **After:** Added `GOTO ERROR` inside the ERRORLEVEL check after nmake
- **Why:** Without this, nmake build failures were silently ignored and the script continued as if successful.

## make-addons.bat

### BUG-01 — VS2015 → vswhere detection (~line 27)
- **Before:** `call "%VS140COMNTOOLS%..\..\VC\bin\vcvars32.bat"`
- **After:**
  ```bat
  CALL "%~dp0find-vs.bat" || EXIT /B 1
  call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" %vcarch% || exit /b 1
  ```

### BUG-03 — Addon failure file check (before GOTO END)
- **What was added:**
  ```bat
  FOR %%F IN (%ADDONS_FAILURE_FILE%) DO IF %%~zF GTR 0 (
    SET EXITCODE=1
    ECHO Some addons failed to build. See %ADDONS_FAILURE_FILE%
  )
  ```
- **Why:** Without it, a non-empty failure file was not caught and the script exited with code 0 even when individual addons failed.

### BUG-05 — Append vs overwrite for error log (~lines 139, 146)
- **Before:** `ECHO nmake %%a error level: %ERRORLEVEL% > %ERRORFILE%` and `ECHO nmake package-%%a error level: %ERRORLEVEL% > %ERRORFILE%`
- **After:** Changed `>` to `>>` on both lines
- **Why:** Each loop iteration overwrote the previous error, hiding earlier failures. Using `>>` appends so all errors accumulate.

## run-tests.bat

### BUG-04 — Missing exitcode initialization
- **Disposition:** Already present at line 22 (`SET exitcode=0`). No change needed.

### BUG-01 — MSBuild 14.0 registry → vswhere (~lines 30-36)
- **Before:**
  ```bat
  FOR /F "tokens=2,* delims= " %%A IN ('REG QUERY HKLM\SOFTWARE\Microsoft\MSBuild\ToolsVersions\14.0 /v MSBuildToolsRoot') DO SET MSBUILDROOT=%%B
  SET NET="%MSBUILDROOT%14.0\bin\MSBuild.exe"
  ```
- **After:**
  ```bat
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET NET="%%i"
  IF NOT DEFINED NET (
    set DIETEXT=MSBuild was not found.
    goto DIE
  )
  ```

### BUG-02 — VS2010Express solution paths
- **Solution files found:** No .sln files exist under `project/`. The `project/VS2010Express` directory does not exist. This branch uses CMake-based builds via `project/cmake/`.
- **Before:** All paths referenced `VS2010Express` (e.g., `..\VS2010Express\XBMC for Windows.sln`, `..\VS2010Express\XBMC\%buildconfig%\...`, `%WORKSPACE%\project\vs2010express\`)
- **After:** Updated to `cmake`-based paths:
  - `OPTS_EXE` → `..\cmake\kodi.sln`
  - `CLEAN_EXE` → `..\cmake\kodi.sln`
  - `EXE` → `..\cmake\kodi\%buildconfig%\%APP_NAME%-test.exe`
  - `PDB` → `..\cmake\kodi\%buildconfig%\%APP_NAME%.pdb`
  - Error log path → `..\cmake\kodi\%buildconfig%\objs\XBMC.log`
  - `cd` for test run → `%WORKSPACE%\project\cmake\`
  - Removed VCTargetsPath property (not needed with modern VS/vswhere)

## prepare-env.bat

### BUG-04 — WORKSPACE guard
- **What was added:** `IF "%WORKSPACE%"=="" (ECHO ERROR: WORKSPACE environment variable is not set & EXIT /B 1)` at line 2, immediately after `@ECHO OFF`

### BUG-02 — msys64 cleanup
- **What was added:** `IF EXIST %WORKSPACE%\project\BuildDependencies\msys64 rmdir %WORKSPACE%\project\BuildDependencies\msys64 /S /Q` after the existing msys rmdir line
- **Why:** The script cleaned up `msys` but not `msys64`, leaving stale MSYS2 directories.

### BUG-03 — VS2010Express rmdir blocks
- **Disposition:** The `project/VS2010Express` directory does not exist anywhere in the repository (verified with Glob). The three rmdir lines for `VS2010Express\XBMC`, `VS2010Express\objs`, and `VS2010Express\libs` were removed as dead code.

## Files Changed
- `tools/buildsteps/win32/bootstrap-addons.bat`
- `tools/buildsteps/win32/make-addons.bat`
- `tools/buildsteps/win32/run-tests.bat`
- `tools/buildsteps/win32/prepare-env.bat`
