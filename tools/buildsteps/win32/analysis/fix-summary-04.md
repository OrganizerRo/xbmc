# Fix Summary: make-mingwlibs.bat + download-msys.bat + find-vs.bat

## New file: find-vs.bat
- **What:** Shared vswhere helper that sets VSINSTALLDIR and VSMSBUILD
- **Why:** VS140COMNTOOLS is only set by VS 2015 installations; vswhere supports VS 2017+

## make-mingwlibs.bat

### BUG-01 — VS2015 → vswhere detection (~line 28)
- **Before:** `call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat" %vcarch% || exit /b 1`
- **After:** `CALL "%~dp0find-vs.bat" || EXIT /B 1` followed by `call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" %vcarch% || exit /b 1`
- **Why:** VS140COMNTOOLS not set on VS 2017+ builds.

### BUG-03 — start /wait for mintty (~line 57)
- **Before:** `%WORKDIR%\project\BuildDependencies\%msys2%\usr\bin\mintty.exe -d -i /msys2.ico /usr/bin/bash --login /xbmc/tools/buildsteps/win32/make-mingwlibs.sh --prompt=%PROMPTLEVEL% --mode=%BUILDMODE% --build32=%build32% --build64=%build64% --tools=%tools%`
- **After:** `start /wait %WORKDIR%\project\BuildDependencies\%msys2%\usr\bin\mintty.exe -d -i /msys2.ico /usr/bin/bash --login /xbmc/tools/buildsteps/win32/make-mingwlibs.sh --prompt=%PROMPTLEVEL% --mode=%BUILDMODE% --build32=%build32% --build64=%build64% --tools=%tools%`
- **Why:** Without /wait, the batch script returns immediately and exit code checking is always wrong.

## download-msys.bat

### BUG-04 — msys → msys64 path (~line 9)
- **Before:** `SET MSYS_INSTALL_PATH="%CUR_PATH%\msys"`
- **After:** `SET MSYS_INSTALL_PATH="%CUR_PATH%\msys64"`
- **Why:** Path mismatch with make-mingwlibs.bat which expects msys64.

### BUG-01 — VS2015 → vswhere in msys.bat patch (~line 61)
- **Before:** `ECHO CALL "%VS140COMNTOOLS%\..\..\VC\bin\vcvars32.bat">>msys.bat`
- **After:**
  ```bat
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
  IF NOT "%VSINSTALLDIR%"=="" (
    ECHO CALL "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars32.bat">>msys.bat
  )
  ```
- **Why:** VS140COMNTOOLS not set on VS 2017+ builds.

### BUG-03 — fstab dedup guards (~lines 45-49)
- **Before:**
  ```bat
  ECHO %FSTAB% /mingw>>"%MSYS_INSTALL_PATH%\etc\fstab"
  SET FSTAB=%APP_PATH%
  SET FSTAB=%FSTAB:\=/%
  SET FSTAB=%FSTAB:"=%
  ECHO %FSTAB% /xbmc>>"%MSYS_INSTALL_PATH%\etc\fstab"
  ```
- **After:**
  ```bat
  FINDSTR /C:"/mingw" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB% /mingw>>"%MSYS_INSTALL_PATH%\etc\fstab"
  SET FSTAB2=%APP_PATH%
  SET FSTAB2=%FSTAB2:\=/%
  SET FSTAB2=%FSTAB2:"=%
  FINDSTR /C:"/xbmc" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB2% /xbmc>>"%MSYS_INSTALL_PATH%\etc\fstab"
  ```
- **Why:** Running the script multiple times would append duplicate fstab entries.

## Files Changed
- `tools/buildsteps/win32/find-vs.bat` (new)
- `tools/buildsteps/win32/make-mingwlibs.bat`
- `tools/buildsteps/win32/download-msys.bat`
