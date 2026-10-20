#include "../broker/SafetyState.h"
#include "../broker/StopRecoveryPolicy.h"

#include <chrono>
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
    using namespace std::chrono_literals;
    ok &= Expect(
        dfgt::broker::StopRecoveryBackoff ==
            std::array{100ms, 250ms, 500ms},
        "stop recovery backoff is not bounded and deterministic");

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
    failedStop.ConfirmAllStopped();
    ok &= Expect(
        failedStop.Mode() == SafetyMode::EmergencyLatched,
        "recovered stop lost emergency latch");
    ok &= Expect(
        SUCCEEDED(failedStop.AcknowledgeAndRearm(true, true)),
        "recovered stop could not be explicitly rearmed");

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

    SafetyState maintenance;
    maintenance.BeginInitialize();
    maintenance.ConfirmNeutralReady();
    maintenance.EnterMaintenanceSafe();
    ok &= Expect(
        maintenance.Mode() == SafetyMode::MaintenanceSafe &&
            maintenance.StopConfirmed(),
        "confirmed neutral state did not enter maintenance");
    ok &= Expect(
        FAILED(maintenance.CanAcceptForce()),
        "maintenance-safe state accepted force");
    maintenance.MarkStopUnconfirmed(E_FAIL);
    maintenance.EnterMaintenanceSafe();
    ok &= Expect(
        maintenance.Mode() == SafetyMode::StopUnconfirmed,
        "unconfirmed stop was hidden by maintenance state");

    return ok ? 0 : 1;
}
