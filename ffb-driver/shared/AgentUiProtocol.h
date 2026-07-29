#pragma once

#include <cstdint>

namespace dfgt::agentui {

inline constexpr wchar_t PipeName[] =
    LR"(\\.\pipe\DfgtControl.Agent.v1)";
inline constexpr std::uint32_t Magic = 0x55474644; // "DFGU"
inline constexpr std::uint16_t Version = 1;
inline constexpr std::uint16_t RequestSize = 16;
inline constexpr std::uint16_t ResponseSize = 184;

enum class Command : std::uint32_t {
    GetStatus = 1,
    EmergencyStop = 2,
    AcknowledgeAndRearm = 3,
};

struct Request final {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{RequestSize};
    Command command{Command::GetStatus};
    std::uint32_t sequence{};
};

struct Response final {
    std::uint32_t magic{Magic};
    std::uint16_t version{Version};
    std::uint16_t size{ResponseSize};
    std::uint32_t sequence{};
    std::int32_t result{};
    std::uint32_t deviceState{};
    std::uint32_t connected{};
    std::uint32_t ffbClientConnected{};
    std::uint32_t safetyMode{};
    std::uint32_t activeSlots{};
    std::uint32_t stopConfirmed{};
    std::int32_t lastSafetyError{};
    std::uint64_t safetyGeneration{};
    wchar_t activeProfileId[64]{};
};

static_assert(sizeof(Request) == RequestSize);
static_assert(sizeof(Response) == ResponseSize);

} // namespace dfgt::agentui
