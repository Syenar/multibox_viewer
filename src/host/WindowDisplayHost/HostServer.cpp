#include "pch.h"
#include "HostServer.h"
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

HostServer::HostServer(Handler handler) : m_handler(std::move(handler)) {}
HostServer::~HostServer() { Stop(); }
bool HostServer::Start() { if (m_thread.joinable()) return true; m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!m_stop) return false; m_thread = std::thread(&HostServer::Run, this); return true; }
void HostServer::Stop() { if (!m_stop) return; SetEvent(m_stop); if (m_thread.joinable()) m_thread.join(); CloseHandle(m_stop); m_stop = nullptr; }
bool HostServer::ReadExact(HANDLE pipe, void* buffer, DWORD bytes)
{
    BYTE* at = static_cast<BYTE*>(buffer);
    while (bytes)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;

        DWORD read = 0;
        BOOL ok = ReadFile(pipe, at, bytes, &read, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            HANDLE waits[] = { m_stop, ov.hEvent };
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
            {
                CancelIoEx(pipe, &ov);
                CloseHandle(ov.hEvent);
                return false;
            }
            ok = GetOverlappedResult(pipe, &ov, &read, FALSE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || !read) return false;
        at += read;
        bytes -= read;
    }
    return true;
}

bool HostServer::WriteExact(HANDLE pipe, const void* buffer, DWORD bytes)
{
    const BYTE* at = static_cast<const BYTE*>(buffer);
    while (bytes)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;

        DWORD written = 0;
        BOOL ok = WriteFile(pipe, at, bytes, &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            HANDLE waits[] = { m_stop, ov.hEvent };
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
            {
                CancelIoEx(pipe, &ov);
                CloseHandle(ov.hEvent);
                return false;
            }
            ok = GetOverlappedResult(pipe, &ov, &written, FALSE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || !written) return false;
        at += written;
        bytes -= written;
    }
    return true;
}
void HostServer::Run()
{
    while (WaitForSingleObject(m_stop, 0) == WAIT_TIMEOUT)
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)",
                SDDL_REVISION_1,
                &descriptor,
                nullptr))
        {
            if (WaitForSingleObject(m_stop, 250) != WAIT_TIMEOUT) break;
            continue;
        }
        SECURITY_ATTRIBUTES security{
            sizeof(SECURITY_ATTRIBUTES),
            descriptor,
            FALSE
        };
        HANDLE pipe = CreateNamedPipeW(WD_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64*1024, 64*1024, 0, &security);
        LocalFree(descriptor);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (WaitForSingleObject(m_stop, 250) != WAIT_TIMEOUT) break;
            continue;
        }
        OVERLAPPED ov{}; ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        BOOL connected=ConnectNamedPipe(pipe,&ov); DWORD error=connected?ERROR_SUCCESS:GetLastError();
        if (!connected && error==ERROR_IO_PENDING) { HANDLE waits[]={m_stop,ov.hEvent}; DWORD w=WaitForMultipleObjects(2,waits,FALSE,INFINITE); if(w==WAIT_OBJECT_0){CancelIoEx(pipe,&ov);CloseHandle(ov.hEvent);CloseHandle(pipe);break;} }
        else if (!connected && error!=ERROR_PIPE_CONNECTED) { CloseHandle(ov.hEvent);CloseHandle(pipe);continue; }
        CloseHandle(ov.hEvent);
        WdHostRequestHeader header{};
        if (ReadExact(pipe,&header,sizeof(header)) && header.PayloadBytes <= 1024*1024)
        {
            std::vector<BYTE> payload(header.PayloadBytes), response;
            DWORD status = header.PayloadBytes && !ReadExact(pipe,payload.data(),header.PayloadBytes) ? ERROR_INVALID_DATA : m_handler(header.Command,payload,response);
            WdHostResponseHeader reply{header.RequestId,status,static_cast<UINT32>(response.size())};
            WriteExact(pipe,&reply,sizeof(reply)); if(!response.empty()) WriteExact(pipe,response.data(),reply.PayloadBytes);
        }
        FlushFileBuffers(pipe); DisconnectNamedPipe(pipe); CloseHandle(pipe);
    }
}
