# Analysis: fmpeg_options.txt

**File:** `tools/buildsteps/win32/fmpeg_options.txt`
**Role:** Extra configure flags appended to `FFMPEG_BASE_OPTS` when building FFmpeg. Read by `buildffmpeg.sh` via `cat "$FFMPEG_CONFIG_FILE"`.

---

## Errors Found

### BUG-01 [CRITICAL] `--enable-memalign-hack` was removed from FFmpeg (line 2)
```
--enable-memalign-hack
```
This option was **removed from FFmpeg around version 2.x**. Passing it to any modern FFmpeg `./configure` causes:
```
ERROR: option --enable-memalign-hack did not match anything
```
and terminates the configure step, failing the entire FFmpeg build.

**Fix:** Remove this line entirely.

---

### BUG-02 [HIGH] `--disable-crystalhd` may not exist in modern FFmpeg (line 10)
```
--disable-crystalhd
```
The CrystalHD decoder was removed from FFmpeg around version 4.x. On newer versions this option causes configure to abort with an unrecognized option error.

**Fix:** Remove this line, or wrap in a version check.

---

### BUG-03 [MEDIUM] `--enable-shared` duplicated (lines 1 and buildffmpeg.sh line 167)
```
--enable-shared          # fmpeg_options.txt line 1
```
`buildffmpeg.sh` also passes `--enable-shared` explicitly in the `./configure` invocation at line 167. The option is passed twice. While FFmpeg configure tolerates duplicates, it is confusing and can mask override intent.

**Fix:** Remove `--enable-shared` from `fmpeg_options.txt` since it is already set explicitly in `buildffmpeg.sh`.

---

### BUG-04 [LOW] `--disable-static` duplicated with `buildffmpeg.sh` (line 5)
Same issue as BUG-03 — `--disable-static` appears in both places.

---

### BUG-05 [LOW] `--disable-avdevice` conflicts with intent of `--disable-devices` (lines 7–8)
```
--disable-devices
--disable-avdevice
```
`--disable-devices` disables all device I/O, while `--disable-avdevice` disables the entire `libavdevice` library. These are consistent but redundant — `--disable-avdevice` already implies no device support. Having both can cause version-specific configure warnings.

---

## Summary Table

| ID | Severity | Line | Description |
|----|----------|------|-------------|
| BUG-01 | CRITICAL | 2 | `--enable-memalign-hack` removed from FFmpeg; breaks configure |
| BUG-02 | HIGH | 10 | `--disable-crystalhd` removed from FFmpeg 4.x+ |
| BUG-03 | MEDIUM | 1 | `--enable-shared` duplicated in buildffmpeg.sh |
| BUG-04 | LOW | 5 | `--disable-static` duplicated in buildffmpeg.sh |
| BUG-05 | LOW | 7–8 | `--disable-devices` and `--disable-avdevice` are redundant |
