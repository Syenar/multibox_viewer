#pragma once

#include <windows.h>
#include <winioctl.h>
#include <guiddef.h>

// WindowDisplay shared protocol between driver, host, and controller.
// Keep this header C-compatible for potential C# P/Invoke interop helpers.

#ifdef __cplusplus
extern "C" {
#endif

// {8F3C2A91-6B4E-4D17-9C8A-1E5F0D2B7A44}
// Defined once in WindowDisplayGuids.cpp / driver or host translation unit.
EXTERN_C const GUID GUID_DEVINTERFACE_WINDOWDISPLAY;

#define WD_MAX_MONITORS                 8
#define WD_MAX_NAME_CHARS               64
#define WD_MAX_MODES                    16
#define WD_PIPE_NAME                    L"\\\\.\\pipe\\WindowDisplay.Host"
#define WD_SHARED_STATE_NAME            L"Global\\WindowDisplay.SharedState"
#define WD_FRAME_EVENT_PREFIX           L"Global\\WindowDisplay.FrameReady."
#define WD_PIXEL_BUFFER_PREFIX          L"Global\\WindowDisplay.Pixels."
#define WD_MAX_PIXEL_WIDTH              2560
#define WD_MAX_PIXEL_HEIGHT             1440
#define WD_PIXEL_BUFFER_BYTES           (WD_MAX_PIXEL_WIDTH * WD_MAX_PIXEL_HEIGHT * 4)

#define WD_IOCTL_BASE                   0x8000

#define IOCTL_WD_PLUG_IN \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WD_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WD_PLUG_OUT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WD_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WD_UPDATE_MODE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WD_IOCTL_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WD_GET_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WD_IOCTL_BASE + 4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WD_RESTART_MONITOR \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WD_IOCTL_BASE + 5, METHOD_BUFFERED, FILE_ANY_ACCESS)

enum WdDisplayState : UINT32
{
    WdState_Inactive = 0,
    WdState_Starting = 1,
    WdState_Active = 2,
    WdState_Paused = 3,
    WdState_NeedsAttention = 4
};

enum WdScaleMode : UINT32
{
    WdScale_Fit = 0,
    WdScale_Fill = 1,
    WdScale_ActualSize = 2,
    WdScale_MatchWindow = 3
};

enum WdOrientation : UINT32
{
    WdOrientation_Landscape = 0,
    WdOrientation_Portrait = 1,
    WdOrientation_LandscapeFlipped = 2,
    WdOrientation_PortraitFlipped = 3
};

typedef struct WdMode
{
    UINT32 Width;
    UINT32 Height;
    UINT32 RefreshHz;
} WdMode;

typedef struct WdPlugInRequest
{
    UINT32 ConnectorIndex;          // 0..WD_MAX_MONITORS-1, or 0xFFFFFFFF for auto
    GUID   ContainerId;
    WdMode PreferredMode;
    UINT32 ModeCount;
    WdMode Modes[WD_MAX_MODES];
    WCHAR  FriendlyName[WD_MAX_NAME_CHARS];
} WdPlugInRequest;

typedef struct WdPlugInResponse
{
    UINT32 ConnectorIndex;
    GUID   ContainerId;
    UINT32 NtStatus;
} WdPlugInResponse;

typedef struct WdPlugOutRequest
{
    UINT32 ConnectorIndex;
} WdPlugOutRequest;

typedef struct WdCreateDisplayRequest
{
    WdMode Mode;
    UINT32 OpenViewer;
} WdCreateDisplayRequest;

typedef struct WdUpdateModeRequest
{
    UINT32 ConnectorIndex;
    WdMode Mode;
} WdUpdateModeRequest;

typedef struct WdMonitorRuntime
{
    UINT32 ConnectorIndex;
    UINT32 Active;
    WdDisplayState State;
    WdMode Mode;
    LUID   RenderAdapterLuid;
    UINT64 SharedTextureHandle;     // HANDLE value in the driver process (not directly usable cross-process)
    UINT32 TextureWidth;
    UINT32 TextureHeight;
    UINT64 FrameSerial;
    GUID   ContainerId;
    WCHAR  FriendlyName[WD_MAX_NAME_CHARS];
    WCHAR  SharedTextureName[80];   // name for ID3D11Device1::OpenSharedResourceByName
} WdMonitorRuntime;

// Cross-session BGRA fallback. DXGI named shares cannot cross UMDF session 0 → user session.
typedef struct WdPixelBuffer
{
    UINT32 Width;
    UINT32 Height;
    UINT32 Pitch;
    UINT32 Sequence;                // odd while writing, even when stable
    UINT64 FrameSerial;
    UINT8  Pixels[WD_PIXEL_BUFFER_BYTES];
} WdPixelBuffer;

typedef struct WdAdapterStatus
{
    UINT32 AdapterReady;
    UINT32 MonitorCount;
    WdMonitorRuntime Monitors[WD_MAX_MONITORS];
} WdAdapterStatus;

#ifdef __cplusplus
static_assert(sizeof(WdMonitorRuntime) == 360, "Controller ABI requires WdMonitorRuntime size 360");
static_assert(sizeof(WdAdapterStatus) == 2888, "Controller ABI requires WdAdapterStatus size 2888");
#endif

// Named shared memory layout published by the driver for low-latency frame metadata.
typedef struct WdSharedState
{
    UINT32 Version;                 // currently 1
    UINT32 MonitorCount;
    WdMonitorRuntime Monitors[WD_MAX_MONITORS];
} WdSharedState;

// Host <-> Controller named-pipe binary messages.
enum WdHostCommand : UINT32
{
    WdCmd_Ping = 1,
    WdCmd_CreateDisplay = 2,
    WdCmd_RemoveDisplay = 3,
    WdCmd_OpenViewer = 4,
    WdCmd_RestartDisplay = 5,
    WdCmd_ListDisplays = 6,
    WdCmd_MoveWindow = 7,
    WdCmd_SetScaleMode = 8,
    WdCmd_SetAlwaysOnTop = 9,
    WdCmd_SaveLayout = 10,
    WdCmd_RestoreLayout = 11,
    WdCmd_RescueOffscreen = 12,
    WdCmd_CreateDiagnostic = 13,
    WdCmd_SetDropWindows = 14,   // UINT32 enabled — drag title-bar onto PiP to move apps
    WdCmd_SetMirrorSource = 15,  // UINT32 connector + WCHAR[128] GDI device (empty = this display)
    WdCmd_SetViewerInteraction = 16, // UINT32 requireDoubleClick, UINT32 hoverFade
    WdCmd_Shutdown = 100
};

typedef struct WdHostRequestHeader
{
    UINT32 Command;
    UINT32 PayloadBytes;
    UINT32 RequestId;
} WdHostRequestHeader;

typedef struct WdHostResponseHeader
{
    UINT32 RequestId;
    UINT32 Status;          // 0 = success, Win32 error otherwise
    UINT32 PayloadBytes;
} WdHostResponseHeader;

#ifdef __cplusplus
}
#endif
