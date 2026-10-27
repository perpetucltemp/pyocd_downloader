@echo off
rem ---------------------------------------------------------------
rem  pyocd_ui - build the MSI from an already deployed installer\
rem
rem  Prerequisite: deploy.ps1 (or package.bat) has been run, i.e. the
rem  installer\ folder exists. For the full flow use package.bat.
rem
rem  Usage:
rem      build_msi.bat 1.0.0
rem
rem  Requires: wix.exe (WiX Toolset v4+) + WixToolset.UI.wixext
rem  (first run may need network; afterwards the extension is cached)
rem ---------------------------------------------------------------
setlocal

set "VER=%~1"
if "%VER%"=="" set "VER=1.0.0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_msi.ps1" -Version "%VER%" -OpenFolder

if errorlevel 1 (
    echo.
    echo [FAILED] MSI build failed, see messages above.
) else (
    echo.
    echo [OK] MSI is in ..\output
)

endlocal
pause
