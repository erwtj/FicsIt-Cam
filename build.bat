@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ================================================================
REM FicsIt-Cam local build helper
REM
REM 1) Update the USER CONFIG section below.
REM 2) Run from any terminal: build.bat
REM
REM Optional flags:
REM   build.bat --with-editor   (also build Development Editor)
REM   build.bat --no-package    (skip PackagePlugin)
REM ================================================================

REM -------------------- USER CONFIG --------------------
set "UE_ROOT=C:\ue4"
set "SML_ROOT=C:\SatisfactoryModLoader"
set "MOD_NAME=FicsItCam"
REM ----------------------------------------------------

set "BUILD_EDITOR=0"
set "DO_PACKAGE=1"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--with-editor" (
  set "BUILD_EDITOR=1"
  shift
  goto parse_args
)
if /I "%~1"=="--no-package" (
  set "DO_PACKAGE=0"
  shift
  goto parse_args
)
echo [WARN] Unknown argument: %~1
shift
goto parse_args

:args_done
set "UPROJECT=%SML_ROOT%\FactoryGame.uproject"
set "SLN=%SML_ROOT%\FactoryGame.sln"
set "UBT=%UE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
set "UAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"

echo.
echo === FicsIt-Cam Build Script ===
echo UE_ROOT   = %UE_ROOT%
echo SML_ROOT  = %SML_ROOT%
echo MOD_NAME  = %MOD_NAME%
echo WITH_EDITOR = %BUILD_EDITOR%
echo PACKAGE     = %DO_PACKAGE%
echo.

if not exist "%UE_ROOT%" (
  echo [ERROR] UE_ROOT does not exist: %UE_ROOT%
  exit /b 1
)
if not exist "%SML_ROOT%" (
  echo [ERROR] SML_ROOT does not exist: %SML_ROOT%
  exit /b 1
)
if not exist "%UPROJECT%" (
  echo [ERROR] Missing project file: %UPROJECT%
  echo [HINT] This repo is a plugin, not a full project.
  echo [HINT] Put it under: %SML_ROOT%\Mods\FicsItCam
  exit /b 1
)
if not exist "%UBT%" (
  echo [ERROR] UnrealBuildTool not found: %UBT%
  exit /b 1
)
if not exist "%UAT%" (
  echo [ERROR] RunUAT not found: %UAT%
  exit /b 1
)

where MSBuild.exe >nul 2>&1
if errorlevel 1 (
  echo [ERROR] MSBuild.exe not found in PATH.
  echo [HINT] Run from a Visual Studio Developer Command Prompt.
  exit /b 1
)

echo [1/4] Generating project files...
"%UBT%" -projectfiles -project="%UPROJECT%" -game -rocket -progress
if errorlevel 1 (
  echo [ERROR] Project file generation failed.
  exit /b 1
)

echo [2/4] Building Shipping Win64...
MSBuild.exe "%SLN%" /p:CL_MPCount=5 /p:Configuration="Shipping" /p:Platform="Win64" /t:"Games\FactoryGame"
if errorlevel 1 (
  echo [ERROR] Shipping build failed.
  exit /b 1
)

if "%BUILD_EDITOR%"=="1" (
  echo [3/4] Building Development Editor Win64...
  MSBuild.exe "%SLN%" /p:CL_MPCount=5 /p:Configuration="Development Editor" /p:Platform="Win64" /t:"Games\FactoryGame"
  if errorlevel 1 (
    echo [ERROR] Development Editor build failed.
    exit /b 1
  )
) else (
  echo [3/4] Skipping Development Editor build.
)

if "%DO_PACKAGE%"=="1" (
  echo [4/4] Packaging plugin with UAT...
  "%UAT%" -ScriptsForProject="%UPROJECT%" PackagePlugin -project="%UPROJECT%" -clientconfig=Shipping -serverconfig=Shipping -utf8output -DLCName="%MOD_NAME%" -build -platform=Win64 -nocompileeditor
  if errorlevel 1 (
    echo [ERROR] Packaging failed.
    exit /b 1
  )
  echo [OK] Done.
  echo [INFO] Zip should be at:
  echo        %SML_ROOT%\Saved\ArchivedPlugins\%MOD_NAME%\%MOD_NAME%-Windows.zip
) else (
  echo [4/4] Packaging skipped (--no-package).
  echo [OK] Build complete.
)

exit /b 0
