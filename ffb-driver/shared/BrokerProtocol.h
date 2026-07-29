#pragma once

#include <cstdint>

namespace dfgt::ipc {

inline constexpr wchar_t FfbPipeName[] = LR"(\\.\pipe\DfgtFfbBroker.v1)";
inline constexpr wchar_t PipeName[] = LR"(\\.\pipe\DfgtFfbBroker.v1)";
inline constexpr wchar_t ControlPipeName[] =
    LR"(\\.\pipe\DfgtFfbBroker.control.v1)";
inline constexpr std::uint32_t Magic = 0x54474644; // "DFGT" little-endian
inline constexpr std::uint16_t Version = 3;
inline constexpr std::size_t DevicePathCharacters = 512;

enum class Command : std::uint32_t {
    Open = 1,
    Constant = 2,
    StopAll = 3,
    SetRange = 4,
    Heartbeat = 5,
    Close = 6,
    Spring = 7,
    Damper = 8,
    Friction = 9,
    StopSoftwareSlot = 10,
};

struct Message {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{sizeof(Message)};
    Command command{};
    std::uint32_t sequence{};
    std::int32_t value{};
    std::int32_t auxiliary{};
    std::int32_t value3{};
    std::int32_t value4{};
    std::int32_t value5{};
    std::int32_t value6{};
    std::uint64_t renderEpoch{};
    wchar_t devicePath[DevicePathCharacters]{};
};

struct Response {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{sizeof(Response)};
    std::uint32_t sequence{};
    std::int32_t result{};
};

static_assert(sizeof(Message) <= 4096);
static_assert(sizeof(Response) == 16);

namespace control {

inline constexpr std::uint32_t Magic = 0x43474644; // "DFGC" little-endian
inline constexpr std::uint16_t Version = 1;
inline constexpr std::size_t ProfileIdCharacters = 64;

enum class Command : std::uint32_t {
    Ping = 1,
    GetStatus = 2,
    AttachDevice = 3,
    ApplyProfile = 4,
    EmergencyStop = 5,
    DetachDevice = 6,
    AcknowledgeAndRearm = 7,
};

enum class DeviceState : std::uint32_t {
    Disconnected = 0,
    Ready = 1,
    ProfileActive = 2,
    GameActive = 3,
    FailSafe = 4,
    Faulted = 5,
};

struct Profile {
    wchar_t id[ProfileIdCharacters]{};
    std::int32_t rangeDegrees{900};
    std::int32_t overallGain{10000};
    std::int32_t boundaryForce{3000};
    std::int32_t springGain{10000};
    std::int32_t damperGain{10000};
    std::int32_t frictionGain{10000};
    std::int32_t periodicGain{10000};
};

struct Request {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{sizeof(Request)};
    Command command{};
    std::uint32_t sequence{};
    Profile profile{};
    wchar_t devicePath[DevicePathCharacters]{};
};

struct Status {
    DeviceState state{DeviceState::Disconnected};
    std::uint32_t connected{};
    std::uint32_t ffbClientConnected{};
    std::uint32_t controlClientConnected{};
    std::uint32_t failSafeCount{};
    std::int32_t rangeDegrees{900};
    std::int32_t overallGain{10000};
    std::int32_t boundaryForce{3000};
    std::int32_t lastResult{};
    std::uint32_t safetyMode{};
    std::uint32_t activeSlots{};
    std::uint32_t stopConfirmed{};
    std::int32_t lastSafetyError{};
    std::uint64_t safetyGeneration{};
    wchar_t activeProfileId[ProfileIdCharacters]{};
};

struct Response {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{sizeof(Response)};
    std::uint32_t sequence{};
    std::int32_t result{};
    Status status{};
};

static_assert(sizeof(Request) <= 4096);
static_assert(sizeof(Response) <= 512);

} // namespace control

} // namespace dfgt::ipc
