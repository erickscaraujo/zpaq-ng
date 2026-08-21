@echo off
setlocal enabledelayedexpansion

rem =====================================================================
rem  ZPAQ-NG: compress ffmpeg.exe with maximum compression (ng9).
rem  Uses the next-generation `create` command with the correct number of
rem  threads for this machine (--threads auto -> hardware concurrency),
rem  content-defined chunking (--dedup), SHA-1 verification (--verify) and
rem  verbose progress. Archives remain readable by the original zpaq.
rem =====================================================================

rem Locate the built binary next to this script (build\ or project root).
set "ZPAQ_NG=%~dp0build\zpaq_ng.exe"
if not exist "%ZPAQ_NG%" set "ZPAQ_NG=%~dp0zpaq_ng.exe"
if not exist "%ZPAQ_NG%" (
  echo [error] zpaq_ng.exe not found next to %~dp0
  exit /b 1
)

set "ARCHIVE=E:\Temp\ffmpeg.zpaq"
set "INPUT=E:\Temp\ffmpeg.exe"

if not exist "%INPUT%" (
  echo [error] input not found: %INPUT%
  exit /b 1
)

echo.
echo  ZPAQ-NG maximum compression (ng9, all cores)
echo  archive : %ARCHIVE%
echo  input   : %INPUT%
echo.

"%ZPAQ_NG%" create "%ARCHIVE%" "%INPUT%" ^
  --level ng9 ^
  --threads auto ^
  --device cpu ^
  --dedup ^
  --verify ^
  --verbose

set "RC=%ERRORLEVEL%"
echo.
if %RC%==0 (
  echo  Done. Output: %ARCHIVE%
) else (
  echo  [error] create failed with exit code %RC%
)
exit /b %RC%