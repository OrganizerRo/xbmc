# Analysis: run-tests.bat

**File:** `tools/buildsteps/win32/run-tests.bat`
**Role:** Builds the Kodi test suite using MSBuild and then runs the test executable, converting GTest XML output to JUnit-compatible format.

---

## Errors Found

### BUG-01 [CRITICAL] Queries MSBuild 14.0 (VS 2015) registry key; not present on VS 2017+ (lines 30–31)
```bat
FOR /F "tokens=2,* delims= " %%A IN ('REG QUERY HKLM\SOFTWARE\Microsoft\MSBuild\ToolsVersions\14.0 /v MSBuildToolsRoot') DO SET MSBUILDROOT=%%B
SET NET="%MSBUILDROOT%14.0\bin\MSBuild.exe"
```
`MSBuild\ToolsVersions\14.0` only exists for VS 2015. On VS 2017+ MSBuild is located at `%VS2017INSTALLDIR%\MSBuild\15.0\Bin\` (or found via vswhere). This registry query returns nothing, `MSBUILDROOT` is empty, `%NET%` is `"14.0\bin\MSBuild.exe"` (a relative path), and the `IF NOT EXIST %NET%` check at line 39 triggers the `DIE` label.

**Fix:** Use vswhere to locate MSBuild:
```bat
FOR /F "usebackq tokens=*" %%i IN (`vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET NET="%%i"
```

---

### BUG-02 [CRITICAL] Solution and output paths reference `VS2010Express` (lines 35–36, 44–45, 60–61, 69)
```bat
set OPTS_EXE="..\VS2010Express\XBMC for Windows.sln" ...
set EXE= "..\VS2010Express\XBMC\%buildconfig%\%APP_NAME%-test.exe"
set PDB= "..\VS2010Express\XBMC\%buildconfig%\%APP_NAME%.pdb"
```
And at runtime:
```bat
type "%CD%\..\vs2010express\XBMC\%buildconfig%\objs\XBMC.log"
cd %WORKSPACE%\project\vs2010express\
```
These paths are 15+ years out of date. The Kodi project has long since moved to a different solution layout. These paths will not exist and all build/run steps will fail.

**Fix:** Update paths to reflect the current VS solution location (e.g., `project\cmake` or the actual `.sln` path).

---

### BUG-03 [HIGH] `getbranch.bat` called but not in this directory (line 48)
```bat
call getbranch.bat
```
`getbranch.bat` is called without a path. It must either be in `%PATH%` or in the same directory as `run-tests.bat`. No check ensures it exists. If not found the `call` silently does nothing and `%BRANCH%` is never set — but since `%BRANCH%` is not actually used in the script after being set, this is currently a dead-code issue.

---

### BUG-04 [MEDIUM] `%exitcode%` uses mixed case — `%exitcode%` set on line 95 but `%EXITCODE%` is the naming convention elsewhere in the file (lines 59, 95)
```bat
IF %errorlevel%==1 (
  ...
  goto DIE
)
...
:DIE
  SET exitcode=1    ← lower case
...
EXIT /B %exitcode%  ← lower case
```
Windows environment variables are case-insensitive but the inconsistency makes the code harder to read. More importantly, `SET exitcode=0` is never done at startup — the variable starts undefined. `EXIT /B %exitcode%` when `exitcode` is undefined exits with code `0` (empty string treated as 0), masking failures unless `DIE` was reached.

**Fix:** Add `SET exitcode=0` at the top of the script.

---

### BUG-05 [LOW] `sed` output file handling: deletes input before confirming output is valid (lines 81–83)
```bat
%sed_exe% "s/..." %WORKSPACE%\gtestresults.xml > %WORKSPACE%\gtestresults-skipped.xml
del %WORKSPACE%\gtestresults.xml
move %WORKSPACE%\gtestresults-skipped.xml %WORKSPACE%\gtestresults.xml
```
If `sed` fails (e.g., sed not found, XML missing), `gtestresults-skipped.xml` is empty or missing. The `del` then removes the original results file, and `move` fails — test results are lost with no error.

**Fix:** Check `%ERRORLEVEL%` after sed and skip the del/move if it failed.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | CRITICAL | 30–31 | MSBuild 14.0 registry lookup; not present on VS 2017+ |
| BUG-02 | CRITICAL | 35–36, 44–45 | `VS2010Express` solution/output paths are obsolete |
| BUG-03 | HIGH | 48 | `getbranch.bat` called without path check |
| BUG-04 | MEDIUM | 95 | `exitcode` starts undefined; may exit 0 on failure |
| BUG-05 | LOW | 81–83 | sed failure can destroy test results XML |
