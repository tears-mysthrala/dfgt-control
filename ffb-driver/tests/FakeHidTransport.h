#pragma once

#include "../broker/IHidTransport.h"

#include <deque>
#include <mutex>
#include <vector>

namespace dfgt::tests {

class FakeHidTransport final : public broker::IHidTransport {
public:
    struct Operation {
        std::vector<std::uint8_t> report;
        broker::TransportResult result;
    };

    void Queue(broker::TransportResult result) {
        std::scoped_lock lock(mutex_);
        outcomes_.push_back(result);
    }

    broker::TransportResult Write(
        std::span<const std::uint8_t> report,
        std::chrono::steady_clock::time_point) noexcept override {
        std::scoped_lock lock(mutex_);
        auto result = Next(
            static_cast<std::uint32_t>(report.size()));
        operations_.push_back({
            std::vector<std::uint8_t>(report.begin(), report.end()),
            result});
        return result;
    }

    broker::TransportResult ReopenNeutral(
        std::chrono::steady_clock::time_point) noexcept override {
        std::scoped_lock lock(mutex_);
        const auto result = Next(0);
        if (result.Succeeded()) open_ = true;
        return result;
    }

    void Cancel() noexcept override {
        std::scoped_lock lock(mutex_);
        cancelled_ = true;
    }

    void Close() noexcept override {
        std::scoped_lock lock(mutex_);
        open_ = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept override {
        std::scoped_lock lock(mutex_);
        return open_;
    }

    [[nodiscard]] std::vector<Operation> Operations() const {
        std::scoped_lock lock(mutex_);
        return operations_;
    }

    [[nodiscard]] bool Cancelled() const noexcept {
        std::scoped_lock lock(mutex_);
        return cancelled_;
    }

private:
    broker::TransportResult Next(std::uint32_t expectedBytes) {
        if (outcomes_.empty()) {
            return {S_OK, broker::TransportFailure::None, expectedBytes};
        }
        const auto result = outcomes_.front();
        outcomes_.pop_front();
        return result;
    }

    mutable std::mutex mutex_;
    std::deque<broker::TransportResult> outcomes_;
    std::vector<Operation> operations_;
    bool open_{true};
    bool cancelled_{};
};

} // namespace dfgt::tests
