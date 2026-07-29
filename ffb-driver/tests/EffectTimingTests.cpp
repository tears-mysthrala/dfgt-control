#include "../shared/EffectTiming.h"

#include <chrono>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    const dfgt::timing::Clock::time_point start{};
    bool ok = true;
    ok = ok && !dfgt::timing::Deadline(start, 1000, INFINITE);
    ok = ok && !dfgt::timing::Deadline(start, INFINITE, 1);
    const auto finite = dfgt::timing::Deadline(start, 1000, 3);
    ok = ok && finite &&
        *finite == start + 3000us;
    const auto saturated = dfgt::timing::Deadline(
        start, INFINITE - 1, INFINITE - 1);
    ok = ok && saturated &&
        *saturated > start;
    if (!ok) {
        std::cerr << "effect deadline semantics failed\n";
        return 1;
    }
    return 0;
}
