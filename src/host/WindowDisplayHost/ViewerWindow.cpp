#include "pch.h"
#include "ViewerWindow.h"
#include "DisplayTopology.h"
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;
namespace {
constexpr int kToolbar = 40;
constexpr UINT kTimerRender = 1;
constexpr UINT kScale = 1001, kFull = 1002, kPin = 1003, kSource = 1004;
// Match controller Themes/Colors.xaml (light)
constexpr COLORREF kCanvas = RGB(244, 247, 249);
constexpr COLORREF kSurfaceMuted = RGB(232, 238, 242);
constexpr COLORREF kText = RGB(23, 34, 44);
constexpr COLORREF kSubtle = RGB(91, 104, 116);
constexpr COLORREF kBorder = RGB(216, 224, 230);
constexpr COLORREF kAccent = RGB(23, 107, 147);
struct Vertex { float x, y, u, v; };
}

ViewerWindow::ViewerWindow(UINT32 connector, std::wstring device, WdMode mode) : m_connector(connector), m_displayDevice(std::move(device)), m_mode(mode) {}
ViewerWindow::~ViewerWindow()
{
    DestroyWindowThumbnail();
    Close();
    if (m_uiFont) { DeleteObject(m_uiFont); m_uiFont = nullptr; }
    if (m_toolbarBrush) { DeleteObject(m_toolbarBrush); m_toolbarBrush = nullptr; }
    if (m_controlBrush) { DeleteObject(m_controlBrush); m_controlBrush = nullptr; }
}

bool ViewerWindow::Create(HINSTANCE instance, bool pictureInPicture)
{
    static ATOM atom = 0;
    if (!atom)
    {
        WNDCLASSW wc{};
        wc.style = CS_DBLCLKS;
        wc.hInstance = instance ? instance : GetModuleHandleW(nullptr);
        wc.lpszClassName = L"WindowDisplay.Viewer";
        wc.lpfnWndProc = WndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        atom = RegisterClassW(&wc);
        if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            atom = 1; // class already registered in this process
        if (!atom) return false;
    }

    SetLastError(ERROR_SUCCESS);
    const std::wstring title = L"MultiBox Viewer - Display " + std::to_wstring(m_connector + 1);
    // CreateWindowEx sends WM_NCCREATE/WM_CREATE before returning; HandleMessage must use the
    // HWND argument (m_window is still null here). That bug previously surfaced as error 1400.
    const HWND window = CreateWindowExW(
        WS_EX_WINDOWEDGE | WS_EX_LAYERED,
        L"WindowDisplay.Viewer",
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 460,
        nullptr, nullptr, instance, this);
    if (!window) return false;
    m_window = window;
    SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);

    m_name = CreateWindowW(L"STATIC", (L"Display " + std::to_wstring(m_connector + 1)).c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 12, 8, 210, 24, m_window, nullptr, instance, nullptr);
    m_source = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        228, 7, 168, 320, m_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSource)), instance, nullptr);
    m_scale = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        288, 7, 100, 250, m_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScale)), instance, nullptr);
    for (const wchar_t* s : { L"Fit", L"Fill", L"Actual Size" })
        SendMessageW(m_scale, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(m_scale, CB_SETCURSEL, 0, 0);
    m_fullscreenButton = CreateWindowW(L"BUTTON", L"Full screen",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        396, 7, 96, 26, m_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFull)), instance, nullptr);
    m_pin = CreateWindowW(L"BUTTON", L"Pin",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_FLAT,
        500, 7, 56, 26, m_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPin)), instance, nullptr);
    StyleToolbarControls();
    RefreshMirrorSourceList();

    SetLastError(ERROR_SUCCESS);
    if (!CreateRenderer())
    {
        const DWORD err = GetLastError();
        DestroyWindow(m_window);
        m_window = nullptr;
        if (err) SetLastError(err);
        return false;
    }
    SetTimer(m_window, kTimerRender, 16, nullptr);

    if (pictureInPicture)
        PlaceAsPiPOnPrimary();
    return true;
}

void ViewerWindow::PlaceAsPiPOnPrimary()
{
    if (!m_window) return;

    MONITORINFO mi{ sizeof(mi) };
    const HMONITOR primary = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(primary, &mi)) return;

    const RECT& work = mi.rcWork;
    const int modeW = (std::max)(1, static_cast<int>(m_mode.Width ? m_mode.Width : 1920));
    const int modeH = (std::max)(1, static_cast<int>(m_mode.Height ? m_mode.Height : 1080));
    const int workW = (std::max)(1, static_cast<int>(work.right - work.left));
    const int workH = (std::max)(1, static_cast<int>(work.bottom - work.top));

    // Compact floating PiP on the primary work area (bottom-right).
    int width = (std::clamp)(static_cast<int>(modeW * 0.4), 480, (std::min)(960, workW - 48));
    int height = static_cast<int>(width * (static_cast<float>(modeH) / modeW)) + kToolbar + 40;
    height = (std::clamp)(height, 300, (std::min)(640, workH - 48));

    const int margin = 28;
    const int x = work.left + (std::max)(margin, workW - width - margin);
    const int y = work.top + (std::max)(margin, workH - height - margin);

    // Position only — host applies the saved always-on-top preference after Create.
    SetWindowPos(m_window, HWND_TOP, x, y, width, height,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    ResizeRenderer(static_cast<UINT>(width), static_cast<UINT>(height));
}

void ViewerWindow::Show()
{
    if (!m_window || !IsWindow(m_window)) return;
    ShowWindow(m_window, SW_SHOWNORMAL);
    SetWindowPos(m_window, m_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    // Foreground/flash can fail harmlessly under UIPI; never treat as fatal.
    SetForegroundWindow(m_window);
}
void ViewerWindow::SetAlwaysOnTop(bool enabled)
{
    m_topmost = enabled;
    if (m_pin) SendMessageW(m_pin, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    if (m_window)
        SetWindowPos(m_window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}
void ViewerWindow::SetScaleMode(UINT32 scaleMode)
{
    if (!m_scale) return;
    const int index = scaleMode > 2 ? 0 : static_cast<int>(scaleMode);
    SendMessageW(m_scale, CB_SETCURSEL, index, 0);
    UpdateImageRect();
}
void ViewerWindow::SetMirrorSource(const std::wstring& gdiDeviceName)
{
    if (_wcsicmp(gdiDeviceName.c_str(), m_displayDevice.c_str()) == 0)
        m_mirrorDevice.clear();
    else
        m_mirrorDevice = gdiDeviceName;

    m_duplication.Reset();
    m_dupDevice.Reset();
    m_dupContext.Reset();
    m_haveFrame = false;
    m_lastFrame.Reset();
    m_lastFrameSerial = 0;
    m_capturePath.clear();

    if (m_source)
    {
        int select = 0;
        for (size_t i = 0; i < m_sourceDevices.size(); ++i)
        {
            if (_wcsicmp(m_sourceDevices[i].c_str(), m_mirrorDevice.c_str()) == 0)
            {
                select = static_cast<int>(i);
                break;
            }
        }
        SendMessageW(m_source, CB_SETCURSEL, select, 0);
    }
    UpdateImageRect();
}
void ViewerWindow::SetSuppressInputInject(bool suppress) { m_suppressInputInject = suppress; }

void ViewerWindow::SetInteractionOptions(bool requireDoubleClick, bool hoverFade)
{
    m_requireDoubleClick = requireDoubleClick;
    m_hoverFadeEnabled = hoverFade;
    if (m_requireDoubleClick)
        SetInputEngaged(false);
    else
    {
        m_inputEngaged = true;
        if (m_name)
            SetWindowTextW(m_name, (L"Display " + std::to_wstring(m_connector + 1)).c_str());
    }
    if (!m_hoverFadeEnabled)
        ApplyHoverFade(false);
}

bool ViewerWindow::ShouldInjectInput() const
{
    if (m_suppressInputInject) return false;
    if (m_requireDoubleClick && !m_inputEngaged) return false;
    return true;
}

void ViewerWindow::SetInputEngaged(bool engaged)
{
    m_inputEngaged = engaged;
    if (!engaged)
        m_input.ReleaseAllKeys();
    if (m_name)
    {
        std::wstring label = L"Display " + std::to_wstring(m_connector + 1);
        if (m_requireDoubleClick && !m_inputEngaged)
            label += L"  ·  double-click to control";
        else if (m_requireDoubleClick && m_inputEngaged)
            label += L"  ·  Esc / leave to release";
        SetWindowTextW(m_name, label.c_str());
    }
    if (engaged && m_window && !m_trackingMouse)
    {
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, m_window, 0 };
        TrackMouseEvent(&track);
        m_trackingMouse = true;
    }
    if (m_hoverFaded)
        ApplyHoverFade(true);
}

void ViewerWindow::ApplyHoverFade(bool hovered)
{
    if (!m_window) return;
    LONG_PTR ex = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) == 0)
        SetWindowLongPtrW(m_window, GWL_EXSTYLE, ex | WS_EX_LAYERED);

    BYTE alpha = 255;
    if (m_hoverFadeEnabled && hovered)
    {
        // See-through while pointing at the PiP; a bit more opaque when controlling.
        alpha = m_inputEngaged ? 200 : 145;
        m_hoverFaded = true;
    }
    else
    {
        m_hoverFaded = false;
    }
    SetLayeredWindowAttributes(m_window, 0, alpha, LWA_ALPHA);
}

bool ViewerWindow::IsPointInImage(LPARAM lParam) const
{
    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    return PtInRect(&m_imageRect, pt) != FALSE;
}
bool ViewerWindow::HitTestImage(POINT screenPoint) const
{
    if (!m_window || !IsWindowVisible(m_window)) return false;
    POINT client = screenPoint;
    if (!ScreenToClient(m_window, &client)) return false;
    return PtInRect(&m_imageRect, client) != FALSE;
}
bool ViewerWindow::HitTestDropTarget(POINT screenPoint) const
{
    if (!m_window || !IsWindow(m_window)) return false;
    RECT windowRect{};
    if (!GetWindowRect(m_window, &windowRect)) return false;
    return PtInRect(&windowRect, screenPoint) != FALSE;
}
bool ViewerWindow::GetOwnedMonitorRect(RECT& rect) const
{
    if (m_displayDevice.empty()) return false;
    return DisplayTopology().GetMonitorRect(m_displayDevice, rect);
}
void ViewerWindow::RefreshMirrorSourceList()
{
    if (!m_source) return;
    const std::wstring previous = m_mirrorDevice;
    SendMessageW(m_source, CB_RESETCONTENT, 0, 0);
    m_sourceDevices.clear();
    m_sourceDevices.emplace_back(); // index 0 = this virtual display
    SendMessageW(m_source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"This display"));

    for (const auto& display : DisplayTopology().ListMonitorDevices())
    {
        if (_wcsicmp(display.deviceName.c_str(), m_displayDevice.c_str()) == 0)
            continue;
        m_sourceDevices.push_back(display.deviceName);
        std::wstring label = display.friendlyName.empty() ? display.deviceName : display.friendlyName;
        if (display.isPrimary) label += L" (Primary)";
        else label += L" (" + display.deviceName + L")";
        SendMessageW(m_source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    int select = 0;
    for (size_t i = 0; i < m_sourceDevices.size(); ++i)
    {
        if (_wcsicmp(m_sourceDevices[i].c_str(), previous.c_str()) == 0)
        {
            select = static_cast<int>(i);
            break;
        }
    }
    m_mirrorDevice = m_sourceDevices[static_cast<size_t>(select)];
    SendMessageW(m_source, CB_SETCURSEL, select, 0);
}
void ViewerWindow::ApplyMirrorSourceSelection()
{
    if (!m_source) return;
    const int index = static_cast<int>(SendMessageW(m_source, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(m_sourceDevices.size())) return;
    SetMirrorSource(m_sourceDevices[static_cast<size_t>(index)]);
}
void ViewerWindow::Close() { if (m_window) DestroyWindow(m_window); m_window = nullptr; }
bool ViewerWindow::IsViewerWindow(HWND window) { wchar_t name[64]{}; return GetClassNameW(window, name, 64) && wcscmp(name, L"WindowDisplay.Viewer") == 0; }

bool ViewerWindow::CreateRenderer()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &m_device, &level, &m_context))) return false;
    m_device.As(&m_device1);
    ComPtr<IDXGIDevice> dxgiDevice; m_device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter; dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 1; desc.Height = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(m_device.Get(), m_window, &desc, nullptr, nullptr, &m_swapChain))) return false;
    factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
    const char* vs = "struct V{float2 p:POSITION;float2 t:TEXCOORD;};struct O{float4 p:SV_POSITION;float2 t:TEXCOORD;};O main(V i){O o;o.p=float4(i.p,0,1);o.t=i.t;return o;}";
    const char* ps = "Texture2D t:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 u:TEXCOORD):SV_TARGET{return t.Sample(s,u);}";
    ComPtr<ID3DBlob> bvs, bps, err;
    if (FAILED(D3DCompile(vs, strlen(vs), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &bvs, &err)) ||
        FAILED(D3DCompile(ps, strlen(ps), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &bps, &err))) return false;
    m_device->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &m_vs);
    m_device->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &m_ps);
    D3D11_INPUT_ELEMENT_DESC input[] = { {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}, {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0} };
    m_device->CreateInputLayout(input, 2, bvs->GetBufferPointer(), bvs->GetBufferSize(), &m_layout);
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(Vertex) * 4; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&bd, nullptr, &m_vertices);
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_device->CreateSamplerState(&sd, &m_sampler);
    RECT rc{}; GetClientRect(m_window, &rc); ResizeRenderer(rc.right, rc.bottom); OpenDesktopDuplication();
    return true;
}

void ViewerWindow::DestroyRenderer()
{
    m_duplication.Reset();
    m_dupContext.Reset();
    m_dupDevice.Reset();
    m_shareContext.Reset();
    m_shareDevice1.Reset();
    m_shareDevice.Reset();
    m_shareAdapterLuid = {};
    m_copyFrame.Reset();
    m_lastFrame.Reset();
    m_haveFrame = false;
    m_lastFrameSerial = 0;
    m_rtv.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device1.Reset();
    m_device.Reset();
}
void ViewerWindow::ResizeRenderer(UINT w, UINT h)
{
    if (!m_swapChain || !w || !h) return;
    m_context->OMSetRenderTargets(0, nullptr, nullptr); m_rtv.Reset();
    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)))
    { ComPtr<ID3D11Texture2D> back; m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back)); m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv); }
    UpdateImageRect();
}

void ViewerWindow::ResolveDisplayDevice()
{
    if (!m_displayDevice.empty())
    {
        // Keep if still active; otherwise refresh.
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i)
        {
            if (_wcsicmp(device.DeviceName, m_displayDevice.c_str()) == 0 &&
                (device.StateFlags & DISPLAY_DEVICE_ACTIVE))
            {
                return;
            }
        }
    }
    m_displayDevice = DisplayTopology().FindWindowDisplayDevice(m_connector);
}

bool ViewerWindow::UploadBgraFrame(UINT width, UINT height, const void* bgraPixels, UINT pitch,
    ComPtr<ID3D11Texture2D>& texture)
{
    if (!m_device || !bgraPixels || !width || !height) return false;

    D3D11_TEXTURE2D_DESC desc{};
    if (m_lastFrame) m_lastFrame->GetDesc(&desc);
    if (!m_lastFrame || desc.Width != width || desc.Height != height)
    {
        desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_lastFrame.Reset();
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_lastFrame))) return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_lastFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    auto* dst = static_cast<BYTE*>(mapped.pData);
    const auto* src = static_cast<const BYTE*>(bgraPixels);
    const UINT rowBytes = width * 4;
    for (UINT y = 0; y < height; ++y)
        memcpy(dst + y * mapped.RowPitch, src + y * pitch, rowBytes);
    m_context->Unmap(m_lastFrame.Get(), 0);
    texture = m_lastFrame;
    m_haveFrame = true;
    return true;
}

bool ViewerWindow::ReadMonitorRuntime(WdMonitorRuntime& runtime)
{
    ZeroMemory(&runtime, sizeof(runtime));
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, WD_SHARED_STATE_NAME);
    if (mapping)
    {
        auto* state = static_cast<WdSharedState*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(WdSharedState)));
        if (state)
        {
            for (UINT32 i = 0; i < WD_MAX_MONITORS; ++i)
            {
                if (!state->Monitors[i].Active) continue;
                if (state->Monitors[i].ConnectorIndex != m_connector) continue;
                runtime = state->Monitors[i];
                UnmapViewOfFile(state);
                CloseHandle(mapping);
                return true;
            }
            UnmapViewOfFile(state);
        }
        CloseHandle(mapping);
    }

    // Session/global namespace or ACL policy can prevent opening the shared mapping.
    // The IOCTL status path carries the same metadata and is a reliable fallback.
    if (!m_runtimeDriver.IsOpen() && !m_runtimeDriver.Open(100))
        return false;
    WdAdapterStatus status{};
    if (!m_runtimeDriver.GetStatus(status))
        return false;
    for (UINT32 i = 0; i < status.MonitorCount && i < WD_MAX_MONITORS; ++i)
    {
        if (!status.Monitors[i].Active) continue;
        if (status.Monitors[i].ConnectorIndex != m_connector) continue;
        runtime = status.Monitors[i];
        return true;
    }
    return false;
}

bool ViewerWindow::EnsureShareDevice(const LUID& adapterLuid)
{
    if (m_shareDevice && m_shareAdapterLuid.LowPart == adapterLuid.LowPart &&
        m_shareAdapterLuid.HighPart == adapterLuid.HighPart)
    {
        return true;
    }

    m_shareDevice.Reset();
    m_shareDevice1.Reset();
    m_shareContext.Reset();
    m_shareAdapterLuid = {};

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.AdapterLuid.LowPart != adapterLuid.LowPart ||
            desc.AdapterLuid.HighPart != adapterLuid.HighPart)
        {
            continue;
        }

        D3D_FEATURE_LEVEL level{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                nullptr, 0, D3D11_SDK_VERSION, &m_shareDevice, &level, &m_shareContext)))
        {
            return false;
        }
        m_shareDevice.As(&m_shareDevice1);
        m_shareAdapterLuid = adapterLuid;
        return m_shareDevice1 != nullptr;
    }
    return false;
}

bool ViewerWindow::IsMostlyBlackBgra(const void* bgraPixels, UINT width, UINT height, UINT pitch)
{
    if (!bgraPixels || !width || !height) return true;
    const auto* rows = static_cast<const BYTE*>(bgraPixels);
    // Empty Win11 virtual desktops are often dark gray (not pure black). Use average
    // luminance so those still count as "empty" and we can show the placeholder.
    UINT64 lumaSum = 0;
    UINT samples = 0;
    UINT bright = 0;
    for (UINT y = 0; y < height; y += (std::max)(1u, height / 24))
    {
        const auto* row = rows + y * pitch;
        for (UINT x = 0; x < width; x += (std::max)(1u, width / 24))
        {
            const BYTE b = row[x * 4 + 0];
            const BYTE g = row[x * 4 + 1];
            const BYTE r = row[x * 4 + 2];
            const UINT luma = (r * 30 + g * 59 + b * 11) / 100;
            lumaSum += luma;
            if (luma > 48) ++bright;
            ++samples;
        }
    }
    if (samples == 0) return true;
    const UINT avgLuma = static_cast<UINT>(lumaSum / samples);
    return avgLuma < 28 || bright * 20 < samples; // dim overall, or <5% bright samples
}

bool ViewerWindow::OpenNamedSharedTexture(
    const WCHAR* name,
    const LUID* preferredAdapter,
    ComPtr<ID3D11Texture2D>& shared)
{
    shared.Reset();
    if (!name || !name[0]) return false;

    auto tryDevice = [&](ID3D11Device1* device1) -> bool
    {
        if (!device1) return false;
        m_lastShareHr = device1->OpenSharedResourceByName(
            name, DXGI_SHARED_RESOURCE_READ, IID_PPV_ARGS(&shared));
        return SUCCEEDED(m_lastShareHr) && shared;
    };

    if (preferredAdapter && EnsureShareDevice(*preferredAdapter) && tryDevice(m_shareDevice1.Get()))
        return true;

    if (m_device1 && tryDevice(m_device1.Get()))
        return true;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (!EnsureShareDevice(desc.AdapterLuid)) continue;
        if (tryDevice(m_shareDevice1.Get())) return true;
    }
    return false;
}

bool ViewerWindow::BuildPlaceholderFrame(ComPtr<ID3D11Texture2D>& texture, bool signalLive)
{
    const UINT width = (std::max)(1u, m_mode.Width ? m_mode.Width : 1280u);
    const UINT height = (std::max)(1u, m_mode.Height ? m_mode.Height : 720u);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!screen || !mem || !dib || !bits)
    {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        if (screen) ReleaseDC(nullptr, screen);
        return false;
    }

    const HGDIOBJ old = SelectObject(mem, dib);
    RECT rc{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    HBRUSH brush = CreateSolidBrush(RGB(18, 28, 48));
    FillRect(mem, &rc, brush);
    DeleteObject(brush);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(235, 240, 250));
    HFONT titleFont = CreateFontW(static_cast<int>(height / 8), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT bodyFont = CreateFontW(static_cast<int>(height / 18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(mem, titleFont);

    std::wstring title = L"Display " + std::to_wstring(m_connector + 1);
    RECT titleRc = rc;
    titleRc.top = height / 3;
    DrawTextW(mem, title.c_str(), -1, &titleRc, DT_CENTER | DT_SINGLELINE);

    SelectObject(mem, bodyFont);
    SetTextColor(mem, RGB(160, 180, 210));
    std::wstring body = signalLive
        ? L"This display is ready — same as any other monitor.\n"
          L"Move a window here, open an app on it, or drag a title bar onto this preview."
        : L"Waiting for the display to come online…";
    RECT bodyRc = rc;
    bodyRc.top = titleRc.top + height / 7;
    bodyRc.left = width / 10;
    bodyRc.right = width - width / 10;
    DrawTextW(mem, body.c_str(), -1, &bodyRc, DT_CENTER | DT_WORDBREAK);

    SelectObject(mem, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    SelectObject(mem, old);

    const bool ok = UploadBgraFrame(width, height, bits, width * 4, texture);
    m_lastFrameBlack = true;
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

bool ViewerWindow::OpenPixelBufferFrame(ComPtr<ID3D11Texture2D>& texture)
{
    WCHAR name[128];
    swprintf_s(name, L"%s%u", WD_PIXEL_BUFFER_PREFIX, m_connector);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!mapping) return false;

    auto* view = static_cast<WdPixelBuffer*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(WdPixelBuffer)));
    if (!view)
    {
        CloseHandle(mapping);
        return false;
    }

    bool ok = false;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (view->Sequence != 0) { Sleep(0); continue; } // writer in progress
        if (view->FrameSerial == 0 || view->Width == 0 || view->Height == 0) break;
        if (view->Width > WD_MAX_PIXEL_WIDTH || view->Height > WD_MAX_PIXEL_HEIGHT) break;

        const UINT64 serial = view->FrameSerial;
        const UINT width = view->Width;
        const UINT height = view->Height;
        const UINT pitch = view->Pitch ? view->Pitch : width * 4;
        std::vector<BYTE> copy(static_cast<size_t>(pitch) * height);
        memcpy(copy.data(), view->Pixels, copy.size());
        if (view->Sequence != 0 || view->FrameSerial != serial) continue;

        m_lastFrameBlack = IsMostlyBlackBgra(copy.data(), width, height, pitch);
        ok = UploadBgraFrame(width, height, copy.data(), pitch, texture);
        if (ok)
        {
            m_lastFrameSerial = serial;
            m_capturePath = L"pixels";
        }
        break;
    }

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return ok;
}

bool ViewerWindow::OpenSharedFrame(ComPtr<ID3D11Texture2D>& texture)
{
    WdMonitorRuntime runtime{};
    if (!ReadMonitorRuntime(runtime) || runtime.FrameSerial == 0)
        return false;
    if (runtime.SharedTextureName[0] == L'\0')
    {
        m_lastShareHr = E_FAIL;
        return false;
    }

    const bool haveAdapter =
        runtime.RenderAdapterLuid.LowPart != 0 || runtime.RenderAdapterLuid.HighPart != 0;
    ComPtr<ID3D11Texture2D> shared;
    if (!OpenNamedSharedTexture(
            runtime.SharedTextureName,
            haveAdapter ? &runtime.RenderAdapterLuid : nullptr,
            shared))
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    shared->GetDesc(&desc);

    // Prefer the device that opened the shared resource for the staging copy.
    ID3D11Device* copyDevice = m_shareDevice ? m_shareDevice.Get() : m_device.Get();
    ID3D11DeviceContext* copyContext = m_shareContext ? m_shareContext.Get() : m_context.Get();
    if (!copyDevice || !copyContext) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(copyDevice->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        return false;

    copyContext->CopyResource(staging.Get(), shared.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(copyContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    m_lastFrameBlack = IsMostlyBlackBgra(mapped.pData, desc.Width, desc.Height, mapped.RowPitch);
    const bool ok = UploadBgraFrame(desc.Width, desc.Height, mapped.pData, mapped.RowPitch, texture);
    copyContext->Unmap(staging.Get(), 0);
    if (ok)
    {
        m_lastFrameSerial = runtime.FrameSerial;
        m_capturePath = L"shared";
        // Empty virtual desktops are legitimately black — keep the frame, but expose a label.
        if (m_lastFrameBlack && m_name)
        {
            const std::wstring label = L"Display " + std::to_wstring(m_connector + 1) +
                L"  ·  empty desktop (signal OK)";
            SetWindowTextW(m_name, label.c_str());
        }
    }
    return ok;
}

bool ViewerWindow::OpenDesktopDuplication()
{
    ResolveDisplayDevice();
    m_duplication.Reset();
    m_dupDevice.Reset();
    m_dupContext.Reset();
    const std::wstring capture = CaptureDevice();
    if (capture.empty()) return false;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        for (UINT outputIndex = 0;; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_OUTPUT_DESC outputDesc{};
            output->GetDesc(&outputDesc);
            if (_wcsicmp(outputDesc.DeviceName, capture.c_str()) != 0) continue;

            // Desktop Duplication requires a D3D device on the SAME adapter as the output.
            D3D_FEATURE_LEVEL level{};
            ComPtr<ID3D11Device> dupDevice;
            ComPtr<ID3D11DeviceContext> dupContext;
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                    nullptr, 0, D3D11_SDK_VERSION, &dupDevice, &level, &dupContext)))
            {
                return false;
            }

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) return false;
            ComPtr<IDXGIOutputDuplication> duplication;
            if (FAILED(output1->DuplicateOutput(dupDevice.Get(), &duplication))) return false;

            m_dupDevice = dupDevice;
            m_dupContext = dupContext;
            m_duplication = duplication;
            return true;
        }
    }
    return false;
}

bool ViewerWindow::AcquireDesktopFrame(ComPtr<ID3D11Texture2D>& texture)
{
    if (!m_duplication && !OpenDesktopDuplication()) return false;

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const HRESULT hr = m_duplication->AcquireNextFrame(0, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        if (m_haveFrame && m_lastFrame) { texture = m_lastFrame; return true; }
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST)
    {
        m_duplication.Reset();
        m_dupDevice.Reset();
        m_dupContext.Reset();
        return false;
    }
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> source;
    resource.As(&source);
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);

    // Readback on the duplication device, then upload to the window device (cross-adapter safe).
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;

    ComPtr<ID3D11Texture2D> staging;
    bool ok = false;
    if (SUCCEEDED(m_dupDevice->CreateTexture2D(&stagingDesc, nullptr, &staging)))
    {
        m_dupContext->CopyResource(staging.Get(), source.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_dupContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            // Empty virtual desktops are black — still a valid live signal.
            m_lastFrameBlack = IsMostlyBlackBgra(mapped.pData, desc.Width, desc.Height, mapped.RowPitch);
            ok = UploadBgraFrame(desc.Width, desc.Height, mapped.pData, mapped.RowPitch, texture);
            if (ok) m_capturePath = L"duplication";
            m_dupContext->Unmap(staging.Get(), 0);
        }
    }

    m_duplication->ReleaseFrame();
    return ok;
}

bool ViewerWindow::AcquireGdiFrame(ComPtr<ID3D11Texture2D>& texture)
{
    ResolveDisplayDevice();
    const std::wstring capture = CaptureDevice();
    if (capture.empty()) return false;

    // Captures the real desktop for that GDI device — including Windows "Identify" numbers.
    HDC src = CreateDCW(capture.c_str(), nullptr, nullptr, nullptr);
    if (!src) return false;

    const int width = GetDeviceCaps(src, HORZRES);
    const int height = GetDeviceCaps(src, VERTRES);
    if (width <= 0 || height <= 0)
    {
        DeleteDC(src);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(src);
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mem || !dib || !bits)
    {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        DeleteDC(src);
        return false;
    }

    const HGDIOBJ old = SelectObject(mem, dib);
    BitBlt(mem, 0, 0, width, height, src, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);

    // IddCx virtual monitors often BitBlt as solid black even when DWM is composing.
    // Prefer showing black over inventing frames — shared/duplication paths are better.
    m_lastFrameBlack = IsMostlyBlackBgra(bits, static_cast<UINT>(width), static_cast<UINT>(height),
        static_cast<UINT>(width * 4));
    bool ok = UploadBgraFrame(static_cast<UINT>(width), static_cast<UINT>(height), bits,
        static_cast<UINT>(width * 4), texture);
    if (ok) m_capturePath = L"gdi";

    DeleteObject(dib);
    DeleteDC(mem);
    DeleteDC(src);
    return ok;
}

HWND ViewerWindow::FindLargestWindowOnCaptureMonitor() const
{
    RECT monitor{};
    if (!DisplayTopology().GetMonitorRect(CaptureDevice(), monitor)) return nullptr;

    struct Finder
    {
        RECT monitor{};
        HWND best = nullptr;
        LONG bestArea = 0;
    } finder{ monitor };

    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL
    {
        auto* state = reinterpret_cast<Finder*>(value);
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
        if (IsViewerWindow(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW) return TRUE;

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) return TRUE;
        const int cx = (rect.left + rect.right) / 2;
        const int cy = (rect.top + rect.bottom) / 2;
        if (cx < state->monitor.left || cx >= state->monitor.right ||
            cy < state->monitor.top || cy >= state->monitor.bottom)
        {
            return TRUE;
        }

        const LONG area = (std::max)(0L, rect.right - rect.left) * (std::max)(0L, rect.bottom - rect.top);
        if (area > state->bestArea)
        {
            state->bestArea = area;
            state->best = hwnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&finder));

    return (finder.best && finder.bestArea >= 200 * 200) ? finder.best : nullptr;
}

void ViewerWindow::DestroyWindowThumbnail()
{
    if (m_thumbnail)
    {
        DwmUnregisterThumbnail(m_thumbnail);
        m_thumbnail = nullptr;
    }
    m_thumbnailSource = nullptr;
    m_usingThumbnail = false;
}

bool ViewerWindow::EnsureWindowThumbnail(HWND source)
{
    if (!m_window || !source || !IsWindow(source)) return false;
    if (m_thumbnail && m_thumbnailSource == source) return true;

    DestroyWindowThumbnail();
    if (FAILED(DwmRegisterThumbnail(m_window, source, &m_thumbnail)) || !m_thumbnail)
        return false;

    m_thumbnailSource = source;
    m_usingThumbnail = true;
    m_capturePath = L"thumbnail";
    m_lastFrameBlack = false;
    UpdateWindowThumbnail();
    return true;
}

void ViewerWindow::UpdateWindowThumbnail()
{
    if (!m_thumbnail || !m_usingThumbnail) return;
    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.fVisible = TRUE;
    props.opacity = 255;
    props.fSourceClientAreaOnly = FALSE;
    props.rcDestination = m_imageRect;
    DwmUpdateThumbnailProperties(m_thumbnail, &props);
}

bool ViewerWindow::AcquireWindowOnMonitorFrame(ComPtr<ID3D11Texture2D>& texture)
{
    UNREFERENCED_PARAMETER(texture);
    ResolveDisplayDevice();
    const HWND source = FindLargestWindowOnCaptureMonitor();
    if (!source)
    {
        DestroyWindowThumbnail();
        return false;
    }
    return EnsureWindowThumbnail(source);
}

void ViewerWindow::UpdateImageRect()
{
    RECT rc{}; GetClientRect(m_window, &rc); const int top = (m_fullscreen && m_hiddenToolbar) ? 0 : kToolbar;
    const int cw = rc.right, ch = std::max(1, static_cast<int>(rc.bottom) - top), iw = std::max(1, static_cast<int>(m_mode.Width)), ih = std::max(1, static_cast<int>(m_mode.Height));
    const int mode = m_scale ? static_cast<int>(SendMessageW(m_scale, CB_GETCURSEL, 0, 0)) : 0;
    float scale = mode == 1 ? std::max(float(cw) / iw, float(ch) / ih) : mode == 2 ? 1.0f : std::min(float(cw) / iw, float(ch) / ih);
    int w = std::min(cw, std::max(1, int(iw * scale))), h = std::min(ch, std::max(1, int(ih * scale)));
    m_imageRect = { (cw-w)/2, top+(ch-h)/2, (cw+w)/2, top+(ch+h)/2 };
    RECT monitor{};
    DisplayTopology().GetMonitorRect(CaptureDevice(), monitor);
    m_input.SetTarget(monitor, m_imageRect);
}

void ViewerWindow::Render()
{
    if (IsIconic(m_window) || !IsWindowVisible(m_window) || !m_rtv) return;

    ComPtr<ID3D11Texture2D> texture;
    const bool mirroring = !m_mirrorDevice.empty();
    if (mirroring)
    {
        // Mirror mode always shows the selected source display.
        if (!AcquireDesktopFrame(texture))
            AcquireGdiFrame(texture);
    }
    else if (!OpenPixelBufferFrame(texture) && !OpenSharedFrame(texture))
    {
        if (!AcquireDesktopFrame(texture))
            AcquireGdiFrame(texture);
    }

    // DX/Unreal on IddCx virtual monitors often compose as black to Desktop Duplication.
    // DWM thumbnails can still show those windows live inside the PiP.
    m_usingThumbnail = false;
    if ((!texture || m_lastFrameBlack) && AcquireWindowOnMonitorFrame(texture))
    {
        m_usingThumbnail = true;
        m_lastFrameBlack = false;
        UpdateWindowThumbnail();
    }
    else if (!FindLargestWindowOnCaptureMonitor())
    {
        DestroyWindowThumbnail();
    }

    // Empty virtual desktops look black — show guidance (not when mirroring / thumbnailing).
    if (!mirroring && !m_usingThumbnail &&
        ((!texture && !m_haveFrame) || (texture && m_lastFrameBlack)))
    {
        const bool signalLive = m_lastFrameSerial != 0 || m_haveFrame || !m_displayDevice.empty();
        ComPtr<ID3D11Texture2D> placeholder;
        if (BuildPlaceholderFrame(placeholder, signalLive))
            texture = placeholder;
    }
    if (!texture && m_haveFrame && !m_usingThumbnail)
        texture = m_lastFrame;

    // Waiting = deep blue (not black) so a dead capture path is obvious.
    const float clearColor[4] = {
        texture ? 0.04f : 0.10f,
        texture ? 0.04f : 0.18f,
        texture ? 0.04f : 0.32f,
        1.0f
    };
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    // When a DWM thumbnail is live, leave the swapchain dark underneath — DWM draws the game on top.
    if (texture && !m_usingThumbnail)
    {
        ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &srv)))
        {
            RECT rc{}; GetClientRect(m_window, &rc);
            float l = 2.f * m_imageRect.left / std::max(1L, rc.right) - 1;
            float r = 2.f * m_imageRect.right / std::max(1L, rc.right) - 1;
            float t = 1 - 2.f * m_imageRect.top / std::max(1L, rc.bottom);
            float b = 1 - 2.f * m_imageRect.bottom / std::max(1L, rc.bottom);
            Vertex v[] = { {l,b,0,1},{l,t,0,0},{r,b,1,1},{r,t,1,0} };
            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(m_context->Map(m_vertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
            {
                memcpy(map.pData, v, sizeof(v));
                m_context->Unmap(m_vertices.Get(), 0);
            }
            UINT stride = sizeof(Vertex), offset = 0;
            m_context->IASetInputLayout(m_layout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            m_context->IASetVertexBuffers(0, 1, m_vertices.GetAddressOf(), &stride, &offset);
            m_context->VSSetShader(m_vs.Get(), nullptr, 0);
            m_context->PSSetShader(m_ps.Get(), nullptr, 0);
            m_context->PSSetShaderResources(0, 1, srv.GetAddressOf());
            m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
            m_context->Draw(4, 0);
            ComPtr<ID3D11ShaderResourceView> nullSrv;
            m_context->PSSetShaderResources(0, 1, nullSrv.GetAddressOf());
        }
    }

    if (m_window)
    {
        std::wstring title = L"MultiBox Viewer - Display " + std::to_wstring(m_connector + 1);
        if (!m_displayDevice.empty()) title += L" (" + m_displayDevice + L")";
        if (mirroring) title += L" [mirror " + m_mirrorDevice + L"]";
        if (m_usingThumbnail) title += L" [app thumbnail]";
        else if (!texture) title += L" [no signal]";
        else if (!m_capturePath.empty()) title += L" [" + m_capturePath + L" #" + std::to_wstring(m_lastFrameSerial) + L"]";
        else if (m_lastFrameSerial) title += L" [shared #" + std::to_wstring(m_lastFrameSerial) + L"]";
        if (!mirroring && !m_usingThumbnail && m_lastFrameBlack && texture) title += L" [empty]";
        if (title != m_lastTitle)
        {
            m_lastTitle = title;
            SetWindowTextW(m_window, title.c_str());
        }
    }

    m_swapChain->Present(1, 0);
}

void ViewerWindow::StyleToolbarControls()
{
    if (!m_uiFont)
    {
        m_uiFont = CreateFontW(
            -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }
    if (!m_toolbarBrush)
        m_toolbarBrush = CreateSolidBrush(kSurfaceMuted);
    if (!m_controlBrush)
        m_controlBrush = CreateSolidBrush(RGB(255, 255, 255));

    for (HWND h : { m_name, m_source, m_scale, m_fullscreenButton, m_pin })
    {
        if (!h) continue;
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        // Drop classic 3D chrome so controls sit flat on the toolbar.
        SetWindowTheme(h, L"", L"");
    }
}

void ViewerWindow::PaintToolbar(HDC hdc, const RECT& client)
{
    if (m_fullscreen && m_hiddenToolbar) return;
    RECT bar{ client.left, client.top, client.right, client.top + kToolbar };
    if (!m_toolbarBrush)
        m_toolbarBrush = CreateSolidBrush(kSurfaceMuted);
    FillRect(hdc, &bar, m_toolbarBrush);

    // Hairline separator matching the app border color.
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    const HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, bar.left, bar.bottom - 1, nullptr);
    LineTo(hdc, bar.right, bar.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void ViewerWindow::ReleaseInputIfLeavingPip(LPARAM lParam)
{
    if (!m_requireDoubleClick || !m_inputEngaged) return;
    if (!IsPointInImage(lParam))
        SetInputEngaged(false);
}

void ViewerWindow::LayoutToolbar()
{
    const BOOL show = !(m_fullscreen && m_hiddenToolbar);
    for (HWND h : { m_name, m_source, m_scale, m_fullscreenButton, m_pin })
        if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    if (show && m_window)
    {
        RECT client{};
        GetClientRect(m_window, &client);
        // Keep controls right-aligned on wider windows.
        const int right = client.right - 12;
        if (m_pin) SetWindowPos(m_pin, nullptr, right - 56, 7, 56, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_fullscreenButton) SetWindowPos(m_fullscreenButton, nullptr, right - 160, 7, 96, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_scale) SetWindowPos(m_scale, nullptr, right - 268, 7, 100, 250, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_name) SetWindowPos(m_name, nullptr, 12, 8, 210, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_source) SetWindowPos(m_source, nullptr, 228, 7, (std::max)(120, right - 268 - 236), 320, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UpdateImageRect();
}
void ViewerWindow::ToggleFullscreen() { if (!m_fullscreen) { GetWindowRect(m_window,&m_restore); MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(m_window,MONITOR_DEFAULTTONEAREST),&mi);SetWindowLongPtrW(m_window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(m_window,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);m_fullscreen=true;m_hiddenToolbar=true; } else { SetWindowLongPtrW(m_window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPos(m_window,nullptr,m_restore.left,m_restore.top,m_restore.right-m_restore.left,m_restore.bottom-m_restore.top,SWP_FRAMECHANGED|SWP_NOZORDER);m_fullscreen=false;m_hiddenToolbar=false; } LayoutToolbar(); }

LRESULT CALLBACK ViewerWindow::WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = reinterpret_cast<ViewerWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self)
            self->m_window = window; // available before CreateWindowEx returns
    }
    return self ? self->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ViewerWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        ResizeRenderer(LOWORD(lParam), HIWORD(lParam));
        LayoutToolbar();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        PaintToolbar(hdc, client);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
    {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT client{};
        GetClientRect(window, &client);
        PaintToolbar(hdc, client);
        // Leave the content area to D3D / thumbnail.
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, message == WM_CTLCOLORSTATIC ? kSubtle : kText);
        return reinterpret_cast<LRESULT>(m_toolbarBrush ? m_toolbarBrush : GetStockObject(NULL_BRUSH));
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, kText);
        return reinterpret_cast<LRESULT>(m_controlBrush ? m_controlBrush : GetStockObject(WHITE_BRUSH));
    }
    case WM_TIMER:
        if (wParam == kTimerRender) Render();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kFull) { ToggleFullscreen(); return 0; }
        if (LOWORD(wParam) == kPin)
        {
            m_topmost = SendMessageW(m_pin, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SetWindowPos(window, m_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return 0;
        }
        if (LOWORD(wParam) == kScale && HIWORD(wParam) == CBN_SELCHANGE) { UpdateImageRect(); return 0; }
        if (LOWORD(wParam) == kSource)
        {
            if (HIWORD(wParam) == CBN_DROPDOWN) { RefreshMirrorSourceList(); return 0; }
            if (HIWORD(wParam) == CBN_SELCHANGE) { ApplyMirrorSourceSelection(); return 0; }
        }
        break;
    case WM_MOUSEMOVE:
        if (m_fullscreen && m_hiddenToolbar && GET_Y_LPARAM(lParam) < 4) { m_hiddenToolbar = false; LayoutToolbar(); }
        // Always track leave while we care about hover fade or engaged capture.
        if ((m_hoverFadeEnabled || (m_requireDoubleClick && m_inputEngaged)) && !m_trackingMouse)
        {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            m_trackingMouse = true;
            if (m_hoverFadeEnabled)
                ApplyHoverFade(true);
        }
        // Leaving the picture (toolbar / chrome / edge) releases capture.
        ReleaseInputIfLeavingPip(lParam);
        if (m_requireDoubleClick && m_inputEngaged && IsPointInImage(lParam))
        {
            // Pushing to the PiP edge lets the user walk the cursor back to the main screen.
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            constexpr int kEscape = 10;
            if (pt.x <= m_imageRect.left + kEscape || pt.x >= m_imageRect.right - kEscape ||
                pt.y <= m_imageRect.top + kEscape || pt.y >= m_imageRect.bottom - kEscape)
            {
                SetInputEngaged(false);
                return 0;
            }
        }
        if (ShouldInjectInput() && IsPointInImage(lParam))
        {
            POINT keep{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(window, &keep);
            m_input.InjectMouse(message, wParam, lParam);
            // Keep the real cursor in the PiP. Absolute injection otherwise jumps onto the
            // virtual monitor, and the next hover re-sucks the pointer into the preview.
            if (m_requireDoubleClick && m_inputEngaged)
                SetCursorPos(keep.x, keep.y);
        }
        return 0;
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        ApplyHoverFade(false);
        // Leaving the PiP window always releases virtual-display mouse capture.
        if (m_requireDoubleClick && m_inputEngaged)
            SetInputEngaged(false);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (m_requireDoubleClick && !m_inputEngaged && IsPointInImage(lParam))
        {
            SetInputEngaged(true);
            SetFocus(window);
            return 0;
        }
        if (ShouldInjectInput() && IsPointInImage(lParam))
            m_input.InjectMouse(WM_LBUTTONDOWN, wParam, lParam);
        return 0;
    case WM_LBUTTONDOWN:
        if (m_requireDoubleClick && !m_inputEngaged && IsPointInImage(lParam))
        {
            // Manual double-click fallback if class style lacked CS_DBLCLKS (already-registered class).
            const DWORD now = GetMessageTime();
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int dx = GetSystemMetrics(SM_CXDOUBLECLK);
            const int dy = GetSystemMetrics(SM_CYDOUBLECLK);
            if (m_lastClickTime != 0 &&
                (now - m_lastClickTime) <= static_cast<DWORD>(GetDoubleClickTime()) &&
                abs(pt.x - m_lastClickPos.x) <= dx &&
                abs(pt.y - m_lastClickPos.y) <= dy)
            {
                SetInputEngaged(true);
                SetFocus(window);
                m_lastClickTime = 0;
            }
            else
            {
                m_lastClickTime = now;
                m_lastClickPos = pt;
            }
            return 0;
        }
        ReleaseInputIfLeavingPip(lParam);
        if (ShouldInjectInput() && IsPointInImage(lParam))
            m_input.InjectMouse(message, wParam, lParam);
        return 0;
    case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSEWHEEL:
        ReleaseInputIfLeavingPip(lParam);
        if (ShouldInjectInput() && IsPointInImage(lParam))
            m_input.InjectMouse(message, wParam, lParam);
        return 0;
    case WM_KEYDOWN:
        if (m_requireDoubleClick && m_inputEngaged && wParam == VK_ESCAPE)
        {
            SetInputEngaged(false);
            return 0;
        }
        if (ShouldInjectInput()) m_input.InjectKey(message, wParam, lParam);
        return 0;
    case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        if (ShouldInjectInput()) m_input.InjectKey(message, wParam, lParam);
        return 0;
    case WM_KILLFOCUS:
        m_input.ReleaseAllKeys();
        if (m_requireDoubleClick && m_inputEngaged)
            SetInputEngaged(false);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kTimerRender);
        DestroyWindowThumbnail();
        DestroyRenderer();
        m_window = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
