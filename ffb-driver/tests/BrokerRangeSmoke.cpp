#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "../shared/BrokerProtocol.h"

namespace {

HRESULT Send(
    HANDLE pipe,
    dfgt::ipc::Command command,
    std::uint32_t sequence,
    std::int32_t value = 0,
    const wchar_t* path = nullptr) {
    dfgt::ipc::Message message{};
    message.command = command;
    message.sequence = sequence;
    message.value = value;
    if (path != nullptr) {
        const auto length = wcsnlen(
            path,
            dfgt::ipc::DevicePathCharacters);
        if (length >= dfgt::ipc::DevicePathCharacters) return E_INVALIDARG;
        std::copy_n(path, length + 1, message.devicePath);
    }

    DWORD written = 0;
    if (!WriteFile(
            pipe,
            &message,
            sizeof(message),
            &written,
            nullptr) ||
        written != sizeof(message)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    dfgt::ipc::Response response{};
    DWORD read = 0;
    if (!ReadFile(
            pipe,
            &response,
            sizeof(response),
            &read,
            nullptr) ||
        read != sizeof(response)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (response.magic != dfgt::ipc::Magic ||
        response.version != dfgt::ipc::Version ||
        response.size != sizeof(response) ||
        response.sequence != sequence) {
        return E_INVALIDARG;
    }
    return response.result;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4) {
        std::wcerr
            << L"usage: DfgtBrokerRangeSmoke <DFGT HID path> "
               L"[degrees] [seconds]\n";
        return 2;
    }
    const auto degrees = argc >= 3 ? _wtoi(argv[2]) : 40;
    const auto seconds = argc >= 4 ? _wtoi(argv[3]) : 10;
    if (degrees < 40 || degrees > 900 || seconds < 1 || seconds > 20) {
        return 3;
    }

    const auto pipe = CreateFileW(
        dfgt::ipc::PipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"open pipe failed: " << GetLastError() << L"\n";
        return 4;
    }

    auto result = Send(pipe, dfgt::ipc::Command::Open, 1, 0, argv[1]);
    if (SUCCEEDED(result)) {
        result = Send(pipe, dfgt::ipc::Command::SetRange, 2, degrees);
    }
    if (SUCCEEDED(result)) {
        std::wcout
            << L"{\"rangeActive\":true,\"degrees\":" << degrees
            << L",\"seconds\":" << seconds << L"}\n"
            << std::flush;
        Sleep(static_cast<DWORD>(seconds) * 1000);
    }
    const auto restore = Send(pipe, dfgt::ipc::Command::SetRange, 3, 900);
    const auto close = Send(pipe, dfgt::ipc::Command::Close, 4);
    CloseHandle(pipe);

    std::wcout
        << L"{\"result\":" << result
        << L",\"restore\":" << restore
        << L",\"close\":" << close << L"}\n";
    return FAILED(result) || FAILED(restore) || FAILED(close) ? 5 : 0;
}
