#include <windows.h>

#include <cstdint>
#include <iostream>

#include "../shared/BrokerProtocol.h"
#include "../shared/BoundedIo.h"
#include "../shared/WireCodec.h"

namespace {

HRESULT Exchange(
    HANDLE pipe,
    dfgt::ipc::control::Request& request,
    dfgt::ipc::control::Response& response,
    std::uint64_t& nonce) {
    request.sessionNonce = nonce;
    const auto encoded =
        dfgt::ipc::control::wire::Encode(request);
    const auto write = dfgt::io::WriteExact(
        pipe, encoded.data(),
        static_cast<DWORD>(encoded.size()), 500);
    if (!write.Succeeded(
            static_cast<DWORD>(encoded.size()))) {
        return write.result;
    }
    dfgt::ipc::control::wire::EncodedResponse encodedResponse{};
    const auto read = dfgt::io::ReadExact(
        pipe, encodedResponse.data(),
        static_cast<DWORD>(encodedResponse.size()), 500);
    if (!read.Succeeded(
            static_cast<DWORD>(encodedResponse.size()))) {
        return read.result;
    }
    if (!dfgt::ipc::control::wire::Decode(
            encodedResponse, response) ||
        response.sequence != request.sequence) {
        return E_INVALIDARG;
    }
    if (nonce == 0) nonce = response.sessionNonce;
    else if (nonce != response.sessionNonce) return E_ACCESSDENIED;
    return response.result;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) {
        std::wcerr
            << L"usage: DfgtControlSmoke <DFGT HID path> [hold-ms]\n";
        return 2;
    }
    const auto holdMs = argc == 3 ? _wtoi(argv[2]) : 0;
    if (holdMs < 0 || holdMs > 10000) return 3;

    if (!WaitNamedPipeW(dfgt::ipc::ControlPipeName, 2000)) return 4;
    const auto pipe = CreateFileW(
        dfgt::ipc::ControlPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 5;
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        CloseHandle(pipe);
        return 6;
    }

    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    std::uint64_t nonce = 0;
    request.command = dfgt::ipc::control::Command::Ping;
    request.sequence = 1;
    if (FAILED(Exchange(pipe, request, response, nonce))) {
        CloseHandle(pipe);
        return 7;
    }
    request = {};
    request.command = dfgt::ipc::control::Command::AttachDevice;
    request.sequence = 2;
    if (wcsncpy_s(
            request.devicePath,
            argv[1],
            _TRUNCATE) != 0 ||
        FAILED(Exchange(pipe, request, response, nonce))) {
        CloseHandle(pipe);
        return 7;
    }

    request = {};
    request.command = dfgt::ipc::control::Command::ApplyProfile;
    request.sequence = 3;
    wcsncpy_s(request.profile.id, L"smoke", _TRUNCATE);
    request.profile.rangeDegrees = 900;
    request.profile.overallGain = 9000;
    request.profile.boundaryForce = 3000;
    request.profile.springGain = 8000;
    request.profile.damperGain = 7000;
    request.profile.frictionGain = 6000;
    request.profile.periodicGain = 9000;
    if (FAILED(Exchange(pipe, request, response, nonce))) {
        CloseHandle(pipe);
        return 8;
    }

    if (holdMs != 0) Sleep(static_cast<DWORD>(holdMs));
    request = {};
    request.command = dfgt::ipc::control::Command::GetStatus;
    request.sequence = 4;
    const auto result = Exchange(
        pipe, request, response, nonce);
    CloseHandle(pipe);
    if (FAILED(result)) return 9;

    std::wcout
        << L"{\"connected\":" << response.status.connected
        << L",\"controlClient\":"
        << response.status.controlClientConnected
        << L",\"ffbClient\":"
        << response.status.ffbClientConnected
        << L",\"range\":" << response.status.rangeDegrees
        << L",\"overallGain\":" << response.status.overallGain
        << L",\"profile\":\""
        << response.status.activeProfileId << L"\"}\n";
    return response.status.connected == 1 &&
            response.status.controlClientConnected == 1 &&
            response.status.rangeDegrees == 900 &&
            response.status.overallGain == 9000
        ? 0
        : 10;
}
