@echo off
rem ================================================================
rem  pyocd_ui auto build script
rem
rem  Usage:
rem     build.bat              incremental build
rem     build.bat clean        clean and rebuild
rem     build.bat nobuild      configure only
rem
rem  Paths can be overridden with environment variables:
rem     set MINGW_DIR=D:\Software_Installed\mingw64\bin
rem     set QT_DIR=D:\Software_Installed\qt_install
rem ================================================================
setlocal EnableDelayedExpansion
cd /d %~dp0

if "%MINGW_DIR%"=="" set "MINGW_DIR=D:\Software_Installed\mingw64\bin"
if "%QT_DIR%"=="" set "QT_DIR=D:\Software_Installed\qt_install"

rem ---- 0. check toolchain ----
if not exist "%MINGW_DIR%\gcc.exe" (
    echo [ERROR] gcc.exe not found in %MINGW_DIR%
    echo         set MINGW_DIR env var or edit the default at the top of this script
    goto :fail
)
if not exist "%QT_DIR%\bin\qt-cmake.bat" (
    echo [ERROR] qt-cmake.bat not found in %QT_DIR%\bin
    echo         set QT_DIR env var or edit the default at the top of this script
    goto :fail
)

rem ---- 1. MinGW runtime into PATH ----
rem     qt-cmake only handles the Qt toolchain; moc/rcc need libgcc/libstdc++,
rem     and the compiler is discovered via PATH, so this step is required
set "PATH=%MINGW_DIR%;%PATH%"

rem ---- 2. optional clean ----
if /i "%1"=="clean" (
    echo [INFO] cleaning build directory...
    if exist build rmdir /s /q build
)

rem ---- 3. configure ----
echo [INFO] qt-cmake -B build -G Ninja
call "%QT_DIR%\bin\qt-cmake.bat" -B build -G Ninja
if errorlevel 1 goto :fail

rem ---- 4. build ----
if /i "%1"=="nobuild" (
    echo [INFO] nobuild: skipping compilation
    goto :ok
)
echo [INFO] ninja -C build
ninja -C build
if errorlevel 1 goto :fail

rem ---- 5. deploy Qt runtime (so the build dir can be copied as a whole) ----
if exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [INFO] windeployqt build\pyocd_ui.exe
    call "%QT_DIR%\bin\windeployqt.exe" build\pyocd_ui.exe
)

:ok
echo.
echo [OK] build finished: %CD%\build\pyocd_ui.exe
endlocal
exit /b 0

:fail
echo.
echo [FAIL] build failed
pause
endlocal
exit /b 1
