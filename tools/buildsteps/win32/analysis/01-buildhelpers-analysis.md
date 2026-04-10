# Analysis: buildhelpers.sh

**File:** `tools/buildsteps/win32/buildhelpers.sh`
**Role:** Shared library of helper functions sourced by `buildffmpeg.sh` and `buildlibdvd.sh`.

---

## Errors Found

### BUG-01 [CRITICAL] Shell redirection instead of numeric comparison (lines 8–12)
```bash
if [ $NUMBER_OF_PROCESSORS > 1 ]; then   # line 8 — redirects to file named "1"
  if [ $NUMBER_OF_PROCESSORS > 4 ]; then  # line 9 — redirects to file named "4"
```
`>` inside `[ ]` is **shell redirection**, not a numeric comparison. This silently creates files `1` and `4` in the current directory and the condition is always true.

**Fix:** Replace `>` with `-gt`:
```bash
if [ "$NUMBER_OF_PROCESSORS" -gt 1 ]; then
  if [ "$NUMBER_OF_PROCESSORS" -gt 4 ]; then
```
Also quote the variable to guard against empty values.

---

### BUG-02 [MEDIUM] `cpuCount` regex guard has unreachable path when `NUMBER_OF_PROCESSORS` is unset (lines 14–16)
```bash
if [[ ! $cpuCount =~ ^[0-9]+$ ]]; then
  cpuCount="$(($(nproc)/2))"
fi
```
If `NUMBER_OF_PROCESSORS` is unset the `>` redirect still runs (BUG-01), so `cpuCount` ends up as `1`. This guard works as a fallback but the root cause is BUG-01.

---

### BUG-03 [LOW] `do_wget` uses `--no-check-certificate` (lines 37–40)
Disabling TLS certificate validation allows man-in-the-middle attacks when downloading source tarballs. Should be removed or replaced with a proper CA bundle.

---

### BUG-04 [LOW] `do_pkgConfig` uses `cygpath` unconditionally (line 86)
```bash
[[ ! -z "$prefix" ]] && prefix="$(cygpath -u "$prefix")"
```
`cygpath` is a Cygwin/MSYS utility. If running outside MSYS2 this will fail silently. Should guard with `command -v cygpath >/dev/null 2>&1`.

---

### BUG-05 [LOW] `do_autoreconf` only checks for `configure`, not `configure.ac` (lines 106–110)
```bash
if [[ ! -f configure ]]; then
  autoreconf -fiv
fi
```
If a tarball ships a stale `configure` that is incompatible with the installed autotools version, `autoreconf` is never re-run. Should always run `autoreconf -fiv` or check `configure.ac` mtime vs `configure`.

---

## Summary Table

| ID | Severity | Line(s) | Description |
|----|----------|---------|-------------|
| BUG-01 | CRITICAL | 8–9 | `>` used as comparison inside `[ ]` (is a redirect) |
| BUG-02 | MEDIUM | 14–16 | Fallback nproc guard masked by BUG-01 |
| BUG-03 | LOW | 37–40 | `--no-check-certificate` on wget |
| BUG-04 | LOW | 86 | `cygpath` not guarded |
| BUG-05 | LOW | 106–110 | `autoreconf` only skipped if `configure` exists |
