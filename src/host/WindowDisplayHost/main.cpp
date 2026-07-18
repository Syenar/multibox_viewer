#include "pch.h"
#include "DriverClient.h"
#include "DisplayTopology.h"
#include "HostServer.h"
#include "ViewerWindow.h"
#include "WindowMover.h"

namespace
{
    constexpr UINT WM_OPEN_VIEWER = WM_APP + 1;
    constexpr UINT WM_CLOSE_VIEWER = WM_APP + 2;
    constexpr int ID_CREATE=1, ID_OPEN=2, ID_RESTART=3, ID_REMOVE=4;
    struct DeviceReady { HANDLE done; HRESULT result; HSWDEVICE device; };
    void WINAPI DeviceCreated(HSWDEVICE device, HRESULT hr, void* context, PCWSTR) { auto* state=static_cast<DeviceReady*>(context);state->result=hr;state->device=device;SetEvent(state->done); }

    class Application;
    Application* g_app = nullptr;

    class Application
    {
    public:
        bool Initialize(std::wstring* errorDetail = nullptr)
        {
            m_ready = {};
            m_ready.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            m_ready.result = E_FAIL;

            // SwDeviceCreate requires COM on the calling thread.
            HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            m_comInitialized = SUCCEEDED(comHr) || comHr == S_FALSE || comHr == RPC_E_CHANGED_MODE;
            if (!m_comInitialized)
            {
                if (errorDetail)
                    *errorDetail = L"CoInitializeEx failed: 0x" + ToHex(comHr);
                return false;
            }

            // If a previous elevated host already created the adapter, just open it.
            if (m_driver.Open(500))
            {
                m_server = std::make_unique<HostServer>([this](UINT32 c, const std::vector<BYTE>& r, std::vector<BYTE>& o) {
                    return Command(c, r, o);
                });
                if (!m_server->Start()) return false;
                StartDropHook();
                return true;
            }

            static const wchar_t kHardwareIds[] = L"WindowDisplay\0\0";
            static const wchar_t kCompatibleIds[] = L"WindowDisplay\0\0";
            SW_DEVICE_CREATE_INFO info{};
            info.cbSize = sizeof(info);
            info.pszzCompatibleIds = kCompatibleIds;
            info.pszInstanceId = L"WindowDisplay";
            info.pszzHardwareIds = kHardwareIds;
            info.pszDeviceDescription = L"MultiBox Viewer Virtual Display Adapter";
            info.CapabilityFlags = SWDeviceCapabilitiesRemovable |
                                   SWDeviceCapabilitiesSilentInstall |
                                   SWDeviceCapabilitiesDriverRequired;

            HRESULT hr = SwDeviceCreate(
                L"WindowDisplay",
                L"HTREE\\ROOT\\0",
                &info,
                0,
                nullptr,
                DeviceCreated,
                &m_ready,
                &m_ready.device);

            if (FAILED(hr))
            {
                if (errorDetail)
                {
                    *errorDetail = L"SwDeviceCreate failed: 0x" + ToHex(hr);
                    if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
                        *errorDetail += L"\n\nAccess denied. Creating the virtual display adapter requires "
                                        L"Administrator once (UAC). The app will try to relaunch elevated.";
                }
                m_lastCreateHr = hr;
                return false;
            }
            m_lastCreateHr = hr;

            const DWORD wait = WaitForSingleObject(m_ready.done, 15000);
            if (wait != WAIT_OBJECT_0)
            {
                if (errorDetail)
                    *errorDetail = L"Timed out waiting for software device creation.";
                return false;
            }

            if (FAILED(m_ready.result))
            {
                if (errorDetail)
                {
                    *errorDetail = L"Software device callback failed: 0x" + ToHex(m_ready.result) +
                        L"\n\nIf Secure Boot is on, disable it in UEFI (or use a Microsoft-signed driver), "
                        L"enable test signing, reboot, then re-run tools\\sign-and-install-driver.ps1.";
                }
                return false;
            }

            if (!m_driver.Open(10000))
            {
                if (errorDetail)
                {
                    ULONG status = 0, problem = 0;
                    DEVINST devInst = 0;
                    CONFIGRET cr = CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(L"SWD\\WINDOWDISPLAY\\WINDOWDISPLAY"),
                        CM_LOCATE_DEVNODE_NORMAL);
                    if (cr == CR_SUCCESS)
                        CM_Get_DevNode_Status(&status, &problem, devInst, 0);

                    *errorDetail = L"Device created but driver interface open failed.\n"
                        L"Win32=" + std::to_wstring(m_driver.LastError()) +
                        L"  PnP problem=" + std::to_wstring(problem) +
                        L" (status=0x" + ToHex(static_cast<HRESULT>(status)) + L")\n\n"
                        L"If problem=31, the UMDF driver failed to start — check "
                        L"Microsoft-Windows-DriverFrameworks-UserMode/Operational for event 2007.\n"
                        L"If Win32=5 (access denied), the INF Security SDDL may be missing — "
                        L"reinstall the driver package.";
                }
                return false;
            }

            m_server = std::make_unique<HostServer>([this](UINT32 c, const std::vector<BYTE>& r, std::vector<BYTE>& o) {
                return Command(c, r, o);
            });
            if (!m_server->Start())
            {
                if (errorDetail)
                    *errorDetail = L"Named pipe server failed to start.";
                return false;
            }
            StartDropHook();
            return true;
        }
        void Shutdown()
        {
            StopDropHook();
            if (m_server) m_server->Stop();
            m_viewers.clear();
            m_driver.Close();
            if (m_ready.device)
            {
                SwDeviceClose(m_ready.device);
                m_ready.device = nullptr;
            }
            if (m_ready.done)
            {
                CloseHandle(m_ready.done);
                m_ready.done = nullptr;
            }
            if (m_comInitialized)
            {
                CoUninitialize();
                m_comInitialized = false;
            }
        }

        void StartDropHook()
        {
            if (m_moveSizeHook) return;
            m_moveSizeHook = SetWinEventHook(
                EVENT_SYSTEM_MOVESIZESTART,
                EVENT_SYSTEM_MOVESIZEEND,
                nullptr,
                MoveSizeWinEvent,
                0,
                0,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        void StopDropHook()
        {
            if (!m_moveSizeHook) return;
            UnhookWinEvent(m_moveSizeHook);
            m_moveSizeHook = nullptr;
            SetSuppressInjectAll(false);
            m_moveSizeWindow = nullptr;
        }
        void OnMoveSizeStart(HWND window)
        {
            if (!m_dropWindowsOntoViewer || !window || ViewerWindow::IsViewerWindow(window)) return;
            m_moveSizeWindow = window;
            SetSuppressInjectAll(true);
        }
        void OnMoveSizeEnd(HWND window)
        {
            const HWND target = window ? window : m_moveSizeWindow;
            m_moveSizeWindow = nullptr;
            SetSuppressInjectAll(false);
            if (!m_dropWindowsOntoViewer || !target || !IsWindow(target)) return;
            if (ViewerWindow::IsViewerWindow(target)) return;

            POINT pt{};
            GetCursorPos(&pt);
            // Prefer the viewer under the cursor; fall back to image hit-test.
            HWND under = WindowFromPoint(pt);
            while (under)
            {
                if (ViewerWindow::IsViewerWindow(under)) break;
                under = GetParent(under);
            }

            std::lock_guard<std::mutex> lock(m_lock);
            ViewerWindow* matched = nullptr;
            for (auto& viewer : m_viewers)
            {
                if (!viewer) continue;
                if ((under && viewer->Handle() == under) || viewer->HitTestDropTarget(pt))
                {
                    matched = viewer.get();
                    break;
                }
            }
            if (!matched)
            {
                for (auto& viewer : m_viewers)
                {
                    if (viewer && viewer->HitTestImage(pt))
                    {
                        matched = viewer.get();
                        break;
                    }
                }
            }
            if (!matched) return;
            RECT monitor{};
            if (!matched->GetOwnedMonitorRect(monitor)) return;
            WindowMover::MoveWindowToDisplay(target, monitor);
        }
        void SetSuppressInjectAll(bool suppress)
        {
            std::lock_guard<std::mutex> lock(m_lock);
            for (auto& viewer : m_viewers)
                if (viewer) viewer->SetSuppressInputInject(suppress);
        }
        static void CALLBACK MoveSizeWinEvent(
            HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
        {
            if (!g_app || idObject != OBJID_WINDOW || !hwnd) return;
            if (event == EVENT_SYSTEM_MOVESIZESTART) g_app->OnMoveSizeStart(hwnd);
            else if (event == EVENT_SYSTEM_MOVESIZEEND) g_app->OnMoveSizeEnd(hwnd);
        }

        static std::wstring ToHex(HRESULT hr)
        {
            wchar_t buf[16];
            swprintf_s(buf, L"%08X", static_cast<unsigned>(hr));
            return buf;
        }
        DWORD CreateDisplay(
            const WdCreateDisplayRequest* createRequest = nullptr,
            UINT32* createdConnector = nullptr)
        {
            UINT32 connector = 0xffffffff;
            std::wstring primaryBefore;
            std::vector<std::wstring> virtualBefore;
            const WdMode requestedMode =
                createRequest && createRequest->Mode.Width && createRequest->Mode.Height
                    ? createRequest->Mode
                    : kWdPresetRecommended;
            const bool openViewer = !createRequest || createRequest->OpenViewer != 0;
            {
                std::lock_guard<std::mutex> lock(m_lock);
                // Capture the real primary before plug-in — Windows often promotes new IddCx targets.
                primaryBefore = m_topology.GetPrimaryDeviceName();
                virtualBefore = m_topology.ListVirtualDisplayDevices();
                WdPlugInRequest request{}; request.ConnectorIndex=0xffffffff; CoCreateGuid(&request.ContainerId); request.PreferredMode=requestedMode;
                WdFillDefaultModeList(request.Modes,&request.ModeCount);
                bool modeListed = false;
                for (UINT32 i = 0; i < request.ModeCount; ++i)
                    modeListed = modeListed ||
                        (request.Modes[i].Width == requestedMode.Width &&
                         request.Modes[i].Height == requestedMode.Height &&
                         request.Modes[i].RefreshHz == requestedMode.RefreshHz);
                if (!modeListed && request.ModeCount < WD_MAX_MODES)
                    request.Modes[request.ModeCount++] = requestedMode;
                wcsncpy_s(request.FriendlyName,L"MultiBox Viewer",_TRUNCATE);
                WdPlugInResponse response{}; if(!m_driver.PlugIn(request,response)) return m_driver.LastError();
                std::wstring device;
                m_topology.ExtendAndPlace(response.ConnectorIndex, request.PreferredMode, &device, primaryBefore);
                // Re-assert after plug-in settles — Windows sometimes steals primary asynchronously.
                if (!primaryBefore.empty())
                    m_topology.ForcePrimaryDevice(primaryBefore);
                // Bind the connector to the newly arrived GDI source rather than treating the
                // connector number as a left-to-right display ordinal.
                const auto virtualAfter = m_topology.ListVirtualDisplayDevices();
                for (const auto& candidate : virtualAfter)
                {
                    if (std::find(virtualBefore.begin(), virtualBefore.end(), candidate) == virtualBefore.end())
                    {
                        device = candidate;
                        break;
                    }
                }
                if (device.empty())
                    device = m_topology.FindWindowDisplayDevice(response.ConnectorIndex);
                m_devices[response.ConnectorIndex]=device;
                connector = response.ConnectorIndex;
            }
            if (createdConnector)
                *createdConnector = connector;

            DWORD viewerError = ERROR_SUCCESS;
            if (openViewer)
            {
                if (GetCurrentThreadId() == m_uiThreadId)
                {
                    viewerError = ERROR_DEVICE_NOT_AVAILABLE;
                    for (int attempt = 0; attempt < 40; ++attempt)
                    {
                        viewerError = OpenViewer(connector);
                        if (viewerError == ERROR_SUCCESS)
                            break;
                        Sleep(75);
                    }
                }
                else
                    viewerError = m_window
                        ? static_cast<DWORD>(SendMessageW(m_window, WM_OPEN_VIEWER, connector, 0))
                        : ERROR_INVALID_WINDOW_HANDLE;
            }
            // One more primary restore after the viewer opens (display arrival can race).
            if (!primaryBefore.empty())
                m_topology.ForcePrimaryDevice(primaryBefore);
            // Display creation succeeded even if PiP failed — Open Viewer can retry.
            (void)viewerError;
            return ERROR_SUCCESS;
        }
        DWORD RemoveDisplay(UINT32 connector=0xffffffff)
        {
            if(connector==0xffffffff){WdAdapterStatus s{};if(!m_driver.GetStatus(s))return m_driver.LastError();for(UINT32 i=0;i<s.MonitorCount;++i)if(s.Monitors[i].Active){connector=s.Monitors[i].ConnectorIndex;break;}}
            if(connector==0xffffffff) return ERROR_NOT_FOUND;
            if (GetCurrentThreadId() == m_uiThreadId)
                CloseViewer(connector);
            else if (!m_window || SendMessageW(m_window, WM_CLOSE_VIEWER, connector, 0) != ERROR_SUCCESS)
                return ERROR_INVALID_WINDOW_HANDLE;
            std::lock_guard<std::mutex> lock(m_lock);
            if(!m_driver.PlugOut(connector)) return m_driver.LastError(); m_devices[connector].clear(); WindowMover::RescueOffscreenWindows(); return ERROR_SUCCESS;
        }
        DWORD Restart(UINT32 connector=0xffffffff) { std::lock_guard<std::mutex> lock(m_lock); if(connector==0xffffffff){WdAdapterStatus s{};if(!m_driver.GetStatus(s))return m_driver.LastError();for(UINT32 i=0;i<s.MonitorCount;++i)if(s.Monitors[i].Active){connector=s.Monitors[i].ConnectorIndex;break;}}if(connector==0xffffffff)return ERROR_NOT_FOUND;return m_driver.Restart(connector)?ERROR_SUCCESS:m_driver.LastError(); }
        DWORD CloseViewer(UINT32 connector)
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_viewers.erase(std::remove_if(m_viewers.begin(),m_viewers.end(),[connector](const std::unique_ptr<ViewerWindow>& v){return v->Connector()==connector;}),m_viewers.end());
            return ERROR_SUCCESS;
        }
        DWORD OpenViewer(UINT32 connector=0)
        {
            std::lock_guard<std::mutex> lock(m_lock);
            for (auto& existing : m_viewers)
            {
                if (existing && existing->Connector() == connector && existing->Handle())
                {
                    ShowWindow(existing->Handle(), SW_SHOW);
                    SetForegroundWindow(existing->Handle());
                    return ERROR_SUCCESS;
                }
            }

            WdAdapterStatus status{};
            if (!m_driver.GetStatus(status)) return m_driver.LastError();

            WdMode mode = kWdPresetRecommended;
            bool found = false;
            for (UINT32 i = 0; i < status.MonitorCount; ++i)
            {
                if (status.Monitors[i].ConnectorIndex == connector)
                {
                    mode = status.Monitors[i].Mode;
                    found = true;
                    break;
                }
            }
            if (!found) return ERROR_NOT_FOUND;

            std::wstring device = m_devices[connector];
            if (device.empty())
                device = m_topology.FindWindowDisplayDevice(connector);

            // Always create the PiP window even if the GDI name is still catching up —
            // the viewer retries desktop duplication each frame.
            auto viewer = std::make_unique<ViewerWindow>(connector, device, mode);
            if (!viewer->Create(GetModuleHandleW(nullptr), true))
            {
                const DWORD err = GetLastError();
                return err ? err : ERROR_GEN_FAILURE;
            }
            viewer->SetAlwaysOnTop(m_viewersAlwaysOnTop);
            viewer->SetInteractionOptions(m_requireDoubleClick, m_hoverFadeEnabled);
            viewer->Show();
            m_viewers.push_back(std::move(viewer));
            return ERROR_SUCCESS;
        }
        DWORD MoveWindow(UINT64 rawWindow, UINT32 connector)
        {
            std::wstring device;
            {
                std::lock_guard<std::mutex> lock(m_lock);
                if (connector >= m_devices.size())
                    return ERROR_INVALID_PARAMETER;
                device = m_devices[connector];
                if (device.empty())
                    device = m_topology.FindWindowDisplayDevice(connector);
            }
            if (device.empty())
                return ERROR_NOT_FOUND;

            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &mode, 0))
                return GetLastError() ? GetLastError() : ERROR_NOT_FOUND;
            RECT bounds{
                mode.dmPosition.x,
                mode.dmPosition.y,
                mode.dmPosition.x + static_cast<LONG>(mode.dmPelsWidth),
                mode.dmPosition.y + static_cast<LONG>(mode.dmPelsHeight)
            };
            return WindowMover::MoveWindowToDisplay(
                reinterpret_cast<HWND>(static_cast<UINT_PTR>(rawWindow)),
                bounds) ? ERROR_SUCCESS : (GetLastError() ? GetLastError() : ERROR_INVALID_WINDOW_HANDLE);
        }
        DWORD Command(UINT32 command,const std::vector<BYTE>& payload,std::vector<BYTE>& output)
        {
            UINT32 connector=0; if(payload.size()>=sizeof(UINT32)) memcpy(&connector,payload.data(),sizeof(connector));
            switch(command) {
            case WdCmd_Ping:return ERROR_SUCCESS;
            case WdCmd_CreateDisplay:
            {
                if (!payload.empty() && payload.size() != sizeof(WdCreateDisplayRequest))
                    return ERROR_INVALID_DATA;
                const auto* request = payload.empty()
                    ? nullptr
                    : reinterpret_cast<const WdCreateDisplayRequest*>(payload.data());
                UINT32 created = 0xffffffff;
                const DWORD result = CreateDisplay(request, &created);
                if (result != ERROR_SUCCESS)
                    return result;
                WdAdapterStatus status{};
                if (!m_driver.GetStatus(status))
                    return m_driver.LastError();
                for (UINT32 i = 0; i < status.MonitorCount; ++i)
                {
                    if (status.Monitors[i].ConnectorIndex != created) continue;
                    output.resize(sizeof(WdMonitorRuntime));
                    memcpy(output.data(), &status.Monitors[i], sizeof(WdMonitorRuntime));
                    return ERROR_SUCCESS;
                }
                return ERROR_NOT_FOUND;
            }
            case WdCmd_RemoveDisplay:return RemoveDisplay(connector);
            case WdCmd_RestartDisplay:return Restart(connector);
            case WdCmd_OpenViewer:
                return m_window
                    ? static_cast<DWORD>(SendMessageW(m_window,WM_OPEN_VIEWER,connector,0))
                    : ERROR_INVALID_WINDOW_HANDLE;
            case WdCmd_ListDisplays:{WdAdapterStatus status{};if(!m_driver.GetStatus(status))return m_driver.LastError();output.resize(sizeof(status));memcpy(output.data(),&status,sizeof(status));return ERROR_SUCCESS;}
            case WdCmd_MoveWindow:
                if(payload.size()!=sizeof(UINT64)+sizeof(UINT32))return ERROR_INVALID_PARAMETER;
                {UINT64 raw;UINT32 target;memcpy(&raw,payload.data(),sizeof(raw));memcpy(&target,payload.data()+sizeof(raw),sizeof(target));return MoveWindow(raw,target);}
            case WdCmd_SetAlwaysOnTop:
            {
                if (payload.size() < sizeof(UINT32)) return ERROR_INVALID_PARAMETER;
                UINT32 enabled = 0;
                memcpy(&enabled, payload.data(), sizeof(enabled));
                std::lock_guard<std::mutex> lock(m_lock);
                m_viewersAlwaysOnTop = enabled != 0;
                for (auto& viewer : m_viewers)
                    if (viewer) viewer->SetAlwaysOnTop(m_viewersAlwaysOnTop);
                return ERROR_SUCCESS;
            }
            case WdCmd_SetScaleMode:
            {
                if (payload.size() < sizeof(UINT32) * 2) return ERROR_INVALID_PARAMETER;
                UINT32 target = 0, scale = 0;
                memcpy(&target, payload.data(), sizeof(target));
                memcpy(&scale, payload.data() + sizeof(target), sizeof(scale));
                std::lock_guard<std::mutex> lock(m_lock);
                for (auto& viewer : m_viewers)
                {
                    if (viewer && viewer->Connector() == target)
                    {
                        viewer->SetScaleMode(scale);
                        return ERROR_SUCCESS;
                    }
                }
                return ERROR_NOT_FOUND;
            }
            case WdCmd_RescueOffscreen:WindowMover::RescueOffscreenWindows();return ERROR_SUCCESS;
            case WdCmd_SetDropWindows:
            {
                if (payload.size() < sizeof(UINT32)) return ERROR_INVALID_PARAMETER;
                UINT32 enabled = 0;
                memcpy(&enabled, payload.data(), sizeof(enabled));
                m_dropWindowsOntoViewer = enabled != 0;
                if (!m_dropWindowsOntoViewer)
                {
                    m_moveSizeWindow = nullptr;
                    SetSuppressInjectAll(false);
                }
                return ERROR_SUCCESS;
            }
            case WdCmd_SetMirrorSource:
            {
                if (payload.size() < sizeof(UINT32) + sizeof(WCHAR) * 128) return ERROR_INVALID_PARAMETER;
                UINT32 target = 0;
                WCHAR device[128]{};
                memcpy(&target, payload.data(), sizeof(target));
                memcpy(device, payload.data() + sizeof(target), sizeof(device));
                device[127] = L'\0';
                std::lock_guard<std::mutex> lock(m_lock);
                for (auto& viewer : m_viewers)
                {
                    if (viewer && viewer->Connector() == target)
                    {
                        viewer->SetMirrorSource(device);
                        return ERROR_SUCCESS;
                    }
                }
                return ERROR_NOT_FOUND;
            }
            case WdCmd_SetViewerInteraction:
            {
                if (payload.size() < sizeof(UINT32) * 2) return ERROR_INVALID_PARAMETER;
                UINT32 requireDoubleClick = 0, hoverFade = 0;
                memcpy(&requireDoubleClick, payload.data(), sizeof(requireDoubleClick));
                memcpy(&hoverFade, payload.data() + sizeof(requireDoubleClick), sizeof(hoverFade));
                std::lock_guard<std::mutex> lock(m_lock);
                m_requireDoubleClick = requireDoubleClick != 0;
                m_hoverFadeEnabled = hoverFade != 0;
                for (auto& viewer : m_viewers)
                    if (viewer) viewer->SetInteractionOptions(m_requireDoubleClick, m_hoverFadeEnabled);
                return ERROR_SUCCESS;
            }
            case WdCmd_Shutdown:PostMessageW(m_window,WM_CLOSE,0,0);return ERROR_SUCCESS;
            default:return ERROR_NOT_SUPPORTED;
            }
        }
        HWND m_window = nullptr;
        HRESULT LastCreateHr() const { return m_lastCreateHr; }
    private:
        DeviceReady m_ready{};
        bool m_comInitialized = false;
        HRESULT m_lastCreateHr = S_OK;
        DriverClient m_driver;
        DisplayTopology m_topology;
        std::array<std::wstring, WD_MAX_MONITORS> m_devices;
        std::vector<std::unique_ptr<ViewerWindow>> m_viewers;
        std::unique_ptr<HostServer> m_server;
        std::mutex m_lock;
        bool m_viewersAlwaysOnTop = true;
        bool m_dropWindowsOntoViewer = true;
        bool m_requireDoubleClick = false;
        bool m_hoverFadeEnabled = true;
        HWND m_moveSizeWindow = nullptr;
        HWINEVENTHOOK m_moveSizeHook = nullptr;
        DWORD m_uiThreadId = GetCurrentThreadId();
    };
    LRESULT CALLBACK PrototypeProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
    {
        switch(message) {
        case WM_CREATE: CreateWindowW(L"BUTTON",L"Create Display",WS_CHILD|WS_VISIBLE,12,12,145,30,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CREATE)),GetModuleHandleW(nullptr),nullptr);
                        CreateWindowW(L"BUTTON",L"Open Viewer",WS_CHILD|WS_VISIBLE,12,50,145,30,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN)),GetModuleHandleW(nullptr),nullptr);
                        CreateWindowW(L"BUTTON",L"Restart Display",WS_CHILD|WS_VISIBLE,12,88,145,30,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RESTART)),GetModuleHandleW(nullptr),nullptr);
                        CreateWindowW(L"BUTTON",L"Remove Display",WS_CHILD|WS_VISIBLE,12,126,145,30,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REMOVE)),GetModuleHandleW(nullptr),nullptr);return 0;
        case WM_COMMAND: if(g_app){
            DWORD e=ERROR_SUCCESS;
            switch(LOWORD(wParam)){
            case ID_CREATE:e=g_app->CreateDisplay();break;
            case ID_OPEN:e=g_app->OpenViewer();break;
            case ID_RESTART:e=g_app->Restart();break;
            case ID_REMOVE:e=g_app->RemoveDisplay();break;
            }
            if(e){
                std::wstring msg = L"Operation failed: " + std::to_wstring(e);
                if (e == ERROR_INVALID_WINDOW_HANDLE) msg += L"\n(Invalid window handle during viewer create.)";
                MessageBoxW(window,msg.c_str(),L"MultiBox Viewer",MB_ICONERROR);
            }
        }return 0;
        case WM_OPEN_VIEWER:
            if (g_app)
            {
                const DWORD e = g_app->OpenViewer(static_cast<UINT32>(wParam));
                return static_cast<LRESULT>(e);
            }
            return ERROR_INVALID_HANDLE;
        case WM_CLOSE_VIEWER:
            return g_app
                ? static_cast<LRESULT>(g_app->CloseViewer(static_cast<UINT32>(wParam)))
                : ERROR_INVALID_HANDLE;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        } return DefWindowProcW(window,message,wParam,lParam);
    }
}

namespace
{
    bool IsProcessElevated()
    {
        BOOL elevated = FALSE;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated;
        CloseHandle(token);
        return elevated == TRUE;
    }

    bool RelaunchElevated(HINSTANCE instance, PWSTR commandLine)
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(instance, path, MAX_PATH))
            return false;

        std::wstring params = commandLine ? commandLine : L"";
        if (params.find(L"--elevated") == std::wstring::npos)
        {
            if (!params.empty())
                params += L' ';
            params += L"--elevated";
        }

        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = path;
        sei.lpParameters = params.c_str();
        sei.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&sei) == TRUE;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show)
{
    const bool alreadyElevatedFlag = commandLine && wcsstr(commandLine, L"--elevated") != nullptr;
    const bool showPrototype = commandLine && wcsstr(commandLine, L"--prototype") != nullptr;

    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpszClassName = L"WindowDisplay.Prototype";
    wc.lpfnWndProc = PrototypeProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    Application app;
    g_app = &app;
    // Hidden host HWND is still required for UI-thread viewer create/destroy messages.
    app.m_window = CreateWindowExW(
        0,
        L"WindowDisplay.Prototype",
        L"MultiBox Viewer Host",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        80, 80, 190, 210,
        nullptr, nullptr, instance, nullptr);
    if (!app.m_window)
    {
        g_app = nullptr;
        return 1;
    }

    std::wstring initError;
    if (!app.Initialize(&initError))
    {
        const bool accessDenied = app.LastCreateHr() == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        if (accessDenied && !IsProcessElevated() && !alreadyElevatedFlag)
        {
            if (RelaunchElevated(instance, commandLine))
                return 0;
        }

        const std::wstring message =
            L"Could not create or open the MultiBox Viewer software device.\n\n" + initError;
        MessageBoxW(nullptr, message.c_str(), L"MultiBox Viewer", MB_ICONERROR);
        DestroyWindow(app.m_window);
        return 1;
    }

    ShowWindow(app.m_window, showPrototype ? show : SW_HIDE);
    if (showPrototype)
        UpdateWindow(app.m_window);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.Shutdown();
    g_app = nullptr;
    return 0;
}
