#include "DeviceDiscovery.h"
#include "DfgtModeSwitch.h"

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace dfgt::agent {
namespace {

constexpr DWORD kModeSwitchWriteTimeoutMs = 100;

HRESULT WriteReport(
    HANDLE handle,
    const std::array<std::uint8_t, 8>& report) noexcept {
    struct PendingWrite final {
        OVERLAPPED overlapped{};
        std::array<std::uint8_t, 8> bytes{};
        HANDLE event{nullptr};
        ~PendingWrite() {
            if (event != nullptr) CloseHandle(event);
        }
    };
    auto pending = std::make_unique<PendingWrite>();
    pending->bytes = report;
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return HRESULT_FROM_WIN32(GetLastError());
    pending->event = event;
    pending->overlapped.hEvent = event;
    DWORD written = 0;
    const auto completed = WriteFile(
        handle,
        pending->bytes.data(),
        static_cast<DWORD>(pending->bytes.size()),
        &written,
        &pending->overlapped);
    if (!completed) {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) return HRESULT_FROM_WIN32(error);
        const auto wait =
            WaitForSingleObject(event, kModeSwitchWriteTimeoutMs);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(handle, &pending->overlapped);
            if (WaitForSingleObject(
                    event,
                    kModeSwitchWriteTimeoutMs) != WAIT_OBJECT_0) {
                // Preserve the OVERLAPPED and its backing buffer if the
                // inbox HID stack ever fails to complete cancellation.
                // Process teardown reclaims this quarantined allocation.
                pending.release();
            }
            return wait == WAIT_TIMEOUT
                ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                : HRESULT_FROM_WIN32(GetLastError());
        }
        if (!GetOverlappedResult(
                handle,
                &pending->overlapped,
                &written,
                FALSE)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return written == report.size()
        ? S_OK
        : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
}

HRESULT SwitchCompatibilityDevice(const wchar_t* path) noexcept {
    const auto handle = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const auto resetResult =
        WriteReport(handle, DfgtResetRevertMode);
    const auto switchResult = SUCCEEDED(resetResult)
        ? WriteReport(handle, DfgtCompatibilityToNative)
        : resetResult;
    CloseHandle(handle);
    // The successful switch report normally invalidates the handle while
    // returning success. Any later attach is performed after PnP arrival.
    return switchResult;
}

} // namespace

std::optional<std::wstring> FindDfgtDevicePath() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    const auto devices = SetupDiGetClassDevsW(
        &hidGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return std::nullopt;

    std::optional<std::wstring> result;
    for (DWORD index = 0; !result; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(
                devices,
                nullptr,
                &hidGuid,
                index,
                &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interfaceData,
            nullptr,
            0,
            &required,
            nullptr);
        if (required == 0 ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        std::vector<std::byte> storage(required);
        auto* detail =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
                storage.data());
        detail->cbSize =
            sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices,
                &interfaceData,
                detail,
                required,
                nullptr,
                nullptr)) {
            continue;
        }
        auto handle = CreateFileW(
            detail->DevicePath,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (HidD_GetAttributes(handle, &attributes)) {
            if (attributes.VendorID == LogitechVendorId &&
                attributes.ProductID == DrivingForceGtProductId) {
                result = detail->DevicePath;
            } else if (IsDfgtCompatibilityIdentity(
                           attributes.VendorID,
                           attributes.ProductID,
                           attributes.VersionNumber)) {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
                SwitchCompatibilityDevice(detail->DevicePath);
                // The device detaches during the second command. Stop
                // enumeration and let the PnP arrival retry find C29A.
                break;
            }
        }
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

} // namespace dfgt::agent
