# Analysis: make-addons.bat

**File:** `tools/buildsteps/win32/make-addons.bat`
**Role:** Builds Kodi addons using CMake + NMake. Iterates all addon definitions produced by `bootstrap-addons.bat`, building and optionally packaging each one.

---

## Errors Found

### BUG-01 [HIGH] Hard dependency on VS 2015 (`VS140COMNTOOLS`) (line 27)
```bat
call "%VS140COMNTOOLS%..\..\VC\bin\vcvars32.bat"
```
Same pattern as other scripts — VS 2015 only. Fails silently on VS 2017+ because `VS140COMNTOOLS` is undefined.

---

### BUG-02 [HIGH] `ADDONS_TO_MAKE` parsing relies on fragile string substitution (lines 122–130)
```bat
FOR /f "delims=" %%i IN ('nmake supported_addons') DO (
  SET line="%%i"
  SET addons=!line:ALL_ADDONS_BUILDING=!
  IF NOT "!addons!" == "!line!" (
    SET ADDONS_TO_MAKE=!addons:~3,-1!
  )
)
```
This parses nmake output by looking for the string `ALL_ADDONS_BUILDING` and then strips the first 3 and last 1 characters to extract the addon list. This is extremely brittle:
- If the NMake output format changes (different whitespace, line endings), the substring offset `~3,-1` will cut the wrong characters, silently producing an incorrect or empty addon list.
- On Windows, nmake output may include CR+LF; the `~3,-1` trimming does not account for a trailing `\r`.

**Fix:** Use a CMake-generated file to list supported addons instead of parsing nmake stdout.

---

### BUG-03 [MEDIUM] Individual addon build failures do not set the script exit code (lines 134–154)
```bat
FOR %%a IN (%ADDONS_TO_MAKE%) DO (
  nmake %%a
  IF ERRORLEVEL 1 (
    ECHO nmake %%a error level: %ERRORLEVEL% > %ERRORFILE%
    ECHO %%a >> %ADDONS_FAILURE_FILE%
  ) ELSE (
    ...
    ECHO %%a >> %ADDONS_SUCCESS_FILE%
  )
)
rem everything was fine
GOTO END
```
After the loop, `GOTO END` is always reached regardless of individual failures. `EXITCODE` stays 0 even if some addons failed. The `%ADDONS_FAILURE_FILE%` is written, but if the caller only checks the exit code it will think everything succeeded.

**Fix:** After the loop, check if the failure file is non-empty and set `EXITCODE=1`:
```bat
FOR %%F IN (%ADDONS_FAILURE_FILE%) DO IF %%~zF GTR 0 SET EXITCODE=1
```

---

### BUG-04 [MEDIUM] `%base_dir%` dependency (line 29)
```bat
SET WORKDIR=%base_dir%
```
Same undocumented caller-provided variable as in `make-mingwlibs.bat`. No guard if unset.

---

### BUG-05 [LOW] `%ERRORFILE%` written with `>` (overwrite) inside the per-addon loop (line 138)
```bat
ECHO nmake %%a error level: %ERRORLEVEL% > %ERRORFILE%
```
Each failed addon **overwrites** the error file, preserving only the last error. All previous errors are lost. Should append (`>>`).

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | HIGH | 27 | VS 2015 hard dependency |
| BUG-02 | HIGH | 122–130 | Fragile nmake output parsing for addon list |
| BUG-03 | MEDIUM | 134–154 | Addon failures not reflected in exit code |
| BUG-04 | MEDIUM | 29 | `%base_dir%` undocumented, no guard |
| BUG-05 | LOW | 138 | Error file overwritten per addon; only last error preserved |
