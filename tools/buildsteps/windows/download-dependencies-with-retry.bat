@echo off
setlocal EnableExtensions EnableDelayedExpansion

if not defined KODI_MIRRORS set "KODI_MIRRORS=https://mirrors.kodi.tv/"
if not defined KODI_DOWNLOAD_RETRIES set "KODI_DOWNLOAD_RETRIES=3"
if not defined KODI_RETRY_DELAY_SECONDS set "KODI_RETRY_DELAY_SECONDS=15"

set "MIRRORS=%KODI_MIRRORS%"
set "RETRIES=%KODI_DOWNLOAD_RETRIES%"
set "RETRY_DELAY=%KODI_RETRY_DELAY_SECONDS%"
set "DOWNLOAD_OK=0"

for %%M in (%MIRRORS%) do (
  if !DOWNLOAD_OK! EQU 0 (
    set "KODI_MIRROR=%%M"
    echo Trying KODI_MIRROR=!KODI_MIRROR!
    for /L %%R in (1,1,!RETRIES!) do (
      if !DOWNLOAD_OK! EQU 0 (
        echo Attempt %%R of !RETRIES! on !KODI_MIRROR!
        call tools\buildsteps\windows\download-dependencies.bat
        if not errorlevel 1 (
          set "DOWNLOAD_OK=1"
        ) else (
          echo Download failed on !KODI_MIRROR! attempt %%R
          if not %%R==!RETRIES! timeout /t !RETRY_DELAY! /nobreak >nul
        )
      )
    )
  )
)

if !DOWNLOAD_OK! EQU 1 (
  echo Build dependencies downloaded successfully.
  exit /b 0
)

echo ERROR: Failed to download build dependencies from all configured Kodi mirrors.
exit /b 1
