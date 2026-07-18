$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$hostExe = Join-Path $root "artifacts\Release\WindowDisplayHost.exe"
if (-not (Test-Path $hostExe)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Configuration Release
}
Write-Host "Launching elevated prototype (UAC prompt)..." -ForegroundColor Cyan
Start-Process -FilePath $hostExe -ArgumentList "--prototype --elevated" -Verb RunAs -WorkingDirectory (Split-Path $hostExe)
