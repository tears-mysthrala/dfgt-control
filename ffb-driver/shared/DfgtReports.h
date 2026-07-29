#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace dfgt::reports {

using OutputReport = std::array<std::uint8_t, 8>;

inline constexpr OutputReport StopAll() noexcept {
    return {0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline constexpr OutputReport DisableAutoCenter() noexcept {
    return {0x00, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline constexpr OutputReport StopSlotOne() noexcept {
    return {0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline constexpr OutputReport StopSlotTwo() noexcept {
    return {0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline constexpr OutputReport StopSlotThree() noexcept {
    return {0x00, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline constexpr OutputReport StopSlotFour() noexcept {
    return {0x00, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

inline OutputReport Constant(std::int32_t magnitude) noexcept {
    magnitude = std::clamp(magnitude, -10000, 10000);
    const auto scaled = static_cast<std::int32_t>(
        std::lround(magnitude / 10000.0 * 127.0));
    const auto translated = static_cast<std::uint8_t>(
        std::clamp(0x80 + scaled, 0x01, 0xFF));
    return {0x00, 0x11, 0x08, translated, 0x80, 0x00, 0x00, 0x00};
}

inline OutputReport Range(std::int32_t degrees) noexcept {
    return {
        0x00,
        0xF8,
        0x81,
        static_cast<std::uint8_t>(degrees & 0xFF),
        static_cast<std::uint8_t>((degrees >> 8) & 0xFF),
        0x00,
        0x00,
        0x00};
}

inline std::uint16_t ScalePosition(std::int32_t value) noexcept {
    const auto bounded = std::clamp(value, -10000, 10000);
    const auto translated = static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(bounded) + 10000) * 65535 / 20000);
    return static_cast<std::uint16_t>(translated >> 5);
}

inline std::uint32_t ScaleSigned16Magnitude(std::int32_t coefficient) noexcept {
    const auto bounded = std::clamp<std::int64_t>(
        coefficient,
        -10000,
        10000);
    const auto magnitude = bounded < 0 ? -bounded : bounded;
    return static_cast<std::uint32_t>(magnitude * 32767 / 10000);
}

inline std::uint8_t ScaleRawCoefficient(
    std::uint32_t rawMagnitude,
    unsigned bits) noexcept {
    const auto doubled = std::min<std::uint32_t>(rawMagnitude * 2, 65535);
    return static_cast<std::uint8_t>(doubled >> (16 - bits));
}

inline std::uint8_t ScaleCoefficient(
    std::int32_t coefficient,
    unsigned bits) noexcept {
    return ScaleRawCoefficient(ScaleSigned16Magnitude(coefficient), bits);
}

inline std::uint8_t ScaleSaturation(std::int32_t saturation) noexcept {
    const auto bounded = std::clamp(saturation, 0, 10000);
    return static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(bounded * 65535LL / 10000) >> 8);
}

inline OutputReport Spring(
    std::int32_t negativeCoefficient,
    std::int32_t positiveCoefficient,
    std::int32_t center,
    std::int32_t deadBand,
    std::int32_t negativeSaturation,
    std::int32_t positiveSaturation) noexcept {
    auto d1 = ScalePosition(center - deadBand / 2);
    auto d2 = ScalePosition(center + deadBand / 2);
    auto rawK1 = ScaleSigned16Magnitude(negativeCoefficient);
    auto rawK2 = ScaleSigned16Magnitude(positiveCoefficient);
    if (rawK1 < 2048) d1 = 0;
    else rawK1 -= 2048;
    if (rawK2 < 2048) d2 = 2047;
    else rawK2 -= 2048;
    const auto k1 = ScaleRawCoefficient(rawK1, 4);
    const auto k2 = ScaleRawCoefficient(rawK2, 4);
    const auto s1 = negativeCoefficient < 0 ? 1 : 0;
    const auto s2 = positiveCoefficient < 0 ? 1 : 0;
    const auto clip = ScaleSaturation(
        std::max(negativeSaturation, positiveSaturation));
    return {
        0x00,
        0x21,
        0x0B,
        static_cast<std::uint8_t>(d1 >> 3),
        static_cast<std::uint8_t>(d2 >> 3),
        static_cast<std::uint8_t>((k2 << 4) | k1),
        static_cast<std::uint8_t>(
            ((d2 & 7) << 5) | ((d1 & 7) << 1) | (s2 << 4) | s1),
        clip};
}

inline OutputReport Damper(
    std::int32_t negativeCoefficient,
    std::int32_t positiveCoefficient,
    std::int32_t negativeSaturation,
    std::int32_t positiveSaturation) noexcept {
    return {
        0x00,
        0x41,
        0x0C,
        ScaleCoefficient(negativeCoefficient, 4),
        static_cast<std::uint8_t>(negativeCoefficient < 0 ? 1 : 0),
        ScaleCoefficient(positiveCoefficient, 4),
        static_cast<std::uint8_t>(positiveCoefficient < 0 ? 1 : 0),
        ScaleSaturation(std::max(negativeSaturation, positiveSaturation))};
}

inline OutputReport Friction(
    std::int32_t negativeCoefficient,
    std::int32_t positiveCoefficient,
    std::int32_t negativeSaturation,
    std::int32_t positiveSaturation) noexcept {
    return {
        0x00,
        0x81,
        0x0E,
        ScaleCoefficient(negativeCoefficient, 8),
        ScaleCoefficient(positiveCoefficient, 8),
        ScaleSaturation(std::max(negativeSaturation, positiveSaturation)),
        static_cast<std::uint8_t>(
            (positiveCoefficient < 0 ? 0x10 : 0) |
            (negativeCoefficient < 0 ? 0x01 : 0)),
        0x00};
}

} // namespace dfgt::reports
