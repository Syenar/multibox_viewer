#include "pch.h"
#include "WindowMover.h"
#include "ViewerWindow.h"

namespace
{
    bool Eligible(HWND w)
    {
        if (!IsWindowVisible(w) || ViewerWindow::IsViewerWindow(w)) return false;
        const LONG_PTR style = GetWindowLongPtrW(w, GWL_STYLE);
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        return (style & WS_CHILD) == 0 && (ex & WS_EX_TOOLWINDOW) == 0 && GetWindow(w, GW_OWNER) == nullptr;
    }

    BOOL CALLBACK MonitorEnum(HMONITOR, HDC, LPRECT, LPARAM value)
    {
        *reinterpret_cast<bool*>(value) = true;
        return TRUE;
    }

    bool IntersectsAnyMonitor(const RECT& rect)
    {
        bool result = false;
        EnumDisplayMonitors(nullptr, &rect, MonitorEnum, reinterpret_cast<LPARAM>(&result));
        return result;
    }

    // Place the window onto the target monitor first so SW_MAXIMIZE uses that display.
    bool PlaceOnMonitor(HWND window, const RECT& monitorRect, bool fill)
    {
        if (!IsWindow(window)) return false;

        // Minimized windows sit at (-32000,-32000) and never appear in the PiP until restored.
        if (IsIconic(window))
            ShowWindow(window, SW_RESTORE);
        if (IsZoomed(window))
            ShowWindow(window, SW_RESTORE);

        const int monW = (std::max)(1L, monitorRect.right - monitorRect.left);
        const int monH = (std::max)(1L, monitorRect.bottom - monitorRect.top);

        RECT current{};
        GetWindowRect(window, &current);
        int width = current.right - current.left;
        int height = current.bottom - current.top;
        if (width < 200 || height < 100 || width > monW || height > monH)
        {
            width = (std::min)(monW, 1280);
            height = (std::min)(monH, 720);
        }
        if (fill)
        {
            width = monW;
            height = monH;
        }

        const int x = static_cast<int>(monitorRect.left) + (std::max)(0, (monW - width) / 2);
        const int y = static_cast<int>(monitorRect.top) + (std::max)(0, (monH - height) / 2);

        // Drop TOPMOST so the window can leave the primary and sit on the virtual display.
        const LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (ex & WS_EX_TOPMOST)
            SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        SetWindowPos(
            window,
            HWND_TOP,
            x,
            y,
            width,
            height,
            SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);

        // Maximize onto whichever monitor now contains the window (the virtual one).
        ShowWindow(window, SW_SHOW);
        ShowWindow(window, SW_MAXIMIZE);

        // Some exclusive / borderless games ignore Maximize — force a full-monitor rect.
        RECT after{};
        GetWindowRect(window, &after);
        const int afterCx = (after.left + after.right) / 2;
        const int afterCy = (after.top + after.bottom) / 2;
        const bool onTarget =
            afterCx >= monitorRect.left && afterCx < monitorRect.right &&
            afterCy >= monitorRect.top && afterCy < monitorRect.bottom;
        if (!onTarget || IsIconic(window))
        {
            ShowWindow(window, SW_RESTORE);
            SetWindowPos(
                window,
                HWND_TOP,
                monitorRect.left,
                monitorRect.top,
                monW,
                monH,
                SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        }

        SetForegroundWindow(window);
        return true;
    }
}

bool WindowMover::MoveWindowToDisplay(HWND window, const RECT& monitorRect)
{
    return PlaceOnMonitor(window, monitorRect, true);
}

BOOL CALLBACK WindowMover::RescueEnum(HWND window, LPARAM)
{
    if (!Eligible(window)) return TRUE;
    RECT rect{};
    GetWindowRect(window, &rect);
    if (IntersectsAnyMonitor(rect)) return TRUE;
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY), &info);
    MoveWindowToDisplay(window, info.rcWork);
    return TRUE;
}

void WindowMover::RescueOffscreenWindows() { EnumWindows(RescueEnum, 0); }
