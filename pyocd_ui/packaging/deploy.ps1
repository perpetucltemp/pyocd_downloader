<#
.SYNOPSIS
    Deploy pyocd_ui: build\pyocd_ui.exe  ->  deploy folder  ->  windeployqt
    + bundle the private pyocd.exe that lives next to this project.

.DESCRIPTION
    1. locates pyocd_ui.exe under  <pyocd_ui>\build  (Ninja single-config or <Config>\ subdir)
    2. copies it into the deploy folder (default: <pyocd_ui>\installer)
    3. finds windeployqt (Qt dir from build\CMakeCache.txt -> PATH -> C:\Qt\*\*\bin)
    4. runs windeployqt -> Qt DLLs, plugins (platforms/imageformats/tls/...), translations
    5. fills runtime DLLs that windeployqt missed (objdump import-table scan)
    6. copies  <own_pyocd>\pyocd-windows-<ver>\  into the deploy folder, so the
       installed app finds its pyocd.exe next to the exe (PyocdLocator walks up
       from the exe directory). Without this step the installed copy would have
       no pyocd to call and would fall back to nothing.
    7. verifies the result (Qt6Core/Gui/Widgets/qwindows + pyocd.exe present)

    The MSI builder (build_msi.ps1) packages this deploy folder.

.PARAMETER DeployDir
    Where the runnable app folder is produced. Default: <pyocd_ui>\installer

.PARAMETER BuildDir
    CMake build folder. Default: <pyocd_ui>\build

.PARAMETER QtDir
    Qt prefix (the folder that contains bin\windeployqt.exe).
    Default: auto-detected from build\CMakeCache.txt.

.PARAMETER ExtraDeps
    Extra folders to look for runtime DLLs that windeployqt misses
    (Qt6Core.dll links against zstd.dll, which is not shipped inside Qt).

.PARAMETER Configuration
    Release (default) or Debug. Also selects the <Config>\ subfolder of multi-config builds.

.PARAMETER Build
    Run "cmake --build <BuildDir>" first, so the exe is up to date.

.PARAMETER Clean
    Delete the deploy folder before deploying (removes stale plugins).

.PARAMETER OpenFolder
    Open the deploy folder in Explorer when done.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\deploy.ps1
    powershell -ExecutionPolicy Bypass -File .\deploy.ps1 -Build -Clean
    powershell -ExecutionPolicy Bypass -File .\deploy.ps1 -Configuration Debug -DeployDir ..\install_dbg
#>

param(
    [string]$DeployDir     = "",
    [string]$BuildDir      = "",
    [string]$QtDir         = "",
    [string[]]$ExtraDeps   = @(),
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Build,
    [switch]$Clean,
    [switch]$OpenFolder
)

$ErrorActionPreference = "Stop"

# ------------------------------------------------------------------ paths
$ScriptDir = $PSScriptRoot
$RootDir   = Split-Path -Parent $ScriptDir                # ...\tools\own_pyocd\pyocd_ui
$OwnDir    = Split-Path -Parent $RootDir                  # ...\tools\own_pyocd
if (-not $BuildDir)  { $BuildDir  = Join-Path $RootDir "build" }
if (-not $DeployDir) { $DeployDir = Join-Path $RootDir "installer" }
$BuildDir  = (Resolve-Path -LiteralPath $BuildDir).Path
$ExeName   = "pyocd_ui.exe"
$CachePath = Join-Path $BuildDir "CMakeCache.txt"

function Write-Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn([string]$msg) { Write-Host "WARN: $msg" -ForegroundColor Yellow }

# ------------------------------------------------------------- 1. the exe
if (-not (Test-Path -LiteralPath $BuildDir)) {
    throw "Build folder not found: $BuildDir  (configure/build the project first)"
}

if ($Build) {
    Write-Step "Building ($Configuration): $BuildDir"
    & cmake --build $BuildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw "cmake --build failed with exit code $LASTEXITCODE" }
}

function Find-AppExe {
    param([string]$Dir, [string]$Config)

    # multi-config generators (MSVC/Ninja Multi-Config):  build\<Config>\app.exe
    # single-config generators (Ninja / MinGW Makefiles):  build\app.exe
    foreach ($c in @((Join-Path $Dir "$Config\$ExeName"), (Join-Path $Dir $ExeName))) {
        if (Test-Path -LiteralPath $c) { return Get-Item -LiteralPath $c }
    }

    $f = Get-ChildItem -LiteralPath $Dir -Recurse -File -Filter $ExeName -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -notmatch '\\autogen\\' } |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($f) { return $f }

    throw "No $ExeName under $Dir  - build the project first (or pass -Build)"
}

$AppExe = Find-AppExe -Dir $BuildDir -Config $Configuration
Write-Step "App exe : $($AppExe.FullName)"

# warn when the exe is older than the sources it was built from
$newerSrc = Get-ChildItem -LiteralPath $RootDir -File |
            Where-Object { $_.Extension -in ".cpp", ".h", ".ui" -or $_.Name -eq "CMakeLists.txt" } |
            Where-Object { $_.LastWriteTime -gt $AppExe.LastWriteTime }
if ($newerSrc) {
    Write-Warn ("exe is older than {0} source file(s), e.g. {1} - rebuild if this is unexpected" -f
                $newerSrc.Count, $newerSrc[0].Name)
}

# ------------------------------------------------ 2. copy into deploy dir
if ($Clean -and (Test-Path -LiteralPath $DeployDir)) {
    Write-Step "Cleaning $DeployDir"
    Remove-Item -LiteralPath $DeployDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $DeployDir)) {
    New-Item -ItemType Directory -Path $DeployDir | Out-Null
}
$DeployDir = (Resolve-Path -LiteralPath $DeployDir).Path

$TargetExe = Join-Path $DeployDir $ExeName
if ($AppExe.FullName -ne $TargetExe) {
    Write-Step "Copying $ExeName -> $DeployDir"
    Copy-Item -LiteralPath $AppExe.FullName -Destination $TargetExe -Force
} else {
    Write-Step "App exe already in place: $TargetExe"
}

# ------------------------------------------------------- 3. locate windeployqt
function Get-QtPrefixFromCache {
    param([string]$Cache)

    if (-not (Test-Path -LiteralPath $Cache)) { return $null }
    $m = Select-String -LiteralPath $Cache -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
    if (-not $m) { return $null }

    #  Qt6_DIR = <prefix>\lib\cmake\Qt6   ->   <prefix>
    $p = $m.Matches[0].Groups[1].Value.Trim()
    $p = $p -replace '/', '\'
    try { return (Split-Path (Split-Path (Split-Path $p -Parent) -Parent) -Parent) } catch { return $null }
}

function Find-WindeployQt {
    param([string]$Prefix)

    if ($Prefix) {
        $p = Join-Path $Prefix "bin\windeployqt.exe"
        if (Test-Path -LiteralPath $p) { return $p }
        Write-Warn "windeployqt.exe not found under $Prefix"
    }

    $cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @("C:\Qt", "D:\Qt", (Join-Path $env:SystemDrive "Qt"), "D:\Software_Installed", "C:\Software_Installed")
    foreach ($r in $roots) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $hit = Get-ChildItem -LiteralPath $r -Recurse -File -Filter windeployqt.exe -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match '\\bin\\windeployqt\.exe$' } | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw "windeployqt.exe not found. Pass -QtDir <Qt prefix> (the folder containing bin\windeployqt.exe)."
}

if (-not $QtDir) { $QtDir = Get-QtPrefixFromCache -Cache $CachePath }
if ($QtDir) { Write-Step "Qt prefix: $QtDir" } else { Write-Warn "Qt prefix not in CMakeCache.txt, falling back to search" }

$WindeployQt = Find-WindeployQt -Prefix $QtDir
Write-Step "windeployqt: $WindeployQt"
Write-Step ("windeployqt version: {0}" -f ((& $WindeployQt --version 2>&1 | Select-Object -First 1)))

# MinGW builds need the compiler runtime on PATH so windeployqt can pick up
# libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll
$mingwBin = $null
if (Test-Path -LiteralPath $CachePath) {
    $m = Select-String -LiteralPath $CachePath -Pattern '^CMAKE_CXX_COMPILER:FILEPATH=(.+)$' | Select-Object -First 1
    if ($m) {
        $compiler = $m.Matches[0].Groups[1].Value.Trim() -replace '/', '\'
        if (Test-Path -LiteralPath $compiler) {
            $mingwBin = Split-Path $compiler -Parent
            Write-Step "Compiler: $compiler"
        }
    }
}

# --------------------------------------------------------- 4. windeployqt
$deployArgs = @(
    "--dir", $DeployDir,
    "--verbose", "1"
)
$deployArgs += if ($Configuration -eq "Debug") { "--debug" } else { "--release" }

Write-Step "Running windeployqt on $ExeName"
if ($mingwBin) { $env:PATH = "$mingwBin;$env:PATH" }
Push-Location $DeployDir
try {
    & $WindeployQt @deployArgs $TargetExe
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# --------------------------------------- 5. runtime deps windeployqt missed
# windeployqt does not copy every DLL a Qt library links against: it ignores
# libraries it cannot find inside the Qt installation, and Qt6Core.dll links
# against zstd.dll which Qt does not ship. Scan the import tables of the exe and
# of every deployed DLL, then fill the gaps from the folders below.
# NOTE: this runs BEFORE the pyocd bundle is copied in, so the scan only looks
# at the Qt payload - the bundled pyocd is a self-contained PyInstaller build.
function Get-Objdump {
    if ($mingwBin) {
        $p = Join-Path $mingwBin "objdump.exe"
        if (Test-Path -LiteralPath $p) { return $p }
    }
    $cmd = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-ImportNames {
    param([string]$Objdump, [string]$File)

    $lines = & $Objdump -p $File 2>$null | Select-String -Pattern 'DLL Name:\s*(\S+)'
    foreach ($l in $lines) { $l.Matches[0].Groups[1].Value }
}

$searchDirs = @($DeployDir, $ScriptDir, $RootDir, (Join-Path $ScriptDir "runtime-deps")) + $ExtraDeps
if ($QtDir)  { $searchDirs += (Join-Path $QtDir "bin") }
if ($mingwBin) { $searchDirs += $mingwBin }
$searchDirs = @($searchDirs | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object { (Resolve-Path -LiteralPath $_).Path } | Select-Object -Unique)

$Objdump   = Get-Objdump
$addedDeps = @()

if (-not $Objdump) {
    Write-Warn "objdump.exe not found - skipping the missing-dependency scan"
} else {
    for ($pass = 1; $pass -le 3; $pass++) {
        $present = @{}
        foreach ($f in (Get-ChildItem -LiteralPath $DeployDir -Recurse -File)) { $present[$f.Name.ToLower()] = $true }

        $targets = @(Get-Item -LiteralPath $TargetExe) + @(Get-ChildItem -LiteralPath $DeployDir -Recurse -File -Filter *.dll)
        $missing = @{}
        foreach ($t in $targets) {
            foreach ($imp in (Get-ImportNames -Objdump $Objdump -File $t.FullName)) {
                $k = $imp.ToLower()
                if ($present.ContainsKey($k)) { continue }
                # api-ms-win-*/ext-ms-* are API sets resolved by the loader, not real files
                if ($k -match '^(api-ms-win|ext-ms)-') { continue }
                # everything that ships with Windows is available anyway
                if (Test-Path -LiteralPath (Join-Path "$env:SystemRoot\System32" $imp)) { continue }
                $missing[$k] = $imp
            }
        }
        if ($missing.Count -eq 0) { break }

        $copied = 0
        foreach ($imp in ($missing.Values | Sort-Object)) {
            $src = $null
            foreach ($d in $searchDirs) {
                $cand = Join-Path $d $imp
                if (Test-Path -LiteralPath $cand) { $src = $cand; break }
            }
            if ($src) {
                Write-Step "Copying missing dependency $imp"
                Write-Host "         <- $src" -ForegroundColor DarkGray
                Copy-Item -LiteralPath $src -Destination (Join-Path $DeployDir $imp) -Force
                $addedDeps += $imp
                $copied++
            }
        }

        if ($copied -eq 0) {
            Write-Warn ("missing runtime DLL(s): {0}" -f (($missing.Values | Sort-Object) -join ", "))
            Write-Warn ("  windeployqt does not know these. Put them in $RootDir")
            Write-Warn ("  (or pass -ExtraDeps <folder>) and run this script again.")
            break
        }
    }
}

# ------------------------------------- 6. bundle the private pyocd (own_pyocd)
# pyocd_ui calls <dir>\pyocd-windows-<ver>\pyocd.exe; PyocdLocator finds it by
# walking up from the exe. Shipping it inside the installer keeps the installed
# app self-contained: no mamba/conda/Python environment required.
$PyocdSrc = $null
foreach ($cand in @((Join-Path $OwnDir "pyocd-windows-*"), (Join-Path $RootDir "pyocd-windows-*"))) {
    $hit = Get-Item -Path $cand -ErrorAction SilentlyContinue |
           Sort-Object Name -Descending | Select-Object -First 1
    if ($hit -and (Test-Path -LiteralPath (Join-Path $hit.FullName "pyocd.exe"))) { $PyocdSrc = $hit; break }
}
if (-not $PyocdSrc) {
    throw ("bundled pyocd.exe not found. Expected <root>\pyocd-windows-<ver>\pyocd.exe " +
           "next to $RootDir (the pyocd-windows-* folder with pyocd.exe inside).")
}

$PyocdDst = Join-Path $DeployDir $PyocdSrc.Name
Write-Step "Bundling pyocd: $($PyocdSrc.Name) -> $DeployDir"
if (Test-Path -LiteralPath $PyocdDst) { Remove-Item -LiteralPath $PyocdDst -Recurse -Force }
& robocopy $PyocdSrc.FullName $PyocdDst /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

$BundledPyocdExe = Join-Path $PyocdDst "pyocd.exe"
if (-not (Test-Path -LiteralPath $BundledPyocdExe)) {
    throw "pyocd.exe missing after copy: $BundledPyocdExe"
}
$pyocdVer = (& $BundledPyocdExe --version 2>&1 | Select-Object -First 1).Trim()
Write-Step "Bundled pyocd version: $pyocdVer"

# ------------------------------------- 6b. bundle the .pack files (own_pyocd\packs)
# pyocd_ui passes --pack <path> to every command, so the installed app does not
# depend on the global cmsis-pack-manager cache (which is empty on other PCs).
$PacksSrc = Join-Path $OwnDir "packs"
if (Test-Path -LiteralPath $PacksSrc) {
    $packFiles = Get-ChildItem -LiteralPath $PacksSrc -File -Filter *.pack
    if ($packFiles.Count -gt 0) {
        $PacksDst = Join-Path $DeployDir "packs"
        Write-Step "Bundling packs: $($packFiles.Count) file(s) -> $PacksDst"
        if (Test-Path -LiteralPath $PacksDst) { Remove-Item -LiteralPath $PacksDst -Recurse -Force }
        New-Item -ItemType Directory -Path $PacksDst | Out-Null
        foreach ($f in $packFiles) {
            Copy-Item -LiteralPath $f.FullName -Destination $PacksDst -Force
            Write-Host "         <- $($f.Name)" -ForegroundColor DarkGray
        }
    } else {
        Write-Warn "no .pack files in $PacksSrc (pack targets will need network or a local pack cache)"
    }
} else {
    Write-Warn "packs folder not found: $PacksSrc (pack targets will need network or a local pack cache)"
}

# ------------------------------------------------------------ 7. verify
$missing = @()
foreach ($required in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms\qwindows.dll",
                        "zstd.dll", "$($PyocdSrc.Name)\pyocd.exe")) {
    if (-not (Test-Path -LiteralPath (Join-Path $DeployDir $required))) { $missing += $required }
}

$files = @(Get-ChildItem -LiteralPath $DeployDir -Recurse -File)
$size  = [math]::Round((($files | Measure-Object Length -Sum).Sum) / 1MB, 2)

Write-Host ""
Write-Host "SUCCESS: $DeployDir" -ForegroundColor Green
Write-Host "         files: $($files.Count)   total: $size MB   config: $Configuration" -ForegroundColor Green

if ($missing.Count -gt 0) {
    Write-Warn ("still missing: {0}  - check the log above" -f ($missing -join ", "))
} else {
    Write-Host "         Qt6Core/Qt6Gui/Qt6Widgets/qwindows.dll + pyocd.exe all present" -ForegroundColor Green
}
if ($addedDeps.Count -gt 0) {
    Write-Host ("         added by hand-in-pass: {0}" -f (($addedDeps | Sort-Object -Unique) -join ", ")) -ForegroundColor Yellow
}
Write-Host "         next: packaging\build_msi.ps1  (packages this folder)" -ForegroundColor DarkGray

if ($OpenFolder) { Start-Process explorer.exe -ArgumentList $DeployDir }
