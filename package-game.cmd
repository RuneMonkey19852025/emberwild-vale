@echo off
rem Emberwild Vale - package the UE5 game for Windows (staged + pak'd).
rem Output: Dist\Windows\EmberwildVale\EmberwildVale.exe
rem Set EMBERWILD_ENGINE if the engine is not in the default launcher folder.
setlocal

set "PROJ=%~dp0"
if "%PROJ:~-1%"=="\" set "PROJ=%PROJ:~0,-1%"

if not defined EMBERWILD_ENGINE set "EMBERWILD_ENGINE=C:\Program Files\Epic Games\UE_5.8"
set "UAT=%EMBERWILD_ENGINE%\Engine\Build\BatchFiles\RunUAT.bat"
set "LOG=%TEMP%\emberwild-package.log"

if not exist "%UAT%" (
  echo [ERROR] Unreal Engine not found at "%EMBERWILD_ENGINE%".
  echo         Install Unreal Engine 5.8 from the Epic Games Launcher, or set
  echo         EMBERWILD_ENGINE to the engine root ^(the folder containing Engine\^).
  exit /b 1
)
if not exist "%PROJ%\EmberwildVale.uproject" (
  echo [ERROR] EmberwildVale.uproject not found in "%PROJ%".
  exit /b 1
)

echo packaging to "%PROJ%\Dist\Windows"  (log: %LOG%)
call "%UAT%" BuildCookRun -project="%PROJ%\EmberwildVale.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Development -target=EmberwildVale ^
  -build -cook -stage -pak -compressed -archive ^
  -archivedirectory="%PROJ%\Dist" ^
  -unattended -utf8output > "%LOG%" 2>&1

findstr /C:"AutomationTool exiting with ExitCode(0)" /C:"BUILD SUCCESSFUL" "%LOG%" >nul
if errorlevel 1 (
  echo [ERROR] PACKAGE FAILED - see %LOG%
  findstr /C:"Error:" /C:"Fatal" "%LOG%"
  exit /b 1
)

echo [OK] PACKAGE OK at "%PROJ%\Dist\Windows\EmberwildVale"
exit /b 0
