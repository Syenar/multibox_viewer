# Release gates

Track each gate before advancing stages. Mark `[x]` only after evidence on real hardware.

## Stage 1 — Driver foundation

- [ ] Install driver package successfully  
- [ ] One virtual monitor appears in Windows Display Settings  
- [ ] 720p and 1080p modes selectable  
- [ ] Connect/disconnect via host controls  
- [ ] **Gate:** create and remove the monitor **100** consecutive times without crash, reboot, or stranded display  

## Stage 2 — Local viewer proof

- [ ] Frames appear in native viewer  
- [ ] Fit scaling  
- [ ] Move/resize viewer  
- [ ] Recover after DXGI/device reset  
- [ ] **Gate:** continuous viewer **8 hours** without frame loss, runaway memory, or driver failure  

## Stage 3 — Input proof

- [ ] Mouse, keyboard, wheel  
- [ ] Focus safety (release keys)  
- [ ] Scaling-aware mapping  
- [ ] **Gate:** correct input at multiple viewer sizes, Windows scaling (100–200%), arrangements, orientations  

## Stage 4 — Multi-display engine

- [ ] Multiple monitors + separate pipelines  
- [ ] Multiple viewers + throttling  
- [ ] Safe removal moves apps back  
- [ ] **Gate:** four simultaneous 1080p displays stable through create, resize, sleep, resume, remove  

## Stage 5 — Usable controller

- [ ] Home screen, three-action create, cards, picker, settings, error states  
- [ ] **Gate:** every core workflow ≤ 3–5 actions  

## Stage 6 — Layout and recovery

- [ ] Save/restore layouts  
- [ ] Reboot persistence  
- [ ] Off-screen rescue  
- [ ] **Gate:** restore works after reboot, sleep, driver restart, changed physical monitors  

## Stage 7 — Installation and compatibility

- [ ] Unified installer, upgrade, clean uninstall  
- [ ] Signed release driver  
- [ ] Matrix: Win11 releases; AMD/NVIDIA/Intel; 1+/multi physical; 100–200% scaling; mixed resolutions; sleep/hibernate/fast startup/reboot; standard + non-admin users  

## Stage 8 — Release candidate

- [ ] No open critical defects  
- [ ] No unexplained driver crashes  
- [ ] No reproducible stranded windows  
- [ ] No persistent virtual monitors after uninstall  
- [ ] No memory growth in long-duration tests  
- [ ] All core tasks stay within five actions  

## Current engineering status

| Area | Status |
|------|--------|
| Solution scaffold | Done |
| Driver compiles (x64) | Done |
| Host compiles + prototype UI | Done |
| Controller compiles (WPF) | Done |
| Installer scaffold | Done |
| Test-sign / 100× create-remove gate | Pending on machine (needs elevation + reboot + soak) |
| 8-hour viewer soak | Pending |
| Partner Center signing | Pending product milestone |
