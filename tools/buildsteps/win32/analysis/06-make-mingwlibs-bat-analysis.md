# Analysis: make-mingwlibs.bat

**File:** `tools/buildsteps/win32/make-mingwlibs.bat`
**Role:** Windows batch launcher. Sets build flags, activates the MSVC environment, and invokes `make-mingwlibs.sh` through MSYS2's `mintty` or `sh` binary.

---

## Errors Found

### BUG-01 [HIGH] Hard dependency on Visual Studio 2015 (`VS140COMNTOOLS`) (line 28)
```bat
call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" %vcarch% || exit /b 1
```
`VS140COMNTOOLS` is the environment variable for **VS 2015**. On machines with VS 2019 or VS 2022, this variable is not set and the `call` silently expands to `call "..\..\VC\vcvarsall.bat"` which fails. The `|| exit /b 1` will catch the failure, but the error message gives no guidance.

**Fix:** Use `vswhere.exe` (shipped with VS 2017+) to dynamically locate `vcvarsall.bat`:
```bat
FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" %vcarch% || exit /b 1
```

---

### BUG-02 [MEDIUM] MSYS2 path assumes `project\BuildDependencies\msys` layout (lines 35–36)
```bat
SET MSYS_INSTALL_PATH=%WORKDIR%\project\BuildDependencies\msys
SET PATH=%MSYS_INSTALL_PATH%\mingw\bin;%MSYS_INSTALL_PATH%\bin;%PATH%
```
This references the old bundled MSYS installation under `project\BuildDependencies\msys`. The script also references `%msys2%` variable (set to `msys64`) but only uses the old path for `PATH` prepending. If the build environment uses system MSYS2 (e.g., at `C:\msys64`), this path block does nothing useful and may shadow system tools with stale/missing binaries.

---

### BUG-03 [MEDIUM] `mintty` launch does not wait for completion before checking `%ERRORFILE%` (lines 55–58)
```bat
%WORKDIR%\project\BuildDependencies\%msys2%\usr\bin\mintty.exe ... /xbmc/tools/buildsteps/win32/make-mingwlibs.sh ...
GOTO END
```
`mintty` launches a new terminal window asynchronously. Control returns to the batch script immediately, so `GOTO END` is reached before the build finishes. The `%ERRORFILE%` check at `:END` (lines 69–72) will always see no error file because the build hasn't had time to create it.

By contrast, the `sh` path (lines 47–53) runs synchronously — `sh.exe --login` blocks until the shell exits.

**Fix:** Use `start /wait` to launch mintty synchronously, or prefer the `sh` path in CI:
```bat
start /wait %WORKDIR%\...\mintty.exe ...
```

---

### BUG-04 [LOW] `%base_dir%` used for `WORKDIR` but may be unset (line 5)
```bat
SET WORKDIR=%base_dir%
```
`%base_dir%` is not set by this script. It relies on the caller (e.g., CI pipeline or another batch file) having set it. If called standalone, `%WORKDIR%` is empty and the fallback at lines 31–33 uses `%~dp0\..\..\..` (three levels up from the script location). This fallback is reasonable but the dependency on `%base_dir%` should be documented.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | HIGH | 28 | Hard dependency on VS 2015 (`VS140COMNTOOLS`) |
| BUG-02 | MEDIUM | 35–36 | MSYS2 PATH uses old bundled MSYS path |
| BUG-03 | MEDIUM | 55–58 | `mintty` launched asynchronously; error check is always premature |
| BUG-04 | LOW | 5 | `%base_dir%` dependency undocumented; silent empty fallback |
