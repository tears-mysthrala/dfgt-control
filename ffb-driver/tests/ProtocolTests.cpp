#include <climits>
#include <cstdlib>
#include <iostream>

#include "../shared/DfgtReports.h"
#include "../shared/WireCodec.h"

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

    dfgt::ipc::Message message{};
    message.command = dfgt::ipc::Command::Constant;
    message.sequence = 0x11223344;
    message.sessionNonce = 0x0102030405060708ULL;
    message.renderEpoch = 0x1112131415161718ULL;
    message.value = -2;
    message.auxiliary = 0x01020304;
    message.devicePath[0] = L'A';
    const auto encoded = dfgt::ipc::wire::Encode(message);
    const std::array<std::uint8_t, 24> goldenHeader{
        0x44, 0x46, 0x47, 0x54,
        0x04, 0x00, 0x30, 0x00,
        0x50, 0x04, 0x00, 0x00,
        0x20, 0x04, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00,
        0x44, 0x33, 0x22, 0x11};
    for (std::size_t index = 0;
         index < goldenHeader.size();
         ++index) {
        if (std::to_integer<std::uint8_t>(encoded[index]) !=
            goldenHeader[index]) {
            std::cerr << "wire golden header mismatch\n";
            return 1;
        }
    }
    dfgt::ipc::Message decoded{};
    if (!dfgt::ipc::wire::Decode(encoded, decoded) ||
        decoded.command != message.command ||
        decoded.sequence != message.sequence ||
        decoded.sessionNonce != message.sessionNonce ||
        decoded.renderEpoch != message.renderEpoch ||
        decoded.value != message.value ||
        decoded.auxiliary != message.auxiliary ||
        decoded.devicePath[0] != L'A') {
        std::cerr << "wire round trip mismatch\n";
        return 1;
    }
    auto corrupt = encoded;
    corrupt[4] = std::byte{0x03};
    if (dfgt::ipc::wire::Decode(corrupt, decoded)) {
        std::cerr << "incompatible protocol version accepted\n";
        return 1;
    }
    corrupt = encoded;
    corrupt[8] = std::byte{0x51};
    if (dfgt::ipc::wire::Decode(corrupt, decoded)) {
        std::cerr << "incoherent declared size accepted\n";
        return 1;
    }

    dfgt::ipc::control::Request control{};
    control.command =
        dfgt::ipc::control::Command::ApplyProfile;
    control.sequence = 7;
    control.sessionNonce = 9;
    control.profile.rangeDegrees = 540;
    control.profile.id[0] = L's';
    const auto encodedControl =
        dfgt::ipc::control::wire::Encode(control);
    dfgt::ipc::control::Request decodedControl{};
    if (!dfgt::ipc::control::wire::Decode(
            encodedControl, decodedControl) ||
        decodedControl.command != control.command ||
        decodedControl.sequence != 7 ||
        decodedControl.sessionNonce != 9 ||
        decodedControl.profile.rangeDegrees != 540 ||
        decodedControl.profile.id[0] != L's') {
        std::cerr << "control wire round trip mismatch\n";
        return 1;
    }
    control = {};
    control.command =
        dfgt::ipc::control::Command::PrepareForMaintenance;
    control.sequence = 8;
    const auto maintenance =
        dfgt::ipc::control::wire::Encode(control);
    if (!dfgt::ipc::control::wire::Decode(
            maintenance, decodedControl) ||
        decodedControl.command !=
            dfgt::ipc::control::Command::PrepareForMaintenance) {
        std::cerr << "maintenance command rejected by wire codec\n";
        return 1;
    }

    std::cout << "{\"protocolVectors\":21,\"passed\":true}\n";
    return 0;
}
