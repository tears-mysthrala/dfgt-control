#pragma once

#include <windows.h>

#include <chrono>
#include <mutex>
#include <utility>

namespace dfgt::broker {

template <typename StopOperation>
HRESULT TryEmergencyIo(
    std::timed_mutex& ioMutex,
    std::chrono::milliseconds lockBudget,
    StopOperation&& stop) noexcept {
    std::unique_lock lock(ioMutex, std::defer_lock);
    if (!lock.try_lock_for(lockBudget)) {
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    return std::forward<StopOperation>(stop)();
}

} // namespace dfgt::broker
