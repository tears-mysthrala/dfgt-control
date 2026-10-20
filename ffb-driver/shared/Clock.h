#pragma once

#include <chrono>

namespace dfgt {

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point Now()
        const noexcept = 0;
};

class SteadyClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point Now()
        const noexcept override {
        return std::chrono::steady_clock::now();
    }
};

} // namespace dfgt
