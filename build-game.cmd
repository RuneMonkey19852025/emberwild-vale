@echo off
rem Emberwild Vale - build the UE 5.8 project on Windows (Win64 Development).
rem
rem Usage:  build-game.cmd [editor|game|both]     (default: editor)
rem
rem Set EMBERWILD_ENGINE if the engine is not in the default launcher folder:
rem     set EMBERWILD_ENGINE=D:\Epic\UE_5.8
setlocal

set "PROJ=%~dp0"
if "%PROJ:~-1%"=="\" set "PROJ=%PROJ:~0,-1%"

if not defined EMBERWILD_ENGINE set "EMBERWILD_ENGINE=C:\Program Files\Epic Games\UE_5.8"
set "BUILD=%EMBERWILD_ENGINE%\Engine\Build\BatchFiles\Build.bat"
set "UPROJECT=%PROJ%\EmberwildVale.uproject"

if not exist "%BUILD%" (
  echo [ERROR] Unreal Engine not found at "%EMBERWILD_ENGINE%".
  echo         Install Unreal Engine 5.8 from the Epic Games Launcher, or set
  echo         EMBERWILD_ENGINE to the engine root ^(the folder containing Engine\^).
  exit /b 1
)
if not exist "%UPROJECT%" (
  echo [ERROR] EmberwildVale.uproject not found in "%PROJ%".
  exit /b 1
)

rem Dispatch with labels: %ERRORLEVEL% would be expanded too early inside an
rem if-block, so do not fold these into parenthesised blocks.
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=editor"

if /i "%TARGET%"=="editor" goto :build_editor
if /i "%TARGET%"=="game"   goto :build_game
if /i "%TARGET%"=="both"   goto :build_both

echo [ERROR] Unknown target "%TARGET%" - use: editor ^| game ^| both
exit /b 1

:build_editor
call :build EmberwildValeEditor
exit /b %ERRORLEVEL%

:build_game
call :build EmberwildVale
exit /b %ERRORLEVEL%

:build_both
call :build EmberwildValeEditor || exit /b 1
call :build EmberwildVale || exit /b 1
exit /b 0

:build
echo.
echo === Building %1  [Win64 Development] ===
call "%BUILD%" %1 Win64 Development -project="%UPROJECT%" -WaitMutex
if errorlevel 1 (
  echo [ERROR] Build FAILED for %1
  exit /b 1
)
echo [OK] %1 built.
exit /b 0
