# WindowDisplay development notes

## Components

### Driver (`WindowDisplayDriver`)

- UMDF 2.25 + IddCx 1.4  
- Monitors are **not** created at adapter init — only via IOCTL  
- IOCTLs: `IOCTL_WD_PLUG_IN`, `PLUG_OUT`, `UPDATE_MODE`, `GET_STATUS`, `RESTART_MONITOR`  
- EDID-less monitors; modes from product presets (720p–4K, 30/60 Hz)  
- Swap-chain loop copies frames into a named shared texture `Local\WindowDisplay.Texture.{n}` and updates `Local\WindowDisplay.SharedState`  
- Device interface GUID: `GUID_DEVINTERFACE_WINDOWDISPLAY`  
- Hardware IDs: `Root\WindowDisplay`, `WindowDisplay`  

### Host (`WindowDisplayHost`)

- Creates software device (`SwDeviceCreate`) bound to the INF hardware ID  
- Opens driver via SetupAPI device interface  
- Extends desktop and places new monitors to the right of primary (`SetDisplayConfig`)  
- Viewer: D3D11, Fit/Fill/Actual, fullscreen toolbar auto-hide, always-on-top  
- Frame path: named shared texture, then Desktop Duplication fallback  
- Input: letterbox-aware mapping + `SendInput`, release-all-keys on focus loss  
- Safe removal / off-screen rescue helpers  
- Named pipe `\\.\pipe\WindowDisplay.Host` for the controller  
- `--prototype` UI: Create / Open Viewer / Restart / Remove  

### Controller (`WindowDisplay`)

- WPF home screen matching the product wireframe  
- Three-action create with Recommended (1920×1080) preselected  
- Display cards, app picker, layouts under `%LocalAppData%\WindowDisplay`  
- Repair + diagnostic report services  
- Light/dark theme  

## Practical limits (v1)

- Product max: **8** virtual monitors  
- Recommended: **4** active 1080p viewers  
- UI should warn on heavy configurations  

## Performance targets (measure before publishing)

- One 1080p viewer: 60 FPS, &lt;50 ms ordinary latency, no tearing  
- Four 1080p: visible viewers up to 60 FPS; minimize idle/minimized cost  

## Signing

| Build | Signing |
|-------|---------|
| Development | Test signing |
| Public | Microsoft-recognized driver signing via Partner Center |
| WHQL | HLK + submission |

Do not treat signing as last-minute packaging.
