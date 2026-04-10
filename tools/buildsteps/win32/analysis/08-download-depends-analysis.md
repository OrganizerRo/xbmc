# Analysis: download-depends.bat

**File:** `tools/buildsteps/win32/download-depends.bat`
**Role:** Downloads and installs Win32 build dependencies. Iterates over `*_d.bat` scripts in a `scripts` subdirectory, each of which fetches a specific library.

---

## Errors Found

### BUG-01 [MEDIUM] `rmdir` and `md` back-to-back race condition (lines 13–17)
The script comment says "can't run rmdir and md back to back. access denied error otherwise" yet the code does exactly that:
```bat
IF EXIST lib rmdir lib /S /Q
IF EXIST include rmdir include /S /Q
IF EXIST %TMP_PATH% rmdir %TMP_PATH% /S /Q
...
md lib
md include
md %TMP_PATH%
```
The comment acknowledges the issue but does not fix it. On a slow filesystem or antivirus-scanning system this still triggers access denied errors.

**Fix:** Add a `TIMEOUT /T 2 /NOBREAK` between the rmdir and md blocks, or use robocopy/xcopy with an empty source as a reliable mkdir alternative.

---

### BUG-02 [MEDIUM] `SET WGET` and `SET ZIP` paths not validated (lines 21–22)
```bat
SET WGET=%CUR_PATH%\bin\wget
SET ZIP=%CUR_PATH%\..\Win32BuildSetup\tools\7z\7za
```
If these tools don't exist at the expected paths, the child `*_d.bat` scripts will fail with cryptic "not recognized as an internal or external command" errors. No existence check is performed.

**Fix:** Add:
```bat
IF NOT EXIST "%WGET%.exe" (ECHO wget not found at %WGET% & EXIT /B 1)
IF NOT EXIST "%ZIP%.exe" (ECHO 7za not found at %ZIP% & EXIT /B 1)
```

---

### BUG-03 [LOW] Error propagation from child `*_d.bat` scripts is swallowed (lines 33–36)
```bat
FOR /F "tokens=*" %%S IN ('dir /B "*_d.bat"') DO (
  echo running %%S ...
  CALL %%S
)
```
`CALL %%S` does not check `%ERRORLEVEL%` after each child script. A failed download continues silently and the next script runs. The build will later fail with a missing file error, but the root cause is obscured.

**Fix:**
```bat
CALL %%S
IF ERRORLEVEL 1 (ECHO %%S failed & EXIT /B 1)
```

---

### BUG-04 [LOW] `%DL_PATH%` is set but children may not inherit it (lines 16–20)
```bat
IF $%1$ == $$ (
  SET DL_PATH="%CD%\downloads"
) ELSE (
  SET DL_PATH="%1"
)
```
`%DL_PATH%` is set in this script but child `.bat` files called via `CALL` run in the same environment (within `SETLOCAL` scope), so this should work. However because `SETLOCAL` is active from line 3, the variable is not visible to programs launched outside of `CALL` chains. The pattern is correct but fragile if any child script uses `START` to launch a subprocess.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | MEDIUM | 13–17 | rmdir+md race condition despite comment acknowledging it |
| BUG-02 | MEDIUM | 21–22 | wget and 7za paths not validated before use |
| BUG-03 | LOW | 33–36 | Child script errors swallowed; no ERRORLEVEL check |
| BUG-04 | LOW | 16–20 | `%DL_PATH%` inheritance fragile across subprocess chains |
