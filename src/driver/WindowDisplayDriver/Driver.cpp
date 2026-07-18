/*++
Copyright (c) WindowDisplay project

IddCx UMDF driver with IOCTL-driven monitor lifecycle and shared-frame publish.
--*/

#include "Driver.h"
#include "EdidBlock.h"
#include "../../shared/WindowDisplayGuids.cpp"

#include <strsafe.h>
#include <stdio.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

using namespace std;
using namespace WindowDisplay;
using namespace Microsoft::WRL;

#pragma region Helpers

static inline void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& Mode, DWORD Width, DWORD Height, DWORD VSync, bool bMonitorMode)
{
    Mode.totalSize.cx = Mode.activeSize.cx = Width;
    Mode.totalSize.cy = Mode.activeSize.cy = Height;
    Mode.AdditionalSignalInfo.vSyncFreqDivider = bMonitorMode ? 0 : 1;
    Mode.AdditionalSignalInfo.videoStandard = 255;
    Mode.vSyncFreq.Numerator = VSync;
    Mode.vSyncFreq.Denominator = 1;
    Mode.hSyncFreq.Numerator = VSync * Height;
    Mode.hSyncFreq.Denominator = 1;
    Mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    Mode.pixelRate = ((UINT64)VSync) * ((UINT64)Width) * ((UINT64)Height);
}

static IDDCX_MONITOR_MODE CreateIddCxMonitorMode(DWORD Width, DWORD Height, DWORD VSync, IDDCX_MONITOR_MODE_ORIGIN Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER)
{
    IDDCX_MONITOR_MODE Mode = {};
    Mode.Size = sizeof(Mode);
    Mode.Origin = Origin;
    FillSignalInfo(Mode.MonitorVideoSignalInfo, Width, Height, VSync, true);
    return Mode;
}

static IDDCX_TARGET_MODE CreateIddCxTargetMode(DWORD Width, DWORD Height, DWORD VSync)
{
    IDDCX_TARGET_MODE Mode = {};
    Mode.Size = sizeof(Mode);
    FillSignalInfo(Mode.TargetVideoSignalInfo.targetVideoSignalInfo, Width, Height, VSync, false);
    return Mode;
}

static void BuildDefaultModes(vector<WdMode>& Modes)
{
    Modes.clear();
    WdMode list[WD_MAX_MODES]{};
    UINT32 count = 0;
    WdFillDefaultModeList(list, &count);
    for (UINT32 i = 0; i < count; ++i)
    {
        Modes.push_back(list[i]);
    }
}

#pragma endregion

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD WdDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY WdDeviceD0Entry;
EVT_IDD_CX_DEVICE_IO_CONTROL WdIoDeviceControl;
EVT_IDD_CX_ADAPTER_INIT_FINISHED WdAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES WdAdapterCommitModes;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION WdParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES WdMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES WdMonitorQueryModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN WdMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN WdMonitorUnassignSwapChain;

static void WdLog(PCSTR Msg)
{
    HANDLE h = CreateFileA("C:\\Windows\\Temp\\WindowDisplayDriver.log", FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, Msg, (DWORD)strlen(Msg), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, UINT, LPVOID)
{
    return TRUE;
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
    WdLog("DriverEntry enter");
    WDF_DRIVER_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    WDF_DRIVER_CONFIG_INIT(&Config, WdDeviceAdd);
    NTSTATUS Status = WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config, WDF_NO_HANDLE);
    char buf[80];
    sprintf_s(buf, "DriverEntry WdfDriverCreate=0x%08X", Status);
    WdLog(buf);
    return Status;
}

_Use_decl_annotations_
NTSTATUS WdDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDeviceD0Entry = WdDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

    IDD_CX_CLIENT_CONFIG IddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&IddConfig);
    IddConfig.EvtIddCxDeviceIoControl = WdIoDeviceControl;
    IddConfig.EvtIddCxAdapterInitFinished = WdAdapterInitFinished;
    IddConfig.EvtIddCxParseMonitorDescription = WdParseMonitorDescription;
    IddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = WdMonitorGetDefaultModes;
    IddConfig.EvtIddCxMonitorQueryTargetModes = WdMonitorQueryModes;
    IddConfig.EvtIddCxAdapterCommitModes = WdAdapterCommitModes;
    IddConfig.EvtIddCxMonitorAssignSwapChain = WdMonitorAssignSwapChain;
    IddConfig.EvtIddCxMonitorUnassignSwapChain = WdMonitorUnassignSwapChain;

    NTSTATUS Status = IddCxDeviceInitConfig(pDeviceInit, &IddConfig);
    {
        char buf[80];
        sprintf_s(buf, "WdDeviceAdd IddCxDeviceInitConfig=0x%08X", Status);
        WdLog(buf);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);
    Attr.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Object);
        if (pContext)
        {
            pContext->Cleanup();
        }
    };

    WDFDEVICE Device = nullptr;
    Status = WdfDeviceCreate(&pDeviceInit, &Attr, &Device);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WdfDeviceCreateDeviceInterface(Device, &GUID_DEVINTERFACE_WINDOWDISPLAY, nullptr);
    {
        char buf[96];
        sprintf_s(buf, "WdDeviceAdd CreateDeviceInterface=0x%08X", Status);
        WdLog(buf);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WdfDeviceSetDeviceInterfaceState(Device, &GUID_DEVINTERFACE_WINDOWDISPLAY, nullptr, TRUE);

    Status = IddCxDeviceInitialize(Device);
    {
        char buf[80];
        sprintf_s(buf, "WdDeviceAdd IddCxDeviceInitialize=0x%08X", Status);
        WdLog(buf);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext = new IndirectDeviceContext(Device);
    WdLog("WdDeviceAdd complete");
    return Status;
}

_Use_decl_annotations_
NTSTATUS WdDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext->InitAdapter();
    return STATUS_SUCCESS;
}

#pragma region Direct3DDevice

Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid) {}
Direct3DDevice::Direct3DDevice() { AdapterLuid = LUID{}; }

HRESULT Direct3DDevice::Init()
{
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
    if (FAILED(hr)) return hr;

    hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(hr)) return hr;

    hr = D3D11CreateDevice(
        Adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &Device,
        nullptr,
        &DeviceContext);
    return hr;
}

#pragma endregion

#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(
    IDDCX_SWAPCHAIN hSwapChain,
    shared_ptr<Direct3DDevice> Device,
    HANDLE NewFrameEvent,
    IndirectDeviceContext* DeviceContext,
    UINT32 ConnectorIndex)
    : m_hSwapChain(hSwapChain)
    , m_Device(Device)
    , m_hAvailableBufferEvent(NewFrameEvent)
    , m_DeviceContext(DeviceContext)
    , m_ConnectorIndex(ConnectorIndex)
{
    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor()
{
    SetEvent(m_hTerminateEvent.Get());
    if (m_hThread.Get())
    {
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }
    if (m_SharedHandle)
    {
        CloseHandle(m_SharedHandle);
        m_SharedHandle = nullptr;
    }
    if (m_PixelView)
    {
        UnmapViewOfFile(m_PixelView);
        m_PixelView = nullptr;
    }
    if (m_PixelMapping)
    {
        CloseHandle(m_PixelMapping);
        m_PixelMapping = nullptr;
    }
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    DWORD AvTask = 0;
    HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);
    RunCore();
    WdfObjectDelete((WDFOBJECT)m_hSwapChain);
    m_hSwapChain = nullptr;
    if (AvTaskHandle)
    {
        AvRevertMmThreadCharacteristics(AvTaskHandle);
    }
}

void SwapChainProcessor::RunCore()
{
    ComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = m_Device->Device.As(&DxgiDevice);
    if (FAILED(hr)) return;

    IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
    SetDevice.pDevice = DxgiDevice.Get();
    hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
    if (FAILED(hr)) return;

    for (;;)
    {
        ComPtr<IDXGIResource> AcquiredBuffer;
        IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
        hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

        if (hr == E_PENDING)
        {
            HANDLE WaitHandles[] = { m_hAvailableBufferEvent, m_hTerminateEvent.Get() };
            DWORD WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);
            if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT)
            {
                continue;
            }
            break;
        }
        else if (SUCCEEDED(hr))
        {
            AcquiredBuffer.Attach(Buffer.MetaData.pSurface);

            ComPtr<ID3D11Texture2D> SourceTexture;
            if (SUCCEEDED(AcquiredBuffer.As(&SourceTexture)))
            {
                D3D11_TEXTURE2D_DESC Desc{};
                SourceTexture->GetDesc(&Desc);

                if (!m_SharedTexture || m_SharedWidth != Desc.Width || m_SharedHeight != Desc.Height)
                {
                    if (m_SharedHandle)
                    {
                        CloseHandle(m_SharedHandle);
                        m_SharedHandle = nullptr;
                    }
                    m_SharedTexture.Reset();

                    D3D11_TEXTURE2D_DESC SharedDesc = Desc;
                    SharedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    SharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
                    SharedDesc.CPUAccessFlags = 0;
                    SharedDesc.Usage = D3D11_USAGE_DEFAULT;

                    if (SUCCEEDED(m_Device->Device->CreateTexture2D(&SharedDesc, nullptr, &m_SharedTexture)))
                    {
                        ComPtr<IDXGIResource1> Resource1;
                        if (SUCCEEDED(m_SharedTexture.As(&Resource1)))
                        {
                            // DXGI shared names cannot contain '\' (no Global\ / Local\).
                            // Restrictive-but-readable DACL so the interactive host can open the NT handle.
                            ZeroMemory(m_SharedName, sizeof(m_SharedName));
                            StringCchPrintfW(m_SharedName, ARRAYSIZE(m_SharedName),
                                L"WindowDisplayTexture%u", m_ConnectorIndex);

                            PSECURITY_DESCRIPTOR descriptor = nullptr;
                            SECURITY_ATTRIBUTES security{};
                            security.nLength = sizeof(security);
                            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                    L"D:P(A;;GA;;;SY)(A;;GR;;;BA)(A;;GR;;;AU)(A;;GR;;;WD)",
                                    SDDL_REVISION_1,
                                    &descriptor,
                                    nullptr))
                            {
                                security.lpSecurityDescriptor = descriptor;
                            }

                            HRESULT shareHr = Resource1->CreateSharedHandle(
                                descriptor ? &security : nullptr,
                                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                m_SharedName,
                                &m_SharedHandle);
                            if (FAILED(shareHr))
                            {
                                // Name collision from a previous run — retry with a unique suffix.
                                StringCchPrintfW(m_SharedName, ARRAYSIZE(m_SharedName),
                                    L"WindowDisplayTexture%u_%u", m_ConnectorIndex, GetTickCount());
                                shareHr = Resource1->CreateSharedHandle(
                                    descriptor ? &security : nullptr,
                                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                    m_SharedName,
                                    &m_SharedHandle);
                            }
                            if (descriptor)
                                LocalFree(descriptor);
                            if (FAILED(shareHr))
                            {
                                m_SharedName[0] = L'\0';
                                m_SharedHandle = nullptr;
                            }
                            {
                                char buf[192];
                                sprintf_s(buf, "SharedTexture connector=%u hr=0x%08X handle=%p",
                                    m_ConnectorIndex, static_cast<unsigned>(shareHr), m_SharedHandle);
                                WdLog(buf);
                            }
                        }
                        m_SharedWidth = Desc.Width;
                        m_SharedHeight = Desc.Height;
                    }
                }

                if (m_SharedTexture)
                {
                    m_Device->DeviceContext->CopyResource(m_SharedTexture.Get(), SourceTexture.Get());

                    // Cross-session BGRA publish — DXGI named shares cannot leave UMDF session 0.
                    if (Desc.Width <= WD_MAX_PIXEL_WIDTH && Desc.Height <= WD_MAX_PIXEL_HEIGHT)
                    {
                        if (!m_PixelMapping)
                        {
                            WCHAR mappingName[128];
                            StringCchPrintfW(mappingName, ARRAYSIZE(mappingName),
                                L"%s%u", WD_PIXEL_BUFFER_PREFIX, m_ConnectorIndex);
                            PSECURITY_DESCRIPTOR pixelSd = nullptr;
                            SECURITY_ATTRIBUTES pixelSa{};
                            pixelSa.nLength = sizeof(pixelSa);
                            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                                    L"D:P(A;;GA;;;SY)(A;;GR;;;BA)(A;;GR;;;AU)(A;;GR;;;WD)",
                                    SDDL_REVISION_1,
                                    &pixelSd,
                                    nullptr))
                            {
                                pixelSa.lpSecurityDescriptor = pixelSd;
                            }
                            m_PixelMapping = CreateFileMappingW(
                                INVALID_HANDLE_VALUE,
                                pixelSd ? &pixelSa : nullptr,
                                PAGE_READWRITE,
                                0,
                                sizeof(WdPixelBuffer),
                                mappingName);
                            if (pixelSd) LocalFree(pixelSd);
                            if (m_PixelMapping)
                            {
                                m_PixelView = static_cast<WdPixelBuffer*>(
                                    MapViewOfFile(m_PixelMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WdPixelBuffer)));
                                if (m_PixelView)
                                    ZeroMemory(m_PixelView, sizeof(*m_PixelView));
                            }
                        }

                        if (!m_StagingTexture || m_SharedWidth != Desc.Width || m_SharedHeight != Desc.Height)
                        {
                            m_StagingTexture.Reset();
                            D3D11_TEXTURE2D_DESC stagingDesc = Desc;
                            stagingDesc.BindFlags = 0;
                            stagingDesc.MiscFlags = 0;
                            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                            stagingDesc.Usage = D3D11_USAGE_STAGING;
                            m_Device->Device->CreateTexture2D(&stagingDesc, nullptr, &m_StagingTexture);
                        }

                        if (m_PixelView && m_StagingTexture)
                        {
                            m_Device->DeviceContext->CopyResource(m_StagingTexture.Get(), SourceTexture.Get());
                            D3D11_MAPPED_SUBRESOURCE mapped{};
                                if (SUCCEEDED(m_Device->DeviceContext->Map(m_StagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                            {
                                InterlockedExchange(reinterpret_cast<volatile LONG*>(&m_PixelView->Sequence), 1);
                                m_PixelView->Width = Desc.Width;
                                m_PixelView->Height = Desc.Height;
                                m_PixelView->Pitch = Desc.Width * 4;
                                const UINT rowBytes = Desc.Width * 4;
                                auto* dst = m_PixelView->Pixels;
                                const auto* src = static_cast<const BYTE*>(mapped.pData);
                                for (UINT y = 0; y < Desc.Height; ++y)
                                    memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
                                m_PixelView->FrameSerial += 1;
                                InterlockedExchange(reinterpret_cast<volatile LONG*>(&m_PixelView->Sequence), 0);
                                m_Device->DeviceContext->Unmap(m_StagingTexture.Get(), 0);
                            }
                        }
                    }

                    m_Device->DeviceContext->Flush();
                    if (m_DeviceContext)
                    {
                        m_DeviceContext->PublishFrame(
                            m_ConnectorIndex,
                            m_SharedHandle,
                            m_SharedWidth,
                            m_SharedHeight,
                            m_Device->AdapterLuid,
                            m_SharedName);
                    }
                }
            }

            AcquiredBuffer.Reset();
            hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
            if (FAILED(hr)) break;
        }
        else
        {
            break;
        }
    }
}

#pragma endregion

#pragma region IndirectMonitorContext

IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor, IndirectDeviceContext* DeviceContext, UINT32 ConnectorIndex)
    : m_Monitor(Monitor), m_DeviceContext(DeviceContext), m_ConnectorIndex(ConnectorIndex)
{
}

IndirectMonitorContext::~IndirectMonitorContext()
{
    m_ProcessingThread.reset();
}

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
    m_ProcessingThread.reset();
    auto Device = make_shared<Direct3DDevice>(RenderAdapter);
    if (FAILED(Device->Init()))
    {
        WdfObjectDelete(SwapChain);
        if (m_DeviceContext)
        {
            m_DeviceContext->SetMonitorState(m_ConnectorIndex, WdState_NeedsAttention);
        }
    }
    else
    {
        if (m_DeviceContext)
        {
            m_DeviceContext->SetMonitorState(m_ConnectorIndex, WdState_Active);
        }
        m_ProcessingThread.reset(new SwapChainProcessor(SwapChain, Device, NewFrameEvent, m_DeviceContext, m_ConnectorIndex));
    }
}

void IndirectMonitorContext::UnassignSwapChain()
{
    m_ProcessingThread.reset();
    if (m_DeviceContext)
    {
        m_DeviceContext->SetMonitorState(m_ConnectorIndex, WdState_Paused);
    }
}

bool IndirectMonitorContext::GetModes(vector<WdMode>& Modes, WdMode& Preferred) const
{
    return m_DeviceContext && m_DeviceContext->GetMonitorModes(m_ConnectorIndex, Modes, Preferred);
}

#pragma endregion

#pragma region IndirectDeviceContext

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice)
    : m_WdfDevice(WdfDevice)
{
    EnsureSharedState();
}

IndirectDeviceContext::~IndirectDeviceContext()
{
    for (UINT32 i = 0; i < WD_MAX_MONITORS; ++i)
    {
        DestroyMonitorOnConnector(i);
    }
    lock_guard<recursive_mutex> Guard(m_Lock);
    for (auto& Slot : m_Slots)
    {
        if (Slot.FrameEvent)
        {
            CloseHandle(Slot.FrameEvent);
            Slot.FrameEvent = nullptr;
        }
    }
    if (m_SharedState)
    {
        UnmapViewOfFile(m_SharedState);
        m_SharedState = nullptr;
    }
    if (m_SharedMapping)
    {
        CloseHandle(m_SharedMapping);
        m_SharedMapping = nullptr;
    }
}

void IndirectDeviceContext::EnsureSharedState()
{
    if (m_SharedMapping) return;

    SECURITY_ATTRIBUTES Sa{};
    Sa.nLength = sizeof(Sa);
    Sa.bInheritHandle = FALSE;

    m_SharedMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &Sa,
        PAGE_READWRITE,
        0,
        sizeof(WdSharedState),
        WD_SHARED_STATE_NAME);
    const DWORD createError = GetLastError();

    if (m_SharedMapping)
    {
        m_SharedState = static_cast<WdSharedState*>(
            MapViewOfFile(m_SharedMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WdSharedState)));
        if (m_SharedState && createError != ERROR_ALREADY_EXISTS)
        {
            ZeroMemory(m_SharedState, sizeof(*m_SharedState));
            m_SharedState->Version = 1;
        }
    }
}

void IndirectDeviceContext::SyncSharedState()
{
    if (!m_SharedState) return;
    m_SharedState->Version = 1;
    UINT32 Count = 0;
    for (UINT32 i = 0; i < WD_MAX_MONITORS; ++i)
    {
        auto& Slot = m_Slots[i];
        auto& Runtime = m_SharedState->Monitors[i];
        ZeroMemory(&Runtime, sizeof(Runtime));
        Runtime.ConnectorIndex = i;
        if (!Slot.Occupied) continue;

        Runtime.Active = 1;
        Runtime.State = Slot.State;
        Runtime.Mode = Slot.PreferredMode;
        Runtime.RenderAdapterLuid = Slot.RenderAdapterLuid;
        Runtime.SharedTextureHandle = Slot.SharedTextureHandle;
        Runtime.TextureWidth = Slot.TextureWidth;
        Runtime.TextureHeight = Slot.TextureHeight;
        Runtime.FrameSerial = Slot.FrameSerial;
        Runtime.ContainerId = Slot.ContainerId;
        StringCchCopyW(Runtime.FriendlyName, WD_MAX_NAME_CHARS, Slot.FriendlyName);
        StringCchCopyW(Runtime.SharedTextureName, ARRAYSIZE(Runtime.SharedTextureName), Slot.SharedTextureName);
        ++Count;
    }
    m_SharedState->MonitorCount = Count;
}

void IndirectDeviceContext::InitAdapter()
{
    if (m_AdapterInitStarted.exchange(true))
        return;

    IDDCX_ADAPTER_CAPS AdapterCaps = {};
    AdapterCaps.Size = sizeof(AdapterCaps);
    AdapterCaps.MaxMonitorsSupported = WD_MAX_MONITORS;
    AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
    AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"MultiBox Viewer Display";
    AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"MultiBox Viewer";
    AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"MultiBox Viewer";

    IDDCX_ENDPOINT_VERSION Version = {};
    Version.Size = sizeof(Version);
    Version.MajorVer = 1;
    Version.MinorVer = 0;
    AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
    AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT AdapterInit = {};
    AdapterInit.WdfDevice = m_WdfDevice;
    AdapterInit.pCaps = &AdapterCaps;
    AdapterInit.ObjectAttributes = &Attr;

    IDARG_OUT_ADAPTER_INIT AdapterInitOut;
    NTSTATUS Status = IddCxAdapterInitAsync(&AdapterInit, &AdapterInitOut);
    if (NT_SUCCESS(Status))
    {
        m_Adapter = AdapterInitOut.AdapterObject;
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
        pContext->pContext = this;
    }
    else
    {
        m_AdapterInitStarted = false;
    }
}

void IndirectDeviceContext::SetAdapterReady(bool Ready, IDDCX_ADAPTER Adapter)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    m_AdapterReady = Ready;
    if (Ready && Adapter)
    {
        m_Adapter = Adapter;
    }
    else if (!Ready)
    {
        m_Adapter = nullptr;
        m_AdapterInitStarted = false;
    }
}

INT32 IndirectDeviceContext::FindFreeConnector() const
{
    for (UINT32 i = 0; i < WD_MAX_MONITORS; ++i)
    {
        if (!m_Slots[i].Occupied) return static_cast<INT32>(i);
    }
    return -1;
}

NTSTATUS IndirectDeviceContext::CreateMonitorOnConnector(UINT32 ConnectorIndex)
{
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_AdapterReady || !m_Adapter)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    auto& Slot = m_Slots[ConnectorIndex];

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectMonitorContextWrapper);
    Attr.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        auto* context = WdfObjectGet_IndirectMonitorContextWrapper(Object);
        if (context)
            context->Cleanup();
    };

    // Build a per-connector EDID so Windows Display Settings shows a normal monitor
    // name/identity (MultiBox) instead of an anonymous EDID-less target.
    Edid::Build(Slot.Edid, ConnectorIndex);

    IDDCX_MONITOR_INFO MonitorInfo = {};
    MonitorInfo.Size = sizeof(MonitorInfo);
    MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    MonitorInfo.ConnectorIndex = ConnectorIndex;
    MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
    MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    MonitorInfo.MonitorDescription.DataSize = Edid::kSize;
    MonitorInfo.MonitorDescription.pData = Slot.Edid;
    MonitorInfo.MonitorContainerId = Slot.ContainerId;

    IDARG_IN_MONITORCREATE MonitorCreate = {};
    MonitorCreate.ObjectAttributes = &Attr;
    MonitorCreate.pMonitorInfo = &MonitorInfo;

    IDARG_OUT_MONITORCREATE MonitorCreateOut;
    NTSTATUS Status = IddCxMonitorCreate(m_Adapter, &MonitorCreate, &MonitorCreateOut);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    auto* Wrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorCreateOut.MonitorObject);
    Wrapper->pContext = new IndirectMonitorContext(MonitorCreateOut.MonitorObject, this, ConnectorIndex);

    IDARG_OUT_MONITORARRIVAL ArrivalOut;
    Status = IddCxMonitorArrival(MonitorCreateOut.MonitorObject, &ArrivalOut);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(MonitorCreateOut.MonitorObject);
        return Status;
    }

    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        Slot.Monitor = MonitorCreateOut.MonitorObject;
        Slot.State = WdState_Starting;

        WCHAR EventName[128];
        StringCchPrintfW(EventName, ARRAYSIZE(EventName), L"%s%u", WD_FRAME_EVENT_PREFIX, ConnectorIndex);
        if (!Slot.FrameEvent)
        {
            Slot.FrameEvent = CreateEventW(nullptr, FALSE, FALSE, EventName);
        }
        SyncSharedState();
    }
    return STATUS_SUCCESS;
}

NTSTATUS IndirectDeviceContext::DestroyMonitorOnConnector(UINT32 ConnectorIndex)
{
    if (ConnectorIndex >= WD_MAX_MONITORS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IDDCX_MONITOR Monitor = nullptr;
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        const auto& Slot = m_Slots[ConnectorIndex];
        if (!Slot.Occupied || !Slot.Monitor)
        {
            return STATUS_NOT_FOUND;
        }
        Monitor = Slot.Monitor;
    }

    // Departure waits for swapchain teardown. Never hold m_Lock here because the
    // worker may be finishing PublishFrame on another thread.
    NTSTATUS Status = IddCxMonitorDeparture(Monitor);
    if (!NT_SUCCESS(Status))
        return Status;

    lock_guard<recursive_mutex> Guard(m_Lock);
    auto& Slot = m_Slots[ConnectorIndex];
    if (Slot.Monitor == Monitor)
    {
        if (Slot.FrameEvent)
            CloseHandle(Slot.FrameEvent);
        Slot = MonitorSlot{};
        SyncSharedState();
    }
    return Status;
}

NTSTATUS IndirectDeviceContext::PlugInMonitor(_In_ const WdPlugInRequest* Request, _Out_ WdPlugInResponse* Response)
{
    ZeroMemory(Response, sizeof(*Response));

    UINT32 Connector = Request->ConnectorIndex;
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        if (!m_AdapterReady)
        {
            Response->NtStatus = (UINT32)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }

        if (Connector == 0xFFFFFFFFu)
        {
            INT32 Free = FindFreeConnector();
            if (Free < 0)
            {
                Response->NtStatus = (UINT32)STATUS_INSUFFICIENT_RESOURCES;
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            Connector = static_cast<UINT32>(Free);
        }

        if (Connector >= WD_MAX_MONITORS || m_Slots[Connector].Occupied)
        {
            Response->NtStatus = (UINT32)STATUS_DEVICE_BUSY;
            return STATUS_DEVICE_BUSY;
        }

        auto& Slot = m_Slots[Connector];
        if (Slot.FrameEvent)
            CloseHandle(Slot.FrameEvent);
        Slot = MonitorSlot{};
        Slot.Occupied = true;
        Slot.ContainerId = Request->ContainerId;
        if (Slot.ContainerId.Data1 == 0 && Slot.ContainerId.Data2 == 0)
        {
            CoCreateGuid(&Slot.ContainerId);
        }

        if (Request->ModeCount > 0 && Request->ModeCount <= WD_MAX_MODES)
        {
            Slot.ModeCount = Request->ModeCount;
            CopyMemory(Slot.Modes, Request->Modes, sizeof(WdMode) * Request->ModeCount);
            Slot.PreferredMode = Request->PreferredMode.Width ? Request->PreferredMode : Request->Modes[0];
        }
        else
        {
            vector<WdMode> Defaults;
            BuildDefaultModes(Defaults);
            Slot.ModeCount = min<UINT32>((UINT32)Defaults.size(), WD_MAX_MODES);
            for (UINT32 i = 0; i < Slot.ModeCount; ++i)
            {
                Slot.Modes[i] = Defaults[i];
            }
            Slot.PreferredMode = Request->PreferredMode.Width ? Request->PreferredMode : kWdPresetRecommended;
        }

        if (Request->FriendlyName[0])
        {
            StringCchCopyW(Slot.FriendlyName, WD_MAX_NAME_CHARS, Request->FriendlyName);
        }
        else
        {
            StringCchPrintfW(Slot.FriendlyName, WD_MAX_NAME_CHARS, L"Display %u", Connector + 1);
        }
        Response->ContainerId = Slot.ContainerId;
    }

    NTSTATUS Status = CreateMonitorOnConnector(Connector);
    Response->ConnectorIndex = Connector;
    Response->NtStatus = (UINT32)Status;
    if (!NT_SUCCESS(Status))
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        if (m_Slots[Connector].FrameEvent)
            CloseHandle(m_Slots[Connector].FrameEvent);
        m_Slots[Connector] = MonitorSlot{};
        SyncSharedState();
    }
    return Status;
}

NTSTATUS IndirectDeviceContext::PlugOutMonitor(UINT32 ConnectorIndex)
{
    return DestroyMonitorOnConnector(ConnectorIndex);
}

NTSTATUS IndirectDeviceContext::UpdateMode(UINT32 ConnectorIndex, const WdMode& Mode)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied)
    {
        return STATUS_NOT_FOUND;
    }
    m_Slots[ConnectorIndex].PreferredMode = Mode;
    // Mode commit is performed by the host through SetDisplayConfig after plug-in.
    SyncSharedState();
    return STATUS_SUCCESS;
}

void IndirectDeviceContext::SetCommittedMode(UINT32 ConnectorIndex, const WdMode& Mode)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied)
        return;
    m_Slots[ConnectorIndex].PreferredMode = Mode;
    m_Slots[ConnectorIndex].State = WdState_Active;
    SyncSharedState();
}

NTSTATUS IndirectDeviceContext::RestartMonitor(UINT32 ConnectorIndex)
{
    MonitorSlot Snapshot{};
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied)
        {
            return STATUS_NOT_FOUND;
        }
        Snapshot = m_Slots[ConnectorIndex];
    }

    NTSTATUS Status = DestroyMonitorOnConnector(ConnectorIndex);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Snapshot.Occupied = true;
    Snapshot.Monitor = nullptr;
    Snapshot.FrameEvent = nullptr;
    Snapshot.RenderAdapterLuid = {};
    Snapshot.SharedTextureHandle = 0;
    Snapshot.TextureWidth = 0;
    Snapshot.TextureHeight = 0;
    Snapshot.FrameSerial = 0;
    Snapshot.SharedTextureName[0] = L'\0';
    Snapshot.State = WdState_Starting;
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        m_Slots[ConnectorIndex] = Snapshot;
    }
    Status = CreateMonitorOnConnector(ConnectorIndex);
    if (!NT_SUCCESS(Status))
    {
        lock_guard<recursive_mutex> Guard(m_Lock);
        if (m_Slots[ConnectorIndex].FrameEvent)
            CloseHandle(m_Slots[ConnectorIndex].FrameEvent);
        m_Slots[ConnectorIndex] = MonitorSlot{};
        SyncSharedState();
    }
    return Status;
}

void IndirectDeviceContext::GetStatus(_Out_ WdAdapterStatus* Status)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    ZeroMemory(Status, sizeof(*Status));
    Status->AdapterReady = m_AdapterReady ? 1u : 0u;
    for (UINT32 i = 0; i < WD_MAX_MONITORS; ++i)
    {
        if (!m_Slots[i].Occupied) continue;
        auto& Runtime = Status->Monitors[Status->MonitorCount++];
        Runtime.ConnectorIndex = i;
        Runtime.Active = 1;
        Runtime.State = m_Slots[i].State;
        Runtime.Mode = m_Slots[i].PreferredMode;
        Runtime.RenderAdapterLuid = m_Slots[i].RenderAdapterLuid;
        Runtime.SharedTextureHandle = m_Slots[i].SharedTextureHandle;
        Runtime.TextureWidth = m_Slots[i].TextureWidth;
        Runtime.TextureHeight = m_Slots[i].TextureHeight;
        Runtime.FrameSerial = m_Slots[i].FrameSerial;
        Runtime.ContainerId = m_Slots[i].ContainerId;
        StringCchCopyW(Runtime.FriendlyName, WD_MAX_NAME_CHARS, m_Slots[i].FriendlyName);
        StringCchCopyW(Runtime.SharedTextureName, ARRAYSIZE(Runtime.SharedTextureName), m_Slots[i].SharedTextureName);
    }
}

void IndirectDeviceContext::PublishFrame(
    UINT32 ConnectorIndex,
    HANDLE SharedHandle,
    UINT32 Width,
    UINT32 Height,
    LUID RenderAdapterLuid,
    _In_opt_z_ const WCHAR* SharedTextureName)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied)
    {
        return;
    }
    auto& Slot = m_Slots[ConnectorIndex];
    Slot.SharedTextureHandle = reinterpret_cast<UINT64>(SharedHandle);
    Slot.TextureWidth = Width;
    Slot.TextureHeight = Height;
    Slot.RenderAdapterLuid = RenderAdapterLuid;
    Slot.FrameSerial += 1;
    Slot.State = WdState_Active;
    if (SharedTextureName && SharedTextureName[0])
        StringCchCopyW(Slot.SharedTextureName, ARRAYSIZE(Slot.SharedTextureName), SharedTextureName);
    SyncSharedState();
    if (Slot.FrameEvent)
    {
        SetEvent(Slot.FrameEvent);
    }
}

void IndirectDeviceContext::SetMonitorState(UINT32 ConnectorIndex, WdDisplayState State)
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied) return;
    m_Slots[ConnectorIndex].State = State;
    SyncSharedState();
}

bool IndirectDeviceContext::GetMonitorModes(UINT32 ConnectorIndex, vector<WdMode>& Modes, WdMode& Preferred) const
{
    lock_guard<recursive_mutex> Guard(m_Lock);
    if (ConnectorIndex >= WD_MAX_MONITORS || !m_Slots[ConnectorIndex].Occupied)
    {
        return false;
    }
    Modes.assign(m_Slots[ConnectorIndex].Modes, m_Slots[ConnectorIndex].Modes + m_Slots[ConnectorIndex].ModeCount);
    Preferred = m_Slots[ConnectorIndex].PreferredMode;
    return true;
}

#pragma endregion

#pragma region DDI Callbacks

_Use_decl_annotations_
NTSTATUS WdAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    auto* Wrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (Wrapper && Wrapper->pContext)
    {
        Wrapper->pContext->SetAdapterReady(NT_SUCCESS(pInArgs->AdapterInitStatus), AdapterObject);
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
{
    auto* adapterWrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (!adapterWrapper || !adapterWrapper->pContext || !pInArgs)
        return STATUS_SUCCESS;

    for (UINT i = 0; i < pInArgs->PathCount; ++i)
    {
        const IDDCX_PATH& path = pInArgs->pPaths[i];
        if (!path.MonitorObject)
            continue;

        auto* monWrapper = WdfObjectGet_IndirectMonitorContextWrapper(path.MonitorObject);
        if (!monWrapper || !monWrapper->pContext)
            continue;

        const auto& signal = path.TargetVideoSignalInfo;
        WdMode mode{};
        mode.Width = static_cast<UINT32>(signal.activeSize.cx);
        mode.Height = static_cast<UINT32>(signal.activeSize.cy);
        if (signal.vSyncFreq.Denominator)
            mode.RefreshHz = static_cast<UINT32>(signal.vSyncFreq.Numerator / signal.vSyncFreq.Denominator);
        else
            mode.RefreshHz = static_cast<UINT32>(signal.vSyncFreq.Numerator);

        if (mode.Width && mode.Height && mode.RefreshHz)
            adapterWrapper->pContext->SetCommittedMode(monWrapper->pContext->ConnectorIndex(), mode);
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    pOutArgs->MonitorModeBufferOutputCount = 0;
    pOutArgs->PreferredMonitorModeIdx = 0;

    if (!pInArgs || !Edid::IsOurs(static_cast<const BYTE*>(pInArgs->MonitorDescription.pData),
            pInArgs->MonitorDescription.DataSize))
    {
        return STATUS_INVALID_PARAMETER;
    }

    vector<WdMode> Modes;
    BuildDefaultModes(Modes);
    pOutArgs->MonitorModeBufferOutputCount = static_cast<UINT>(Modes.size());

    if (pInArgs->MonitorModeBufferInputCount == 0)
        return STATUS_SUCCESS;
    if (pInArgs->MonitorModeBufferInputCount < Modes.size())
        return STATUS_BUFFER_TOO_SMALL;

    UINT preferred = 0;
    for (UINT i = 0; i < Modes.size(); ++i)
    {
        pInArgs->pMonitorModes[i] = CreateIddCxMonitorMode(
            Modes[i].Width,
            Modes[i].Height,
            Modes[i].RefreshHz,
            IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR);
        if (Modes[i].Width == kWdPresetRecommended.Width &&
            Modes[i].Height == kWdPresetRecommended.Height &&
            Modes[i].RefreshHz == kWdPresetRecommended.RefreshHz)
        {
            preferred = i;
        }
    }
    pOutArgs->PreferredMonitorModeIdx = preferred;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    vector<WdMode> Modes;
    WdMode Preferred{};
    auto* Wrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    if (!Wrapper || !Wrapper->pContext || !Wrapper->pContext->GetModes(Modes, Preferred))
    {
        BuildDefaultModes(Modes);
        Preferred = kWdPresetRecommended;
    }

    pOutArgs->DefaultMonitorModeBufferOutputCount = (UINT)Modes.size();
    if (pInArgs->DefaultMonitorModeBufferInputCount == 0)
    {
        return STATUS_SUCCESS;
    }
    if (pInArgs->DefaultMonitorModeBufferInputCount < Modes.size())
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    UINT PreferredIdx = 0;
    for (UINT i = 0; i < Modes.size(); ++i)
    {
        pInArgs->pDefaultMonitorModes[i] = CreateIddCxMonitorMode(Modes[i].Width, Modes[i].Height, Modes[i].RefreshHz);
        if (Modes[i].Width == Preferred.Width &&
            Modes[i].Height == Preferred.Height &&
            Modes[i].RefreshHz == Preferred.RefreshHz)
        {
            PreferredIdx = i;
        }
    }
    pOutArgs->PreferredMonitorModeIdx = PreferredIdx;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    vector<WdMode> Modes;
    WdMode Preferred{};
    auto* Wrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    if (!Wrapper || !Wrapper->pContext || !Wrapper->pContext->GetModes(Modes, Preferred))
        BuildDefaultModes(Modes);

    pOutArgs->TargetModeBufferOutputCount = (UINT)Modes.size();
    if (pInArgs->TargetModeBufferInputCount == 0)
        return STATUS_SUCCESS;
    if (pInArgs->TargetModeBufferInputCount < Modes.size())
        return STATUS_BUFFER_TOO_SMALL;
    for (UINT i = 0; i < Modes.size(); ++i)
    {
        pInArgs->pTargetModes[i] =
            CreateIddCxTargetMode(Modes[i].Width, Modes[i].Height, Modes[i].RefreshHz);
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* Wrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    Wrapper->pContext->AssignSwapChain(pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS WdMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
    auto* Wrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    Wrapper->pContext->UnassignSwapChain();
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID WdIoDeviceControl(
    WDFDEVICE Device,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode)
{
    auto* Wrapper = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    auto* Context = Wrapper->pContext;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    size_t BytesReturned = 0;

    switch (IoControlCode)
    {
    case IOCTL_WD_PLUG_IN:
    {
        if (InputBufferLength < sizeof(WdPlugInRequest) || OutputBufferLength < sizeof(WdPlugInResponse))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        WdPlugInRequest* In = nullptr;
        WdPlugInResponse* Out = nullptr;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID*)&In, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID*)&Out, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Status = Context->PlugInMonitor(In, Out);
        BytesReturned = sizeof(*Out);
        break;
    }
    case IOCTL_WD_PLUG_OUT:
    {
        if (InputBufferLength < sizeof(WdPlugOutRequest))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        WdPlugOutRequest* In = nullptr;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID*)&In, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Status = Context->PlugOutMonitor(In->ConnectorIndex);
        break;
    }
    case IOCTL_WD_UPDATE_MODE:
    {
        if (InputBufferLength < sizeof(WdUpdateModeRequest))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        WdUpdateModeRequest* In = nullptr;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID*)&In, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Status = Context->UpdateMode(In->ConnectorIndex, In->Mode);
        break;
    }
    case IOCTL_WD_GET_STATUS:
    {
        if (OutputBufferLength < sizeof(WdAdapterStatus))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        WdAdapterStatus* Out = nullptr;
        Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Out), (PVOID*)&Out, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Context->GetStatus(Out);
        BytesReturned = sizeof(*Out);
        Status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_WD_RESTART_MONITOR:
    {
        if (InputBufferLength < sizeof(WdPlugOutRequest))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        WdPlugOutRequest* In = nullptr;
        Status = WdfRequestRetrieveInputBuffer(Request, sizeof(*In), (PVOID*)&In, nullptr);
        if (!NT_SUCCESS(Status)) break;
        Status = Context->RestartMonitor(In->ConnectorIndex);
        break;
    }
    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, Status, BytesReturned);
}

#pragma endregion
