#pragma once
#include "pch.h"

class WindowMover
{
public:
    static bool MoveWindowToDisplay(HWND window, const RECT& monitorRect);
    static void RescueOffscreenWindows();
private:
    static BOOL CALLBACK RescueEnum(HWND window, LPARAM);
};
