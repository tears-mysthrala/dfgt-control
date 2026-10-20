#include "ControlClient.h"
#include "../shared/BoundedIo.h"
#include "../shared/WireCodec.h"
#include "../shared/PipePeerValidation.h"

namespace dfgt::agent {

namespace {
constexpr DWORD kControlIoTimeoutMs = 500;
}

ControlClient::~ControlClient() {
    Close();
}

HRESULT ControlClient::Connect() noexcept {
    Close();
    if (!WaitNamedPipeW(dfgt::ipc::ControlPipeName, 2000)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    pipe_ = CreateFileW(
        dfgt::ipc::ControlPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!dfgt::ipc::ValidatePipeServer(
            pipe_,
            GetModuleHandleW(nullptr),
            L"DfgtFfbBroker.exe")) {
        Close();
        return E_ACCESSDENIED;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    sequence_ = 0;
    sessionNonce_ = 0;
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command = dfgt::ipc::control::Command::Ping;
    return Exchange(request, response);
}

void ControlClient::Close() noexcept {
    if (pipe_ == INVALID_HANDLE_VALUE) return;
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
    sessionNonce_ = 0;
}

bool ControlClient::Connected() const noexcept {
    return pipe_ != INVALID_HANDLE_VALUE;
}

HRESULT ControlClient::AttachDevice(
    const std::wstring& path) noexcept {
    if (path.empty() ||
        path.size() >= dfgt::ipc::DevicePathCharacters) {
        return E_INVALIDARG;
    }
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::AttachDevice;
    if (wcsncpy_s(
            request.devicePath,
            path.c_str(),
            _TRUNCATE) != 0) {
        return E_INVALIDARG;
    }
    return Exchange(request, response);
}

HRESULT ControlClient::ApplyProfile(
    const dfgt::ipc::control::Profile& profile) noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::ApplyProfile;
    request.profile = profile;
    return Exchange(request, response);
}

HRESULT ControlClient::EmergencyStop() noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::EmergencyStop;
    return Exchange(request, response);
}

HRESULT ControlClient::AcknowledgeAndRearm() noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::AcknowledgeAndRearm;
    return Exchange(request, response);
}

HRESULT ControlClient::DetachDevice() noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::DetachDevice;
    return Exchange(request, response);
}

HRESULT ControlClient::PrepareForMaintenance() noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::PrepareForMaintenance;
    return Exchange(request, response);
}

HRESULT ControlClient::GetStatus(
    dfgt::ipc::control::Status& status) noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::GetStatus;
    const auto result = Exchange(request, response);
    if (SUCCEEDED(result)) status = response.status;
    return result;
}

HRESULT ControlClient::Exchange(
    dfgt::ipc::control::Request& request,
    dfgt::ipc::control::Response& response) noexcept {
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(ERROR_PIPE_NOT_CONNECTED);
    }
    request.sequence = ++sequence_;
    request.capabilities = dfgt::ipc::RequiredCapabilities;
    request.sessionNonce = sessionNonce_;
    request.buildId = dfgt::ipc::BuildId;
    const auto encoded =
        dfgt::ipc::control::wire::Encode(request);
    const auto write = dfgt::io::WriteExact(
        pipe_, encoded.data(),
        static_cast<DWORD>(encoded.size()),
        kControlIoTimeoutMs);
    if (!write.Succeeded(
            static_cast<DWORD>(encoded.size()))) {
        const auto result = write.result;
        Close();
        return result;
    }
    dfgt::ipc::control::wire::EncodedResponse encodedResponse{};
    const auto read = dfgt::io::ReadExact(
        pipe_, encodedResponse.data(),
        static_cast<DWORD>(encodedResponse.size()),
        kControlIoTimeoutMs);
    if (!read.Succeeded(
            static_cast<DWORD>(encodedResponse.size()))) {
        const auto result = read.result;
        Close();
        return result;
    }
    if (!dfgt::ipc::control::wire::Decode(
            encodedResponse, response) ||
        response.sequence != request.sequence) {
        Close();
        return E_INVALIDARG;
    }
    if (sessionNonce_ == 0) {
        if (response.sessionNonce == 0) {
            Close();
            return E_ACCESSDENIED;
        }
        sessionNonce_ = response.sessionNonce;
    } else if (response.sessionNonce != sessionNonce_) {
        Close();
        return E_ACCESSDENIED;
    }
    return response.result;
}

} // namespace dfgt::agent
