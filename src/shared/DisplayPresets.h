#pragma once

#include "WindowDisplayProtocol.h"

#ifdef __cplusplus
inline constexpr WdMode kWdPresetCompact{ 1280, 720, 60 };
inline constexpr WdMode kWdPresetWide{ 1600, 900, 60 };
inline constexpr WdMode kWdPresetRecommended{ 1920, 1080, 60 };
inline constexpr WdMode kWdPresetLarge{ 2560, 1440, 60 };
inline constexpr WdMode kWdPresetUhd{ 3840, 2160, 60 };

inline void WdFillDefaultModeList(WdMode* modes, UINT32* count)
{
    if (!modes || !count)
    {
        return;
    }

    modes[0] = kWdPresetCompact;
    modes[1] = kWdPresetWide;
    modes[2] = kWdPresetRecommended;
    modes[3] = kWdPresetLarge;
    modes[4] = kWdPresetUhd;
    // 30 Hz companions for v1 stability path
    modes[5] = WdMode{ 1280, 720, 30 };
    modes[6] = WdMode{ 1920, 1080, 30 };
    modes[7] = WdMode{ 2560, 1440, 30 };
    *count = 8;
}
#endif
