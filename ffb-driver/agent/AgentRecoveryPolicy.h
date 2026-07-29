#pragma once

#include <windows.h>

#include <algorithm>

#include "../shared/BrokerProtocol.h"

namespace dfgt::agent {

inline bool RequiresReconnect(
    dfgt::ipc::control::DeviceState state) noexcept {
    return state == dfgt::ipc::control::DeviceState::Disconnected ||
        state == dfgt::ipc::control::DeviceState::Faulted;
}

inline DWORD NextBrokerBackoff(DWORD current) noexcept {
    return std::min<DWORD>(
        std::max<DWORD>(current, 500) * 2,
        30000);
}

inline bool ResumeMayAutomaticallyRearm(
    dfgt::ipc::SafetyMode mode) noexcept {
    return mode != dfgt::ipc::SafetyMode::EmergencyLatched &&
        mode != dfgt::ipc::SafetyMode::StopUnconfirmed;
}

} // namespace dfgt::agent
