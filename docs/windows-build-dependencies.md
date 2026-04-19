# Kodi Leia (v18) — Windows Build Dependencies & Runtime Requirements

This document lists every DLL and runtime dependency that **kodi.exe** (Win32 / x86) needs in order to run,
where each file should be placed relative to the executable, and where it comes from during the CI build.

> **Source**: successful GitHub Actions build run for the `Leia` branch
> (`build-windows.yaml`), CMake configure log, and the dependency packages
> downloaded from `mirrors.kodi.tv/build-deps/win32/`.

---

## 1. Directory layout expected at runtime

Kodi resolves paths via `special://` protocol. On Windows:

| Special path | Resolves to |
|---|---|
| `special://xbmc/` | Directory containing `kodi.exe` |
| `special://xbmcbin/` | Same as `special://xbmc/` on Win32 |

So the expected directory tree next to `kodi.exe` is:

```
<app_root>/
├── kodi.exe
├── *.dll                        ← runtime dependency DLLs (see §2)
├── *.jar                        ← libbluray Java menu support
├── libdvdnav.dll                ← DVD navigation (built from source)
├── system/
│   ├── Python/                  ← Python 2.7 runtime
│   │   ├── DLLs/
│   │   ├── Lib/
│   │   └── Lib/site-packages/
│   ├── certs/                   ← TLS certificates
│   ├── keymaps/
│   ├── keyboardlayouts/
│   ├── library/
│   ├── settings/
│   ├── shaders/                 ← DirectX HLSL shaders
│   ├── addon-manifest.xml
│   ├── colors.xml
│   ├── peripherals.xml
│   ├── playercorefactory.xml
│   ├── IRSSmap.xml
│   └── X10-Lola-IRSSmap.xml
├── addons/                      ← built-in + binary add-ons
│   ├── skin.estuary/            (or skin.estouchy)
│   ├── metadata.themoviedb.org.python/
│   ├── pvr.*/                   ← PVR binary add-ons
│   ├── audiodecoder.*/
│   ├── inputstream.*/
│   └── ...
├── media/                       ← splash screen, icons, fonts
├── userdata/                    ← default user-data templates
└── sounds/                      ← UI sound resources (if present)
```

---

## 2. DLLs that must be beside kodi.exe

These are loaded at startup or at runtime from `special://xbmcbin/`.

### 2a. Pre-built dependency DLLs (from mirrors.kodi.tv packages)

The Leia dependency packages extract their DLLs into the source-tree's `system/` directory.
CMake's `export-files` target copies them to the build-tree directory beside `kodi.exe`.

| DLL | Source package | Purpose |
|---|---|---|
| `python27.dll` | python-2.7.13-win32-vc140 | Python 2.7 runtime (statically linked via `python27.lib`) |
| `sqlite3.dll` | sqlite-3.10.2-win32-vc140 | SQLite database (**delay-loaded**) |
| `dnssd.dll` | dnssd-765.50.9-win32-vc140 | Bonjour / mDNS discovery (**delay-loaded**) |
| `libxslt.dll` | libxslt-1.1.29-win32-vc140 | XSLT transforms (**delay-loaded**) |
| `libxml2.dll` | libxml2-2.9.4-win32-vc140 | XML parsing |
| `libcurl.dll` | curl-7.59.0-Win32-v140 | HTTP / network |
| `libeay32.dll` | openssl-1.0.2g-win32-vc140 | OpenSSL crypto |
| `ssleay32.dll` | openssl-1.0.2g-win32-vc140 | OpenSSL SSL/TLS |
| `libmysql.dll` | mysql-connector-c-6.1.6-win32-vc140 | MySQL connector |
| `libmicrohttpd-dll.dll` | libmicrohttpd-0.9.55-win32-vc140 | Embedded web server |
| `libbluray.dll` | libbluray-1.0.2-win32-vc140 | Blu-ray disc playback |
| `cec.dll` | libcec-4.0.2-Win32-v141 | HDMI-CEC control |
| `nfs.dll` | libnfs-3.0.0-Win32-v141 | NFS file access |
| `libass.dll` | libass-d18a5f1-win32-vc140 | ASS/SSA subtitle rendering |
| `freetype6.dll` | freetype-db5a22-win32-vc140 | Font rendering |
| `libfribidi.dll` | libfribidi-0.19.2-win32 | BiDi text layout |
| `libcdio.dll` | libcdio-0.9.3-win32-vc140 | CD-ROM access |
| `libiconv.dll` | libiconv-1.14-win32-vc140 | Character encoding conversion |
| `zlib1.dll` | zlib-1.2.8-win32-vc140 | Compression (delay-loaded as `zlib.dll`) |
| `libpng16.dll` | libpng-1.6.21-win32-vc140 | PNG image decoding |
| `jpeg62.dll` / `turbojpeg.dll` | libjpeg-turbo-1.4.90-win32-vc140 | JPEG image decoding |
| `giflib.dll` | giflib-5.1.4-win32-vc140 | GIF image decoding |
| `expat.dll` | expat-2.2.0-win32-vc140 | XML parsing (Expat) |
| `pcre.dll` / `pcrecpp.dll` | pcre-8.37-win32-vc140 | Regular expressions |
| `CrossGuid.dll` | crossguid-8f399e-win32-vc140 | GUID generation |
| `tag.dll` | taglib-1.11.1-win32-vc140 | Audio tag metadata |
| `tinyxmlSTL.dll` | tinyxmlstl-2.6.2-win32-vc140 | TinyXML with STL |
| `lcms2.dll` | lcms2-2.8-win32-vc140 | ICC color management |
| `fstrcmp.dll` | fstrcmp-0.7-Win32-v141 | Fuzzy string comparison |
| `shairplay.dll` | shairplay-0.9.0-win32-vc140 | AirPlay support |
| `libplist.dll` | libplist-1.13.0-win32-vc140 | Apple plist parsing |
| `lzo2.dll` | lzo-2.09-win32-vc140 | LZO compression |
| `EasyHook32.dll` | easyhook-2.7.5870.0-win32-vc140 | DLL injection / hooking |
| `libyajl.dll` | libyajl-2.0.1-win32 | JSON parsing |
| `libssh.dll` | libssh-0.7.0-win32-vc140 | SSH/SFTP file access |

### 2b. MinGW-built DLLs (built from source in CI)

| DLL | Source | Purpose |
|---|---|---|
| `libdvdnav.dll` | Built via `make-mingwlibs.sh` → `buildlibdvd.sh` | DVD navigation (`DllPaths_win32.h`) |

> **Note**: FFmpeg is built with `--disable-shared` (static linking), so there are no FFmpeg DLLs to ship.

### 2c. Delay-loaded DLLs

Defined in `cmake/scripts/windows/ArchSetup.cmake`:

```
libmariadb.dll  libxslt.dll  dnssd.dll  dwmapi.dll  sqlite3.dll  d3dcompiler_47.dll
```

These are loaded on demand. Kodi won't crash if they're absent, but the corresponding feature will be unavailable.

### 2d. Windows system DLLs (linked, not shipped)

From `ArchSetup.cmake` `DEPLIBS`:

```
bcrypt  d3d11.dll  DInput8.dll  DSound.dll  winmm.dll  Mpr.dll
Iphlpapi.dll  ws2_32.dll  PowrProf.dll  setupapi.dll  Shlwapi.dll
dwmapi.dll  dxguid.dll  RuntimeObject.dll  DelayImp.lib
```

These are provided by Windows itself and do not need to be shipped.

### 2e. Visual C++ Redistributable

Kodi links against the MSVC 2015+ runtime. Users must install the
**Visual C++ Redistributable for Visual Studio 2015–2022 (x86)** which provides:

- `msvcp140.dll`
- `vcruntime140.dll`
- `ucrtbase.dll`

---

## 3. Python 2.7 runtime

Kodi Leia embeds Python 2.7. The distribution is expected at `system/Python/` relative
to `kodi.exe` (see `xbmc/platform/win32/PlatformWin32.cpp`):

```
system/Python/
├── DLLs/        ← compiled extension modules (.pyd)
├── Lib/         ← standard library
└── Lib/site-packages/
    ├── PIL/     ← Pillow (from pillow-3.1.0-win32-vc140.7z)
    └── Crypto/  ← PyCryptodome (from pycryptodome-3.4.3-win32.7z)
```

The `python27.dll` itself must be beside `kodi.exe` (not inside `system/Python/`).

---

## 4. Data directories

| Directory | Source (cmake installdata) | Contents |
|---|---|---|
| `system/keymaps/` | `common/common.txt` | Keyboard & remote mappings |
| `system/settings/` | `common/common.txt` | Settings definitions XML |
| `system/shaders/` | `common/common.txt` | DirectX HLSL shaders |
| `system/library/` | `common/common.txt` | Smart playlist templates |
| `system/keyboardlayouts/` | `common/common.txt` | Keyboard layouts |
| `system/certs/` | `common/certificates.txt` | TLS CA certificates |
| `addons/` | `common/addons.txt` | Built-in addons (skins, scrapers, etc.) |
| `media/` | `common/common.txt` | Splash screen, icons, fonts |
| `userdata/` | `common/common.txt` | Default user config templates |

---

## 5. How the CI build assembles these

### 5a. Dependency packages (mirrors.kodi.tv)

`tools/buildsteps/windows/download-dependencies.bat` downloads packages listed in
`project/BuildDependencies/scripts/0_package.list` and extracts them to
`project/BuildDependencies/win32/`.

The **Leia-era packages** use this internal structure:
- `lib/` — static/import libraries (`.lib`)
- `include/` — headers
- `system/` — runtime DLLs (extracted to source tree's `system/`)
- `project/Win32BuildSetup/dependencies/` — `python27.dll` specifically
- `bin/Python/` — Python 2.7 runtime directory

### 5b. CMake export-files target

`CMakeLists.txt` calls `copy_files_from_filelist_to_buildtree()` which reads
`cmake/installdata/windows/dlls.txt` and `cmake/installdata/windows/python.txt`.

On Windows, files are copied to `$<TARGET_FILE_DIR:kodi>` (= `kodi-build/RelWithDebInfo/`
for Visual Studio multi-config generators).

### 5c. Packaging

The "Collect build output" step copies from the build tree to a staging directory.
The "Package release zip" step creates `kodi-windows-x86.zip` from the staging directory.

---

## 6. Known issues and fixes (as of this writing)

| Issue | Root cause | Fix |
|---|---|---|
| `python27.dll` missing from release zip | `cmake/installdata/windows/dlls.txt` uses newer `bin/*.dll` path but Leia packages put DLLs in `system/` | Updated `dlls.txt` to use Leia paths |
| All dependency DLLs missing | Same as above | Same fix |
| `system/` directory not preserved in zip | `Compress-Archive -Path "system\*"` flattens the container | Changed packaging to use proper staging directory |
| `media/`, `userdata/` missing | "Collect" step only copied `RelWithDebInfo/` content | Fixed to copy full build tree output |
| Python directory missing | `python.txt` expected `bin/Python` but Leia packages use different path | Updated `python.txt` for Leia format |
