@echo off
rem ---------------------------------------------------------------
rem  pyocd_ui - one-click MSI build (deploy + WiX)
rem
rem  Usage:
rem      package.bat              deploy only (do not rebuild the exe)
rem      package.bat 1.0.0        with version number
rem      package.bat 1.0.0 build  rebuild the exe first, then package
rem
rem  Flow:
rem      [1/2] deploy.ps1    build\pyocd_ui.exe -> installer\
rem                           (+ windeployqt + bundled pyocd-windows-<ver>\)
rem      [2/2] build_msi.ps1 installer\ -> output\pyocd_ui-<ver>-x64.msi
rem
rem  The installer ships the bundled pyocd.exe, so the installed app
rem  works without any mamba/conda/Python environment.
rem
rem  Dependencies (all auto-detected, normally no manual setup):
rem      powershell.exe           built into Windows (-ExecutionPolicy Bypass, no admin)
rem      build\CMakeCache.txt     produced by build.bat / qt-cmake (gives the Qt path)
rem      windeployqt.exe          from CMakeCache Qt6_DIR, or PATH / C:\Qt / D:\Software_Installed
rem      robocopy.exe             built into Windows (copies the bundled pyocd folder)
rem      objdump.exe              MinGW binutils, fills DLLs windeployqt misses (warning only)
rem      wix.exe                  WiX Toolset v4+, PATH or %ProgramFiles%\WiX Toolset v*\bin
rem      WixToolset.UI.wixext     first run may need network, then cached
rem ---------------------------------------------------------------
setlocal EnableDelayedExpansion

set "VER=%~1"
if "%VER%"=="" set "VER=1.0.0"

if /i "%~2"=="build" (
    set "DEPLOY_ARGS=-Build -Clean"
) else (
    set "DEPLOY_ARGS="
)

echo ============================================================
echo   pyocd_ui  MSI one-click build    version=%VER%
echo ============================================================

echo.
echo [1/2] deploy.ps1  (build\pyocd_ui.exe -^> installer\)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" !DEPLOY_ARGS!
if errorlevel 1 (
    echo.
    echo [FAILED] deploy.ps1 failed, see the messages above.
    echo          Hint: run tools\own_pyocd\pyocd_ui\build.bat first so that
    echo          build\pyocd_ui.exe exists.
    pause
    exit /b 1
)

echo.
echo [2/2] build_msi.ps1  (installer\ -^> output\pyocd_ui-%VER%-x64.msi)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_msi.ps1" -Version "%VER%" -OpenFolder
if errorlevel 1 (
    echo.
    echo [FAILED] build_msi.ps1 failed, see the messages above.
    echo          Hint: WiX Toolset v4+ must be installed ^(https://wixtoolset.org^).
    pause
    exit /b 1
)

echo.
echo [OK] MSI: %~dp0..\output\pyocd_ui-%VER%-x64.msi
pause
