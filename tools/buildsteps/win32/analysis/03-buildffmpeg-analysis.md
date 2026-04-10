# Analysis: buildffmpeg.sh

**File:** `tools/buildsteps/win32/buildffmpeg.sh`
**Role:** Downloads and builds FFmpeg (and its gnutls dependency) for the Win32/Win64 MinGW target. Copies resulting DLLs to `/xbmc/system/`.

---

## Errors Found

### BUG-01 [HIGH] gnutls downloaded over plain HTTP (line 119)
```bash
do_wget "http://mirrors.xbmc.org/build-deps/sources/gnutls-${GNUTLS_VER}.tar.xz"
```
Plain HTTP allows MITM injection of a malicious tarball. Should use `https://`.

---

### BUG-02 [MEDIUM] `do_checkForOptions` logic is inverted (lines 61–71, 76–78)
`do_checkForOptions` returns `0` (success/true) when the option **is present** and `1` when absent. `do_addOption` calls it as:
```bash
if ! do_checkForOptions "$option"; then   # adds only if NOT present — correct
```
But call sites in `do_getFFmpegConfig` like:
```bash
if do_checkForOptions "--enable-gnutls"; then   # line 116 — outer gate
```
…and the same at line 45–48 for gplv3 look correct. However BUG-02 is subtle: `do_checkForOptions` iterates `"$@"` but calls `grep -E -e "$option2"` without anchoring. An option like `--enable-libass` would match `--enable-libassert` if it existed. Not currently broken but fragile.

---

### BUG-03 [HIGH] MSVC path uses VS2015 (`VS140COMNTOOLS`) which is likely absent (lines 99–103)
```bash
VCTOOLSPATH="$VS140COMNTOOLS../../VC/BIN/amd64"
```
`VS140COMNTOOLS` is the VS 2015 environment variable. On modern CI/dev machines running VS 2019/2022 this variable is not set. The MSVC toolchain path will be empty and `cl.exe`/`link.exe` won't be found.

**Fix:** Use `vswhere` to detect the installed VS version dynamically, or document VS 2015 as a hard requirement.

---

### BUG-04 [MEDIUM] `extra_cflags`/`extra_ldflags` cleared for MSVC but may carry values from prior MinGW path (lines 106–108)
```bash
export CFLAGS=""
export CXXFLAGS="" 
export LDFLAGS=""
```
The trailing space in `CXXFLAGS=""` is harmless but sloppy. More importantly, if `CFLAGS`/`LDFLAGS` were set in the environment before this script ran, they are blanked for MSVC but **not** for the MinGW path (lines 163–164 only set defaults if variables are unset via `[[ -z … ]]`). If `CFLAGS` was non-empty in the environment the MinGW build will use unexpected flags.

---

### BUG-05 [MEDIUM] gnutls `make distclean` left in wrong directory (lines 159–160)
```bash
do_clean_get $1
[ -f config.mak ] && make distclean
```
These lines run in `$LOCALBUILDDIR` after the gnutls build, intended for the FFmpeg source. However `do_clean_get` calls `do_download` which `cd`s into the FFmpeg source directory. A `make distclean` check on `config.mak` is thus run inside the FFmpeg directory, which is correct, but the flow depends on `do_clean_get` leaving the working directory inside the library folder — that contract should be documented.

---

### BUG-06 [LOW] Removing bgprocess file at end is guarded but trap already handles it (lines 173–175)
```bash
if [ -f $BGPROCESSFILE ]; then
  rm $BGPROCESSFILE
fi
```
The script already registers `trap 'rm -f "$BGPROCESSFILE"' EXIT` (line 8). This explicit removal is redundant and harmless but confusing.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | HIGH | 119 | gnutls downloaded over HTTP, not HTTPS |
| BUG-02 | MEDIUM | 61–71 | Option grep not anchored; potential false matches |
| BUG-03 | HIGH | 99–103 | VS2015 (`VS140COMNTOOLS`) hard dependency, likely absent |
| BUG-04 | MEDIUM | 163–164 | Environment `CFLAGS` not cleared for MinGW path |
| BUG-05 | MEDIUM | 159–160 | Silent dependency on `do_clean_get` CWD side-effect |
| BUG-06 | LOW | 173–175 | Redundant bgprocess removal (trap already handles it) |
