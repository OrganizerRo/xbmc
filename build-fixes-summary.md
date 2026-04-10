# Windows Build Fixes — libdvd autoreconf failure

## Problem

The GitHub Actions `Build Windows` workflow failed during the "Build FFmpeg and libdvd
from source" step with:

```
buildhelpers.sh: line 108: autoreconf: command not found
./buildlibdvd.sh: line 19: ./configure: No such file or directory
make: *** No rule to make target 'clean'.  Stop.
```

This cascade hit all three libdvd libraries (libdvdcss, libdvdread, libdvdnav):

1. `autoreconf -fiv` was not found → `configure` script never generated.
2. Without `configure`, `./configure` exited immediately → no `Makefile` produced.
3. Without a `Makefile`, `make clean` failed → build marked **Failed**.
4. Without the build, `strip` could not find the output DLLs.

## Root cause

`buildhelpers.sh::do_autoreconf()` calls `autoreconf -fiv` to generate the `configure`
script from `configure.ac` (the libdvd source repos do not ship a pre-generated
`configure`).  `autoreconf` is provided by the `autoconf` package; `automake` and
`libtool` are also required because the `configure.ac` files use AM_INIT_AUTOMAKE and
LT_INIT macros.

The workflow's "Install MinGW32 build tools" step installed `base-devel`, which nominally
includes these tools, but the GitHub Actions `windows-2022` runner's pre-installed MSYS2
does not guarantee that every `base-devel` member is present.  The packages were therefore
absent at build time.

FFmpeg is unaffected by this issue: `buildffmpeg.sh` does not call `do_autoreconf` because
the FFmpeg source tarball already contains a pre-generated `configure` script.

## Fix

File: `.github/workflows/build-windows.yaml`

Added three explicit packages to the `pacman -S` invocation in the
"Install MinGW32 build tools" step:

```diff
       bash -lc "pacman --needed --noconfirm -S
       base-devel
+      autoconf
+      automake
+      libtool
       git
       wget
       ...
```

This ensures `autoreconf` is available regardless of what the runner's MSYS2 snapshot
contains, making the install step idempotent across different runner image versions.
