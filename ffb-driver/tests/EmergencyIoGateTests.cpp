#include "../broker/EmergencyIoGate.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    std::timed_mutex io;
    std::atomic<bool> holding{false};
    std::jthread blockedWriter([&] {
        std::scoped_lock lock(io);
        holding = true;
        std::this_thread::sleep_for(150ms);
    });
    while (!holding) std::this_thread::yield();

    bool stopCalled = false;
    const auto began = std::chrono::steady_clock::now();
    const auto busy = dfgt::broker::TryEmergencyIo(
        io, 5ms, [&]() noexcept {
            stopCalled = true;
            return S_OK;
        });
    const auto elapsed = std::chrono::steady_clock::now() - began;
    bool ok =
        busy == HRESULT_FROM_WIN32(ERROR_BUSY) &&
        !stopCalled &&
        elapsed < 50ms;
    if (!ok) {
        std::cerr << "emergency waited behind blocked I/O\n";
    }
    blockedWriter.join();
    const auto stopped = dfgt::broker::TryEmergencyIo(
        io, 5ms, [&]() noexcept {
            stopCalled = true;
            return S_OK;
        });
    ok = ok && SUCCEEDED(stopped) && stopCalled;
    if (FAILED(stopped)) {
        std::cerr << "emergency could not run after I/O released\n";
    }
    return ok ? 0 : 1;
}
