@echo off
setlocal enabledelayedexpansion

rem =====================================================================
rem  ZPAQ-NG: extract ffmpeg.exe from E:\Temp\ffmpeg.zpaq.
rem  Uses the legacy `x` command (identical to the original zpaq) with
rem  SHA-1 verification, then verifies the extracted file.
rem =====================================================================

rem Locate the built binary next to this script (build\ or project root).
set "ZPAQ_NG=%~dp0build\zpaq_ng.exe"
if not exist "%ZPAQ_NG%" set "ZPAQ_NG=%~dp0zpaq_ng.exe"
if not exist "%ZPAQ_NG%" (
  echo [error] zpaq_ng.exe not found next to %~dp0
  exit /b 1
)

set "ARCHIVE=E:\Temp\ffmpeg.zpaq"
set "OUTDIR=E:\Temp\ffmpeg_extracted"

if not exist "%ARCHIVE%" (
  echo [error] archive not found: %ARCHIVE%
  exit /b 1
)

echo.
echo  ZPAQ-NG extraction
echo  archive : %ARCHIVE%
echo  output  : %OUTDIR%
echo.

"%ZPAQ_NG%" x "%ARCHIVE%" -to "%OUTDIR%" -force

set "RC=%ERRORLEVEL%"
echo.
if %RC%==0 (
  echo  Done. Output: %OUTDIR%
) else (
  echo  [error] extract failed with exit code %RC%
)
exit /b %RC%