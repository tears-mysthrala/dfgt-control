#include "../broker/SafetyState.h"

#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace dfgt::broker;
    bool ok = true;

    SafetyState emergency;
    emergency.BeginInitialize();
    emergency.ConfirmNeutralReady();
    ok &= Expect(SUCCEEDED(emergency.Arm()), "initial arm failed");
    ok &= Expect(
        SUCCEEDED(emergency.Activate(PhysicalSlot::Software)),
        "software activation failed");
    ok &= Expect(
        emergency.LatchEmergency() == S_OK,
        "emergency did not newly latch");
    ok &= Expect(
        emergency.CanAcceptForce() == DFGT_E_EMERGENCY_LATCHED,
        "latched emergency accepted force");
    ok &= Expect(
        emergency.LatchEmergency() == S_FALSE,
        "repeat emergency did not report already latched");
    ok &= Expect(
        emergency.AcknowledgeAndRearm(true, true) ==
            DFGT_E_STOP_UNCONFIRMED,
        "rearm succeeded before confirmed stop");
    emergency.ConfirmAllStopped();
    ok &= Expect(
        emergency.Mode() == SafetyMode::EmergencyLatched,
        "confirmed stop cleared emergency latch");
    ok &= Expect(
        SUCCEEDED(emergency.AcknowledgeAndRearm(true, true)),
        "explicit safe rearm failed");
    ok &= Expect(
        emergency.Mode() == SafetyMode::Armed,
        "rearm did not enter armed state");

    SafetyState failedStop;
    failedStop.BeginInitialize();
    failedStop.ConfirmNeutralReady();
    failedStop.Arm();
    failedStop.Activate(PhysicalSlot::Spring);
    failedStop.LatchEmergency();
    failedStop.MarkStopUnconfirmed(E_FAIL);
    ok &= Expect(
        failedStop.Mode() == SafetyMode::StopUnconfirmed,
        "failed stop did not enter StopUnconfirmed");
    ok &= Expect(
        failedStop.CanAcceptForce() == DFGT_E_STOP_UNCONFIRMED,
        "StopUnconfirmed accepted force");
    ok &= Expect(
        failedStop.LastSafetyError() == E_FAIL,
        "last safety error was lost");

    SafetyState slots;
    slots.BeginInitialize();
    slots.ConfirmNeutralReady();
    slots.Arm();
    slots.Activate(PhysicalSlot::Spring);
    slots.Activate(PhysicalSlot::Software);
    slots.ConfirmSlotStopped(PhysicalSlot::Software);
    ok &= Expect(
        (slots.ActiveSlots() & PhysicalSlot::Spring) ==
            PhysicalSlot::Spring,
        "software stop cleared spring");
    ok &= Expect(
        (slots.ActiveSlots() & PhysicalSlot::Software) ==
            PhysicalSlot::None,
        "software slot remained active");
    ok &= Expect(
        slots.Mode() == SafetyMode::Active,
        "remaining spring did not keep active state");

    return ok ? 0 : 1;
}
