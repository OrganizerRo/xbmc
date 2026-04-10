# Analysis: make-mingwlibs.sh

**File:** `tools/buildsteps/win32/make-mingwlibs.sh`
**Role:** Main bash orchestrator. Sources `buildhelpers.sh`, sets global variables, and calls `buildProcess()` for 32-bit and/or 64-bit builds. Invokes `buildffmpeg.sh` and `buildlibdvd.sh` via `runBackgroundProcess`.

---

## Errors Found

### BUG-01 [CRITICAL] Shell redirection instead of numeric comparison (line 149)
```bash
if [ $NUMBER_OF_PROCESSORS > 1 ]; then
```
Identical to BUG-01 in `buildhelpers.sh`. The `>` redirects stdout to a file named `1` rather than performing a numeric comparison. This condition is always true (any non-empty write succeeds), creating a stray file `1`.

**Fix:**
```bash
if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then
```

---

### BUG-02 [MEDIUM] MAKEFLAGS arithmetic uses backtick expression that is fragile (line 150)
```bash
MAKEFLAGS=-j`expr $NUMBER_OF_PROCESSORS + $NUMBER_OF_PROCESSORS / 2`
```
- Integer arithmetic: `expr` follows operator precedence so `4 + 4 / 2 = 6`, which happens to be the intended 1.5× formula. But if `NUMBER_OF_PROCESSORS` is empty or non-numeric `expr` exits non-zero and the shell continues with a broken `MAKEFLAGS`.
- `$((…))` with a guard is more robust:
```bash
MAKEFLAGS="-j$(( NUMBER_OF_PROCESSORS + NUMBER_OF_PROCESSORS / 2 ))"
```

---

### BUG-03 [HIGH] Hardcoded FFmpeg DLL version numbers in `checkfiles` (line 89)
```bash
checkfiles avcodec-57.dll avformat-57.dll avutil-55.dll postproc-54.dll swscale-4.dll avfilter-6.dll swresample-2.dll
```
These version suffixes (57, 55, 54, 4, 6, 2) match an old FFmpeg build. Any FFmpeg version update will cause `checkfiles` to report failure even when the build succeeded.

**Fix:** Either derive the version from `FFMPEG-VERSION` at runtime or use a glob pattern in the `checkfiles` helper.

---

### BUG-04 [MEDIUM] `runBackgroundProcess` echoes `PWD` via subshell `$(PWD)` instead of `$PWD` (line 54)
```bash
echo "backgrounding: bash $1 $BGPROCESSFILE $TOOLS & (workdir: $(PWD))"
```
`$(PWD)` spawns a subshell trying to run a command named `PWD` (not the variable). Should be `$PWD` or `$(pwd)` (lowercase).

---

### BUG-05 [LOW] `run_builds` sources profile scripts that may not exist (lines 117, 124)
```bash
source /local32/etc/profile.local
source /local64/etc/profile.local
```
No existence check. If MSYS2 is configured differently these `source` calls silently fail, leaving `LOCALBUILDDIR`, `LOCALDESTDIR`, etc. unset, causing downstream failures with confusing errors.

**Fix:** Add `[[ -f /local32/etc/profile.local ]] || { echo "…"; exit 1; }` before each `source`.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | CRITICAL | 149 | `>` redirect instead of `-gt` comparison |
| BUG-02 | MEDIUM | 150 | Fragile `expr` arithmetic for MAKEFLAGS |
| BUG-03 | HIGH | 89 | Hardcoded FFmpeg DLL version numbers |
| BUG-04 | MEDIUM | 54 | `$(PWD)` should be `$PWD` or `$(pwd)` |
| BUG-05 | LOW | 117,124 | Profile scripts sourced without existence check |
