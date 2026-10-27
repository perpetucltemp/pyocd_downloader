@echo off
rem ================================================================
rem  pyocd_cli.bat
rem
rem  Calls the bundled pyocd.exe in THIS folder to:
rem    connect MCU / erase / download firmware / reset
rem
rem  No mamba / conda / Python dependency.
rem
rem  Usage:
rem    pyocd_cli.bat probes                 list debug probes
rem    pyocd_cli.bat targets [keyword]      list/search targets
rem    pyocd_cli.bat connect                connect MCU (status + vector table)
rem    pyocd_cli.bat erase                  chip erase
rem    pyocd_cli.bat flash <file> [opts]    download firmware (auto reset after)
rem    pyocd_cli.bat reset                  reset and run
rem    pyocd_cli.bat version                show pyocd version
rem
rem  Common options (connect / erase / flash / reset):
rem    -t <target>     MCU type,  default stm32f407zgtx (env PYOCD_TARGET)
rem    -f <freq>       SWD clock, default 4000k         (env PYOCD_FREQ)
rem    -u <uid>        probe unique id when multiple probes
rem
rem  Flash options:
rem    -e <sector|auto|chip>   erase mode, default sector
rem    -a <0x08000000>         base address for .bin (not needed for hex/elf)
rem    --format <bin|hex|elf>  file format, default by extension
rem    --no-reset              do not reset after programming
rem
rem  Erase options:
rem    --sector <addr>         erase only the given sector
rem
rem  Examples:
rem    pyocd_cli.bat probes
rem    pyocd_cli.bat connect
rem    pyocd_cli.bat erase
rem    pyocd_cli.bat flash build\app.bin -a 0x08000000
rem    pyocd_cli.bat flash build\app.hex
rem    pyocd_cli.bat flash build\app.bin -a 0x08000000 --no-reset
rem    pyocd_cli.bat reset
rem ================================================================
setlocal EnableDelayedExpansion

set "HERE=%~dp0"
set "PYOCD_EXE="

rem ---- 1. locate pyocd.exe in this folder ----
rem    prefer pyocd-windows-<ver>\pyocd.exe (newest version first), then .\pyocd.exe
if exist "%HERE%pyocd.exe" set "PYOCD_EXE=%HERE%pyocd.exe"
if "%PYOCD_EXE%"=="" (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%HERE%pyocd-windows-*" 2^>nul') do (
        if "!PYOCD_EXE!"=="" if exist "%HERE%%%D\pyocd.exe" set "PYOCD_EXE=%HERE%%%D\pyocd.exe"
    )
)
if "%PYOCD_EXE%"=="" (
    echo [ERROR] pyocd.exe not found under "%HERE%pyocd-windows-*\"
    exit /b 1
)

rem ---- 2. defaults ----
if "%PYOCD_TARGET%"=="" set "PYOCD_TARGET=stm32f407zgtx"
if "%PYOCD_FREQ%"=="" set "PYOCD_FREQ=4000k"

set "TARGET=%PYOCD_TARGET%"
set "FREQ=%PYOCD_FREQ%"
set "UID="
set "FILE="
set "ERASE=sector"
set "ADDR="
set "FORMAT="
set "NORESET="
set "SECTOR="

rem ---- 3. dispatch ----
set "CMD=%~1"
if "%CMD%"=="" goto usage
if /i "%CMD%"=="help"    goto usage
if /i "%CMD%"=="-h"      goto usage
if /i "%CMD%"=="--help"  goto usage
if /i "%CMD%"=="version" ("%PYOCD_EXE%" --version & goto :eof)
if /i "%CMD%"=="probes"  ("%PYOCD_EXE%" list --probes & goto :eof)
if /i "%CMD%"=="targets" goto dotargets
if /i "%CMD%"=="connect" (set "NEXT=doconnect" & shift & goto parseargs)
if /i "%CMD%"=="erase"   (set "NEXT=doerase"   & shift & goto parseargs)
if /i "%CMD%"=="flash"   goto doflash
if /i "%CMD%"=="reset"   (set "NEXT=doreset"   & shift & goto parseargs)

echo [ERROR] unknown command: %CMD%
goto usage

rem ---- 4. shared option parsing ----
:parseargs
if "%~1"=="" goto %NEXT%
if /i "%~1"=="-t" (
    set "TARGET=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--target" (
    set "TARGET=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="-f" (
    set "FREQ=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--frequency" (
    set "FREQ=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="-u" (
    set "UID=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--uid" (
    set "UID=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="-e" (
    set "ERASE=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--erase" (
    set "ERASE=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="-a" (
    set "ADDR=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--address" (
    set "ADDR=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--format" (
    set "FORMAT=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--sector" (
    set "SECTOR=%~2"
    shift & shift
    goto parseargs
)
if /i "%~1"=="--no-reset" (
    set "NORESET=1"
    shift
    goto parseargs
)
echo [ERROR] unknown option: %~1
exit /b 1

rem ---- 5. targets: 2nd arg is the filter keyword ----
:dotargets
set "KW=%~2"
if not "%KW%"=="" (
    "%PYOCD_EXE%" list --targets --color never | findstr /i /c:"%KW%"
) else (
    "%PYOCD_EXE%" list --targets --color never
)
goto :eof

rem ---- 6. flash: 1st arg after subcommand is the firmware file ----
:doflash
if "%~2"=="" (
    echo [ERROR] flash needs a firmware file, e.g. flash build\app.bin -a 0x08000000
    goto usage
)
set "FILE=%~2"
shift & shift
set "NEXT=doflash_run"
goto parseargs

rem ---- 7. run ----
rem    note: stdin is redirected from nul so that commander exits after
rem    the -c commands instead of dropping into the interactive console.
:doconnect
echo [*] connect %TARGET% at %FREQ% ...
set "UARG="
if not "%UID%"=="" set "UARG=-u %UID%"
"%PYOCD_EXE%" commander -t %TARGET% -f %FREQ% %UARG% -c status -c "read32 0x08000000 8" < nul
goto :eof

:doerase
echo [*] erase %TARGET% at %FREQ% ...
set "UARG="
if not "%UID%"=="" set "UARG=-u %UID%"
set "SARG=--chip"
if not "%SECTOR%"=="" set "SARG=--sector %SECTOR%"
"%PYOCD_EXE%" erase %SARG% -t %TARGET% -f %FREQ% %UARG%
goto :eof

:doflash_run
if not exist "%FILE%" (
    echo [ERROR] file not found: %FILE%
    exit /b 1
)
echo [*] flash "%FILE%" to %TARGET% at %FREQ% ...
set "UARG="
if not "%UID%"=="" set "UARG=-u %UID%"
set "FMTARG="
if not "%FORMAT%"=="" set "FMTARG=--format %FORMAT%"
set "ADDRARG="
if not "%ADDR%"=="" set "ADDRARG=-a %ADDR%"
set "RSTARG="
if not "%NORESET%"=="" set "RSTARG=--no-reset"
"%PYOCD_EXE%" load "%FILE%" -t %TARGET% -f %FREQ% -e %ERASE% %FMTARG% %ADDRARG% %RSTARG% %UARG%
goto :eof

:doreset
echo [*] reset %TARGET% at %FREQ% ...
set "UARG="
if not "%UID%"=="" set "UARG=-u %UID%"
"%PYOCD_EXE%" reset -t %TARGET% -f %FREQ% %UARG%
goto :eof

rem ---- 8. help ----
:usage
echo.
echo pyocd_cli.bat - connect / erase / flash / reset via bundled pyocd.exe
echo.
echo   pyocd_cli.bat probes                 list debug probes
echo   pyocd_cli.bat targets [keyword]      list/search targets
echo   pyocd_cli.bat connect                connect MCU ^(status + vector table^)
echo   pyocd_cli.bat erase                  chip erase
echo   pyocd_cli.bat flash ^<file^> [opts]    download firmware
echo   pyocd_cli.bat reset                  reset and run
echo   pyocd_cli.bat version                show pyocd version
echo.
echo common options ^(-t target, -f freq, -u probe-uid^):
echo   -t ^<target^>   MCU type, default %PYOCD_TARGET% ^(env PYOCD_TARGET^)
echo   -f ^<freq^>     SWD clock, default %PYOCD_FREQ% ^(env PYOCD_FREQ^)
echo   -u ^<uid^>      probe unique id when multiple probes
echo.
echo flash options:
echo   -e ^<sector^|auto^|chip^>   erase mode, default sector
echo   -a ^<addr^>                 base address for .bin, e.g. 0x08000000
echo   --format ^<bin^|hex^|elf^>  file format, default by extension
echo   --no-reset                do not reset after programming
echo.
echo erase options:
echo   --sector ^<addr^>          erase only the given sector
echo.
echo examples:
echo   pyocd_cli.bat connect
echo   pyocd_cli.bat flash build\app.bin -a 0x08000000
echo   pyocd_cli.bat flash build\app.hex --no-reset
echo   pyocd_cli.bat reset
echo.
exit /b 1
