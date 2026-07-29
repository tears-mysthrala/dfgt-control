#include "../broker/LeaseWatchdog.h"

#include <chrono>
#include <iostream>

namespace {
bool Expect(bool value, const char* message) {
    if (!value) std::cerr << message << '\n';
    return value;
}
}

int main() {
    using namespace std::chrono_literals;
    using dfgt::broker::LeaseWatchdog;
    bool ok = true;
    const LeaseWatchdog::TimePoint start{};
    LeaseWatchdog watchdog(350ms);
    watchdog.Reset(start);
    watchdog.ObserveForceCommand(start);

    ok &= Expect(
        watchdog.ObserveRenderer(1, start + 100ms),
        "first renderer epoch was not progress");
    ok &= Expect(
        !watchdog.ObserveRenderer(1, start + 300ms),
        "auxiliary heartbeat renewed a frozen epoch");
    ok &= Expect(
        watchdog.Expired(true, start + 451ms),
        "frozen renderer did not expire with active slots");
    ok &= Expect(
        !watchdog.Expired(false, start + 451ms),
        "inactive slots triggered a lease stop");
    ok &= Expect(
        watchdog.ObserveRenderer(2, start + 451ms),
        "advanced renderer epoch was rejected");
    ok &= Expect(
        !watchdog.Expired(true, start + 700ms),
        "fresh renderer progress did not renew lease");
    return ok ? 0 : 1;
}
