#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace dfgt::io {

struct TransferResult final {
    HRESULT result{S_OK};
    DWORD bytesTransferred{};

    [[nodiscard]] bool Succeeded(DWORD expected) const noexcept {
        return SUCCEEDED(result) && bytesTransferred == expected;
    }
};

namespace detail {

struct PendingTransfer final {
    OVERLAPPED overlapped{};
    std::vector<std::byte> buffer;
    HANDLE event{nullptr};

    explicit PendingTransfer(DWORD size) : buffer(size) {}
    ~PendingTransfer() {
        if (event != nullptr) CloseHandle(event);
    }
};

inline TransferResult Transfer(
    HANDLE handle,
    void* callerBuffer,
    DWORD size,
    DWORD timeoutMs,
    bool write) noexcept {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return {HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), 0};
    }
    auto pending = std::make_unique<PendingTransfer>(size);
    if (write && size != 0) {
        std::memcpy(pending->buffer.data(), callerBuffer, size);
    }
    pending->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (pending->event == nullptr) {
        return {HRESULT_FROM_WIN32(GetLastError()), 0};
    }
    pending->overlapped.hEvent = pending->event;
    DWORD transferred = 0;
    const auto completed = write
        ? WriteFile(
            handle, pending->buffer.data(), size,
            &transferred, &pending->overlapped)
        : ReadFile(
            handle, pending->buffer.data(), size,
            &transferred, &pending->overlapped);
    if (!completed) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            return {HRESULT_FROM_WIN32(error), 0};
        }
        const auto wait = WaitForSingleObject(
            pending->event, timeoutMs);
        if (wait != WAIT_OBJECT_0) {
            const auto cancelError = CancelIoEx(
                handle, &pending->overlapped)
                ? ERROR_SUCCESS
                : GetLastError();
            if (WaitForSingleObject(
                    pending->event, timeoutMs) != WAIT_OBJECT_0) {
                // A broken endpoint ignored cancellation. Retain the
                // OVERLAPPED and its buffer so Windows cannot access freed
                // memory. Process teardown reclaims this quarantine.
                pending.release();
            }
            return {
                HRESULT_FROM_WIN32(
                    wait == WAIT_TIMEOUT
                        ? ERROR_TIMEOUT
                        : cancelError),
                0};
        }
        if (!GetOverlappedResult(
                handle,
                &pending->overlapped,
                &transferred,
                FALSE)) {
            return {HRESULT_FROM_WIN32(GetLastError()), 0};
        }
    }
    if (!write && transferred != 0) {
        std::memcpy(callerBuffer, pending->buffer.data(), transferred);
    }
    if (transferred != size) {
        return {
            HRESULT_FROM_WIN32(
                write ? ERROR_WRITE_FAULT : ERROR_HANDLE_EOF),
            transferred};
    }
    return {S_OK, transferred};
}

} // namespace detail

inline TransferResult WriteExact(
    HANDLE handle,
    const void* buffer,
    DWORD size,
    DWORD timeoutMs) noexcept {
    return detail::Transfer(
        handle, const_cast<void*>(buffer), size, timeoutMs, true);
}

inline TransferResult ReadExact(
    HANDLE handle,
    void* buffer,
    DWORD size,
    DWORD timeoutMs) noexcept {
    return detail::Transfer(handle, buffer, size, timeoutMs, false);
}

inline HRESULT ConnectPipe(
    HANDLE pipe,
    DWORD timeoutMs) noexcept {
    auto pending = std::make_unique<detail::PendingTransfer>(0);
    pending->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (pending->event == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    pending->overlapped.hEvent = pending->event;
    if (ConnectNamedPipe(pipe, &pending->overlapped)) return S_OK;
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return S_OK;
    if (error != ERROR_IO_PENDING) return HRESULT_FROM_WIN32(error);
    const auto wait = WaitForSingleObject(pending->event, timeoutMs);
    if (wait == WAIT_OBJECT_0) {
        DWORD transferred = 0;
        return GetOverlappedResult(
            pipe, &pending->overlapped, &transferred, FALSE)
            ? S_OK
            : HRESULT_FROM_WIN32(GetLastError());
    }
    CancelIoEx(pipe, &pending->overlapped);
    if (WaitForSingleObject(
            pending->event, timeoutMs) != WAIT_OBJECT_0) {
        pending.release();
    }
    return HRESULT_FROM_WIN32(
        wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
}

} // namespace dfgt::io
