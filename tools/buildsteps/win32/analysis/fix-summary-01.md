# Fix Summary: buildhelpers.sh + fmpeg_options.txt

## buildhelpers.sh

### BUG-01 — Shell comparison operator
- **Before:**
  ```bash
  if [ $NUMBER_OF_PROCESSORS > 1 ]; then
    if [ $NUMBER_OF_PROCESSORS > 4 ]; then
  ```
- **After:**
  ```bash
  if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 1 ]; then
    if [ "${NUMBER_OF_PROCESSORS:-0}" -gt 4 ]; then
  ```
- **Why:** `>` in `[ ]` does string redirection, not numeric comparison. `-gt` is the correct POSIX operator.

### BUG-03 — Remove --no-check-certificate from do_wget
- **Before:**
  ```bash
  wget --tries=5 --retry-connrefused --waitretry=2 --no-check-certificate -c $URL
  wget --tries=5 --retry-connrefused --waitretry=2 --no-check-certificate -c $URL -O $archive
  ```
- **After:**
  ```bash
  wget --tries=5 --retry-connrefused --waitretry=2 -c $URL
  wget --tries=5 --retry-connrefused --waitretry=2 -c $URL -O $archive
  ```
- **Why:** Disabling certificate checks is a security risk.

## fmpeg_options.txt

### BUG-01 — Removed --enable-memalign-hack
- **Why:** Option was removed from FFmpeg; causes configure to abort.

### BUG-02 — Removed --disable-crystalhd
- **Why:** Removed from FFmpeg 4.x+; causes configure to abort on modern versions.

### BUG-03/04 — Removed --enable-shared and --disable-static
- **Why:** Already set explicitly in buildffmpeg.sh; duplicate causes confusion.

## Files Changed
- `tools/buildsteps/win32/buildhelpers.sh`
- `tools/buildsteps/win32/fmpeg_options.txt`
