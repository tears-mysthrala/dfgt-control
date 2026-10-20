#pragma once

#include <windows.h>

namespace dfgt::broker {

class ClientProcessWatch final {
public:
    ClientProcessWatch() = default;
    explicit ClientProcessWatch(DWORD processId) noexcept {
        if (processId != 0) {
            handle_ = OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                processId);
        }
    }
    ~ClientProcessWatch() {
        if (handle_ != nullptr) CloseHandle(handle_);
    }
    ClientProcessWatch(const ClientProcessWatch&) = delete;
    ClientProcessWatch& operator=(const ClientProcessWatch&) = delete;

    [[nodiscard]] bool Valid() const noexcept {
        return handle_ != nullptr;
    }
    [[nodiscard]] bool Terminated() const noexcept {
        return handle_ != nullptr &&
            WaitForSingleObject(handle_, 0) == WAIT_OBJECT_0;
    }

private:
    HANDLE handle_{nullptr};
};

} // namespace dfgt::broker
