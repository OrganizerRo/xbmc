# CMake & Build Script Analysis — Kodi Nexus Windows (Win32)

> **Agent memory file** — Produced by the build-workflow planning subagent.
> Covers every CMake and build-script file relevant to building Kodi Nexus
> on GitHub Actions `windows-2022` runners.  Each file is analysed for
> required changes; most require **no changes** because they already support
> the Nexus code-base correctly.

---

## 1. `cmake/scripts/windows/ArchSetup.cmake`  ✅ No changes required

```cmake
# Minimum SDK version we support
set(VS_MINIMUM_SDK_VERSION 10.0.14393.0)
```

**windows-2022 runner SDK**: `10.0.20348.0` ≥ required minimum — no error.

```cmake
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(ARCH win32)           # Win32 / x86 build
  set(SDK_TARGET_ARCH x86)
```

Architecture is auto-detected from the generator `-A Win32` flag passed on the
CMake command line — no override needed.

```cmake
set(DEPS_FOLDER_RELATIVE project/BuildDependencies)
set(DEPENDS_PATH ${CMAKE_SOURCE_DIR}/${DEPS_FOLDER_RELATIVE}/${ARCH})
set(MINGW_LIBS_DIR ${CMAKE_SOURCE_DIR}/${DEPS_FOLDER_RELATIVE}/mingwlibs/${ARCH})
```

Both paths are populated by the workflow before CMake runs:
- `project/BuildDependencies/win32/` — pre-built packages from `mirrors.kodi.tv`
- `project/BuildDependencies/mingwlibs/win32/` — FFmpeg MSVC libs from MinGW build

**`DELAYLOAD` DLLs** that must be present at runtime (already in pre-built packages):
```
zlib.dll  libmariadb.dll  libxslt.dll  dnssd.dll  dwmapi.dll  sqlite3.dll  d3dcompiler_47.dll
```

---

## 2. `cmake/scripts/windows/CFlagOverrides.cmake` & `CXXFlagOverrides.cmake`  ✅ No changes required

Used by the addon NMake build to override default MSVC flags.
These exist in the Nexus tree and are passed via `-DCMAKE_USER_MAKE_RULES_OVERRIDE`.

---

## 3. `cmake/scripts/windows/Macros.cmake`  ✅ No changes required

Implements `add_precompiled_header()` using MSVC `/Yc`/`/Yu` flags.
PCH output path: `${PROJECT_BINARY_DIR}/${CORE_BUILD_CONFIG}/objs/` — already set.

---

## 4. `cmake/scripts/windows/tools/patch.cmake`  ✅ No changes required

Auto-downloads `patch.exe` from `mirrors.kodi.tv/build-deps/win32/` if not
found via Git. On `windows-2022` runners, Git ships `patch.exe` in
`C:\Program Files\Git\usr\bin\` which CMake can find — this step is usually
a no-op.

---

## 5. `tools/buildsteps/windows/make-mingwlibs.sh`  ✅ No changes required

The script already handles the Nexus workflow correctly:

```bash
buildProcess() {
  export PREFIX=/xbmc/project/BuildDependencies/mingwlibs/$TRIPLET
  # Skip if git-hash + FFMPEG-VERSION unchanged (incremental build cache)
  if [ "$(pathChanged $PREFIX ...)" == "0" ]; then return; fi
  # Build FFmpeg via buildffmpeg.sh
  ./buildffmpeg.sh $MAKECLEAN
  # Validate output
  checkfiles lib/avcodec.lib lib/avformat.lib lib/avutil.lib \
             lib/postproc.lib lib/swscale.lib lib/avfilter.lib lib/swresample.lib
}
```

**MSYS2 fstab mounts** the workflow must write before this script runs:
| MSYS2 path | Host path |
|---|---|
| `/xbmc` | junction `C:\msys64\xbmc → $GITHUB_WORKSPACE` |
| `/build` | `project/BuildDependencies/build/` |
| `/downloads` | `project/BuildDependencies/downloads/` |
| `/local32` | `project/BuildDependencies/locals/win32/` |
| `/local32/etc/profile.local` | written by workflow step |
| `/depends/win32` | `project/BuildDependencies/win32/` |

---

## 6. `tools/buildsteps/windows/buildffmpeg.sh`  ✅ No changes required

Key options applied at runtime (overriding `ffmpeg_options.txt`):

```bash
do_addOption "--enable-openssl"
do_addOption "--enable-nonfree"
do_addOption "--toolchain=msvc"        # produces .lib not .dll.a
do_addOption "--disable-mediafoundation"
do_addOption "--enable-libdav1d"       # from ffmpeg_options.txt
```

OpenSSL MSVC libs are resolved via:
```bash
extra_ldflags="-LIBPATH:\"/depends/$TRIPLET/lib\""
extra_cflags="-I/depends/$TRIPLET/include"
```

`/depends/win32` → `project/BuildDependencies/win32/` which is populated by
the **pre-FFmpeg dependency download** step (must run first in the workflow).

dav1d is also at `/depends/win32/lib/dav1d.lib` after the same step.

---

## 7. `tools/buildsteps/windows/download-dependencies.bat`  ✅ No changes required

```bat
SET TARGETPLATFORM=%1
IF "%TARGETPLATFORM%" == "" SET TARGETPLATFORM=win32
```

Called without arguments in the workflow → downloads both
`0_package.native-win32.list` and `0_package.target-win32.list`.

The `native-win32.list` includes:
- `vswhere-2.2.11-win32.7z` → `project/BuildDependencies/win32/tools/vswhere/vswhere.exe`
- `swig-4.0.1-win32-v141-20200105.7z`
- `TexturePacker-win32-v141-20200105.7z`
- `JsonSchemaBuilder-win32-v141-20200105.7z`

The `target-win32.list` includes all runtime `.dll` libraries.

---

## 8. `tools/buildsteps/windows/bootstrap-addons.bat`  ✅ No changes required

```bat
cmake "%ADDONS_BOOTSTRAP_PATH%" -G "NMake Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_INSTALL_PREFIX=%ADDONS_DEFINITION_PATH% ^
      -DBUILD_DIR=%BOOTSTRAP_BUILD_PATH%
```

Fetches addon definitions from `cmake/addons/bootstrap/` and installs
them to `cmake/addons/addons/`. This step does **not** build anything;
it just downloads addon CMakeLists metadata.

---

## 9. `tools/buildsteps/windows/make-addons.bat`  ✅ No changes required (but inline in workflow)

The workflow inlines the addon build loop directly (same technique as Leia)
to capture per-addon build logs.  The script is not called directly because:
1. It doesn't provide per-addon log files.
2. It calls `vswhere.bat` which expects vswhere.exe from the dependency bundle.

The workflow uses the system-installed `vswhere.exe` instead
(`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`).

---

## 10. `cmake/addons/CMakeLists.txt`  ✅ No changes required

The Nexus addon build system uses `NMake Makefiles` generator with:
```cmake
-DCMAKE_USER_MAKE_RULES_OVERRIDE=…/CFlagOverrides.cmake
-DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX=…/CXXFlagOverrides.cmake
-DCMAKE_INSTALL_PREFIX=project/Win32BuildSetup/BUILD_WIN32/addons
-DADDON_DEPENDS_PATH=cmake/addons/output
-DPACKAGE_ZIP=ON
```

---

## 11. MSVC 14.44+ Compatibility Workarounds  ⚠️ Needed in workflow (not cmake files)

### 11.1 `PTRDIFF_MAX` / `_CRT_DECLARE_NONSTDC_NAMES`

**Problem**: MSVC 14.44+ moved `PTRDIFF_MAX` declaration behind
`_CRT_DECLARE_NONSTDC_NAMES`. Addons that include `<xlocnum>` or `<stddef.h>`
indirectly (e.g., `imagedecoder.heif` via libde265) get `C2065: 'PTRDIFF_MAX': undeclared`.

**Fix (in workflow environment, not in cmake)**:
```yaml
- name: Set CL env for MSVC addon build
  shell: cmd
  run: echo CL=/D_CRT_DECLARE_NONSTDC_NAMES=1>> %GITHUB_ENV%
```

Setting the `CL` environment variable causes `cl.exe` to pick up this define
at every invocation regardless of CMake ExternalProject nesting depth.

**Affected files**: none — this is a host-MSVC compatibility fix, not a source change.

### 11.2 Visualization addons using intrinsic function pointers

**Problem**: `visualization.milkdrop` / `visualization.milkdrop2` use `__floor` /
`__ceil` as function pointers.  MSVC 14.44+ treats these as pure intrinsics
(error `C7552: invalid use of intrinsic function`).

**Fix (in workflow, not cmake)**:
```yaml
- name: Disable visualization milkdrop addons on Windows
  shell: pwsh
  run: |
    foreach ($addon in @("visualization.milkdrop", "visualization.milkdrop2")) {
      $dir = "$env:GITHUB_WORKSPACE\cmake\addons\addons\$addon"
      New-Item -ItemType Directory -Force -Path $dir | Out-Null
      Set-Content -Path "$dir\platforms.txt" -Value "!windows"
    }
```

This is applied **after** `bootstrap-addons.bat` runs (which creates the
addon definition directories) and **before** the addon cmake configure step.

---

## 12. Summary: Files Requiring Code Changes

| File | Change Required | Reason |
|------|----------------|--------|
| `cmake/scripts/windows/ArchSetup.cmake` | **None** | Already correct for Nexus |
| `cmake/scripts/windows/CFlagOverrides.cmake` | **None** | Unchanged |
| `cmake/scripts/windows/CXXFlagOverrides.cmake` | **None** | Unchanged |
| `tools/buildsteps/windows/make-mingwlibs.sh` | **None** | Already Nexus-ready |
| `tools/buildsteps/windows/buildffmpeg.sh` | **None** | Already Nexus-ready |
| `tools/buildsteps/windows/download-dependencies.bat` | **None** | Already correct |
| `tools/buildsteps/windows/bootstrap-addons.bat` | **None** | Already correct |
| `tools/buildsteps/windows/make-addons.bat` | **None** | Not called directly |
| `project/BuildDependencies/scripts/get_formed.cmd` | **None** | Already correct |
| `.github/workflows/build-windows.yaml` | **CREATE NEW** | Does not exist yet |

> **Conclusion**: All existing CMake and build scripts in the Nexus branch are
> already production-ready.  The **only deliverable is the new
> `.github/workflows/build-windows.yaml`** file, which orchestrates these
> existing scripts in the correct order for a GitHub Actions run.
