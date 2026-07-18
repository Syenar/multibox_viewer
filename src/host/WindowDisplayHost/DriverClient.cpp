#include "pch.h"
#include "DriverClient.h"
#include "../../shared/WindowDisplayProtocol.h"

DriverClient::~DriverClient() { Close(); }

bool DriverClient::Open(DWORD timeoutMs)
{
    Close();
    m_lastError = ERROR_DEVICE_NOT_AVAILABLE;
    const auto deadline = GetTickCount64() + timeoutMs;
    do
    {
        HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_WINDOWDISPLAY, nullptr, nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE)
        {
            m_lastError = GetLastError();
        }
        else
        {
            SP_DEVICE_INTERFACE_DATA iface{ sizeof(iface) };
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVINTERFACE_WINDOWDISPLAY, 0, &iface))
            {
                m_lastError = GetLastError();
                if (m_lastError == ERROR_NO_MORE_ITEMS)
                    m_lastError = ERROR_DEVICE_NOT_AVAILABLE;
            }
            else
            {
                DWORD bytes = 0;
                SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &bytes, nullptr);
                std::vector<BYTE> buffer(bytes);
                auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
                detail->cbSize = sizeof(*detail);
                if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, bytes, nullptr, nullptr))
                {
                    m_lastError = GetLastError();
                }
                else
                {
                    m_device = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (m_device == INVALID_HANDLE_VALUE)
                        m_lastError = GetLastError();
                }
            }
            SetupDiDestroyDeviceInfoList(devices);
        }
        if (m_device != INVALID_HANDLE_VALUE) { m_lastError = ERROR_SUCCESS; return true; }
        Sleep(150);
    } while (GetTickCount64() < deadline);
    return false;
}

void DriverClient::Close()
{
    if (m_device != INVALID_HANDLE_VALUE) CloseHandle(m_device);
    m_device = INVALID_HANDLE_VALUE;
}

bool DriverClient::Ioctl(DWORD code, const void* input, DWORD inputBytes, void* output, DWORD outputBytes)
{
    DWORD returned = 0;
    if (!IsOpen() && !Open()) return false;
    const BOOL ok = DeviceIoControl(m_device, code, const_cast<void*>(input), inputBytes, output, outputBytes, &returned, nullptr);
    m_lastError = ok ? ERROR_SUCCESS : GetLastError();
    return ok != FALSE;
}

bool DriverClient::PlugIn(const WdPlugInRequest& request, WdPlugInResponse& response)
{
    return Ioctl(IOCTL_WD_PLUG_IN, &request, sizeof(request), &response, sizeof(response));
}
bool DriverClient::PlugOut(UINT32 connector) { WdPlugOutRequest r{ connector }; return Ioctl(IOCTL_WD_PLUG_OUT, &r, sizeof(r), nullptr, 0); }
bool DriverClient::UpdateMode(UINT32 connector, const WdMode& mode) { WdUpdateModeRequest r{ connector, mode }; return Ioctl(IOCTL_WD_UPDATE_MODE, &r, sizeof(r), nullptr, 0); }
bool DriverClient::GetStatus(WdAdapterStatus& status) { ZeroMemory(&status, sizeof(status)); return Ioctl(IOCTL_WD_GET_STATUS, nullptr, 0, &status, sizeof(status)); }
bool DriverClient::Restart(UINT32 connector) { return Ioctl(IOCTL_WD_RESTART_MONITOR, &connector, sizeof(connector), nullptr, 0); }
