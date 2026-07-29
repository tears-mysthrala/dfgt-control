#pragma once

#include <windows.h>

#include <cstdint>
#include "../shared/BrokerProtocol.h"

namespace dfgt::broker {

inline constexpr HRESULT DFGT_E_EMERGENCY_LATCHED =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);
inline constexpr HRESULT DFGT_E_STOP_UNCONFIRMED =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x202);
inline constexpr HRESULT DFGT_E_NOT_ARMED =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x203);
inline constexpr HRESULT DFGT_E_TRANSPORT_FAULTED =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x204);

using SafetyMode = dfgt::ipc::SafetyMode;

enum class PhysicalSlot : std::uint32_t {
    None = 0,
    Software = 1u << 0,
    Spring = 1u << 1,
    DamperOrInertia = 1u << 2,
    Friction = 1u << 3,
    BoundaryOverride = 1u << 4,
};

constexpr PhysicalSlot operator|(
    PhysicalSlot left,
    PhysicalSlot right) noexcept {
    return static_cast<PhysicalSlot>(
        static_cast<std::uint32_t>(left) |
        static_cast<std::uint32_t>(right));
}

constexpr PhysicalSlot operator&(
    PhysicalSlot left,
    PhysicalSlot right) noexcept {
    return static_cast<PhysicalSlot>(
        static_cast<std::uint32_t>(left) &
        static_cast<std::uint32_t>(right));
}

class SafetyState final {
public:
    [[nodiscard]] SafetyMode Mode() const noexcept {
        return mode_;
    }

    [[nodiscard]] PhysicalSlot ActiveSlots() const noexcept {
        return activeSlots_;
    }

    [[nodiscard]] HRESULT LastSafetyError() const noexcept {
        return lastSafetyError_;
    }

    [[nodiscard]] bool StopConfirmed() const noexcept {
        return stopConfirmed_;
    }

    void BeginInitialize() noexcept {
        if (mode_ == SafetyMode::EmergencyLatched ||
            mode_ == SafetyMode::StopUnconfirmed) {
            return;
        }
        mode_ = SafetyMode::Initializing;
        activeSlots_ = PhysicalSlot::None;
        stopConfirmed_ = false;
    }

    void ConfirmNeutralReady() noexcept {
        if (mode_ != SafetyMode::Initializing &&
            mode_ != SafetyMode::MaintenanceSafe) {
            return;
        }
        mode_ = SafetyMode::ReadyDisarmed;
        activeSlots_ = PhysicalSlot::None;
        stopConfirmed_ = true;
        lastSafetyError_ = S_OK;
    }

    HRESULT Arm() noexcept {
        if (mode_ == SafetyMode::EmergencyLatched) {
            return DFGT_E_EMERGENCY_LATCHED;
        }
        if (mode_ == SafetyMode::StopUnconfirmed) {
            return DFGT_E_STOP_UNCONFIRMED;
        }
        if (mode_ != SafetyMode::ReadyDisarmed &&
            mode_ != SafetyMode::Armed) {
            return DFGT_E_NOT_ARMED;
        }
        mode_ = SafetyMode::Armed;
        return S_OK;
    }

    [[nodiscard]] HRESULT CanAcceptForce() const noexcept {
        if (mode_ == SafetyMode::EmergencyLatched) {
            return DFGT_E_EMERGENCY_LATCHED;
        }
        if (mode_ == SafetyMode::StopUnconfirmed) {
            return DFGT_E_STOP_UNCONFIRMED;
        }
        if (mode_ != SafetyMode::Armed &&
            mode_ != SafetyMode::Active) {
            return DFGT_E_NOT_ARMED;
        }
        return S_OK;
    }

    HRESULT Activate(PhysicalSlot slot) noexcept {
        const auto allowed = CanAcceptForce();
        if (FAILED(allowed) || slot == PhysicalSlot::None) {
            return FAILED(allowed) ? allowed : E_INVALIDARG;
        }
        activeSlots_ = activeSlots_ | slot;
        stopConfirmed_ = false;
        mode_ = SafetyMode::Active;
        return S_OK;
    }

    void ConfirmSlotStopped(PhysicalSlot slot) noexcept {
        activeSlots_ = static_cast<PhysicalSlot>(
            static_cast<std::uint32_t>(activeSlots_) &
            ~static_cast<std::uint32_t>(slot));
        if (activeSlots_ == PhysicalSlot::None &&
            mode_ == SafetyMode::Active) {
            mode_ = SafetyMode::Armed;
        }
    }

    HRESULT LatchEmergency() noexcept {
        const auto alreadyLatched = emergencyRequested_;
        emergencyRequested_ = true;
        mode_ = SafetyMode::EmergencyLatched;
        stopConfirmed_ = false;
        if (alreadyLatched) return S_FALSE;
        return S_OK;
    }

    void ConfirmAllStopped() noexcept {
        activeSlots_ = PhysicalSlot::None;
        stopConfirmed_ = true;
        if (emergencyRequested_) {
            mode_ = SafetyMode::EmergencyLatched;
        } else {
            mode_ = SafetyMode::ReadyDisarmed;
        }
    }

    void EnterMaintenanceSafe() noexcept {
        if (mode_ == SafetyMode::EmergencyLatched ||
            mode_ == SafetyMode::StopUnconfirmed ||
            !stopConfirmed_) {
            return;
        }
        mode_ = SafetyMode::MaintenanceSafe;
        activeSlots_ = PhysicalSlot::None;
        lastSafetyError_ = S_OK;
    }

    void MarkStopUnconfirmed(HRESULT error) noexcept {
        mode_ = SafetyMode::StopUnconfirmed;
        stopConfirmed_ = false;
        lastSafetyError_ = FAILED(error)
            ? error
            : DFGT_E_STOP_UNCONFIRMED;
    }

    HRESULT AcknowledgeAndRearm(
        bool transportHealthy,
        bool neutralConfirmed) noexcept {
        if (mode_ == SafetyMode::StopUnconfirmed ||
            !stopConfirmed_) {
            return DFGT_E_STOP_UNCONFIRMED;
        }
        if (mode_ != SafetyMode::EmergencyLatched) {
            return E_UNEXPECTED;
        }
        if (!transportHealthy || !neutralConfirmed) {
            return DFGT_E_TRANSPORT_FAULTED;
        }
        lastSafetyError_ = S_OK;
        emergencyRequested_ = false;
        mode_ = SafetyMode::Armed;
        return S_OK;
    }

    void Disconnect() noexcept {
        if (mode_ == SafetyMode::EmergencyLatched ||
            mode_ == SafetyMode::StopUnconfirmed) {
            return;
        }
        mode_ = SafetyMode::Disconnected;
        activeSlots_ = PhysicalSlot::None;
        stopConfirmed_ = false;
    }

private:
    SafetyMode mode_{SafetyMode::Disconnected};
    PhysicalSlot activeSlots_{PhysicalSlot::None};
    HRESULT lastSafetyError_{S_OK};
    bool stopConfirmed_{};
    bool emergencyRequested_{};
};

} // namespace dfgt::broker
