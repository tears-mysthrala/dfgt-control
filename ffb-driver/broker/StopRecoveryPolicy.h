#pragma once

#include <array>
#include <chrono>

namespace dfgt::broker {

inline constexpr std::array StopRecoveryBackoff{
    std::chrono::milliseconds(100),
    std::chrono::milliseconds(250),
    std::chrono::milliseconds(500)};

} // namespace dfgt::broker
