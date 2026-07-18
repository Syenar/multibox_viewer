#include "pch.h"
#include "DisplayTopology.h"
#include <algorithm>

namespace
{
    constexpr UINT32 kApplyFlags =
        SDC_APPLY |
        SDC_USE_SUPPLIED_DISPLAY_CONFIG |
        SDC_SAVE_TO_DATABASE |
        SDC_ALLOW_CHANGES |
        SDC_VIRTUAL_MODE_AWARE;

    struct EnumData { std::wstring name; RECT rect{}; bool found = false; };
    BOOL CALLBACK FindMonitor(HMONITOR, HDC, LPRECT rect, LPARAM value)
    {
        auto* data = reinterpret_cast<EnumData*>(value);
        MONITORINFOEXW info{ sizeof(info) };
        if (GetMonitorInfoW(MonitorFromRect(rect, MONITOR_DEFAULTTONULL), &info) &&
            _wcsicmp(info.szDevice, data->name.c_str()) == 0)
        {
            data->rect = *rect; data->found = true; return FALSE;
        }
        return TRUE;
    }

    bool ReadConfig(std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes)
    {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return false;
        paths.resize(pathCount); modes.resize(modeCount);
        return QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) == ERROR_SUCCESS;
    }

    std::wstring SourceName(const DISPLAYCONFIG_PATH_INFO& path)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size = sizeof(name);
        name.header.adapterId = path.sourceInfo.adapterId;
        name.header.id = path.sourceInfo.id;
        return DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS ? name.viewGdiDeviceName : L"";
    }

    bool IsVirtualDisplayTarget(const DISPLAYCONFIG_PATH_INFO& path)
    {
        DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        name.header.size = sizeof(name);
        name.header.adapterId = path.targetInfo.adapterId;
        name.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS) return false;
        const std::wstring friendly = name.monitorFriendlyDeviceName;
        const std::wstring devicePath = name.monitorDevicePath;
        return friendly.find(L"MultiBox Viewer") != std::wstring::npos ||
            friendly.find(L"WindowDisplay") != std::wstring::npos ||
            devicePath.find(L"WindowDisplay") != std::wstring::npos ||
            devicePath.find(L"MultiBox") != std::wstring::npos ||
            devicePath.find(L"SWD#WindowDisplay") != std::wstring::npos ||
            devicePath.find(L"SWD\\WindowDisplay") != std::wstring::npos;
    }

    bool IsVirtualGdiDevice(const std::wstring& gdiName)
    {
        if (gdiName.empty()) return false;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
        {
            if (_wcsicmp(device.DeviceName, gdiName.c_str()) != 0) continue;
            const std::wstring name = device.DeviceString;
            const std::wstring id = device.DeviceID;
            return name.find(L"MultiBox") != std::wstring::npos ||
                name.find(L"WindowDisplay") != std::wstring::npos ||
                id.find(L"WindowDisplay") != std::wstring::npos ||
                id.find(L"WINDOWDISPLAY") != std::wstring::npos;
        }
        return false;
    }

    DISPLAYCONFIG_SOURCE_MODE* SourceMode(
        DISPLAYCONFIG_PATH_INFO& path,
        std::vector<DISPLAYCONFIG_MODE_INFO>& modes)
    {
        if (path.sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID ||
            path.sourceInfo.modeInfoIdx >= modes.size())
        {
            return nullptr;
        }
        auto& mode = modes[path.sourceInfo.modeInfoIdx];
        if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) return nullptr;
        return &mode.sourceMode;
    }

    bool PathMatchesDevice(const DISPLAYCONFIG_PATH_INFO& path, const std::wstring& device)
    {
        if (device.empty()) return false;
        return _wcsicmp(SourceName(path).c_str(), device.c_str()) == 0;
    }

    struct ActiveDisplay
    {
        std::wstring name;
        DEVMODEW mode{};
        bool primary = false;
    };

    std::vector<ActiveDisplay> EnumActiveDisplays()
    {
        std::vector<ActiveDisplay> displays;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
        {
            if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            if (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;

            ActiveDisplay entry;
            entry.name = device.DeviceName;
            entry.primary = (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
            entry.mode = {};
            entry.mode.dmSize = sizeof(entry.mode);
            if (!EnumDisplaySettingsExW(entry.name.c_str(), ENUM_CURRENT_SETTINGS, &entry.mode, 0))
                continue;
            displays.push_back(entry);
        }
        return displays;
    }
}

std::wstring DisplayTopology::GetPrimaryDeviceName() const
{
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
    {
        if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) &&
            (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
        {
            return device.DeviceName;
        }
    }

    MONITORINFOEXW info{ sizeof(info) };
    const HMONITOR monitor = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &info)) return {};
    return info.szDevice;
}

bool DisplayTopology::ForcePrimaryDevice(const std::wstring& primaryDevice) const
{
    if (primaryDevice.empty()) return false;

    // Never promote a MultiBox virtual / removable target to primary.
    if (IsVirtualGdiDevice(primaryDevice)) return false;

    auto displays = EnumActiveDisplays();
    if (displays.empty()) return false;

    ActiveDisplay* wanted = nullptr;
    for (auto& display : displays)
    {
        if (_wcsicmp(display.name.c_str(), primaryDevice.c_str()) == 0)
        {
            wanted = &display;
            break;
        }
    }
    if (!wanted) return false;

    // Already primary and at origin — still re-assert so Windows doesn't keep a stolen layout.
    const LONG originX = wanted->mode.dmPosition.x;
    const LONG originY = wanted->mode.dmPosition.y;

    LONG nextX = static_cast<LONG>(wanted->mode.dmPelsWidth);
    for (auto& display : displays)
    {
        DEVMODEW mode = display.mode;
        mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;

        const bool isPrimary = (&display == wanted);
        if (isPrimary)
        {
            mode.dmPosition.x = 0;
            mode.dmPosition.y = 0;
        }
        else
        {
            // Shift relative to the chosen primary, then ensure non-primary never sits at (0,0).
            LONG x = display.mode.dmPosition.x - originX;
            LONG y = display.mode.dmPosition.y - originY;
            if (x == 0 && y == 0)
            {
                x = nextX;
                y = 0;
                nextX += static_cast<LONG>(display.mode.dmPelsWidth);
            }
            mode.dmPosition.x = x;
            mode.dmPosition.y = y;
        }

        const DWORD flags = CDS_UPDATEREGISTRY | CDS_NORESET |
            (isPrimary ? CDS_SET_PRIMARY : 0);
        if (ChangeDisplaySettingsExW(display.name.c_str(), &mode, nullptr, flags, nullptr) != DISP_CHANGE_SUCCESSFUL)
            return false;
    }

    return ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

bool DisplayTopology::GetMonitorRect(const std::wstring& gdiDeviceName, RECT& rect) const
{
    EnumData data{ gdiDeviceName };
    EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&data));
    if (data.found) rect = data.rect;
    return data.found;
}

bool DisplayTopology::GetPrimaryRect(RECT& rect) const
{
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
    rect = info.rcMonitor; return true;
}

std::vector<std::wstring> DisplayTopology::ListVirtualDisplayDevices() const
{
    struct Item { std::wstring name; LONG x = 0; LONG y = 0; };
    std::vector<Item> items;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (ReadConfig(paths, modes))
    {
        for (auto& path : paths)
        {
            if (!IsVirtualDisplayTarget(path)) continue;
            auto* source = SourceMode(path, modes);
            Item item;
            item.name = SourceName(path);
            if (item.name.empty()) continue;
            if (source)
            {
                item.x = source->position.x;
                item.y = source->position.y;
            }
            items.push_back(item);
        }
    }

    if (items.empty())
    {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
        {
            if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            if (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
            const std::wstring name = device.DeviceString;
            const std::wstring id = device.DeviceID;
            if (name.find(L"MultiBox") == std::wstring::npos &&
                name.find(L"WindowDisplay") == std::wstring::npos &&
                id.find(L"WindowDisplay") == std::wstring::npos &&
                id.find(L"WINDOWDISPLAY") == std::wstring::npos)
            {
                continue;
            }
            Item item;
            item.name = device.DeviceName;
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsExW(item.name.c_str(), ENUM_CURRENT_SETTINGS, &mode, 0))
            {
                item.x = mode.dmPosition.x;
                item.y = mode.dmPosition.y;
            }
            items.push_back(item);
        }
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b)
    {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.name < b.name;
    });

    // Unique preserve order
    std::vector<std::wstring> result;
    for (const auto& item : items)
    {
        if (std::find(result.begin(), result.end(), item.name) == result.end())
            result.push_back(item.name);
    }
    return result;
}

std::wstring DisplayTopology::FindWindowDisplayDevice(UINT32 connector) const
{
    const auto devices = ListVirtualDisplayDevices();
    if (devices.empty()) return {};
    if (connector < devices.size()) return devices[connector];
    return devices.front();
}

std::vector<DisplayTopology::MonitorDevice> DisplayTopology::ListMonitorDevices() const
{
    std::vector<MonitorDevice> result;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
    {
        if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        if (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
        MonitorDevice item;
        item.deviceName = device.DeviceName;
        item.friendlyName = device.DeviceString;
        item.isPrimary = (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        result.push_back(std::move(item));
    }
    return result;
}

bool DisplayTopology::ExtendAndPlace(
    UINT32 connector,
    const WdMode& preferredMode,
    std::wstring* deviceName,
    const std::wstring& preferredPrimaryDevice)
{
    UNREFERENCED_PARAMETER(connector);

    std::wstring primaryDevice = preferredPrimaryDevice;
    if (primaryDevice.empty())
        primaryDevice = GetPrimaryDeviceName();

    // Do NOT use SDC_TOPOLOGY_EXTEND — on IddCx it often promotes the new target to primary.
    // Wait for the virtual path, then apply an explicit extended layout.
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    std::wstring result;

    for (int attempt = 0; attempt < 40; ++attempt)
    {
        if (!ReadConfig(paths, modes))
        {
            Sleep(75);
            continue;
        }

        DISPLAYCONFIG_SOURCE_MODE* primaryMode = nullptr;
        std::vector<std::pair<DISPLAYCONFIG_PATH_INFO*, DISPLAYCONFIG_SOURCE_MODE*>> virtuals;
        std::vector<DISPLAYCONFIG_SOURCE_MODE*> others;

        for (auto& path : paths)
        {
            auto* source = SourceMode(path, modes);
            if (!source) continue;

            const std::wstring gdi = SourceName(path);
            const bool isVirtual = IsVirtualDisplayTarget(path) || IsVirtualGdiDevice(gdi);

            if (isVirtual)
            {
                virtuals.push_back({ &path, source });
                if (result.empty()) result = gdi;
            }
            else if (PathMatchesDevice(path, primaryDevice))
            {
                primaryMode = source;
            }
            else
            {
                others.push_back(source);
            }
        }

        if (virtuals.empty())
        {
            Sleep(75);
            continue;
        }

        if (!primaryMode)
        {
            for (auto& path : paths)
            {
                const std::wstring gdi = SourceName(path);
                if (IsVirtualDisplayTarget(path) || IsVirtualGdiDevice(gdi)) continue;
                auto* source = SourceMode(path, modes);
                if (!source) continue;
                primaryMode = source;
                primaryDevice = gdi;
                break;
            }
        }

        if (primaryMode)
        {
            primaryMode->position.x = 0;
            primaryMode->position.y = 0;

            LONG nextX = static_cast<LONG>(primaryMode->width);
            for (auto* other : others)
            {
                if (other->position.x == 0 && other->position.y == 0)
                {
                    other->position.x = nextX;
                    other->position.y = 0;
                    nextX += static_cast<LONG>(other->width);
                }
            }

            for (auto& entry : virtuals)
            {
                auto* virtualMode = entry.second;
                if (preferredMode.Width && preferredMode.Height)
                {
                    virtualMode->width = preferredMode.Width;
                    virtualMode->height = preferredMode.Height;
                }
                virtualMode->position.x = nextX;
                virtualMode->position.y = 0;
                nextX += static_cast<LONG>(virtualMode->width);
            }

            SetDisplayConfig(
                static_cast<UINT32>(paths.size()), paths.data(),
                static_cast<UINT32>(modes.size()), modes.data(),
                kApplyFlags);
        }

        // Authoritative primary restore — works even when CCD identification is flaky.
        ForcePrimaryDevice(primaryDevice);

        // Place virtuals to the right again via DEVMODE in case CCD ignored positions.
        {
            auto displays = EnumActiveDisplays();
            LONG primaryWidth = 1920;
            for (const auto& display : displays)
            {
                if (_wcsicmp(display.name.c_str(), primaryDevice.c_str()) == 0)
                {
                    primaryWidth = static_cast<LONG>(display.mode.dmPelsWidth);
                    break;
                }
            }

            LONG nextX = primaryWidth;
            for (const auto& display : displays)
            {
                if (_wcsicmp(display.name.c_str(), primaryDevice.c_str()) == 0) continue;
                if (IsVirtualGdiDevice(display.name) || display.name == result) continue;
                nextX = (std::max)(nextX,
                    display.mode.dmPosition.x + static_cast<LONG>(display.mode.dmPelsWidth));
            }
            bool dirty = false;
            for (auto& display : displays)
            {
                if (_wcsicmp(display.name.c_str(), primaryDevice.c_str()) == 0) continue;
                if (!IsVirtualGdiDevice(display.name) && display.name != result) continue;

                DEVMODEW mode = display.mode;
                mode.dmFields = DM_POSITION;
                if (preferredMode.Width && preferredMode.Height &&
                    (IsVirtualGdiDevice(display.name) || display.name == result))
                {
                    mode.dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT;
                    mode.dmPelsWidth = preferredMode.Width;
                    mode.dmPelsHeight = preferredMode.Height;
                }
                mode.dmPosition.x = nextX;
                mode.dmPosition.y = 0;
                nextX += static_cast<LONG>(mode.dmPelsWidth);
                if (ChangeDisplaySettingsExW(display.name.c_str(), &mode, nullptr,
                        CDS_UPDATEREGISTRY | CDS_NORESET, nullptr) == DISP_CHANGE_SUCCESSFUL)
                {
                    dirty = true;
                }
            }
            if (dirty)
                ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);

            // Re-assert primary after secondary moves.
            ForcePrimaryDevice(primaryDevice);
        }

        const std::wstring nowPrimary = GetPrimaryDeviceName();
        const bool ok =
            !nowPrimary.empty() &&
            _wcsicmp(nowPrimary.c_str(), primaryDevice.c_str()) == 0 &&
            !IsVirtualGdiDevice(nowPrimary);

        if (ok)
        {
            if (deviceName) *deviceName = result;
            return !result.empty();
        }

        Sleep(100);
    }

    // Last attempt: force primary even if virtual GDI name was never resolved.
    ForcePrimaryDevice(primaryDevice);
    if (deviceName) *deviceName = result;
    return !result.empty();
}
