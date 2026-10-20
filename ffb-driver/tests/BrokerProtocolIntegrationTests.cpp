#include "../shared/BoundedIo.h"
#include "../shared/BrokerProtocol.h"
#include "../shared/WireCodec.h"

#include <windows.h>

#include <array>
#include <iostream>
#include <string>

namespace {

HANDLE OpenPipe() {
    const auto deadline = GetTickCount64() + 3000;
    while (!WaitNamedPipeW(dfgt::ipc::PipeName, 100)) {
        if (GetTickCount64() >= deadline) {
            return INVALID_HANDLE_VALUE;
        }
        Sleep(10);
    }
    return CreateFileW(
        dfgt::ipc::PipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::wstring command =
        L"\"" + std::wstring(argv[1]) +
        L"\" --diagnostics-no-force";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION broker{};
    if (!CreateProcessW(
            nullptr, command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &broker)) {
        return 3;
    }
    CloseHandle(broker.hThread);
    bool ok = false;
    int stage = 0;
    auto pipe = OpenPipe();
    if (pipe != INVALID_HANDLE_VALUE) {
        stage = 1;
        const std::byte malformed{std::byte{0x44}};
        const auto write =
            dfgt::io::WriteExact(pipe, &malformed, 1, 100);
        CloseHandle(pipe);
        if (write.Succeeded(1)) {
            stage = 2;
            // The partial frame must be evicted within the broker's bounded
            // read window so a second client can connect.
            Sleep(750);
            pipe = OpenPipe();
            if (pipe != INVALID_HANDLE_VALUE) {
                stage = 3;
                dfgt::ipc::Message request{};
                request.command = dfgt::ipc::Command::Open;
                request.sequence = 1;
                const auto encoded =
                    dfgt::ipc::wire::Encode(request);
                const auto sent = dfgt::io::WriteExact(
                    pipe, encoded.data(),
                    static_cast<DWORD>(encoded.size()), 500);
                dfgt::ipc::wire::EncodedResponse encodedResponse{};
                const auto received = dfgt::io::ReadExact(
                    pipe, encodedResponse.data(),
                    static_cast<DWORD>(encodedResponse.size()), 500);
                dfgt::ipc::Response response{};
                ok = sent.Succeeded(
                        static_cast<DWORD>(encoded.size())) &&
                    received.Succeeded(
                        static_cast<DWORD>(encodedResponse.size())) &&
                    dfgt::ipc::wire::Decode(
                        encodedResponse, response) &&
                    response.sequence == 1 &&
                    response.sessionNonce != 0 &&
                    FAILED(response.result);
                if (!ok) {
                    std::cerr
                        << "sent=" << sent.result
                        << " received=" << received.result
                        << " decoded="
                        << dfgt::ipc::wire::Decode(
                            encodedResponse, response)
                        << " nonce=" << response.sessionNonce
                        << " result=" << response.result << '\n';
                }
                CloseHandle(pipe);
            }
        }
    }
    // No valid device path was ever sent, so this test broker cannot own or
    // activate a physical HID device.
    TerminateProcess(broker.hProcess, 0);
    WaitForSingleObject(broker.hProcess, 2000);
    CloseHandle(broker.hProcess);
    if (!ok) {
        std::cerr << "broker framing integration failed stage="
                  << stage << " win32=" << GetLastError() << '\n';
        return 1;
    }
    return 0;
}
