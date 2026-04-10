@ECHO OFF

SETLOCAL ENABLEDELAYEDEXPANSION

IF "%WORKSPACE%"=="" (ECHO ERROR: WORKSPACE environment variable is not set & EXIT /B 1)

SET CUR_PATH=%WORKSPACE%\project\BuildDependencies
SET TMP_PATH=%WORKSPACE%\project\BuildDependencies\scripts\tmp
SET APP_PATH=%WORKSPACE%

cd %CUR_PATH%

rem can't run rmdir and md back to back. access denied error otherwise.
IF EXIST lib rmdir lib /S /Q
IF EXIST include rmdir include /S /Q
IF EXIST %TMP_PATH% rmdir %TMP_PATH% /S /Q

IF "%~1"=="" (
  SET DL_PATH="%CD%\downloads"
) ELSE (
  SET DL_PATH="%1"
)

SET WGET=%CUR_PATH%\bin\wget
SET ZIP=%CUR_PATH%\..\Win32BuildSetup\tools\7z\7za

IF NOT EXIST "%WGET%.exe" (ECHO ERROR: wget not found at %WGET%.exe & EXIT /B 1)
IF NOT EXIST "%ZIP%.exe" (ECHO ERROR: 7za not found at %ZIP%.exe & EXIT /B 1)

IF NOT EXIST %DL_PATH% md %DL_PATH%

IF NOT EXIST lib md lib
IF NOT EXIST include md include
IF NOT EXIST %TMP_PATH% md %TMP_PATH%

cd scripts

FOR /F "tokens=*" %%S IN ('dir /B "*_d.bat"') DO (
  echo running %%S ...
  CALL %%S
  IF !ERRORLEVEL! NEQ 0 (
    ECHO ERROR: %%S failed with exit code !ERRORLEVEL!
    EXIT /B 1
  )
)

cd %CUR_PATH%

rmdir %TMP_PATH% /S /Q

EXIT /B 0