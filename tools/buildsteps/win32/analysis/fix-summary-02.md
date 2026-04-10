# Fix Summary: make-mingwlibs.sh

## BUG-01 — Shell comparison operator (line ~149)
- **Before:** `if [ $NUMBER_OF_PROCESSORS > 1 ]; then`
- **After:** `if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then`
- **Why:** `>` in `[ ]` is string redirection in bash, not numeric comparison.

## BUG-02 — MAKEFLAGS arithmetic (line ~150)
- **Before:** ``MAKEFLAGS=-j`expr $NUMBER_OF_PROCESSORS + $NUMBER_OF_PROCESSORS / 2` ``
- **After:** `MAKEFLAGS="-j$(( ${NUMBER_OF_PROCESSORS:-2} + ${NUMBER_OF_PROCESSORS:-2} / 2 ))"`
- **Why:** Backtick `expr` is POSIX-obsolete; `$((...))` is the modern POSIX shell arithmetic.

## BUG-03 — Hardcoded FFmpeg DLL names (~line 89)
- **Before:**
```bash
setfilepath /xbmc/system
checkfiles avcodec-57.dll avformat-57.dll avutil-55.dll postproc-54.dll swscale-4.dll avfilter-6.dll swresample-2.dll
```
- **After:**
```bash
# Check that all 7 expected FFmpeg DLLs are present (version-agnostic names)
setfilepath /xbmc/system
for pattern in avcodec avformat avutil postproc swscale avfilter swresample; do
  if ! ls "$FILEPATH/${pattern}-"*.dll 1>/dev/null 2>&1; then
    throwerror "$FILEPATH/${pattern}-*.dll"
    exit 1
  fi
done
```
- **Why:** Hardcoded version numbers break every time FFmpeg is updated.

## BUG-04 — $(PWD) -> $PWD (~line 54)
- **Before:** `echo "backgrounding: bash $1 $BGPROCESSFILE $TOOLS & (workdir: $(PWD))"`
- **After:** `echo "backgrounding: bash $1 $BGPROCESSFILE $TOOLS & (workdir: $PWD)"`
- **Why:** `$(PWD)` runs `PWD` as a subprocess; `$PWD` is the shell variable.

## Files Changed
- `tools/buildsteps/win32/make-mingwlibs.sh`
