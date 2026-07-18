#pragma once
#include "pch.h"
#include "../../shared/WindowDisplayProtocol.h"
#include "../../shared/DisplayPresets.h"

class DriverClient
{
public:
    DriverClient() = default;
    ~DriverClient();
    DriverClient(const DriverClient&) = delete;
    DriverClient& operator=(const DriverClient&) = delete;

    bool Open(DWORD timeoutMs = 5000);
    void Close();
    bool IsOpen() const { return m_device != INVALID_HANDLE_VALUE; }
    DWORD LastError() const { return m_lastError; }
    bool PlugIn(const WdPlugInRequest& request, WdPlugInResponse& response);
    bool PlugOut(UINT32 connector);
    bool UpdateMode(UINT32 connector, const WdMode& mode);
    bool GetStatus(WdAdapterStatus& status);
    bool Restart(UINT32 connector);

private:
    bool Ioctl(DWORD code, const void* input, DWORD inputBytes, void* output, DWORD outputBytes);
    HANDLE m_device = INVALID_HANDLE_VALUE;
    DWORD m_lastError = ERROR_SUCCESS;
};
