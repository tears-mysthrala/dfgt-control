#include "../shared/BoundedIo.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
    const auto name =
        std::wstring(LR"(\\.\pipe\DfgtBoundedIoTest.)") +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64());
    const auto server = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 256, 256, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        std::cerr << "server pipe creation failed\n";
        return 1;
    }
    std::atomic<HANDLE> client{INVALID_HANDLE_VALUE};
    std::jthread opener([&] {
        Sleep(10);
        client = CreateFileW(
            name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
    });
    const auto connected = dfgt::io::ConnectPipe(server, 500);
    opener.join();
    if (FAILED(connected) || client == INVALID_HANDLE_VALUE) {
        std::cerr << "bounded connect failed\n";
        CloseHandle(server);
        return 1;
    }

    std::array<std::byte, 8> received{};
    const auto began = std::chrono::steady_clock::now();
    const auto timeout =
        dfgt::io::ReadExact(server, received.data(), 8, 25);
    const auto elapsed = std::chrono::steady_clock::now() - began;
    bool ok =
        timeout.result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) &&
        elapsed < std::chrono::milliseconds(250);
    if (!ok) std::cerr << "read deadline was not enforced\n";

    const std::array<std::byte, 8> sent{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    const auto write =
        dfgt::io::WriteExact(client, sent.data(), 8, 100);
    const auto read =
        dfgt::io::ReadExact(server, received.data(), 8, 100);
    ok = ok && write.Succeeded(8) && read.Succeeded(8) &&
        received == sent;
    if (received != sent) std::cerr << "bounded transfer corrupted data\n";

    CloseHandle(client);
    DisconnectNamedPipe(server);
    CloseHandle(server);
    return ok ? 0 : 1;
}
