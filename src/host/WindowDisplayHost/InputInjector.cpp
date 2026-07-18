#include "pch.h"
#include "InputInjector.h"

void InputInjector::SetTarget(const RECT& desktopMonitor, const RECT& imageClientRect)
{
    m_monitor = desktopMonitor; m_image = imageClientRect;
}

bool InputInjector::ToAbsolute(LPARAM point, LONG& x, LONG& y) const
{
    const LONG cx = GET_X_LPARAM(point), cy = GET_Y_LPARAM(point);
    if (cx < m_image.left || cx >= m_image.right || cy < m_image.top || cy >= m_image.bottom) return false;
    const LONG width = std::max(1L, m_image.right - m_image.left);
    const LONG height = std::max(1L, m_image.bottom - m_image.top);
    const LONG virtualX = m_monitor.left + (cx - m_image.left) * std::max(1L, m_monitor.right - m_monitor.left) / width;
    const LONG virtualY = m_monitor.top + (cy - m_image.top) * std::max(1L, m_monitor.bottom - m_monitor.top) / height;
    x = MulDiv(virtualX - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1));
    y = MulDiv(virtualY - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1));
    return true;
}

bool InputInjector::InjectMouse(UINT message, WPARAM wParam, LPARAM lParam) const
{
    LONG x = 0, y = 0;
    if (!ToAbsolute(lParam, x, y)) return false;
    INPUT input{}; input.type = INPUT_MOUSE;
    input.mi.dx = x; input.mi.dy = y; input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    switch (message)
    {
    case WM_LBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN; break;
    case WM_LBUTTONUP: input.mi.dwFlags |= MOUSEEVENTF_LEFTUP; break;
    case WM_RBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN; break;
    case WM_RBUTTONUP: input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP; break;
    case WM_MBUTTONDOWN: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN; break;
    case WM_MBUTTONUP: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP; break;
    case WM_MOUSEWHEEL: input.mi.mouseData = GET_WHEEL_DELTA_WPARAM(wParam); input.mi.dwFlags |= MOUSEEVENTF_WHEEL; break;
    default: break;
    }
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool InputInjector::InjectKey(UINT message, WPARAM wParam, LPARAM lParam) const
{
    if (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP) return false;
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = static_cast<WORD>(wParam);
    input.ki.wScan = static_cast<WORD>((lParam >> 16) & 0xff);
    if (message == WM_KEYUP || message == WM_SYSKEYUP) input.ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(input)) == 1;
}

void InputInjector::ReleaseAllKeys() const
{
    for (UINT key = 1; key < 256; ++key)
        if (GetAsyncKeyState(static_cast<int>(key)) & 0x8000)
        {
            INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = static_cast<WORD>(key); input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(input));
        }
}
