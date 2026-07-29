#pragma once

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <span>

namespace dfgt::broker {

enum class TransportFailure : std::uint32_t {
    None = 0,
    Timeout,
    ShortWrite,
    Disconnected,
    Blocked,
    SystemError,
};

struct TransportResult {
    HRESULT result{S_OK};
    TransportFailure failure{TransportFailure::None};
    std::uint32_t bytesTransferred{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return SUCCEEDED(result) &&
            failure == TransportFailure::None;
    }
};

class IHidTransport {
public:
    virtual ~IHidTransport() = default;

    virtual TransportResult Write(
        std::span<const std::uint8_t> report,
        std::chrono::steady_clock::time_point deadline) noexcept = 0;

    virtual TransportResult ReopenNeutral(
        std::chrono::steady_clock::time_point deadline) noexcept = 0;

    virtual void Cancel() noexcept = 0;
    virtual void Close() noexcept = 0;
    [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
};

} // namespace dfgt::broker
