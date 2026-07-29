#include "DeviceDiscovery.h"

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <cstddef>
#include <vector>

namespace dfgt::agent {

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
        const auto handle = CreateFileW(
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
        if (HidD_GetAttributes(handle, &attributes) &&
            attributes.VendorID == 0x046D &&
            attributes.ProductID == 0xC29A) {
            result = detail->DevicePath;
        }
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

} // namespace dfgt::agent
