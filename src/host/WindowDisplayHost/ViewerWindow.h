#pragma once
#include "pch.h"
#include "DriverClient.h"
#include "InputInjector.h"
#include "../../shared/WindowDisplayProtocol.h"

class ViewerWindow
{
public:
    ViewerWindow(UINT32 connector, std::wstring displayDevice, WdMode mode);
    ~ViewerWindow();
    // pictureInPicture: compact floating window on the primary monitor (default for Create Display).
    bool Create(HINSTANCE instance, bool pictureInPicture = true);
    void Show();
    void Close();
    void SetAlwaysOnTop(bool enabled);
    void SetScaleMode(UINT32 scaleMode);
    void SetMirrorSource(const std::wstring& gdiDeviceName); // empty = this virtual display
    void SetSuppressInputInject(bool suppress);
    void SetInteractionOptions(bool requireDoubleClick, bool hoverFade);
    bool HitTestImage(POINT screenPoint) const;
    bool HitTestDropTarget(POINT screenPoint) const; // whole viewer window (for drag-drop)
    bool GetOwnedMonitorRect(RECT& rect) const; // virtual display bounds for drop-target moves
    HWND Handle() const { return m_window; }
    UINT32 Connector() const { return m_connector; }
    const std::wstring& DisplayDevice() const { return m_displayDevice; }
    static bool IsViewerWindow(HWND window);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    bool CreateRenderer();
    void DestroyRenderer();
    void ResizeRenderer(UINT width, UINT height);
    void Render();
    bool OpenSharedFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool OpenPixelBufferFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool OpenDesktopDuplication();
    bool AcquireDesktopFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool AcquireGdiFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    // Fallback when DWM/Desktop Duplication can't see DX games on virtual monitors.
    bool AcquireWindowOnMonitorFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    HWND FindLargestWindowOnCaptureMonitor() const;
    bool EnsureWindowThumbnail(HWND source);
    void DestroyWindowThumbnail();
    void UpdateWindowThumbnail();
    bool UploadBgraFrame(UINT width, UINT height, const void* bgraPixels, UINT pitch,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool ReadMonitorRuntime(WdMonitorRuntime& runtime);
    bool EnsureShareDevice(const LUID& adapterLuid);
    bool OpenNamedSharedTexture(const WCHAR* name, const LUID* preferredAdapter,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& shared);
    void ResolveDisplayDevice();
    std::wstring CaptureDevice() const { return m_mirrorDevice.empty() ? m_displayDevice : m_mirrorDevice; }
    static bool IsMostlyBlackBgra(const void* bgraPixels, UINT width, UINT height, UINT pitch);
    bool BuildPlaceholderFrame(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture, bool signalLive);
    void UpdateImageRect();
    void ToggleFullscreen();
    void LayoutToolbar();
    void StyleToolbarControls();
    void PaintToolbar(HDC hdc, const RECT& client);
    void PlaceAsPiPOnPrimary();
    void RefreshMirrorSourceList();
    void ApplyMirrorSourceSelection();
    bool ShouldInjectInput() const;
    void SetInputEngaged(bool engaged);
    void ApplyHoverFade(bool hovered);
    bool IsPointInImage(LPARAM lParam) const;
    void ReleaseInputIfLeavingPip(LPARAM lParam);

    UINT32 m_connector;
    std::wstring m_displayDevice;
    std::wstring m_mirrorDevice; // empty => show owned virtual display
    WdMode m_mode;
    HWND m_window = nullptr, m_scale = nullptr, m_source = nullptr, m_fullscreenButton = nullptr, m_pin = nullptr, m_name = nullptr;
    HFONT m_uiFont = nullptr;
    HBRUSH m_toolbarBrush = nullptr;
    HBRUSH m_controlBrush = nullptr;
    bool m_fullscreen = false, m_topmost = false, m_hiddenToolbar = false;
    bool m_haveFrame = false;
    bool m_lastFrameBlack = false;
    bool m_suppressInputInject = false;
    bool m_requireDoubleClick = false;
    bool m_hoverFadeEnabled = true;
    bool m_inputEngaged = false;
    bool m_trackingMouse = false;
    bool m_hoverFaded = false;
    DWORD m_lastClickTime = 0;
    POINT m_lastClickPos{};
    HRESULT m_lastShareHr = S_OK;
    std::wstring m_capturePath;
    RECT m_restore{};
    RECT m_imageRect{};
    InputInjector m_input;
    DriverClient m_runtimeDriver;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11Device1> m_device1;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11Device> m_dupDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_dupContext;
    Microsoft::WRL::ComPtr<ID3D11Device> m_shareDevice;
    Microsoft::WRL::ComPtr<ID3D11Device1> m_shareDevice1;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_shareContext;
    LUID m_shareAdapterLuid{};
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_copyFrame;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_lastFrame;
    UINT64 m_lastFrameSerial = 0;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertices;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    std::vector<BYTE> m_gdiPixels;
    std::wstring m_lastTitle;
    std::vector<std::wstring> m_sourceDevices; // parallel to source combo (index 0 = this display / empty)
    HTHUMBNAIL m_thumbnail = nullptr;
    HWND m_thumbnailSource = nullptr;
    bool m_usingThumbnail = false;
};
