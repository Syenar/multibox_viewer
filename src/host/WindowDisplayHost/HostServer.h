#pragma once
#include "pch.h"
#include "../../shared/WindowDisplayProtocol.h"

class HostServer
{
public:
    using Handler = std::function<DWORD(UINT32 command, const std::vector<BYTE>& request, std::vector<BYTE>& response)>;
    explicit HostServer(Handler handler);
    ~HostServer();
    bool Start();
    void Stop();
private:
    void Run();
    bool ReadExact(HANDLE pipe, void* buffer, DWORD bytes);
    bool WriteExact(HANDLE pipe, const void* buffer, DWORD bytes);
    Handler m_handler;
    HANDLE m_stop = nullptr;
    std::thread m_thread;
};
