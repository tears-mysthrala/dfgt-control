#include "../broker/EffectCoordinator.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

class FakeEffectDevice final : public dfgt::broker::IEffectDevice {
public:
    HRESULT Constant(std::int32_t, std::int32_t) noexcept override {
        operations.emplace_back("Constant");
        return nextResult;
    }
    HRESULT StopSoftwareSlot() noexcept override {
        operations.emplace_back("StopSlotOne");
        return nextResult;
    }
    HRESULT Spring(
        std::int32_t, std::int32_t, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t) noexcept override {
        operations.emplace_back("Spring");
        return nextResult;
    }
    HRESULT Damper(
        std::int32_t, std::int32_t,
        std::int32_t, std::int32_t) noexcept override {
        operations.emplace_back("Damper");
        return nextResult;
    }
    HRESULT Friction(
        std::int32_t, std::int32_t,
        std::int32_t, std::int32_t) noexcept override {
        operations.emplace_back("Friction");
        return nextResult;
    }
    HRESULT StopAll() noexcept override {
        operations.emplace_back("StopAll");
        return nextResult;
    }

    HRESULT nextResult{S_OK};
    std::vector<std::string> operations;
};

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace dfgt::broker;
    bool ok = true;
    SafetyState safety;
    safety.BeginInitialize();
    safety.ConfirmNeutralReady();
    safety.Arm();
    FakeEffectDevice device;
    EffectCoordinator effects(device, safety);

    ok &= Expect(SUCCEEDED(effects.SetSpring(-3000, -3000, 0, 0, 10000, 10000)),
                 "spring activation failed");
    ok &= Expect(SUCCEEDED(effects.SetSoftware(1200, 0)),
                 "software activation failed");
    ok &= Expect(SUCCEEDED(effects.SetSoftware(0, 0)),
                 "software zero failed");
    ok &= Expect(
        device.operations ==
            std::vector<std::string>{"Spring", "Constant", "StopSlotOne"},
        "software zero emitted the wrong physical sequence");
    ok &= Expect(
        (safety.ActiveSlots() & PhysicalSlot::Spring) ==
            PhysicalSlot::Spring,
        "software zero cleared spring");

    device.nextResult = E_FAIL;
    ok &= Expect(FAILED(effects.StopEverything()),
                 "failed StopAll reported success");
    ok &= Expect(safety.Mode() == SafetyMode::StopUnconfirmed,
                 "failed StopAll did not block force");
    const auto before = device.operations.size();
    ok &= Expect(
        effects.SetSoftware(1000, 0) == DFGT_E_STOP_UNCONFIRMED,
        "force accepted after failed StopAll");
    ok &= Expect(device.operations.size() == before,
                 "rejected force reached fake device");

    return ok ? 0 : 1;
}
