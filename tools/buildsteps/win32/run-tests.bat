@ECHO OFF
SETLOCAL ENABLEDELAYEDEXPANSION
REM setup all paths
IF NOT DEFINED WORKSPACE SET WORKSPACE=%~dp0..\..
SET cur_dir=%WORKSPACE%\project\Win32BuildSetup
cd %cur_dir%
SET base_dir=%cur_dir%\..\..
SET builddeps_dir=%cur_dir%\..\..\project\BuildDependencies
SET msys_dir=%builddeps_dir%\msys64
IF NOT EXIST %msys_dir% (SET msys_dir=%builddeps_dir%\msys32)
SET awk_exe=%msys_dir%\usr\bin\awk.exe
SET sed_exe=%msys_dir%\usr\bin\sed.exe
IF NOT EXIST "%awk_exe%" (
  set DIETEXT=awk not found at %awk_exe% - run download-msys.bat first
  goto DIE
)

REM read the version values from version.txt
FOR /f %%i IN ('%awk_exe% "/APP_NAME/ {print $2}" %base_dir%\version.txt') DO SET APP_NAME=%%i

TITLE %APP_NAME% testsuite Build-/Runscript

rem -------------------------------------------------------------
rem  CONFIG START
SET exitcode=0
SET useshell=sh
SET BRANCH=na
SET buildconfig=Debug Testsuite
REM WORKSPACE is set at script entry via the IF NOT DEFINED guard above


  REM look for MSBuild.exe using vswhere
  FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET NET="%%i"
  IF NOT DEFINED NET (
    set DIETEXT=MSBuild was not found.
    goto DIE
  )

  set msbuildemitsolution=1
  IF NOT EXIST "..\cmake\kodi.sln" (
    set DIETEXT=Solution file not found at ..\cmake\kodi.sln - run CMake configure first
    goto DIE
  )
  set OPTS_EXE="..\cmake\kodi.sln" /t:Build /p:Configuration="%buildconfig%" /m
  set CLEAN_EXE="..\cmake\kodi.sln" /t:Clean /p:Configuration="%buildconfig%"

  set EXE="..\cmake\kodi\%buildconfig%\%APP_NAME%-test.exe"
  set PDB="..\cmake\kodi\%buildconfig%\%APP_NAME%.pdb"
  
  :: sets the BRANCH env var
  call getbranch.bat

  rem  CONFIG END
  rem -------------------------------------------------------------

echo Building %buildconfig%
IF EXIST buildlog.html del buildlog.html /q

ECHO Compiling testsuite...
%NET% %OPTS_EXE%

IF %errorlevel% NEQ 0 (
  set DIETEXT="%APP_NAME%-test.exe failed to build!  See %CD%\..\cmake\kodi\%buildconfig%\objs\XBMC.log"
  type "%CD%\..\cmake\kodi\%buildconfig%\objs\XBMC.log"
  goto DIE
)
ECHO Done building!
ECHO ------------------------------------------------------------

:RUNTESTSUITE
ECHO Running testsuite...
  cd %WORKSPACE%\project\cmake\
  set KODI_HOME=%WORKSPACE%
  set PATH=%WORKSPACE%\system;%PATH%

  %EXE% --gtest_output=xml:%WORKSPACE%\gtestresults.xml
  IF %errorlevel% NEQ 0 SET exitcode=%errorlevel%

  rem Adapt gtest xml output to be conform with junit xml
  rem this basically looks for lines which have "notrun" in the <testcase /> tag
  rem and adds a <skipped/> subtag into it. For example:
  rem <testcase name="IsStarted" status="notrun" time="0" classname="TestWebServer"/>
  rem becomes
  rem <testcase name="IsStarted" status="notrun" time="0" classname="TestWebServer"><skipped/></testcase>
  IF NOT EXIST "%sed_exe%" (
    ECHO WARNING: sed not found at %sed_exe%, skipping JUnit XML transform
    GOTO SKIPSED
  )
  %sed_exe% "s/<testcase\(.*\)\"notrun\"\(.*\)\/>$/<testcase\1\"notrun\"\2><skipped\/><\/testcase>/" %WORKSPACE%\gtestresults.xml > %WORKSPACE%\gtestresults-skipped.xml
  del %WORKSPACE%\gtestresults.xml
  move %WORKSPACE%\gtestresults-skipped.xml %WORKSPACE%\gtestresults.xml
:SKIPSED
ECHO Done running testsuite!
ECHO ------------------------------------------------------------
GOTO END

:DIE
  ECHO ------------------------------------------------------------
  ECHO !-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-
  ECHO    ERROR ERROR ERROR ERROR ERROR ERROR ERROR ERROR ERROR
  ECHO !-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-!-
  set DIETEXT=ERROR: %DIETEXT%
  echo %DIETEXT%
  SET exitcode=1
  ECHO ------------------------------------------------------------

:END
  EXIT /B %exitcode%
