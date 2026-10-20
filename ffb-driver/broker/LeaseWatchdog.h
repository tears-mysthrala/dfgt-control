#pragma once

#include <chrono>
#include <cstdint>

namespace dfgt::broker {

class LeaseWatchdog final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit LeaseWatchdog(std::chrono::milliseconds window) noexcept
        : window_(window) {}

    void Reset(TimePoint now) noexcept {
        deadline_ = now + window_;
        lastRenderEpoch_ = 0;
    }

    bool ObserveRenderer(
        std::uint64_t renderEpoch,
        TimePoint now) noexcept {
        if (renderEpoch <= lastRenderEpoch_) return false;
        lastRenderEpoch_ = renderEpoch;
        deadline_ = now + window_;
        return true;
    }

    void ObserveForceCommand(TimePoint now) noexcept {
        deadline_ = now + window_;
    }

    [[nodiscard]] bool Expired(
        bool physicalSlotsActive,
        TimePoint now) const noexcept {
        return physicalSlotsActive && now >= deadline_;
    }

    [[nodiscard]] std::uint64_t LastRenderEpoch() const noexcept {
        return lastRenderEpoch_;
    }

    [[nodiscard]] TimePoint Deadline() const noexcept {
        return deadline_;
    }

private:
    std::chrono::milliseconds window_;
    TimePoint deadline_{};
    std::uint64_t lastRenderEpoch_{};
};

} // namespace dfgt::broker
