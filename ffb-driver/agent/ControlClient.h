#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "../shared/BrokerProtocol.h"

namespace dfgt::agent {

class ControlClient final {
public:
    ~ControlClient();

    HRESULT Connect() noexcept;
    void Close() noexcept;
    [[nodiscard]] bool Connected() const noexcept;

    HRESULT AttachDevice(const std::wstring& path) noexcept;
    HRESULT ApplyProfile(
        const dfgt::ipc::control::Profile& profile) noexcept;
    HRESULT EmergencyStop() noexcept;
    HRESULT AcknowledgeAndRearm() noexcept;
    HRESULT DetachDevice() noexcept;
    HRESULT GetStatus(
        dfgt::ipc::control::Status& status) noexcept;

private:
    HRESULT Exchange(
        dfgt::ipc::control::Request& request,
        dfgt::ipc::control::Response& response) noexcept;

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::uint32_t sequence_{0};
    std::uint64_t sessionNonce_{0};
};

} // namespace dfgt::agent
