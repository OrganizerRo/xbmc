https://cursor.com/docs/cli/headless#file-modification--scripts


Here is a concise build guide for Kodi (XBMC) 17.6 Krypton on Windows:

---

# Building Kodi/XBMC for Windows – Guide

## Software to install

### 1. **Visual Studio 2015** (required)
- The project uses **Visual Studio 14 (2015)**.
- Install with **C++ desktop development**.
- `BuildSetup.bat` and `make-mingwlibs.bat` use `VS140COMNTOOLS`.

### 2. **CMake** (3.1 or newer)
- Add CMake to `PATH` so `cmake` works from a normal command prompt.

### 3. **Git for Windows**
- Used for branch detection and scripts.
- Also provides `bash` and `wget` if MSYS2 is not used.

### 4. **NSIS** (optional, for installer)
- Used to build the Windows installer.
- `BuildSetup.bat` looks for it in the registry.

### 5. **Java Runtime Environment**
- Needed for some addon build steps.

---

## Pre-build steps (first-time setup)

### Step 1: Put `wget` in the BuildDependencies bin folder
- Create: `projectBuildDependenciesbin`
- Download a Windows `wget` (e.g. from MSYS2 or a standalone build).
- Place `wget.exe` in `projectBuildDependenciesbin`.
- Both `DownloadBuildDeps.bat` and `DownloadMingwBuildEnv.bat` use this.

### Step 2: Download MSVC build dependencies
From the project root:

```batch
cd projectBuildDependencies
DownloadBuildDeps.bat
```

Downloads packages from `http://mirrors.kodi.tv` and extracts them. Uses `projectBuildDependenciesscripts�_package.list`.

### Step 3: Set up MSYS2/MinGW environment
From the project root:

```batch
cd projectBuildDependencies
DownloadMingwBuildEnv.bat
```

Downloads MSYS2 and installs toolchains/libraries for building FFmpeg and other MinGW libraries. On 32‑bit Windows it uses `msys32`, otherwise `msys64`.

---

## Main build

### Option A: Full automated build (recommended)
Run from `projectWin32BuildSetup`:

```batch
cd projectWin32BuildSetup
BuildSetup.bat
```

This will:

1. Build MinGW libs (FFmpeg, libdvd*, etc.) via MSYS2
2. Run CMake with `Visual Studio 14`
3. Build the Kodi exe
4. Build binary addons
5. Create the installer (if NSIS is installed)

**Arguments:**

- `BuildSetup.bat clean` – full rebuild (delete `kodi-build`)
- `BuildSetup.bat noclean` – incremental build
- `BuildSetup.bat noprompt` – no prompts
- `BuildSetup.bat nomingwlibs` – skip MinGW libs (assumes they are already built)
- `BuildSetup.bat nobinaryaddons` – skip addon build

### Option B: Manual build (CMake only)
If dependencies and MinGW libs are already set up:

```batch
mkdir kodi-build
cd kodi-build
cmake -G "Visual Studio 14" ..projectcmake
cmake --build . --config Release
```

Output exe:

`kodi-buildReleaseKodi.exe`

---

## Files and outputs

&#124; Location &#124; Purpose &#124;
&#124;----------&#124;---------&#124;
&#124; `projectWin32BuildSetupBuildSetup.bat` &#124; Main build script &#124;
&#124; `projectBuildDependenciesDownloadBuildDeps.bat` &#124; Download VC140 deps &#124;
&#124; `projectBuildDependenciesDownloadMingwBuildEnv.bat` &#124; Set up MSYS2/MinGW &#124;
&#124; `toolsbuildstepswin32make-mingwlibs.bat` &#124; Build FFmpeg etc. &#124;
&#124; `kodi-build` &#124; CMake build directory (created by BuildSetup) &#124;
&#124; `projectWin32BuildSetupBUILD_WIN32` &#124; Staging for installer &#124;
&#124; `projectWin32BuildSetupKodiSetup-*-x86.exe` &#124; Final installer &#124;

---

## Notes

1. **VS 2015 required**  
   The project is fixed to Visual Studio 14. Newer versions (2017/2019/2022) may need generator changes in `BuildSetup.bat`.

2. **Windows SDK**  
   Needed for HLSL shader compilation (`fxc.exe`). Install from the [Windows SDK archive](https://dev.windows.com/en-us/downloads/sdk-archive).

3. **AppVeyor CI shortcut**  
   In `appveyor.yml`, CI skips building MinGW libs and downloads prebuilt FFmpeg packages. For a local build, `BuildSetup.bat` builds them with `make-mingwlibs.bat`.

4. **Mirror**  
   Dependencies use `http://mirrors.kodi.tv`. You can override with `KODI_MIRROR` if needed.

5. **Git**  
   `getbranch.bat` uses `git` for branch/revision info; make sure Git is in `PATH`.