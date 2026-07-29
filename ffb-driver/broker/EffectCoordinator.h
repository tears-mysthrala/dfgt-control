#pragma once

#include "SafetyState.h"

#include <cstdint>

namespace dfgt::broker {

class IEffectDevice {
public:
    virtual ~IEffectDevice() = default;
    virtual HRESULT Constant(std::int32_t, std::int32_t) noexcept = 0;
    virtual HRESULT StopSoftwareSlot() noexcept = 0;
    virtual HRESULT Spring(
        std::int32_t, std::int32_t, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t) noexcept = 0;
    virtual HRESULT Damper(
        std::int32_t, std::int32_t, std::int32_t, std::int32_t) noexcept = 0;
    virtual HRESULT Friction(
        std::int32_t, std::int32_t, std::int32_t, std::int32_t) noexcept = 0;
    virtual HRESULT StopAll() noexcept = 0;
};

class EffectCoordinator final {
public:
    EffectCoordinator(
        IEffectDevice& device,
        SafetyState& safety) noexcept
        : device_(device), safety_(safety) {}

    HRESULT SetSoftware(
        std::int32_t constant,
        std::int32_t periodic) noexcept {
        if (constant == 0 && periodic == 0) {
            return StopSoftware();
        }
        const auto allowed = safety_.CanAcceptForce();
        if (FAILED(allowed)) return allowed;
        const auto result = device_.Constant(constant, periodic);
        return CompleteWrite(result, PhysicalSlot::Software);
    }

    HRESULT StopSoftware() noexcept {
        const auto result = device_.StopSoftwareSlot();
        if (SUCCEEDED(result)) {
            safety_.ConfirmSlotStopped(PhysicalSlot::Software);
        } else {
            safety_.MarkStopUnconfirmed(result);
        }
        return result;
    }

    HRESULT SetSpring(
        std::int32_t negative,
        std::int32_t positive,
        std::int32_t center,
        std::int32_t deadBand,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        const auto allowed = safety_.CanAcceptForce();
        if (FAILED(allowed)) return allowed;
        return CompleteWrite(
            device_.Spring(
                negative, positive, center, deadBand,
                negativeSaturation, positiveSaturation),
            PhysicalSlot::Spring);
    }

    HRESULT SetDamper(
        std::int32_t negative,
        std::int32_t positive,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        const auto allowed = safety_.CanAcceptForce();
        if (FAILED(allowed)) return allowed;
        return CompleteWrite(
            device_.Damper(
                negative, positive,
                negativeSaturation, positiveSaturation),
            PhysicalSlot::DamperOrInertia);
    }

    HRESULT SetFriction(
        std::int32_t negative,
        std::int32_t positive,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        const auto allowed = safety_.CanAcceptForce();
        if (FAILED(allowed)) return allowed;
        return CompleteWrite(
            device_.Friction(
                negative, positive,
                negativeSaturation, positiveSaturation),
            PhysicalSlot::Friction);
    }

    HRESULT StopEverything() noexcept {
        const auto result = device_.StopAll();
        if (SUCCEEDED(result)) {
            safety_.ConfirmAllStopped();
        } else {
            safety_.MarkStopUnconfirmed(result);
        }
        return result;
    }

private:
    HRESULT CompleteWrite(
        HRESULT result,
        PhysicalSlot slot) noexcept {
        if (SUCCEEDED(result)) {
            safety_.Activate(slot);
        } else {
            safety_.MarkStopUnconfirmed(result);
        }
        return result;
    }

    IEffectDevice& device_;
    SafetyState& safety_;
};

} // namespace dfgt::broker
