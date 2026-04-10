# Analysis: buildlibdvd.sh

**File:** `tools/buildsteps/win32/buildlibdvd.sh`
**Role:** Builds libdvdcss, libdvdread, and libdvdnav inside the MSYS2 environment. Copies `libdvdcss-2.dll` and `libdvdnav.dll` to `/xbmc/system/`.

---

## Errors Found

### BUG-01 [HIGH] `do_makelib` call passes `$MAKEFLAGS` but `$MAKEFLAGS` is set by `buildhelpers.sh` args — not exported (lines 41, 58, 71)
```bash
do_makelib $MAKEFLAGS || exit 1
```
`MAKEFLAGS` is set in `buildhelpers.sh` line 2 as a positional parameter (`MAKEFLAGS="$1"`), but this script sources `buildhelpers.sh` and then calls `do_load_autoconf` which calls `do_clean_get $MAKEFLAGS`. If `MAKEFLAGS` is empty the `-j` flag is missing and builds are single-threaded (not a crash, but a perf regression and not the intent).

The deeper issue: `buildlibdvd.sh` does not accept or forward `$MAKEFLAGS`. It is always empty unless the caller exports it.

**Fix:** Accept `MAKEFLAGS` as `$1` at the top of this script (same pattern as `buildhelpers.sh`) and export it, or inherit via environment export from `make-mingwlibs.sh`.

---

### BUG-02 [HIGH] Final `gcc` link command uses bare wildcard globs that will fail if object files are missing (lines 74–82)
```bash
gcc \
   -shared \
   -o $LIBDVDPREFIX/bin/libdvdnav.dll \
   ...
   libdvdread/src/*.o libdvdnav/src/*.o libdvdnav/src/vm/*.o $LIBDVDPREFIX/lib/libdvdcss.dll.a \
```
- If any sub-library failed to compile, `*.o` globs expand to literal `*.o` strings (when nullglob is not set), causing a confusing linker error rather than a clear build failure.
- The working directory at this point must be `$LOCALBUILDDIR` (enforced by line 73 `cd $LOCALBUILDDIR`), but `libdvdread/src/*.o` paths are relative — if the library directories were cleaned or not fully built, this silently links an incomplete DLL.

**Fix:** Check for the presence of at least one `.o` file in each directory before linking, or build via a proper Makefile target.

---

### BUG-03 [MEDIUM] libdvdnav configure missing `CFLAGS` include for `$LIBDVDPREFIX/include` (lines 62–70)
```bash
./configure \
   ...
   CFLAGS="-D_XBMC -DNDEBUG -I$LIBDVDPREFIX/include" \
```
This is present for libdvdnav but notably the include flag uses `-I$LIBDVDPREFIX/include`. When cross-references are needed (libdvdnav depends on libdvdread headers), this path is correct — however if `$LIBDVDPREFIX` is unset (e.g., due to a sourcing failure), the configure will silently use an empty path `-I/include` which may or may not find headers depending on the system.

**Fix:** Add `[[ -z "$LIBDVDPREFIX" ]] && { echo "LIBDVDPREFIX not set"; exit 1; }` near the top of the script.

---

### BUG-04 [MEDIUM] `strip -S` on a shared DLL is chained with `cp` using `&&` — a strip failure aborts the copy (lines 43–44, 84–85)
```bash
strip -S $LIBDVDPREFIX/bin/libdvdcss-2.dll &&
cp "$LIBDVDPREFIX/bin/libdvdcss-2.dll" /xbmc/system/
```
If `strip` fails (e.g., the DLL is locked), the copy to `/xbmc/system/` is skipped silently with exit code 0 (the `&&` chain ends successfully if the leading `strip` was the last thing). Actually the `&&` chain returns the exit code of the last command executed, but the outer caller checks for specific files via `checkfiles`, so the real symptom is a missing DLL.

More importantly, `strip -S` (strip debug symbols only) may fail on a MinGW DLL that lacks a symbol table — this should use `strip --strip-debug` or just `strip` for DLLs. On older binutils `-S` is an alias for `--strip-debug`, but this inconsistency is worth noting.

---

### BUG-05 [LOW] libdvdcss configure uses `ac_cv_path_GIT=` to suppress git (line 26)
```bash
ac_cv_path_GIT= ./configure \
```
This sets `ac_cv_path_GIT` to empty string to prevent autoconf from finding `git`. This is a workaround for the ChangeLog generation issue and is intentional, but it is fragile: if the configure script's internal variable name changes across versions this workaround silently breaks.

The script already patches the `Makefile` ChangeLog rule (lines 38–40) as the primary fix. The `ac_cv_path_GIT=` prefix is therefore redundant and should be removed after verifying the Makefile patch is sufficient.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | HIGH | 41,58,71 | `$MAKEFLAGS` not passed/exported; builds are single-threaded |
| BUG-02 | HIGH | 74–82 | Bare `*.o` globs in final link; fails silently if builds incomplete |
| BUG-03 | MEDIUM | 62–70 | No guard for unset `$LIBDVDPREFIX` |
| BUG-04 | MEDIUM | 43–44, 84–85 | `strip -S` failure silently skips `cp` to system dir |
| BUG-05 | LOW | 26 | Redundant `ac_cv_path_GIT=` after Makefile patch |
