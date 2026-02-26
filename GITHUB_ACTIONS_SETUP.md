# GitHub Actions Setup Guide: Building Kodi for Windows

This guide walks you through setting up GitHub Actions to build Kodi (XBMC) for Windows directly from your forked repository.

---

## Prerequisites

- A GitHub account
- Your Kodi/XBMC source code in a GitHub repository (fork or your own)
- The workflow files from this project (`.github/workflows/build-windows.yml`)

---

## Step-by-Step Setup

### Step 1: Ensure Your Repository Has the Workflow

The workflow file should be at:

```
.github/workflows/build-windows.yml
```

If you're setting this up from scratch:

1. Create the directory: `.github/workflows/`
2. Copy or create the `build-windows.yml` file in that directory

### Step 2: Push to Your GitHub Repository

1. **Commit the workflow file:**
   ```bash
   git add .github/workflows/build-windows.yml
   git commit -m "Add GitHub Actions workflow for Windows build"
   ```

2. **Push to your fork:**
   ```bash
   git push origin main
   ```
   *(Replace `main` with your default branch name if different: `master`, `Krypton`, etc.)*

### Step 3: Verify the Workflow Triggers

The workflow runs on:

- **Push** to branches: `main`, `master`, or `Krypton`
- **Pull requests** targeting those branches
- **Manual trigger** via the Actions tab

If your default branch has a different name, edit the workflow file and update the `on:` section:

```yaml
on:
  push:
    branches: [main, master, Krypton, YOUR_BRANCH_NAME]
  pull_request:
    branches: [main, master, Krypton, YOUR_BRANCH_NAME]
  workflow_dispatch:
```

### Step 4: Run the Build

**Option A: Automatic (on push)**  
Push any commit to the configured branches. The workflow will start automatically.

**Option B: Manual run**

1. Go to your repository on GitHub
2. Click the **Actions** tab
3. Select **Build Kodi (Windows)** in the left sidebar
4. Click **Run workflow**
5. Choose the branch and click **Run workflow**

### Step 5: Monitor the Build

1. In the **Actions** tab, click on the running or completed workflow run
2. Click on the **build-windows** job to see the steps
3. Expand any step to view its output
4. Build time is typically **30–60 minutes** (first run may be longer due to dependency downloads)

### Step 6: Download the Build Artifacts

When the build succeeds:

1. Scroll to the bottom of the workflow run page
2. Under **Artifacts**, you’ll see **Kodi-Windows-x86**
3. Click it to download a ZIP file containing:
   - `Release/` – Kodi.exe and required DLLs
   - `system/` – system DLLs (e.g. FFmpeg)

4. Extract the ZIP and run `Kodi.exe` from the `Release` folder (ensure `system` DLLs are on the same path or in a `system` subfolder as expected by Kodi)

---

## Workflow Overview

The workflow performs these steps:

| Step | Description |
|------|-------------|
| Checkout | Clones your repository |
| Setup MSYS2 | Installs MSYS2 with wget, tar, xz |
| Bootstrap wget | Copies wget to `BuildDependencies/bin` for dependency downloads |
| Create junction | Links `BuildDependencies/msys64` to MSYS2 install |
| Download build deps | Runs `DownloadBuildDeps.bat` (VC140 libraries from mirrors.kodi.tv) |
| Install MinGW libs | Installs FFmpeg, libdvdcss, libdvdnav, libdvdread via pacman |
| Prepare MinGW libs | Converts `.dll.a` to `.lib` and copies to build tree |
| Configure CMake | Generates Visual Studio 2022 project (Win32) |
| Build | Compiles Kodi with MSVC |
| Upload artifacts | Uploads the build output for download |

---

## Troubleshooting

### Build fails at "Download build dependencies"

- **Cause:** `DownloadBuildDeps.bat` needs `wget` in `project/BuildDependencies/bin/`
- **Fix:** The workflow copies wget from MSYS2. If it still fails, check that the "Bootstrap wget" step completed and that `project/BuildDependencies/bin/wget.exe` exists in the repo or is created by that step.

### Build fails at "Configure CMake"

- **Cause:** Visual Studio 2022 generator or architecture mismatch
- **Fix:** Try changing the generator:
  - For VS 2019: `-G "Visual Studio 16 2019" -A Win32`
  - Ensure the runner is `windows-latest` (VS 2022) or `windows-2019` (VS 2019)

### Build fails with linker errors (e.g. vc140, LNK2019)

- **Cause:** Prebuilt dependencies were built with VS 2015; newer MSVC may have ABI differences
- **Fix:** Try `windows-2019` and `"Visual Studio 16 2019"`. If it persists, the project may need dependency rebuilds for newer toolchains.

### "MSYS2 mingw32 not found"

- **Cause:** MSYS2 or MinGW packages did not install correctly
- **Fix:** Check the "Setup MSYS2" and "Install precompiled MinGW libraries" steps. Ensure `C:\msys64` exists and pacman completed without errors.

### Build timeout

- **Cause:** Default job timeout is 6 hours; the workflow sets 90 minutes
- **Fix:** Increase `timeout-minutes` in the workflow if needed. Consider caching dependencies (see below) to speed up builds.

### Kodi.exe fails to run after download

- **Cause:** Missing DLLs or incorrect layout
- **Fix:** Place all DLLs from `Release/` and `system/` in the same directory as `Kodi.exe`, or mirror the layout expected by Kodi (e.g. `system/` as a subfolder).

---

## Optional: Add Caching

To speed up repeated builds, add a cache step for dependencies. Insert this **after** "Checkout repository" and **before** "Setup MSYS2":

```yaml
      - name: Cache build dependencies
        uses: actions/cache@v4
        id: deps-cache
        with:
          path: |
            project/BuildDependencies/downloads
            project/BuildDependencies/lib
            project/BuildDependencies/include
          key: kodi-deps-${{ runner.os }}-${{ hashFiles('project/BuildDependencies/scripts/0_package.list') }}
          restore-keys: |
            kodi-deps-${{ runner.os }}-
```

Then wrap "Download build dependencies" and "Install precompiled MinGW libraries" with a conditional so they run only when the cache is not restored (e.g. using `steps.deps-cache.outputs.cache-hit`).

---

## Branch Names

If your repository uses different branch names, update the workflow:

```yaml
on:
  push:
    branches: [main, master, Krypton]   # Add or remove as needed
  pull_request:
    branches: [main, master, Krypton]
  workflow_dispatch:
```

---

## Summary Checklist

- [ ] `.github/workflows/build-windows.yml` exists in your repo
- [ ] Workflow triggers match your branch names
- [ ] Changes are committed and pushed to GitHub
- [ ] Build runs in the Actions tab (automatically or manually)
- [ ] Artifacts are downloaded after a successful build
- [ ] Kodi.exe runs correctly with the provided DLLs

---

## Additional Resources

- [GitHub Actions documentation](https://docs.github.com/en/actions)
- [Kodi build documentation](https://github.com/xbmc/xbmc/tree/master/docs)
- [MSYS2 setup action](https://github.com/msys2/setup-msys2)
