param(
    [string]$Configuration = "Release",
    [switch]$SkipDriver,
    [switch]$SkipHost,
    [switch]$SkipController
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$preferredMsbuild = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$msbuild = if (Test-Path $preferredMsbuild) {
    $preferredMsbuild
} else {
    & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
}
if (-not $msbuild) { throw "MSBuild not found." }
Write-Host "Using MSBuild: $msbuild"

if (-not (Test-Path "$root\packages\Microsoft.Windows.WDK.x64.10.0.26100.6584")) {
    nuget restore "$root\packages.config" -PackagesDirectory "$root\packages"
}

if (-not $SkipDriver) {
    & $msbuild "$root\src\driver\WindowDisplayDriver\WindowDisplayDriver.vcxproj" /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Driver build failed" }
}

if (-not $SkipHost) {
    & $msbuild "$root\src\host\WindowDisplayHost\WindowDisplayHost.vcxproj" /p:Configuration=$Configuration /p:Platform=x64 /p:SolutionDir="$root\\" /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Host build failed" }
}

if (-not $SkipController) {
    dotnet build "$root\src\controller\WindowDisplay\WindowDisplay.csproj" -c $Configuration
    if ($LASTEXITCODE -ne 0) { throw "Controller build failed" }
}

$stage = Join-Path $root "artifacts\$Configuration"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item "$root\src\driver\WindowDisplayDriver\bin\x64\$Configuration\WindowDisplayDriver.dll" $stage -Force -ErrorAction SilentlyContinue
Copy-Item "$root\src\driver\WindowDisplayDriver\WindowDisplayDriver.inf" $stage -Force
$hostExe = @(
    "$root\src\host\WindowDisplayHost\bin\x64\$Configuration\WindowDisplayHost.exe",
    "$root\bin\x64\$Configuration\WindowDisplayHost.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($hostExe) {
    Copy-Item $hostExe $stage -Force
    $controllerOut = "$root\src\controller\WindowDisplay\bin\$Configuration\net8.0-windows"
    if (Test-Path $controllerOut) {
        Copy-Item $hostExe $controllerOut -Force
    }
}
Copy-Item "$root\src\controller\WindowDisplay\bin\$Configuration\net8.0-windows\*" $stage -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item "$root\tools\install-driver.ps1" $stage -Force
Copy-Item "$root\tools\uninstall-driver.ps1" $stage -Force
Copy-Item "$root\tools\sign-and-install-driver.ps1" $stage -Force -ErrorAction SilentlyContinue
Copy-Item "$root\tools\launch-prototype.ps1" $stage -Force -ErrorAction SilentlyContinue

Write-Host "Staged to $stage"
Get-ChildItem $stage | Format-Table Name, Length