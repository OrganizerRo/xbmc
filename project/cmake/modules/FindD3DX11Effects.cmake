# - Finds D3DX11 dependencies
# Once done this will define
#
# D3DCOMPILER_DLL - Path to the Direct3D Compiler
# FXC - Path to the DirectX Effects Compiler (FXC)

# Glob for the standard Windows Kits Redist path (works without env vars)
file(GLOB _D3D_REDIST_DIRS
     "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x86")

find_file(D3DCOMPILER_DLL
          NAMES d3dcompiler_47.dll d3dcompiler_46.dll
          PATHS
            "$ENV{WindowsSdkDir}Redist/D3D/x86"
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0;InstallationFolder]/Redist/D3D/x86"
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v8.1;InstallationFolder]/Redist/D3D/x86"
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v8.0;InstallationFolder]/Redist/D3D/x86"
            ${_D3D_REDIST_DIRS}
          NO_DEFAULT_PATH)
if(NOT D3DCOMPILER_DLL)
  message(WARNING "Could NOT find Direct3D Compiler")
endif()
mark_as_advanced(D3DCOMPILER_DLL)
copy_file_to_buildtree(${D3DCOMPILER_DLL} DIRECTORY .)

# On CI runners (and outside VS Developer Command Prompt) the WindowsSdk*
# environment variables are typically unset, so also search the standard
# Windows Kits install location via a glob to pick up any SDK version.
file(GLOB _FXC_SDK_BIN_DIRS
     "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x86"
     "C:/Program Files (x86)/Windows Kits/10/bin/x86")
# Sort descending so the newest SDK version is found first
list(SORT _FXC_SDK_BIN_DIRS ORDER DESCENDING)

find_program(FXC fxc
             PATHS
               "$ENV{WindowsSdkVerBinPath}x86"
               "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0;InstallationFolder]/bin/$ENV{WindowsSDKVersion}x86"
               "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0;InstallationFolder]/bin/x86"
               "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v8.1;InstallationFolder]/bin/x86"
               "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v8.0;InstallationFolder]/bin/x86"
               "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x86"
               "$ENV{WindowsSdkDir}bin/x86"
               ${_FXC_SDK_BIN_DIRS})
if(NOT FXC)
  message(WARNING "Could NOT find DirectX Effects Compiler (FXC)")
endif()
mark_as_advanced(FXC)
