#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "../shared/BrokerProtocol.h"
#include "../shared/BoundedIo.h"
#include "../shared/WireCodec.h"

namespace {

HRESULT Send(
    HANDLE pipe,
    dfgt::ipc::Command command,
    std::uint32_t sequence,
    std::int32_t value = 0,
    const wchar_t* path = nullptr,
    std::uint64_t* nonce = nullptr) {
    dfgt::ipc::Message message{};
    message.command = command;
    message.sequence = sequence;
    message.value = value;
    message.sessionNonce = nonce == nullptr ? 0 : *nonce;
    if (path != nullptr) {
        const auto length = wcsnlen(
            path,
            dfgt::ipc::DevicePathCharacters);
        if (length >= dfgt::ipc::DevicePathCharacters) return E_INVALIDARG;
        std::copy_n(path, length + 1, message.devicePath);
    }

    const auto encoded = dfgt::ipc::wire::Encode(message);
    const auto write = dfgt::io::WriteExact(
        pipe, encoded.data(),
        static_cast<DWORD>(encoded.size()), 500);
    if (!write.Succeeded(
            static_cast<DWORD>(encoded.size()))) return write.result;
    dfgt::ipc::Response response{};
    dfgt::ipc::wire::EncodedResponse encodedResponse{};
    const auto read = dfgt::io::ReadExact(
        pipe, encodedResponse.data(),
        static_cast<DWORD>(encodedResponse.size()), 500);
    if (!read.Succeeded(
            static_cast<DWORD>(encodedResponse.size()))) return read.result;
    if (!dfgt::ipc::wire::Decode(
            encodedResponse, response) ||
        response.sequence != sequence) {
        return E_INVALIDARG;
    }
    if (nonce != nullptr) {
        if (*nonce == 0) *nonce = response.sessionNonce;
        else if (*nonce != response.sessionNonce) return E_ACCESSDENIED;
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
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"open pipe failed: " << GetLastError() << L"\n";
        return 4;
    }

    std::uint64_t nonce = 0;
    auto result = Send(
        pipe, dfgt::ipc::Command::Open, 1, 0, argv[1], &nonce);
    if (SUCCEEDED(result)) {
        result = Send(
            pipe, dfgt::ipc::Command::SetRange,
            2, degrees, nullptr, &nonce);
    }
    if (SUCCEEDED(result)) {
        std::wcout
            << L"{\"rangeActive\":true,\"degrees\":" << degrees
            << L",\"seconds\":" << seconds << L"}\n"
            << std::flush;
        Sleep(static_cast<DWORD>(seconds) * 1000);
    }
    const auto restore = Send(
        pipe, dfgt::ipc::Command::SetRange,
        3, 900, nullptr, &nonce);
    const auto close = Send(
        pipe, dfgt::ipc::Command::Close,
        4, 0, nullptr, &nonce);
    CloseHandle(pipe);

    std::wcout
        << L"{\"result\":" << result
        << L",\"restore\":" << restore
        << L",\"close\":" << close << L"}\n";
    return FAILED(result) || FAILED(restore) || FAILED(close) ? 5 : 0;
}
