#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

#include <iostream>

static constexpr GUID CLSID_DfgtDirectInputFfb{
    0x806a045d, 0xe7e8, 0x491e, {0xa6, 0x07, 0x31, 0x49, 0x5a, 0x7b, 0xf4, 0xa3}};

using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(
    REFCLSID, REFIID, void**);

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto module = LoadLibraryW(argv[1]);
    if (module == nullptr) return 3;
    const auto getClassObject = reinterpret_cast<GetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    IClassFactory* factory = nullptr;
    IDirectInputEffectDriver* driver = nullptr;
    auto result = getClassObject(
        CLSID_DfgtDirectInputFfb, IID_IClassFactory,
        reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(result)) {
        result = factory->CreateInstance(
            nullptr, IID_IDirectInputEffectDriver,
            reinterpret_cast<void**>(&driver));
        factory->Release();
    }
    if (FAILED(result) || driver == nullptr) {
        FreeLibrary(module);
        return 4;
    }

    DWORD axis = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0);
    LONG direction = 1;
    DICONSTANTFORCE constant{1000};
    DIEFFECT full{};
    full.dwSize = sizeof(full);
    full.dwFlags = DIEFF_OBJECTIDS | DIEFF_CARTESIAN;
    full.dwDuration = 100000;
    full.dwGain = 10000;
    full.dwTriggerButton = DIEB_NOTRIGGER;
    full.cAxes = 1;
    full.rgdwAxes = &axis;
    full.rglDirection = &direction;
    full.cbTypeSpecificParams = sizeof(constant);
    full.lpvTypeSpecificParams = &constant;

    bool ok = true;
    const auto check = [&](bool value, const char* label) {
        if (!value) std::cerr << label << '\n';
        ok = ok && value;
    };
    DWORD first = 0;
    check(SUCCEEDED(
        driver->DownloadEffect(0, 0, &first, &full, 0)) &&
        first != 0, "full download");

    DIEFFECT gainOnly{};
    gainOnly.dwSize = sizeof(gainOnly);
    gainOnly.dwGain = 5000;
    check(SUCCEEDED(driver->DownloadEffect(
        0, 0, &first, &gainOnly, DIEP_GAIN)), "partial gain");
    gainOnly.dwGain = 10001;
    check(FAILED(driver->DownloadEffect(
        0, 0, &first, &gainOnly, DIEP_GAIN)), "invalid gain");

    DWORD validationHandle = 0;
    check(SUCCEEDED(driver->DownloadEffect(
        0, 0, &validationHandle, &full, DIEP_NODOWNLOAD)) &&
        validationHandle == 0, "nodownload");
    check(FAILED(driver->DownloadEffect(
        0, 0, &validationHandle, &full,
        DIEP_NODOWNLOAD | DIEP_START)), "nodownload start");
    check(FAILED(driver->DownloadEffect(
        0, 0, &validationHandle, &full, 0x10000000u)),
        "unknown flags");

    DIEFFECT directionOnly{};
    directionOnly.dwSize = sizeof(directionOnly);
    directionOnly.dwFlags = DIEFF_CARTESIAN;
    directionOnly.cAxes = 1;
    LONG zeroDirection = 0;
    directionOnly.rglDirection = &zeroDirection;
    check(FAILED(driver->DownloadEffect(
        0, 0, &first, &directionOnly, DIEP_DIRECTION)),
        "zero direction");

    DWORD second = 0;
    constant.lMagnitude = -1000;
    check(SUCCEEDED(
        driver->DownloadEffect(0, 0, &second, &full, 0)),
        "second download");
    driver->StartEffect(0, first, 0, INFINITE);
    driver->StartEffect(0, second, 0, INFINITE);
    driver->StartEffect(0, second, DIES_SOLO, INFINITE);
    DWORD firstStatus = DIEGES_PLAYING;
    DWORD secondStatus = 0;
    check(SUCCEEDED(
        driver->GetEffectStatus(0, first, &firstStatus)), "first status");
    check(SUCCEEDED(
        driver->GetEffectStatus(0, second, &secondStatus)), "second status");
    check((firstStatus & DIEGES_PLAYING) == 0 &&
        (secondStatus & DIEGES_PLAYING) != 0, "DIES_SOLO");
    check(driver->StartEffect(
        0, second, DIES_SOLO | 0x100, 1) ==
        DIERR_INVALIDPARAM, "invalid start mode");

    DIDEVICESTATE deviceState{};
    deviceState.dwSize = sizeof(deviceState);
    check(SUCCEEDED(driver->GetForceFeedbackState(
        0, &deviceState)), "GetForceFeedbackState");
    check(
        (deviceState.dwState &
            (DIGFFS_POWERON | DIGFFS_ACTUATORSON)) == 0,
        "disconnected provider claimed powered actuators");

    DICONDITION condition{};
    condition.lPositiveCoefficient = 1000;
    condition.lNegativeCoefficient = -1000;
    condition.dwPositiveSaturation = 10000;
    condition.dwNegativeSaturation = 10000;
    full.cbTypeSpecificParams = sizeof(condition);
    full.lpvTypeSpecificParams = &condition;
    DWORD springOne = 0;
    DWORD springTwo = 0;
    check(SUCCEEDED(driver->DownloadEffect(
        0, 7, &springOne, &full, 0)), "first spring download");
    check(SUCCEEDED(driver->DownloadEffect(
        0, 7, &springTwo, &full, 0)), "second spring download");
    driver->StartEffect(0, springOne, 0, INFINITE);
    check(driver->StartEffect(
        0, springTwo, 0, INFINITE) == DIERR_DEVICEFULL,
        "same-slot condition was not rejected deterministically");
    driver->StartEffect(0, springTwo, DIES_SOLO, INFINITE);
    DWORD springOneStatus = DIEGES_PLAYING;
    DWORD springTwoStatus = 0;
    driver->GetEffectStatus(0, springOne, &springOneStatus);
    driver->GetEffectStatus(0, springTwo, &springTwoStatus);
    check((springOneStatus & DIEGES_PLAYING) == 0 &&
        (springTwoStatus & DIEGES_PLAYING) != 0,
        "DIES_SOLO did not transfer condition-slot ownership");

    driver->Release();
    FreeLibrary(module);
    if (!ok) {
        std::cerr << "DirectInput contract regression\n";
        return 1;
    }
    return 0;
}
