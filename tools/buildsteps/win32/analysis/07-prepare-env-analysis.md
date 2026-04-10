# Analysis: prepare-env.bat

**File:** `tools/buildsteps/win32/prepare-env.bat`
**Role:** Pre-build workspace cleanup. Removes old build artifacts and runs `git clean` to reset the workspace, preserving downloaded dependency archives.

---

## Errors Found

### BUG-01 [MEDIUM] `git clean` preserves hardcoded old dependency paths (lines 12–13)
```bat
git clean -xfd -e "project/BuildDependencies/downloads" -e "project/BuildDependencies/downloads2"
```
Only two legacy download cache directories are excluded. If downloads are placed elsewhere (e.g., a CI cache mounted at a different path, or `project\BuildDependencies\msys64`), they will be deleted by `git clean`. The MSYS2 installation itself is also under `project/BuildDependencies` and would be cleaned if it were git-tracked.

This is not a crash bug but can cause unexpectedly slow CI runs if MSYS2 and dependencies are re-downloaded every time.

---

### BUG-02 [MEDIUM] `rmdir` on `project\BuildDependencies\msys` but not `msys64` (line 21)
```bat
IF EXIST %WORKSPACE%\project\BuildDependencies\msys rmdir %WORKSPACE%\project\BuildDependencies\msys /S /Q
```
The script removes the old `msys` directory but not `msys64` (the actual MSYS2 directory referenced in `make-mingwlibs.bat`). This is an inconsistency — either the cleanup is incomplete (msys64 should also be cleaned) or the msys removal is a stale leftover that should be removed.

---

### BUG-03 [LOW] References `VS2010Express` paths (lines 23–25)
```bat
IF EXIST %WORKSPACE%\project\VS2010Express\XBMC rmdir %WORKSPACE%\project\VS2010Express\XBMC /S /Q
IF EXIST %WORKSPACE%\project\VS2010Express\objs rmdir %WORKSPACE%\project\VS2010Express\objs /S /Q
IF EXIST %WORKSPACE%\project\VS2010Express\libs rmdir %WORKSPACE%\project\VS2010Express\libs /S /Q
```
`VS2010Express` is a very old project layout. If the project has migrated to a newer Visual Studio solution structure, these `rmdir` calls target directories that no longer exist (harmless, but confusing dead code that should be removed or updated).

---

### BUG-04 [LOW] No check that `%WORKSPACE%` is set (line 5)
```bat
cd %WORKSPACE%
```
If `%WORKSPACE%` is unset, the `cd` command silently fails and all subsequent `rmdir` calls operate relative to the script's launch directory, potentially cleaning the wrong location. Should add:
```bat
IF "%WORKSPACE%"=="" (ECHO WORKSPACE not set & EXIT /B 1)
```

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | MEDIUM | 12–13 | `git clean` excludes only legacy download dirs; may delete MSYS2 |
| BUG-02 | MEDIUM | 21 | Cleans old `msys` dir but not `msys64` |
| BUG-03 | LOW | 23–25 | References stale `VS2010Express` paths |
| BUG-04 | LOW | 5 | No guard for unset `%WORKSPACE%` |
