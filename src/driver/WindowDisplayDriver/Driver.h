/*++
Copyright (c) WindowDisplay project

WindowDisplay IddCx UMDF driver — dynamic virtual monitors via IOCTL.
--*/

#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

#include "WindowDisplayProtocol.h"
#include "DisplayPresets.h"
#include "Trace.h"

namespace Microsoft
{
    namespace WRL
    {
        namespace Wrappers
        {
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace WindowDisplay
{
    struct Direct3DDevice
    {
        Direct3DDevice(LUID AdapterLuid);
        Direct3DDevice();
        HRESULT Init();

        LUID AdapterLuid;
        Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
    };

    class IndirectDeviceContext;

    class SwapChainProcessor
    {
    public:
        SwapChainProcessor(
            IDDCX_SWAPCHAIN hSwapChain,
            std::shared_ptr<Direct3DDevice> Device,
            HANDLE NewFrameEvent,
            IndirectDeviceContext* DeviceContext,
            UINT32 ConnectorIndex);
        ~SwapChainProcessor();

    private:
        static DWORD CALLBACK RunThread(LPVOID Argument);
        void Run();
        void RunCore();

        IDDCX_SWAPCHAIN m_hSwapChain;
        std::shared_ptr<Direct3DDevice> m_Device;
        HANDLE m_hAvailableBufferEvent;
        IndirectDeviceContext* m_DeviceContext;
        UINT32 m_ConnectorIndex;
        Microsoft::WRL::Wrappers::Thread m_hThread;
        Microsoft::WRL::Wrappers::Event m_hTerminateEvent;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SharedTexture;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_StagingTexture;
        HANDLE m_SharedHandle = nullptr;
        UINT32 m_SharedWidth = 0;
        UINT32 m_SharedHeight = 0;
        WCHAR m_SharedName[80]{};
        HANDLE m_PixelMapping = nullptr;
        WdPixelBuffer* m_PixelView = nullptr;
    };

    class IndirectMonitorContext
    {
    public:
        IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor, IndirectDeviceContext* DeviceContext, UINT32 ConnectorIndex);
        virtual ~IndirectMonitorContext();

        void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
        void UnassignSwapChain();

        UINT32 ConnectorIndex() const { return m_ConnectorIndex; }
        bool GetModes(std::vector<WdMode>& Modes, WdMode& Preferred) const;

    private:
        IDDCX_MONITOR m_Monitor;
        IndirectDeviceContext* m_DeviceContext;
        UINT32 m_ConnectorIndex;
        std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
    };

    struct MonitorSlot
    {
        bool Occupied = false;
        IDDCX_MONITOR Monitor = nullptr;
        GUID ContainerId{};
        WdMode PreferredMode{};
        UINT32 ModeCount = 0;
        WdMode Modes[WD_MAX_MODES]{};
        WCHAR FriendlyName[WD_MAX_NAME_CHARS]{};
        WdDisplayState State = WdState_Inactive;
        LUID RenderAdapterLuid{};
        UINT64 SharedTextureHandle = 0;
        UINT32 TextureWidth = 0;
        UINT32 TextureHeight = 0;
        UINT64 FrameSerial = 0;
        HANDLE FrameEvent = nullptr;
        WCHAR SharedTextureName[80]{};
        BYTE Edid[128]{};
    };

    class IndirectDeviceContext
    {
    public:
        IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
        virtual ~IndirectDeviceContext();

        void InitAdapter();
        void SetAdapterReady(bool Ready, IDDCX_ADAPTER Adapter);

        NTSTATUS PlugInMonitor(_In_ const WdPlugInRequest* Request, _Out_ WdPlugInResponse* Response);
        NTSTATUS PlugOutMonitor(UINT32 ConnectorIndex);
        NTSTATUS UpdateMode(UINT32 ConnectorIndex, const WdMode& Mode);
        NTSTATUS RestartMonitor(UINT32 ConnectorIndex);
        void GetStatus(_Out_ WdAdapterStatus* Status);

        void PublishFrame(
            UINT32 ConnectorIndex,
            HANDLE SharedHandle,
            UINT32 Width,
            UINT32 Height,
            LUID RenderAdapterLuid,
            _In_opt_z_ const WCHAR* SharedTextureName);

        void SetMonitorState(UINT32 ConnectorIndex, WdDisplayState State);
        void SetCommittedMode(UINT32 ConnectorIndex, const WdMode& Mode);
        bool GetMonitorModes(UINT32 ConnectorIndex, std::vector<WdMode>& Modes, WdMode& Preferred) const;

        IDDCX_ADAPTER GetAdapter() const { return m_Adapter; }
        bool IsAdapterReady() const { return m_AdapterReady; }

    private:
        NTSTATUS CreateMonitorOnConnector(UINT32 ConnectorIndex);
        NTSTATUS DestroyMonitorOnConnector(UINT32 ConnectorIndex);
        void EnsureSharedState();
        void SyncSharedState();
        INT32 FindFreeConnector() const;

        WDFDEVICE m_WdfDevice;
        IDDCX_ADAPTER m_Adapter = {};
        bool m_AdapterReady = false;
        std::atomic_bool m_AdapterInitStarted = false;
        // IddCx may synchronously call mode callbacks from MonitorArrival while lifecycle
        // code holds this lock, so the same thread must be allowed to re-enter.
        mutable std::recursive_mutex m_Lock;
        MonitorSlot m_Slots[WD_MAX_MONITORS];
        HANDLE m_SharedMapping = nullptr;
        WdSharedState* m_SharedState = nullptr;
    };
}

struct IndirectDeviceContextWrapper
{
    WindowDisplay::IndirectDeviceContext* pContext;
    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

struct IndirectMonitorContextWrapper
{
    WindowDisplay::IndirectMonitorContext* pContext;
    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);
WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);
