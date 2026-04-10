@ECHO OFF

SETLOCAL

IF "%WORKSPACE%"=="" (ECHO ERROR: WORKSPACE environment variable is not set & EXIT /B 1)

SET CUR_PATH=%WORKSPACE%\project\BuildDependencies
SET APP_PATH=%WORKSPACE%
SET TMP_PATH=%CUR_PATH%\scripts\tmp

SET MSYS_INSTALL_PATH="%CUR_PATH%\msys64"
SET MINGW_INSTALL_PATH="%CUR_PATH%\msys\mingw"

cd %CUR_PATH%

rem can't run rmdir and md back to back. access denied error otherwise.
IF EXIST %MSYS_INSTALL_PATH% rmdir %MSYS_INSTALL_PATH% /S /Q
IF EXIST %TMP_PATH% rmdir %TMP_PATH% /S /Q

IF "%~1"=="" (
  SET DL_PATH="%CD%\downloads2"
) ELSE (
  SET DL_PATH="%1"
)

SET WGET=%CUR_PATH%\bin\wget
SET ZIP=%CUR_PATH%\..\Win32BuildSetup\tools\7z\7za

IF NOT EXIST "%WGET%.exe" (ECHO ERROR: wget not found at %WGET%.exe & EXIT /B 1)
IF NOT EXIST "%ZIP%.exe" (ECHO ERROR: 7za not found at %ZIP%.exe & EXIT /B 1)

IF NOT EXIST %DL_PATH% md %DL_PATH%

IF NOT EXIST %MSYS_INSTALL_PATH% md %MSYS_INSTALL_PATH%
IF NOT EXIST %MINGW_INSTALL_PATH% md %MINGW_INSTALL_PATH%
IF NOT EXIST %TMP_PATH% md %TMP_PATH%

rem Install msys2 and mingw environment via DownloadMingwBuildEnv.bat
CALL "%CUR_PATH%\DownloadMingwBuildEnv.bat"
IF %ERRORLEVEL% NEQ 0 (ECHO ERROR: DownloadMingwBuildEnv.bat failed with exit code %ERRORLEVEL% & EXIT /B 1)

cd %CUR_PATH%

rem update fstab to install path
SET FSTAB=%MINGW_INSTALL_PATH%
SET FSTAB=%FSTAB:\=/%
SET FSTAB=%FSTAB:"=%
FINDSTR /C:"/mingw" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB% /mingw>>"%MSYS_INSTALL_PATH%\etc\fstab"
SET FSTAB2=%APP_PATH%
SET FSTAB2=%FSTAB2:\=/%
SET FSTAB2=%FSTAB2:"=%
FINDSTR /C:"/xbmc" "%MSYS_INSTALL_PATH%\etc\fstab" >NUL 2>&1 || ECHO %FSTAB2% /xbmc>>"%MSYS_INSTALL_PATH%\etc\fstab"

rem patch mingw headers to compile ffmpeg
IF EXIST mingw_support\postinstall\ (
  xcopy mingw_support\postinstall\* "%MSYS_INSTALL_PATH%\postinstall\" /E /Q /I /Y
  IF %ERRORLEVEL% NEQ 0 (ECHO ERROR: xcopy of mingw postinstall patches failed & EXIT /B 1)
  cd "%MSYS_INSTALL_PATH%\postinstall"
  IF EXIST pi_patches.bat (
    CALL pi_patches.bat
    IF %ERRORLEVEL% NEQ 0 (ECHO ERROR: pi_patches.bat failed & EXIT /B 1)
  )
) ELSE (
  ECHO WARNING: mingw_support\postinstall\ not found, skipping MinGW header patches.
)

cd %CUR_PATH%

rem insert call to vsvars32.bat in msys.bat
cd %MSYS_INSTALL_PATH%
IF NOT EXIST msys.bat_dist (Move msys.bat msys.bat_dist)
IF EXIST msys.bat del msys.bat
IF NOT EXIST "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  ECHO WARNING: vswhere.exe not found. Visual Studio environment will not be configured in msys.bat.
) ELSE (
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
)
IF NOT "%VSINSTALLDIR%"=="" (
  ECHO CALL "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars32.bat">>msys.bat
)
TYPE msys.bat_dist>>msys.bat

cd %CUR_PATH%

IF EXIST %TMP_PATH% rmdir %TMP_PATH% /S /Q

EXIT /B 0
