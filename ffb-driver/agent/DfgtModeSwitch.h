#pragma once

#include <array>
#include <cstdint>

namespace dfgt::agent {

constexpr std::uint16_t LogitechVendorId = 0x046D;
constexpr std::uint16_t DrivingForceCompatibilityProductId = 0xC294;
constexpr std::uint16_t DrivingForceGtProductId = 0xC29A;

constexpr bool IsDfgtCompatibilityIdentity(
    std::uint16_t vendorId,
    std::uint16_t productId,
    std::uint16_t versionNumber) noexcept {
    // The DFGT reports a 0x13xx revision while using the shared C294
    // compatibility PID. Do not mode-switch other C294 wheels.
    return vendorId == LogitechVendorId &&
        productId == DrivingForceCompatibilityProductId &&
        (versionNumber & 0xFF00u) == 0x1300u;
}

// Report ID 0 followed by the seven-byte EXT_CMD9 payloads used by the
// upstream Linux hid-lg4ff driver. The second report detaches and
// re-enumerates the wheel as C29A; neither report starts a force effect.
constexpr std::array<std::uint8_t, 8> DfgtResetRevertMode{
    0x00, 0xF8, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 8> DfgtCompatibilityToNative{
    0x00, 0xF8, 0x09, 0x03, 0x01, 0x00, 0x00, 0x00};

} // namespace dfgt::agent
