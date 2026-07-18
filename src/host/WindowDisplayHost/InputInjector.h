#pragma once
#include "pch.h"

class InputInjector
{
public:
    void SetTarget(const RECT& desktopMonitor, const RECT& imageClientRect);
    bool InjectMouse(UINT message, WPARAM wParam, LPARAM lParam) const;
    bool InjectKey(UINT message, WPARAM wParam, LPARAM lParam) const;
    void ReleaseAllKeys() const;

private:
    bool ToAbsolute(LPARAM point, LONG& x, LONG& y) const;
    RECT m_monitor{};
    RECT m_image{};
};
