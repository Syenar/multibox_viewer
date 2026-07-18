param(
    [string]$Configuration = "Release",
    [int]$CleanupProcessId = 0,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$stage = Join-Path $root "artifacts\$Configuration"
$hostSource = Join-Path $root "src\host\WindowDisplayHost\bin\x64\$Configuration\WindowDisplayHost.exe"
$driverSource = Join-Path $root "src\driver\WindowDisplayDriver\bin\x64\$Configuration\WindowDisplayDriver.dll"
$log = Join-Path $env:TEMP "MultiBoxViewer-runtime-smoke.log"

if ($CleanupProcessId) {
    Stop-Process -Id $CleanupProcessId -Force -ErrorAction SilentlyContinue
}
if (-not $SkipInstall) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Run this script as Administrator, or use -SkipInstall with an already running host."
    }
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class MultiBoxNative {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct DISPLAY_DEVICE {
        public int cb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string DeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceString;
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceID;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string DeviceKey;
    }
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr value);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumDisplayDevices(
        string device, uint index, ref DISPLAY_DEVICE display, uint flags);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(
        IntPtr hwnd, StringBuilder name, int count);
    public static string PrimaryDevice() {
        for (uint i=0;;i++) {
            var d = new DISPLAY_DEVICE(); d.cb = Marshal.SizeOf<DISPLAY_DEVICE>();
            if (!EnumDisplayDevices(null, i, ref d, 0)) return "";
            if ((d.StateFlags & 4) != 0) return d.DeviceName;
        }
    }
    public static int ViewerCount() {
        int count = 0;
        EnumWindows((hwnd, value) => {
            var name = new StringBuilder(128);
            if (GetClassName(hwnd, name, name.Capacity) > 0 && name.ToString() == "WindowDisplay.Viewer") count++;
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
"@

function Send-Command([uint32]$Command, [byte[]]$Payload = [byte[]]::new(0)) {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", "WindowDisplay.Host",
        [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::None)
    try {
        $pipe.Connect(5000)
        $writer = [System.IO.BinaryWriter]::new($pipe, [Text.Encoding]::UTF8, $true)
        $requestId = [uint32]([Environment]::TickCount -band 0xffffffffL)
        $writer.Write($Command)
        $writer.Write([uint32]$Payload.Length)
        $writer.Write($requestId)
        $writer.Write($Payload)
        $writer.Flush()
        $reader = [System.IO.BinaryReader]::new($pipe, [Text.Encoding]::UTF8, $true)
        $responseId = $reader.ReadUInt32()
        $status = $reader.ReadUInt32()
        $length = $reader.ReadUInt32()
        if ($responseId -ne $requestId) { throw "Mismatched response id" }
        if ($status -ne 0) { throw "Command $Command failed with Win32=$status" }
        $bytes = $reader.ReadBytes([int]$length)
        if ($bytes.Length -ne $length) { throw "Truncated response to command $Command" }
        return ,$bytes
    } finally {
        $pipe.Dispose()
    }
}

function Connector-Payload([uint32]$Connector) {
    return [BitConverter]::GetBytes($Connector)
}

function Create-Display([uint32]$Width, [uint32]$Height, [uint32]$Refresh, [bool]$OpenViewer) {
    $payload = [byte[]]::new(16)
    [BitConverter]::GetBytes($Width).CopyTo($payload, 0)
    [BitConverter]::GetBytes($Height).CopyTo($payload, 4)
    [BitConverter]::GetBytes($Refresh).CopyTo($payload, 8)
    [BitConverter]::GetBytes([uint32]$OpenViewer).CopyTo($payload, 12)
    $created = Send-Command 2 $payload
    if ($created.Length -ne 360) { throw "CreateDisplay did not return a monitor record" }
    return [BitConverter]::ToUInt32($created, 0)
}

function List-Displays {
    $bytes = Send-Command 6
    if ($bytes.Length -ne 2888) { throw "ListDisplays ABI mismatch: $($bytes.Length) bytes" }
    $count = [BitConverter]::ToUInt32($bytes, 4)
    $result = @()
    for ($i = 0; $i -lt $count; $i++) {
        $offset = 8 + 360 * $i
        $result += [pscustomobject]@{
            Connector = [BitConverter]::ToUInt32($bytes, $offset)
            Active = [BitConverter]::ToUInt32($bytes, $offset + 4)
            Width = [BitConverter]::ToUInt32($bytes, $offset + 12)
            Height = [BitConverter]::ToUInt32($bytes, $offset + 16)
            Refresh = [BitConverter]::ToUInt32($bytes, $offset + 20)
        }
    }
    return $result
}

Remove-Item $log -Force -ErrorAction SilentlyContinue
$primaryBefore = [MultiBoxNative]::PrimaryDevice()
"Primary before: $primaryBefore" | Add-Content $log

if (-not $SkipInstall) {
    Get-Process WindowDisplayHost -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item $hostSource $stage -Force
    Copy-Item $driverSource $stage -Force

    pnputil /remove-device "SWD\WINDOWDISPLAY\WINDOWDISPLAY" | Add-Content $log
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "sign-and-install-driver.ps1") -Configuration $Configuration |
        Add-Content $log

    $hostProcess = Start-Process (Join-Path $stage "WindowDisplayHost.exe") -ArgumentList "--prototype", "--elevated" -PassThru
} else {
    $hostProcess = Get-Process WindowDisplayHost -ErrorAction Stop | Select-Object -First 1
}
for ($attempt = 0; $attempt -lt 30; $attempt++) {
    try { Send-Command 1 | Out-Null; break } catch { if ($attempt -eq 29) { throw }; Start-Sleep -Milliseconds 250 }
}

foreach ($display in @(List-Displays)) {
    Send-Command 3 (Connector-Payload $display.Connector) | Out-Null
}

$connector = Create-Display 1280 720 60 $true

Start-Sleep -Seconds 2
$displays = @(List-Displays)
if ($displays.Count -ne 1) { throw "Expected one display after create; got $($displays.Count)" }
if ($displays[0].Width -ne 1280 -or $displays[0].Height -ne 720 -or $displays[0].Refresh -ne 60) {
    throw "Created mode mismatch: $($displays[0].Width)x$($displays[0].Height)@$($displays[0].Refresh)"
}
if ([MultiBoxNative]::ViewerCount() -lt 1) { throw "PiP viewer window was not created" }

Send-Command 4 (Connector-Payload $connector) | Out-Null
Send-Command 5 (Connector-Payload $connector) | Out-Null
Start-Sleep -Seconds 2
if (@(List-Displays).Count -ne 1) { throw "Display disappeared after restart" }

$secondConnector = Create-Display 1920 1080 60 $false
Start-Sleep -Seconds 1
if (@(List-Displays).Count -ne 2) { throw "Expected two displays in multi-display test" }
Send-Command 4 (Connector-Payload $secondConnector) | Out-Null

$hostProcess.Refresh()
if ($hostProcess.MainWindowHandle -ne 0) {
    $move = [byte[]]::new(12)
    [BitConverter]::GetBytes([uint64]($hostProcess.MainWindowHandle.ToInt64())).CopyTo($move, 0)
    [BitConverter]::GetBytes([uint32]$secondConnector).CopyTo($move, 8)
    Send-Command 7 $move | Out-Null
}

Send-Command 5 (Connector-Payload $secondConnector) | Out-Null
Send-Command 3 (Connector-Payload $secondConnector) | Out-Null
Send-Command 3 (Connector-Payload $connector) | Out-Null
Start-Sleep -Seconds 1
if (@(List-Displays).Count -ne 0) { throw "Display remained after remove" }

for ($cycle = 1; $cycle -le 3; $cycle++) {
    $cycleConnector = Create-Display 1280 720 60 $false
    Send-Command 5 (Connector-Payload $cycleConnector) | Out-Null
    Send-Command 3 (Connector-Payload $cycleConnector) | Out-Null
}
if (@(List-Displays).Count -ne 0) { throw "Churn test left an active display" }

$primaryAfter = [MultiBoxNative]::PrimaryDevice()
if ($primaryBefore -and $primaryAfter -ne $primaryBefore) {
    throw "Primary display changed from $primaryBefore to $primaryAfter"
}

"PASS hostPid=$($hostProcess.Id) primary=$primaryAfter" | Tee-Object -FilePath $log -Append
