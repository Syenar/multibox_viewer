# Elevated one-shot: enable test signing, build, install driver, stage local data folders.
#Requires -RunAsAdministrator
param(
    [switch]$SkipBuild,
    [switch]$SkipRebootPrompt
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root
$log = Join-Path $env:TEMP "WindowDisplay-setup.log"
function Log($msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $msg
    Add-Content -Path $log -Value $line
    Write-Host $line
}

Log "WindowDisplay setup starting. Log: $log"

# 1) Test signing
Log "Enabling testsigning..."
$before = (bcdedit /enum "{current}" | Out-String)
bcdedit /set testsigning on | Out-String | ForEach-Object { Log $_.Trim() }
$after = (bcdedit /enum "{current}" | Out-String)
if ($after -match "testsigning\s+Yes") {
    Log "testsigning is Yes"
} else {
    Log "WARNING: could not confirm testsigning Yes. Secure Boot may block it. Output:"
    Log $after
}

# 2) Build + stage
if (-not $SkipBuild) {
    Log "Building Release..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed: $LASTEXITCODE" }
}

$stage = Join-Path $root "artifacts\Release"
$inf = Join-Path $stage "WindowDisplayDriver.inf"
$dll = Join-Path $stage "WindowDisplayDriver.dll"
if (-not (Test-Path $inf) -or -not (Test-Path $dll)) {
    throw "Missing driver artifacts in $stage"
}

# 3) Local data folders
$data = Join-Path $env:LOCALAPPDATA "WindowDisplay"
New-Item -ItemType Directory -Force -Path (Join-Path $data "layouts") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $data "diagnostics") | Out-Null
Log "Data folders ready under $data"

# 4) Install driver package
$temp = Join-Path $env:TEMP "WindowDisplayDriverInstall"
New-Item -ItemType Directory -Force -Path $temp | Out-Null
Copy-Item (Join-Path $stage "WindowDisplayDriver.dll") $temp -Force
Copy-Item $inf $temp -Force
Log "pnputil /add-driver ..."
$pnp = & pnputil /add-driver (Join-Path $temp "WindowDisplayDriver.inf") /install 2>&1 | Out-String
Log $pnp

# 5) Marker for post-reboot continue
$marker = Join-Path $env:LOCALAPPDATA "WindowDisplay\pending-prototype.flag"
Set-Content -Path $marker -Value @"
created=$(Get-Date -Format o)
host=$stage\WindowDisplayHost.exe
"@
Log "Wrote continue marker: $marker"

$needReboot = $true
if ($after -match "testsigning\s+Yes" -and $before -match "testsigning\s+Yes") {
    $needReboot = $false
    Log "Testsigning was already enabled; reboot may still help after first driver install."
}

if ($needReboot -and -not $SkipRebootPrompt) {
    Log "REBOOT REQUIRED before unsigned/test-signed driver will load."
    $choice = Read-Host "Reboot now? [Y/N]"
    if ($choice -match '^[Yy]') {
        Log "Rebooting..."
        shutdown /r /t 5 /c "WindowDisplay: reboot to activate test signing"
    } else {
        Log "Skipped reboot. Reboot manually, then run tools\launch-prototype.ps1"
    }
} else {
    Log "Setup finished. Next: tools\launch-prototype.ps1"
}

Log "Done."
