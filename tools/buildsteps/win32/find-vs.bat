@ECHO OFF
REM find-vs.bat — sets VSINSTALLDIR and VSMSBUILD via vswhere
SET "_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST "%_VSWHERE%" (ECHO vswhere.exe not found at "%_VSWHERE%" & EXIT /B 1)
FOR /F "usebackq tokens=*" %%i IN (`"%_VSWHERE%" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
FOR /F "usebackq tokens=*" %%i IN (`"%_VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET VSMSBUILD=%%i
IF "%VSINSTALLDIR%"=="" (ECHO Visual Studio not found & EXIT /B 1)
IF "%VSMSBUILD%"=="" (ECHO MSBuild not found in Visual Studio installation & EXIT /B 1)
