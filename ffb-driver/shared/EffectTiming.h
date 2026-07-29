#pragma once

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace dfgt::timing {

using Clock = std::chrono::steady_clock;

inline std::optional<Clock::time_point> Deadline(
    Clock::time_point startsAt,
    DWORD durationUs,
    DWORD iterations) noexcept {
    if (durationUs == INFINITE || iterations == INFINITE) {
        return std::nullopt;
    }
    const auto duration = static_cast<std::uint64_t>(durationUs);
    const auto count = static_cast<std::uint64_t>(iterations);
    const auto maximum =
        static_cast<std::uint64_t>(
            std::chrono::microseconds::max().count());
    const auto total =
        count != 0 && duration > maximum / count
        ? maximum
        : duration * count;
    const auto remaining = std::chrono::duration_cast<
        std::chrono::microseconds>(Clock::time_point::max() - startsAt);
    return total >= static_cast<std::uint64_t>(remaining.count())
        ? Clock::time_point::max()
        : startsAt + std::chrono::microseconds(total);
}

} // namespace dfgt::timing
