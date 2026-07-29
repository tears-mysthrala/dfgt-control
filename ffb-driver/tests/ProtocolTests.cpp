#include <climits>
#include <cstdlib>
#include <iostream>

#include "../shared/DfgtReports.h"

namespace {

void Expect(
    const char* name,
    const dfgt::reports::OutputReport& actual,
    const dfgt::reports::OutputReport& expected) {
    if (actual == expected) return;
    std::cerr << name << " report mismatch\n";
    std::exit(1);
}

} // namespace

int main() {
    using dfgt::reports::OutputReport;

    Expect(
        "stop-all",
        dfgt::reports::StopAll(),
        OutputReport{0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "disable-auto-center",
        dfgt::reports::DisableAutoCenter(),
        OutputReport{0x00, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "stop-slot-one",
        dfgt::reports::StopSlotOne(),
        OutputReport{0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "stop-slot-two",
        dfgt::reports::StopSlotTwo(),
        OutputReport{0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "stop-slot-three",
        dfgt::reports::StopSlotThree(),
        OutputReport{0x00, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "stop-slot-four",
        dfgt::reports::StopSlotFour(),
        OutputReport{0x00, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "constant-negative-full",
        dfgt::reports::Constant(-10000),
        OutputReport{0x00, 0x11, 0x08, 0x01, 0x80, 0x00, 0x00, 0x00});
    Expect(
        "constant-positive-full",
        dfgt::reports::Constant(10000),
        OutputReport{0x00, 0x11, 0x08, 0xFF, 0x80, 0x00, 0x00, 0x00});
    Expect(
        "range-40",
        dfgt::reports::Range(40),
        OutputReport{0x00, 0xF8, 0x81, 0x28, 0x00, 0x00, 0x00, 0x00});
    Expect(
        "range-900",
        dfgt::reports::Range(900),
        OutputReport{0x00, 0xF8, 0x81, 0x84, 0x03, 0x00, 0x00, 0x00});
    Expect(
        "spring-below-first-step",
        dfgt::reports::Spring(-700, -700, 0, 0, 10000, 10000),
        OutputReport{0x00, 0x21, 0x0B, 0x7F, 0x7F, 0x00, 0xFF, 0xFF});
    Expect(
        "spring-first-low-step",
        dfgt::reports::Spring(-1500, -1500, 0, 0, 10000, 10000),
        OutputReport{0x00, 0x21, 0x0B, 0x7F, 0x7F, 0x11, 0xFF, 0xFF});
    Expect(
        "damper-low",
        dfgt::reports::Damper(-700, -700, 10000, 10000),
        OutputReport{0x00, 0x41, 0x0C, 0x01, 0x01, 0x01, 0x01, 0xFF});
    Expect(
        "friction-low",
        dfgt::reports::Friction(-700, -700, 10000, 10000),
        OutputReport{0x00, 0x81, 0x0E, 0x11, 0x11, 0xFF, 0x11, 0x00});
    Expect(
        "malicious-int-min-is-clamped",
        dfgt::reports::Damper(INT_MIN, INT_MIN, INT_MAX, INT_MAX),
        OutputReport{0x00, 0x41, 0x0C, 0x0F, 0x01, 0x0F, 0x01, 0xFF});

    std::cout << "{\"protocolVectors\":15,\"passed\":true}\n";
    return 0;
}
