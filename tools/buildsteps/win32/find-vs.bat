@ECHO OFF
REM find-vs.bat — sets VSINSTALLDIR and VSMSBUILD via vswhere
FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSINSTALLDIR=%%i
FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO SET VSMSBUILD=%%i
IF "%VSINSTALLDIR%"=="" (ECHO Visual Studio not found & EXIT /B 1)
