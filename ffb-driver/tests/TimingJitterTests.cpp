#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

int wmain() {
    auto timer = CreateWaitableTimerExW(
        nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS);
    if (timer == nullptr)
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (timer == nullptr) return 2;
    LARGE_INTEGER due{};
    due.QuadPart = -20000;
    if (!SetWaitableTimer(timer, &due, 2, nullptr, nullptr, FALSE)) {
        CloseHandle(timer);
        return 3;
    }

    constexpr std::size_t samples = 1000;
    std::vector<std::int64_t> jitter;
    jitter.reserve(samples);
    std::uint32_t missed = 0;
    auto previous = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < samples; ++index) {
        if (WaitForSingleObject(timer, 100) != WAIT_OBJECT_0) {
            CancelWaitableTimer(timer);
            CloseHandle(timer);
            return 4;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - previous).count();
        previous = now;
        jitter.push_back(elapsed - 2000);
        if (elapsed >= 4000)
            missed += static_cast<std::uint32_t>(elapsed / 2000 - 1);
    }
    CancelWaitableTimer(timer);
    CloseHandle(timer);

    std::ranges::sort(jitter);
    const auto percentile = [&](double value) {
        const auto index = static_cast<std::size_t>(
            value * static_cast<double>(jitter.size() - 1));
        return jitter[index];
    };
    std::wcout
        << L"{\"samples\":" << samples
        << L",\"periodUs\":2000"
        << L",\"jitterP50Us\":" << percentile(0.50)
        << L",\"jitterP95Us\":" << percentile(0.95)
        << L",\"jitterP99Us\":" << percentile(0.99)
        << L",\"jitterMaxUs\":" << jitter.back()
        << L",\"missedTicks\":" << missed
        << L"}\n";
    return jitter.back() < 100000 ? 0 : 5;
}
