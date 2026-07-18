#pragma once
#include "pch.h"
#include "../../shared/WindowDisplayProtocol.h"

class DisplayTopology
{
public:
    // Capture the current primary GDI device (e.g. \\.\DISPLAY1) before plugging a virtual monitor.
    std::wstring GetPrimaryDeviceName() const;

    // Extend desktop and place the virtual monitor beside the primary — never as the main display.
    // preferredPrimaryDevice: GDI name captured before plug-in; empty = best-effort current primary.
    bool ExtendAndPlace(
        UINT32 connector,
        const WdMode& preferredMode,
        std::wstring* deviceName = nullptr,
        const std::wstring& preferredPrimaryDevice = {});

    // Force a specific GDI device to remain/become the Windows primary (origin at 0,0).
    bool ForcePrimaryDevice(const std::wstring& primaryDevice) const;

    bool GetMonitorRect(const std::wstring& gdiDeviceName, RECT& rect) const;
    bool GetPrimaryRect(RECT& rect) const;
    // Virtual MultiBox GDI devices ordered left-to-right (then by name).
    std::vector<std::wstring> ListVirtualDisplayDevices() const;
    // Prefer connector ordinal among virtual displays; falls back to first.
    std::wstring FindWindowDisplayDevice(UINT32 connector) const;

    struct MonitorDevice
    {
        std::wstring deviceName;   // \\.\DISPLAYn
        std::wstring friendlyName; // adapter string
        bool isPrimary = false;
    };
    // All active, non-mirroring GDI displays (physical + virtual).
    std::vector<MonitorDevice> ListMonitorDevices() const;
};
