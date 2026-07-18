# WindowDisplay

Windows 11 virtual displays with a movable viewer for each monitor. Built on Microsoft’s Indirect Display Driver model (IddCx).

## Architecture

| Component | Path | Role |
|-----------|------|------|
| **Driver** | `src/driver/WindowDisplayDriver` | UMDF IddCx driver — create/remove up to 8 virtual monitors, advertise modes, process swap chains, publish shared frames |
| **Host** | `src/host/WindowDisplayHost` | C++/D3D11 — software device, topology, viewers, input, named-pipe control |
| **Controller** | `src/controller/WindowDisplay` | .NET 8 WPF — home UI, create dialog, app picker, layouts, repair |

Shared protocol: `src/shared/WindowDisplayProtocol.h`

## Correct build order (product plan)

1. Prove real virtual monitor create/remove  
2. Prove local frame viewing  
3. Prove accurate input  
4. Prove multi-display  
5. Ship simple controller UI  
6. Layouts + recovery  
7. Signing + installer  
8. Polish + release  

First internal deliverable: host `--prototype` with **Create Display / Open Viewer / Restart Display / Remove Display**.

## Prerequisites

- Windows 11  
- Visual Studio 2022 Build Tools (C++), Windows SDK 10.0.26100  
- Windows Driver Kit 10.0.26100 (headers/libs)  
- .NET 8 SDK  
- NuGet CLI (`winget install Microsoft.NuGet`)  
- For loading unsigned drivers in development: test signing (`tools/enable-testsigning.ps1`) + reboot  

## Build

```powershell
nuget restore .\packages.config -PackagesDirectory .\packages
.\tools\build.ps1 -Configuration Release
```

Outputs stage to `artifacts\Release\`.

## Development install

```powershell
# Elevated
.\tools\enable-testsigning.ps1   # once, then reboot
.\tools\build.ps1
.\tools\install-driver.ps1
```

Then run the engineering prototype:

```powershell
.\artifacts\Release\WindowDisplayHost.exe --prototype
```

Or the full controller:

```powershell
.\artifacts\Release\WindowDisplay.exe
```

## Uninstall

```powershell
.\tools\uninstall-driver.ps1
```

## Version 1 scope (summary)

**In:** real virtual displays, create/remove, ≤8 monitors, auto extend/place, viewers (Fit/Fill/Actual), mouse/keyboard, app move, presets, orientation, names, layouts, reboot persistence, sleep/reset recovery, off-screen rescue, light/dark UI, diagnostics, clean uninstall.

**Out:** HDR, touch/pen, relative gaming mouse, per-display audio, remote/streaming, high refresh gaming, protected content, macOS/Linux.

## Release gates

See [docs/GATES.md](docs/GATES.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

Driver signing for public builds is a formal milestone (Partner Center + EV certificate; WHQL via HLK).

## License

**Proprietary — all rights reserved.** See [LICENSE](LICENSE).

You may not copy, modify, redistribute, or reuse this software (or any part of it) in another product without a separate written license from Syenar. Public visibility of this repository does not grant any open-source rights.

Third-party notices (including Microsoft Windows Driver Samples / IddCx patterns under MIT) are listed in [NOTICE](NOTICE).
