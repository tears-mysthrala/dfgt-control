#include "../agent/DfgtModeSwitch.h"

#include <iostream>

int main() {
    using namespace dfgt::agent;

    if (!IsDfgtCompatibilityIdentity(0x046D, 0xC294, 0x1322) ||
        IsDfgtCompatibilityIdentity(0x046D, 0xC294, 0x1222) ||
        IsDfgtCompatibilityIdentity(0x046D, 0xC294, 0x8900) ||
        IsDfgtCompatibilityIdentity(0x046D, 0xC29A, 0x1322) ||
        IsDfgtCompatibilityIdentity(0x1234, 0xC294, 0x1322)) {
        std::cerr << "compatibility identity gate failed\n";
        return 1;
    }
    constexpr std::array<std::uint8_t, 8> expectedReset{
        0x00, 0xF8, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 8> expectedSwitch{
        0x00, 0xF8, 0x09, 0x03, 0x01, 0x00, 0x00, 0x00};
    if (DfgtResetRevertMode != expectedReset ||
        DfgtCompatibilityToNative != expectedSwitch) {
        std::cerr << "mode-switch command vector changed\n";
        return 2;
    }
    return 0;
}
