# Registers WindowsUserModeDriver10.0 platform toolset against VS Build Tools + WDK NuGet/content.
# Run elevated if writing into Program Files fails; otherwise uses the repo-local override via Directory.Build.props.

param(
    [string]$VcTargetsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Microsoft\VC\v170",
    [string]$WdkContentRoot = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $root) { $root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path }

if (-not $WdkContentRoot) {
    $nugetWdk = Join-Path $root "packages\Microsoft.Windows.WDK.x64.10.0.26100.6584\c"
    if (Test-Path $nugetWdk) {
        $WdkContentRoot = $nugetWdk
    } else {
        $WdkContentRoot = "C:\Program Files (x86)\Windows Kits\10\"
    }
}

$toolsetName = "WindowsUserModeDriver10.0"
$platforms = @("x64")

foreach ($platform in $platforms) {
    $toolsetDir = Join-Path $VcTargetsPath "Platforms\$platform\PlatformToolsets\$toolsetName"
    New-Item -ItemType Directory -Force -Path $toolsetDir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $toolsetDir "ImportAfter") | Out-Null

    $props = @"
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="`$(MSBuildThisFileDirectory)ImportBefore\*.props" Condition="Exists('`$(MSBuildThisFileDirectory)ImportBefore')" />
  <PropertyGroup>
    <IsUserModeToolset>true</IsUserModeToolset>
    <PlatformToolsetShortName>WindowsUserModeDriver</PlatformToolsetShortName>
    <WDKContentRoot Condition="'`$(WDKContentRoot)'==''">$WdkContentRoot</WDKContentRoot>
    <WDKBuildFolder Condition="'`$(WDKBuildFolder)'==''">10.0.26100.0</WDKBuildFolder>
    <DriverTargetPlatform Condition="'`$(DriverTargetPlatform)'==''">Universal</DriverTargetPlatform>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.MSVC.Toolset.$platform.props" Condition="Exists('`$(VCTargetsPath)\Microsoft.Cpp.MSVC.Toolset.$platform.props')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.Common.props" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.Common.props')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.Default.props" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.Default.props')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.props" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.props')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.LateEvaluation.props" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.LateEvaluation.props')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.LateEvaluation.props" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.LateEvaluation.props')" />
  <Import Project="`$(MSBuildThisFileDirectory)ImportAfter\*.props" Condition="Exists('`$(MSBuildThisFileDirectory)ImportAfter')" />
  <Import Project="`$(_PlatformFolder)Platform.Common.props" Condition="'`$(_PlatformFolder)' != '' and Exists('`$(_PlatformFolder)Platform.Common.props')" />
</Project>
"@
    Set-Content -Path (Join-Path $toolsetDir "Toolset.props") -Value $props -Encoding UTF8

    $targets = @"
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="`$(MSBuildThisFileDirectory)ImportBefore\*.targets" Condition="Exists('`$(MSBuildThisFileDirectory)ImportBefore')" />
  <Import Project="`$(VCTargetsPath)\Microsoft.CppCommon.targets" />
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.WindowsSDK.targets" Condition="Exists('`$(VCTargetsPath)\Microsoft.Cpp.WindowsSDK.targets')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.Common.targets" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.Common.targets')" />
  <Import Project="`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.CX.targets" Condition="Exists('`$(WDKContentRoot)build\`$(WDKBuildFolder)\WindowsDriver.UserMode.CX.targets')" />
  <Import Project="`$(MSBuildThisFileDirectory)ImportAfter\*.targets" Condition="Exists('`$(MSBuildThisFileDirectory)ImportAfter')" />
</Project>
"@
    Set-Content -Path (Join-Path $toolsetDir "Toolset.targets") -Value $targets -Encoding UTF8

    Write-Host "Installed toolset at $toolsetDir"
}

Write-Host "WDKContentRoot=$WdkContentRoot"
Write-Host "Done."
