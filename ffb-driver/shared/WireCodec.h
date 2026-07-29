#pragma once

#include "BrokerProtocol.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace dfgt::ipc::wire {

static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(wchar_t) == 2);

inline constexpr std::uint16_t HeaderSize = 48;
inline constexpr std::size_t MessageSize =
    HeaderSize + 8 + 6 * 4 + DevicePathCharacters * 2;
inline constexpr std::size_t ResponseSize =
    HeaderSize + 5 * 4;

using EncodedMessage = std::array<std::byte, MessageSize>;
using EncodedResponse = std::array<std::byte, ResponseSize>;

namespace detail {

template <typename T, std::size_t N>
void Put(
    std::array<std::byte, N>& output,
    std::size_t& offset,
    T value) noexcept {
    using U = std::make_unsigned_t<T>;
    const auto unsignedValue = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        output[offset++] = static_cast<std::byte>(
            (unsignedValue >> (index * 8)) & 0xFFu);
    }
}

template <typename T>
bool Get(
    std::span<const std::byte> input,
    std::size_t& offset,
    T& value) noexcept {
    if (offset + sizeof(T) > input.size()) return false;
    using U = std::make_unsigned_t<T>;
    U result = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        result |= static_cast<U>(
            std::to_integer<std::uint8_t>(input[offset++]))
            << (index * 8);
    }
    value = static_cast<T>(result);
    return true;
}

template <std::size_t N>
void PutHeader(
    std::array<std::byte, N>& output,
    std::uint32_t magic,
    std::uint16_t version,
    std::uint32_t command,
    std::uint32_t sequence,
    std::uint64_t capabilities,
    std::uint64_t nonce,
    std::uint64_t buildId) noexcept {
    std::size_t offset = 0;
    Put(output, offset, magic);
    Put(output, offset, version);
    Put(output, offset, HeaderSize);
    Put(output, offset, static_cast<std::uint32_t>(N));
    Put(output, offset, static_cast<std::uint32_t>(N - HeaderSize));
    Put(output, offset, command);
    Put(output, offset, sequence);
    Put(output, offset, capabilities);
    Put(output, offset, nonce);
    Put(output, offset, buildId);
}

struct Header final {
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t headerSize{};
    std::uint32_t messageSize{};
    std::uint32_t payloadLength{};
    std::uint32_t command{};
    std::uint32_t sequence{};
    std::uint64_t capabilities{};
    std::uint64_t nonce{};
    std::uint64_t buildId{};
};

inline bool GetHeader(
    std::span<const std::byte> input,
    Header& header) noexcept {
    std::size_t offset = 0;
    return Get(input, offset, header.magic) &&
        Get(input, offset, header.version) &&
        Get(input, offset, header.headerSize) &&
        Get(input, offset, header.messageSize) &&
        Get(input, offset, header.payloadLength) &&
        Get(input, offset, header.command) &&
        Get(input, offset, header.sequence) &&
        Get(input, offset, header.capabilities) &&
        Get(input, offset, header.nonce) &&
        Get(input, offset, header.buildId);
}

inline bool ValidHeader(
    const Header& header,
    std::uint32_t magic,
    std::uint16_t version,
    std::size_t expectedSize) noexcept {
    return header.magic == magic &&
        header.version == version &&
        header.headerSize == HeaderSize &&
        header.messageSize == expectedSize &&
        header.payloadLength == expectedSize - HeaderSize &&
        (header.capabilities & RequiredCapabilities) ==
            RequiredCapabilities &&
        header.buildId == BuildId;
}

} // namespace detail

inline EncodedMessage Encode(const Message& message) noexcept {
    EncodedMessage output{};
    detail::PutHeader(
        output,
        Magic,
        Version,
        static_cast<std::uint32_t>(message.command),
        message.sequence,
        message.capabilities,
        message.sessionNonce,
        message.buildId);
    std::size_t offset = HeaderSize;
    detail::Put(output, offset, message.renderEpoch);
    detail::Put(output, offset, message.value);
    detail::Put(output, offset, message.auxiliary);
    detail::Put(output, offset, message.value3);
    detail::Put(output, offset, message.value4);
    detail::Put(output, offset, message.value5);
    detail::Put(output, offset, message.value6);
    for (const auto character : message.devicePath) {
        detail::Put(
            output, offset,
            static_cast<std::uint16_t>(character));
    }
    return output;
}

inline bool Decode(
    std::span<const std::byte> input,
    Message& message) noexcept {
    detail::Header header{};
    if (!detail::GetHeader(input, header) ||
        !detail::ValidHeader(
            header, Magic, Version, MessageSize) ||
        header.command <
            static_cast<std::uint32_t>(Command::Open) ||
        header.command >
            static_cast<std::uint32_t>(Command::StopSoftwareSlot)) {
        return false;
    }
    message = {};
    message.command = static_cast<Command>(header.command);
    message.sequence = header.sequence;
    message.capabilities = header.capabilities;
    message.sessionNonce = header.nonce;
    message.buildId = header.buildId;
    std::size_t offset = HeaderSize;
    if (!detail::Get(input, offset, message.renderEpoch) ||
        !detail::Get(input, offset, message.value) ||
        !detail::Get(input, offset, message.auxiliary) ||
        !detail::Get(input, offset, message.value3) ||
        !detail::Get(input, offset, message.value4) ||
        !detail::Get(input, offset, message.value5) ||
        !detail::Get(input, offset, message.value6)) {
        return false;
    }
    for (auto& character : message.devicePath) {
        std::uint16_t value = 0;
        if (!detail::Get(input, offset, value)) return false;
        character = static_cast<wchar_t>(value);
    }
    return offset == input.size();
}

inline EncodedResponse Encode(const Response& response) noexcept {
    EncodedResponse output{};
    detail::PutHeader(
        output,
        Magic,
        Version,
        0,
        response.sequence,
        response.capabilities,
        response.sessionNonce,
        response.buildId);
    std::size_t offset = HeaderSize;
    detail::Put(output, offset, response.result);
    detail::Put(output, offset, response.safetyMode);
    detail::Put(output, offset, response.activeSlots);
    detail::Put(output, offset, response.connected);
    detail::Put(output, offset, response.stopConfirmed);
    return output;
}

inline bool Decode(
    std::span<const std::byte> input,
    Response& response) noexcept {
    detail::Header header{};
    if (!detail::GetHeader(input, header) ||
        !detail::ValidHeader(
            header, Magic, Version, ResponseSize) ||
        header.command != 0) {
        return false;
    }
    response = {};
    response.sequence = header.sequence;
    response.capabilities = header.capabilities;
    response.sessionNonce = header.nonce;
    response.buildId = header.buildId;
    std::size_t offset = HeaderSize;
    return detail::Get(input, offset, response.result) &&
        detail::Get(input, offset, response.safetyMode) &&
        detail::Get(input, offset, response.activeSlots) &&
        detail::Get(input, offset, response.connected) &&
        detail::Get(input, offset, response.stopConfirmed) &&
        offset == input.size();
}

static_assert(MessageSize == 1104);
static_assert(ResponseSize == 68);

} // namespace dfgt::ipc::wire

namespace dfgt::ipc::control::wire {

inline constexpr std::size_t RequestSize =
    dfgt::ipc::wire::HeaderSize +
    ProfileIdCharacters * 2 + 7 * 4 +
    DevicePathCharacters * 2;
inline constexpr std::size_t ResponseSize =
    dfgt::ipc::wire::HeaderSize + 4 +
    13 * 4 + 8 + ProfileIdCharacters * 2;

using EncodedRequest = std::array<std::byte, RequestSize>;
using EncodedResponse = std::array<std::byte, ResponseSize>;

inline EncodedRequest Encode(const Request& request) noexcept {
    EncodedRequest output{};
    dfgt::ipc::wire::detail::PutHeader(
        output,
        Magic,
        Version,
        static_cast<std::uint32_t>(request.command),
        request.sequence,
        request.capabilities,
        request.sessionNonce,
        request.buildId);
    std::size_t offset = dfgt::ipc::wire::HeaderSize;
    for (const auto character : request.profile.id) {
        dfgt::ipc::wire::detail::Put(
            output, offset,
            static_cast<std::uint16_t>(character));
    }
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.rangeDegrees);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.overallGain);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.boundaryForce);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.springGain);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.damperGain);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.frictionGain);
    dfgt::ipc::wire::detail::Put(
        output, offset, request.profile.periodicGain);
    for (const auto character : request.devicePath) {
        dfgt::ipc::wire::detail::Put(
            output, offset,
            static_cast<std::uint16_t>(character));
    }
    return output;
}

inline bool Decode(
    std::span<const std::byte> input,
    Request& request) noexcept {
    dfgt::ipc::wire::detail::Header header{};
    if (!dfgt::ipc::wire::detail::GetHeader(input, header) ||
        !dfgt::ipc::wire::detail::ValidHeader(
            header, Magic, Version, RequestSize) ||
        header.command <
            static_cast<std::uint32_t>(Command::Ping) ||
        header.command >
            static_cast<std::uint32_t>(
                Command::PrepareForMaintenance)) {
        return false;
    }
    request = {};
    request.command = static_cast<Command>(header.command);
    request.sequence = header.sequence;
    request.capabilities = header.capabilities;
    request.sessionNonce = header.nonce;
    request.buildId = header.buildId;
    std::size_t offset = dfgt::ipc::wire::HeaderSize;
    for (auto& character : request.profile.id) {
        std::uint16_t value = 0;
        if (!dfgt::ipc::wire::detail::Get(
                input, offset, value)) return false;
        character = static_cast<wchar_t>(value);
    }
    if (!dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.rangeDegrees) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.overallGain) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.boundaryForce) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.springGain) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.damperGain) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.frictionGain) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, request.profile.periodicGain)) {
        return false;
    }
    for (auto& character : request.devicePath) {
        std::uint16_t value = 0;
        if (!dfgt::ipc::wire::detail::Get(
                input, offset, value)) return false;
        character = static_cast<wchar_t>(value);
    }
    return offset == input.size();
}

inline EncodedResponse Encode(const Response& response) noexcept {
    EncodedResponse output{};
    dfgt::ipc::wire::detail::PutHeader(
        output,
        Magic,
        Version,
        0,
        response.sequence,
        response.capabilities,
        response.sessionNonce,
        response.buildId);
    std::size_t offset = dfgt::ipc::wire::HeaderSize;
    dfgt::ipc::wire::detail::Put(
        output, offset, response.result);
    const auto& status = response.status;
    dfgt::ipc::wire::detail::Put(
        output, offset,
        static_cast<std::uint32_t>(status.state));
    dfgt::ipc::wire::detail::Put(output, offset, status.connected);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.ffbClientConnected);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.controlClientConnected);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.failSafeCount);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.rangeDegrees);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.overallGain);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.boundaryForce);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.lastResult);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.safetyMode);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.activeSlots);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.stopConfirmed);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.lastSafetyError);
    dfgt::ipc::wire::detail::Put(
        output, offset, status.safetyGeneration);
    for (const auto character : status.activeProfileId) {
        dfgt::ipc::wire::detail::Put(
            output, offset,
            static_cast<std::uint16_t>(character));
    }
    return output;
}

inline bool Decode(
    std::span<const std::byte> input,
    Response& response) noexcept {
    dfgt::ipc::wire::detail::Header header{};
    if (!dfgt::ipc::wire::detail::GetHeader(input, header) ||
        !dfgt::ipc::wire::detail::ValidHeader(
            header, Magic, Version, ResponseSize) ||
        header.command != 0) {
        return false;
    }
    response = {};
    response.sequence = header.sequence;
    response.capabilities = header.capabilities;
    response.sessionNonce = header.nonce;
    response.buildId = header.buildId;
    std::size_t offset = dfgt::ipc::wire::HeaderSize;
    std::uint32_t state = 0;
    auto& status = response.status;
    if (!dfgt::ipc::wire::detail::Get(
            input, offset, response.result) ||
        !dfgt::ipc::wire::detail::Get(input, offset, state) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.connected) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.ffbClientConnected) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.controlClientConnected) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.failSafeCount) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.rangeDegrees) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.overallGain) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.boundaryForce) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.lastResult) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.safetyMode) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.activeSlots) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.stopConfirmed) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.lastSafetyError) ||
        !dfgt::ipc::wire::detail::Get(
            input, offset, status.safetyGeneration)) {
        return false;
    }
    status.state = static_cast<DeviceState>(state);
    for (auto& character : status.activeProfileId) {
        std::uint16_t value = 0;
        if (!dfgt::ipc::wire::detail::Get(
                input, offset, value)) return false;
        character = static_cast<wchar_t>(value);
    }
    return offset == input.size();
}

static_assert(RequestSize == 1228);
static_assert(ResponseSize == 240);

} // namespace dfgt::ipc::control::wire
