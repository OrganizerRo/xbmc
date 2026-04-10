# Analysis: bootstrap-addons.bat

**File:** `tools/buildsteps/win32/bootstrap-addons.bat`
**Role:** Runs CMake to generate NMake Makefiles that fetch addon definitions (metadata, version pins) from a remote repository. This is the first step before `make-addons.bat` can build addons.

---

## Errors Found

### BUG-01 [HIGH] Hard dependency on VS 2015 (`VS140COMNTOOLS`) (line 21)
```bat
call "%VS140COMNTOOLS%..\..\VC\bin\vcvars32.bat"
```
Same VS 2015 dependency as in other scripts. `vcvars32.bat` (32-bit only) is used even though addon bootstrap only needs `nmake.exe` on the PATH. On VS 2017+ `nmake.exe` is at a different path and `VS140COMNTOOLS` is undefined.

**Fix:** Use `vswhere.exe` to locate nmake, or rely on the caller having already activated a VS environment.

---

### BUG-02 [MEDIUM] `%EXITCODE%` set to 0 at start but `:ERROR` block sets it to 1 — however GOTO flow bypasses SET (lines 5, 92–94)
```bat
SET EXITCODE=0
...
:ERROR
ECHO Failed to bootstrap addons
SET EXITCODE=1

:END
cd %CUR_PATH%
EXIT /B %EXITCODE%
```
This pattern is correct. However `nmake` failure (lines 84–87) falls through to `:END` with `EXITCODE=0` because the failure block only echoes a message without `GOTO ERROR`:
```bat
nmake
IF ERRORLEVEL 1 (
  ECHO nmake failed with error level: %ERRORLEVEL%
)
rem everything was fine
GOTO END
```
A failed `nmake` run is logged but does not set `EXITCODE=1`. The script exits 0, signaling success to the caller.

**Fix:**
```bat
IF ERRORLEVEL 1 (
  ECHO nmake failed with error level: %ERRORLEVEL%
  GOTO ERROR
)
```

---

### BUG-03 [MEDIUM] `%WORKSPACE%` used but not set in this script (line 23)
```bat
SET WORKDIR=%WORKSPACE%
```
`%WORKSPACE%` must be set by the caller (CI pipeline, parent batch). No guard exists. If unset, the fallback (lines 27–33) uses `PUSHD ..\..\..` which is three levels above the script's location — that may or may not be the workspace root depending on how the script is invoked.

---

### BUG-04 [LOW] `%REPOSITORY%` and `%REPOSITORY_REVISION%` default to empty strings (lines 73–77)
```bat
-DREPOSITORY_TO_BUILD="%REPOSITORY%" ^
-DREPOSITORY_REVISION="%REPOSITORY_REVISION%"
```
If not set, CMake receives empty strings. Whether CMake handles empty `REPOSITORY_TO_BUILD` gracefully depends on the CMakeLists — it may select a default repository, or it may configure nothing and produce an empty addons definition directory, causing `make-addons.bat` to build zero addons silently.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | HIGH | 21 | VS 2015 hard dependency for `nmake` path |
| BUG-02 | MEDIUM | 84–87 | `nmake` failure logged but not propagated; exits 0 |
| BUG-03 | MEDIUM | 23 | `%WORKSPACE%` not validated before use |
| BUG-04 | LOW | 73–77 | Empty repository args silently build no addons |
