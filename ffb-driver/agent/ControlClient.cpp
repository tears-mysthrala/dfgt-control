#include "ControlClient.h"

namespace dfgt::agent {

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
        0,
        nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    sequence_ = 0;
    return S_OK;
}

void ControlClient::Close() noexcept {
    if (pipe_ == INVALID_HANDLE_VALUE) return;
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
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

HRESULT ControlClient::DetachDevice() noexcept {
    dfgt::ipc::control::Request request{};
    dfgt::ipc::control::Response response{};
    request.command =
        dfgt::ipc::control::Command::DetachDevice;
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
    DWORD written = 0;
    if (!WriteFile(
            pipe_,
            &request,
            sizeof(request),
            &written,
            nullptr) ||
        written != sizeof(request)) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    DWORD read = 0;
    if (!ReadFile(
            pipe_,
            &response,
            sizeof(response),
            &read,
            nullptr) ||
        read != sizeof(response)) {
        const auto result = HRESULT_FROM_WIN32(GetLastError());
        Close();
        return result;
    }
    if (response.magic != dfgt::ipc::control::Magic ||
        response.version != dfgt::ipc::control::Version ||
        response.size != sizeof(response) ||
        response.sequence != request.sequence) {
        Close();
        return E_INVALIDARG;
    }
    return response.result;
}

} // namespace dfgt::agent
