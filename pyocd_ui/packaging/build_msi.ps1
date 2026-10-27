<#
.SYNOPSIS
    One-click MSI builder for pyocd_ui (WiX Toolset v4/v5/v6/v7).

.DESCRIPTION
    1. locates wix.exe (PATH or "%ProgramFiles%\WiX Toolset v*\bin")
    2. makes sure the WixUI extension is available
    3. runs  wix build  on Product.wxs only - the payload list is produced by the
       <Files Include="$(var.SourceDir)\**" /> element inside Product.wxs, so there
       is no heat.exe / harvested Files.g.wxs step any more
    4. ->  <pyocd_ui>\output\pyocd_ui-<version>-<arch>.msi

    The installer ships the bundled pyocd.exe (pyocd-windows-<ver>\) that
    deploy.ps1 copied into  installer\  - the installed app is self-contained.

.PARAMETER Version
    MSI ProductVersion, 3 numbers max. Default 1.0.0

.PARAMETER Arch
    x64 (default) or x86.

.PARAMETER Manufacturer
    Publisher name shown in "Programs and Features".

.PARAMETER OpenFolder
    Open the output folder in Explorer when done.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build_msi.ps1
    powershell -ExecutionPolicy Bypass -File .\build_msi.ps1 -Version 1.2.0 -OpenFolder
#>

param(
    [string]$Version      = "1.0.0",
    [string]$Arch         = "x64",
    [string]$Manufacturer = "YourCompany",
    [switch]$OpenFolder
)

$ErrorActionPreference = "Stop"

# ------------------------------------------------------------------ paths
$ScriptDir  = $PSScriptRoot
$RootDir    = Split-Path -Parent $ScriptDir              # ...\tools\own_pyocd\pyocd_ui
$SourceDir  = Join-Path $RootDir "installer"             # deploy.ps1 / windeployqt output
$OutDir     = Join-Path $RootDir "output"
$ObjDir     = Join-Path $ScriptDir "obj"
$ProductWxs = Join-Path $ScriptDir "Product.wxs"
$MsiPath    = Join-Path $OutDir    ("pyocd_ui-{0}-{1}.msi" -f $Version, $Arch)
$PdbPath    = Join-Path $ObjDir    ("pyocd_ui-{0}-{1}.wixpdb" -f $Version, $Arch)

function Write-Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn([string]$msg) { Write-Host "WARN: $msg" -ForegroundColor Yellow }

# ------------------------------------------------------- sanity / WiX find
if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "Source folder not found: $SourceDir  (run deploy.ps1 / package.bat first)"
}
if (-not (Test-Path -LiteralPath $ProductWxs)) {
    throw "Product.wxs not found next to this script: $ProductWxs"
}
if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') {
    throw "Version must look like 1.0.0 (numbers only, max 3 parts)"
}

function Find-Wix {
    $cmd = Get-Command wix.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA)
    foreach ($r in $roots) {
        if (-not $r) { continue }
        foreach ($d in (Get-ChildItem -LiteralPath $r -Directory -Filter "WiX Toolset v*" -ErrorAction SilentlyContinue)) {
            $p = Join-Path $d.FullName "bin\wix.exe"
            if (Test-Path -LiteralPath $p) { return $p }
        }
    }
    throw "wix.exe not found. Install WiX Toolset (https://wixtoolset.org) or add it to PATH."
}

$Wix = Find-Wix
Write-Step "WiX CLI : $Wix"

# WiX v5+ requires the OSMF EULA to be accepted; pass it per invocation
# (run 'wix eula accept wix7' once if you prefer a permanent acceptance file).
$EulaArgs = @("--acceptEula", "wix7")

# --------------------------------------------------------------- extension
# WixToolset.UI.wixext provides WixUI_InstallDir (install-dir selection UI).
# The extension cache is global; the first run may need network access.
Write-Step "Checking WiX extension: WixToolset.UI.wixext"
$extOk = $false
try {
    $extList = & $Wix extension list 2>$null | Out-String
    if ($extList -match "WixToolset\.UI\.wixext") { $extOk = $true }
} catch {
    $extOk = $false
}

if (-not $extOk) {
    # extension list prints nothing on some environments; just add once
    # (cached locally = no network). A failure here is not fatal: the real
    # missing-extension error would come from wix build below.
    Write-Step "Adding WiX extension (uses the local cache when offline)"
    try {
        & $Wix $EulaArgs extension add WixToolset.UI.wixext 2>&1 |
            Select-String -Pattern "error|Error" | ForEach-Object { $_.Line }
    } catch {
        Write-Warn "extension add failed: $($_.Exception.Message)"
    }
}

# ------------------------------------------------------------- output dirs
if (Test-Path -LiteralPath $ObjDir) { Remove-Item -LiteralPath $ObjDir -Recurse -Force }
New-Item -ItemType Directory -Path $ObjDir | Out-Null
New-Item -ItemType Directory -Path $OutDir -ErrorAction SilentlyContinue | Out-Null

# ------------------------------------------------------------------ build
# Product.wxs is the ONLY source file: its <Files Include="$(var.SourceDir)\**" />
# element makes the compiler walk  installer\  (including the bundled
# pyocd-windows-<ver>\ folder) and emit the components itself.
Write-Step "Building MSI: $MsiPath"

& $Wix $EulaArgs build `
    -arch $Arch `
    -d "ProductVersion=$Version" `
    -d "Manufacturer=$Manufacturer" `
    -d "SourceDir=$SourceDir" `
    -ext WixToolset.UI.wixext `
    -intermediatefolder $ObjDir `
    -pdb $PdbPath `
    -out $MsiPath `
    $ProductWxs

if ($LASTEXITCODE -ne 0) {
    throw "wix build failed with exit code $LASTEXITCODE"
}

$size = [math]::Round((Get-Item -LiteralPath $MsiPath).Length / 1MB, 2)
Write-Host ""
Write-Host "SUCCESS: $MsiPath ($size MB)" -ForegroundColor Green

if ($OpenFolder) { Start-Process explorer.exe -ArgumentList $OutDir }
