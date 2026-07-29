#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

#include <iostream>

static constexpr GUID CLSID_DfgtDirectInputFfb{
    0x806a045d, 0xe7e8, 0x491e, {0xa6, 0x07, 0x31, 0x49, 0x5a, 0x7b, 0xf4, 0xa3}};

using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using CanUnloadNowFn = HRESULT(STDAPICALLTYPE*)();

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4) {
        std::wcerr << L"usage: DfgtFfbComSmoke <provider.dll> [DFGT HID path] [--open-only|--validate-effects|--hold]\n";
        return 2;
    }

    const auto module = LoadLibraryW(argv[1]);
    if (module == nullptr) {
        std::wcerr << L"LoadLibraryExW failed: " << GetLastError() << L"\n";
        return 3;
    }

    const auto getClassObject = reinterpret_cast<GetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    const auto canUnloadNow = reinterpret_cast<CanUnloadNowFn>(
        GetProcAddress(module, "DllCanUnloadNow"));
    if (getClassObject == nullptr || canUnloadNow == nullptr) {
        std::wcerr << L"Required COM exports are missing\n";
        FreeLibrary(module);
        return 4;
    }

    IClassFactory* factory = nullptr;
    auto result = getClassObject(
        CLSID_DfgtDirectInputFfb,
        IID_IClassFactory,
        reinterpret_cast<void**>(&factory));
    if (FAILED(result) || factory == nullptr) {
        std::wcerr << L"DllGetClassObject failed: 0x" << std::hex << result << L"\n";
        FreeLibrary(module);
        return 5;
    }

    IDirectInputEffectDriver* driver = nullptr;
    result = factory->CreateInstance(
        nullptr,
        IID_IDirectInputEffectDriver,
        reinterpret_cast<void**>(&driver));
    factory->Release();
    if (FAILED(result) || driver == nullptr) {
        std::wcerr << L"CreateInstance failed: 0x" << std::hex << result << L"\n";
        FreeLibrary(module);
        return 6;
    }

    DIDRIVERVERSIONS versions{};
    versions.dwSize = sizeof(versions);
    result = driver->GetVersions(&versions);
    if (FAILED(result)) {
        std::wcerr << L"GetVersions failed: 0x" << std::hex << result << L"\n";
        driver->Release();
        FreeLibrary(module);
        return 7;
    }

    bool hardwarePulse = false;
    DWORD validatedEffects = 0;
    bool rejectedInvalidPeriodic = false;
    bool rejectedInvalidCustom = false;
    if (argc >= 3) {
        const bool openOnly = argc == 4 && wcscmp(argv[3], L"--open-only") == 0;
        const bool validateEffects =
            argc == 4 && wcscmp(argv[3], L"--validate-effects") == 0;
        const bool holdUntilKilled = argc == 4 && wcscmp(argv[3], L"--hold") == 0;
        DIHIDFFINITINFO init{};
        init.dwSize = sizeof(init);
        init.pwszDeviceInterface = argv[2];
        result = driver->DeviceID(0x0800, 1, TRUE, 0, &init);
        if (SUCCEEDED(result) && validateEffects) {
            DWORD axis = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0);
            LONG direction = 1;
            DIEFFECT effect{};
            effect.dwSize = sizeof(effect);
            effect.dwFlags = DIEFF_OBJECTIDS | DIEFF_CARTESIAN;
            effect.dwDuration = 250000;
            effect.dwGain = 10000;
            effect.dwTriggerButton = DIEB_NOTRIGGER;
            effect.cAxes = 1;
            effect.rgdwAxes = &axis;
            effect.rglDirection = &direction;
            DWORD handle = 0;

            DICONSTANTFORCE constant{500};
            effect.cbTypeSpecificParams = sizeof(constant);
            effect.lpvTypeSpecificParams = &constant;
            if (SUCCEEDED(driver->DownloadEffect(1, 0, &handle, &effect, DIEP_NODOWNLOAD))) {
                ++validatedEffects;
            }

            DIRAMPFORCE ramp{-500, 500};
            effect.cbTypeSpecificParams = sizeof(ramp);
            effect.lpvTypeSpecificParams = &ramp;
            if (SUCCEEDED(driver->DownloadEffect(1, 1, &handle, &effect, DIEP_NODOWNLOAD))) {
                ++validatedEffects;
            }

            DIPERIODIC periodic{500, 0, 0, 100000};
            effect.cbTypeSpecificParams = sizeof(periodic);
            effect.lpvTypeSpecificParams = &periodic;
            for (DWORD effectId = 2; effectId <= 6; ++effectId) {
                if (SUCCEEDED(driver->DownloadEffect(
                        1,
                        effectId,
                        &handle,
                        &effect,
                        DIEP_NODOWNLOAD))) {
                    ++validatedEffects;
                }
            }
            periodic.dwPeriod = 0;
            rejectedInvalidPeriodic = FAILED(driver->DownloadEffect(
                1,
                3,
                &handle,
                &effect,
                DIEP_NODOWNLOAD));

            DICONDITION condition{};
            condition.lPositiveCoefficient = 1000;
            condition.lNegativeCoefficient = -1000;
            condition.dwPositiveSaturation = 1000;
            condition.dwNegativeSaturation = 1000;
            effect.cbTypeSpecificParams = sizeof(condition);
            effect.lpvTypeSpecificParams = &condition;
            for (const DWORD effectId : {7UL, 8UL, 9UL, 10UL}) {
                if (SUCCEEDED(driver->DownloadEffect(
                        1,
                        effectId,
                        &handle,
                        &effect,
                        DIEP_NODOWNLOAD))) {
                    ++validatedEffects;
                }
            }

            LONG customSamples[]{-500, 0, 500, 0};
            DICUSTOMFORCE custom{};
            custom.cChannels = 1;
            custom.dwSamplePeriod = 10000;
            custom.cSamples = static_cast<DWORD>(std::size(customSamples));
            custom.rglForceData = customSamples;
            effect.dwSamplePeriod = custom.dwSamplePeriod;
            effect.cbTypeSpecificParams = sizeof(custom);
            effect.lpvTypeSpecificParams = &custom;
            if (SUCCEEDED(driver->DownloadEffect(
                    1,
                    0x100,
                    &handle,
                    &effect,
                    DIEP_NODOWNLOAD))) {
                ++validatedEffects;
            }
            custom.cChannels = 2;
            rejectedInvalidCustom = FAILED(driver->DownloadEffect(
                1,
                0x100,
                &handle,
                &effect,
                DIEP_NODOWNLOAD));
            driver->SendForceFeedbackCommand(1, DISFFC_STOPALL);
            driver->DeviceID(0x0800, 1, FALSE, 0, &init);
        } else if (SUCCEEDED(result) && !openOnly) {
            DICONSTANTFORCE constant{};
            constant.lMagnitude = holdUntilKilled ? 1000 : 500;
            DWORD axis = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0);
            LONG direction = 1;
            DIEFFECT effect{};
            effect.dwSize = sizeof(effect);
            effect.dwFlags = DIEFF_OBJECTIDS | DIEFF_CARTESIAN;
            effect.dwDuration = holdUntilKilled ? INFINITE : 250000;
            effect.dwGain = 10000;
            effect.cAxes = 1;
            effect.rgdwAxes = &axis;
            effect.rglDirection = &direction;
            effect.cbTypeSpecificParams = sizeof(constant);
            effect.lpvTypeSpecificParams = &constant;
            DWORD handle = 0;
            result = driver->DownloadEffect(1, 0, &handle, &effect, 0);
            // OEM driver ABI order is mode, then iterations. This is the
            // ordering used by the DirectInput runtime.
            if (SUCCEEDED(result)) result = driver->StartEffect(1, handle, 0, 1);
            if (SUCCEEDED(result)) {
                if (holdUntilKilled) {
                    std::wcout << L"{\"holdingForce\":true}\n" << std::flush;
                    Sleep(INFINITE);
                }
                Sleep(250);
                hardwarePulse = true;
            }
            driver->StopEffect(1, handle);
            driver->SendForceFeedbackCommand(1, DISFFC_STOPALL);
            if (handle != 0) driver->DestroyEffect(1, handle);
            driver->DeviceID(0x0800, 1, FALSE, 0, &init);
        } else if (SUCCEEDED(result)) {
            driver->SendForceFeedbackCommand(1, DISFFC_STOPALL);
            driver->DeviceID(0x0800, 1, FALSE, 0, &init);
        }
        if (FAILED(result)) {
            std::wcerr << L"Hardware COM cycle failed: 0x" << std::hex << result << L"\n";
            driver->Release();
            FreeLibrary(module);
            return 9;
        }
    }

    driver->Release();
    const auto unloadResult = canUnloadNow();
    std::wcout
        << L"{\"available\":true,\"comAbi\":true,\"driverVersion\":"
        << versions.dwFFDriverVersion
        << L",\"firmwareRevision\":"
        << versions.dwFirmwareRevision
        << L",\"hardwarePulse\":" << (hardwarePulse ? L"true" : L"false")
        << L",\"validatedEffects\":" << validatedEffects
        << L",\"rejectedInvalidPeriodic\":"
        << (rejectedInvalidPeriodic ? L"true" : L"false")
        << L",\"rejectedInvalidCustom\":"
        << (rejectedInvalidCustom ? L"true" : L"false")
        << L",\"canUnload\":" << (unloadResult == S_OK ? L"true" : L"false")
        << L"}\n";
    FreeLibrary(module);
    return unloadResult == S_OK ? 0 : 8;
}
