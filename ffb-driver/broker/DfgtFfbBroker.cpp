#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <sddl.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../shared/BrokerProtocol.h"
#include "../shared/DfgtReports.h"
#include "../shared/BoundedIo.h"
#include "../shared/WireCodec.h"
#include "SafetyState.h"
#include "EffectCoordinator.h"
#include "LeaseWatchdog.h"
#include "ClientProcessWatch.h"
#include "EmergencyIoGate.h"
#include "StopRecoveryPolicy.h"

namespace {

constexpr DWORD kReadTimeoutMs = 350;
constexpr DWORD kHidWriteTimeoutMs = 75;
constexpr DWORD kPipeIoTimeoutMs = 250;
constexpr std::uint32_t kRangeLockEngageLow = 256;
constexpr std::uint32_t kRangeLockReleaseLow = 1024;
constexpr std::uint32_t kRangeLockEngageHigh = 16127;
constexpr std::uint32_t kRangeLockReleaseHigh = 15359;
constexpr std::int32_t kRangeLockMagnitude = 3000;

class HidDevice final : public dfgt::broker::IEffectDevice {
public:
    ~HidDevice() { Close(); }

    HRESULT Open(const wchar_t* path) noexcept {
        Close();
        if (path == nullptr || path[0] == L'\0') return E_INVALIDARG;
        handle_ = CreateFileW(
            path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle_, &attributes)) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return result;
        }
        if (attributes.VendorID != 0x046D || attributes.ProductID != 0xC29A) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        }
        readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (readEvent_ == nullptr) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandlesOnly();
            return result;
        }
        readHandle_ = CreateFileW(
            path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (readHandle_ == INVALID_HANDLE_VALUE) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandlesOnly();
            return result;
        }
        if (!HidD_GetPreparsedData(readHandle_, &preparsedData_)) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            CloseHandlesOnly();
            return result;
        }
        HIDP_CAPS capabilities{};
        if (HidP_GetCaps(preparsedData_, &capabilities) !=
                HIDP_STATUS_SUCCESS ||
            capabilities.InputReportByteLength == 0 ||
            capabilities.InputReportByteLength > readBuffer_.size()) {
            CloseHandlesOnly();
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        inputReportBytes_ = capabilities.InputReportByteLength;
        const auto readResult = BeginRead();
        if (FAILED(readResult)) {
            CloseHandlesOnly();
            return readResult;
        }
        const auto neutralResult = StopAll();
        if (FAILED(neutralResult)) {
            CloseHandlesOnly();
            return neutralResult;
        }
        return S_OK;
    }

    HRESULT Constant(
        std::int32_t magnitude,
        std::int32_t periodicMagnitude = 0) noexcept {
        const auto bounded = std::clamp(magnitude, -10000, 10000);
        const auto boundedPeriodic =
            std::clamp(periodicMagnitude, -10000, 10000);
        baseConstant_ = static_cast<std::int32_t>(
            static_cast<std::int64_t>(bounded) * overallGain_ / 10000 +
            static_cast<std::int64_t>(boundedPeriodic) *
                periodicGain_ * overallGain_ / 10000 / 10000);
        baseConstant_ =
            std::clamp(baseConstant_, -10000, 10000);
        if (rangeLockState_ != 0) return S_OK;
        return baseConstant_ == 0
            ? Send(dfgt::reports::StopSlotOne())
            : Send(dfgt::reports::Constant(baseConstant_));
    }

    HRESULT StopSoftwareSlot() noexcept {
        baseConstant_ = 0;
        return Send(dfgt::reports::StopSlotOne());
    }

    HRESULT SetRange(std::int32_t degrees) noexcept {
        if (degrees < 40 || degrees > 900) return E_INVALIDARG;
        const auto result = Send(dfgt::reports::Range(degrees));
        if (FAILED(result)) return result;
        rangeDegrees_ = degrees;
        if (degrees == 900 && rangeLockState_ != 0) {
            rangeLockState_ = 0;
            return RestoreBaseConstant();
        }
        return S_OK;
    }

    HRESULT PollRangeLock() noexcept {
        if (readHandle_ == INVALID_HANDLE_VALUE || !readPending_) return S_OK;
        DWORD bytesRead = 0;
        if (!GetOverlappedResult(
                readHandle_,
                &readOverlapped_,
                &bytesRead,
                FALSE)) {
            const auto error = GetLastError();
            if (error == ERROR_IO_INCOMPLETE) return S_OK;
            readPending_ = false;
            return HRESULT_FROM_WIN32(error);
        }
        readPending_ = false;

        auto result = S_OK;
        if (inputReportBytes_ == 0 ||
            bytesRead == 0 ||
            bytesRead % inputReportBytes_ != 0) {
            result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        } else {
            for (DWORD offset = 0;
                 offset < bytesRead && SUCCEEDED(result);
                 offset += inputReportBytes_) {
                std::uint32_t x = 0;
                const auto status = HidP_GetUsageValue(
                    HidP_Input,
                    0x01,
                    0,
                    0x30,
                    reinterpret_cast<PULONG>(&x),
                    preparsedData_,
                    reinterpret_cast<PCHAR>(
                        readBuffer_.data() + offset),
                    inputReportBytes_);
                if (status != HIDP_STATUS_SUCCESS) {
                    result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    break;
                }
                if (rangeDegrees_ >= 900) continue;
                auto nextState = rangeLockState_;
                if (rangeLockState_ == 0) {
                    if (x <= kRangeLockEngageLow) nextState = -1;
                    else if (x >= kRangeLockEngageHigh) nextState = 1;
                } else if (
                    rangeLockState_ < 0 &&
                    x >= kRangeLockReleaseLow) {
                    nextState = 0;
                } else if (
                    rangeLockState_ > 0 &&
                    x <= kRangeLockReleaseHigh) {
                    nextState = 0;
                }
                if (nextState == rangeLockState_) continue;
                rangeLockState_ = nextState;
                result = rangeLockState_ == 0
                    ? RestoreBaseConstant()
                    : Send(dfgt::reports::Constant(
                        rangeLockState_ < 0
                            ? -boundaryForce_
                            : boundaryForce_));
            }
        }
        const auto nextRead = BeginRead();
        return FAILED(result) ? result : nextRead;
    }

    HRESULT StopAll() noexcept {
        if (handle_ == INVALID_HANDLE_VALUE) return S_OK;
        const std::array reports{
            dfgt::reports::StopAll(),
            dfgt::reports::DisableAutoCenter(),
            dfgt::reports::StopSlotOne(),
            dfgt::reports::StopSlotTwo(),
            dfgt::reports::StopSlotThree(),
            dfgt::reports::StopSlotFour()};
        HRESULT firstFailure = S_OK;
        for (const auto& report : reports) {
            const auto result = Send(report);
            if (FAILED(result) && SUCCEEDED(firstFailure)) firstFailure = result;
        }
        baseConstant_ = 0;
        rangeLockState_ = 0;
        return firstFailure;
    }

    HRESULT Spring(
        std::int32_t negativeCoefficient,
        std::int32_t positiveCoefficient,
        std::int32_t center,
        std::int32_t deadBand,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        return Send(dfgt::reports::Spring(
            ScaleGain(negativeCoefficient, springGain_),
            ScaleGain(positiveCoefficient, springGain_),
            center,
            deadBand,
            negativeSaturation,
            positiveSaturation));
    }

    HRESULT Damper(
        std::int32_t negativeCoefficient,
        std::int32_t positiveCoefficient,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        return Send(dfgt::reports::Damper(
            ScaleGain(negativeCoefficient, damperGain_),
            ScaleGain(positiveCoefficient, damperGain_),
            negativeSaturation,
            positiveSaturation));
    }

    HRESULT Friction(
        std::int32_t negativeCoefficient,
        std::int32_t positiveCoefficient,
        std::int32_t negativeSaturation,
        std::int32_t positiveSaturation) noexcept {
        return Send(dfgt::reports::Friction(
            ScaleGain(negativeCoefficient, frictionGain_),
            ScaleGain(positiveCoefficient, frictionGain_),
            negativeSaturation,
            positiveSaturation));
    }

    void Close() noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) {
            StopAll();
            Send(dfgt::reports::Range(900));
        }
        CloseHandlesOnly();
        rangeDegrees_ = 900;
        baseConstant_ = 0;
        rangeLockState_ = 0;
    }

private:
    void CloseHandlesOnly() noexcept {
        if (readHandle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(readHandle_, &readOverlapped_);
            CloseHandle(readHandle_);
            readHandle_ = INVALID_HANDLE_VALUE;
        }
        readPending_ = false;
        if (preparsedData_ != nullptr) {
            HidD_FreePreparsedData(preparsedData_);
            preparsedData_ = nullptr;
        }
        if (readEvent_ != nullptr) {
            CloseHandle(readEvent_);
            readEvent_ = nullptr;
        }
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        inputReportBytes_ = 0;
    }

public:
    HRESULT ApplyProfile(
        const dfgt::ipc::control::Profile& profile) noexcept {
        if (profile.id[dfgt::ipc::control::ProfileIdCharacters - 1] != L'\0' ||
            profile.rangeDegrees < 40 ||
            profile.rangeDegrees > 900 ||
            !ValidGain(profile.overallGain) ||
            profile.boundaryForce < 0 ||
            profile.boundaryForce > 5000 ||
            !ValidGain(profile.springGain) ||
            !ValidGain(profile.damperGain) ||
            !ValidGain(profile.frictionGain) ||
            !ValidGain(profile.periodicGain)) {
            return E_INVALIDARG;
        }
        if (handle_ == INVALID_HANDLE_VALUE) {
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        }
        const auto rangeResult = SetRange(profile.rangeDegrees);
        if (FAILED(rangeResult)) return rangeResult;
        overallGain_ = profile.overallGain;
        boundaryForce_ = profile.boundaryForce;
        springGain_ = profile.springGain;
        damperGain_ = profile.damperGain;
        frictionGain_ = profile.frictionGain;
        periodicGain_ = profile.periodicGain;
        wcsncpy_s(
            activeProfileId_,
            profile.id,
            _TRUNCATE);
        return S_OK;
    }

    [[nodiscard]] bool IsOpen() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] dfgt::ipc::control::Status Status() const noexcept {
        dfgt::ipc::control::Status status{};
        status.connected = IsOpen() ? 1u : 0u;
        status.rangeDegrees = rangeDegrees_;
        status.overallGain = overallGain_;
        status.boundaryForce = boundaryForce_;
        wcsncpy_s(
            status.activeProfileId,
            activeProfileId_,
            _TRUNCATE);
        return status;
    }

private:
    static bool ValidGain(std::int32_t value) noexcept {
        return value >= 0 && value <= 10000;
    }

    static std::int32_t ScaleGain(
        std::int32_t value,
        std::int32_t gain) noexcept {
        const auto bounded = std::clamp(value, -10000, 10000);
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(bounded) * gain / 10000);
    }
    HRESULT RestoreBaseConstant() noexcept {
        return baseConstant_ == 0
            ? Send(dfgt::reports::StopSlotOne())
            : Send(dfgt::reports::Constant(baseConstant_));
    }

    HRESULT BeginRead() noexcept {
        if (readHandle_ == INVALID_HANDLE_VALUE || readEvent_ == nullptr) {
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        }
        ResetEvent(readEvent_);
        readOverlapped_ = {};
        readOverlapped_.hEvent = readEvent_;
        DWORD bytesRead = 0;
        if (ReadFile(
                readHandle_,
                readBuffer_.data(),
                static_cast<DWORD>(readBuffer_.size()),
                &bytesRead,
                &readOverlapped_)) {
            readPending_ = true;
            SetEvent(readEvent_);
            return S_OK;
        }
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING) return HRESULT_FROM_WIN32(error);
        readPending_ = true;
        return S_OK;
    }

    HRESULT Send(const std::array<std::uint8_t, 8>& report) noexcept {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        }
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
                handle_,
                pending->bytes.data(),
                static_cast<DWORD>(pending->bytes.size()),
                &written,
                &pending->overlapped);
        if (!completed) {
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return HRESULT_FROM_WIN32(error);
            }
            const auto wait = WaitForSingleObject(
                event, kHidWriteTimeoutMs);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &pending->overlapped);
                if (WaitForSingleObject(
                        event, kHidWriteTimeoutMs) != WAIT_OBJECT_0) {
                    // Preserve the OVERLAPPED and buffer if a broken driver
                    // ignores cancellation. The broker remains responsive;
                    // process teardown reclaims this quarantined allocation.
                    pending.release();
                }
                return wait == WAIT_TIMEOUT
                    ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                    : HRESULT_FROM_WIN32(GetLastError());
            }
            if (!GetOverlappedResult(
                    handle_, &pending->overlapped, &written, FALSE)) {
                const auto result =
                    HRESULT_FROM_WIN32(GetLastError());
                return result;
            }
        }
        return written == report.size()
            ? S_OK
            : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
    HANDLE readHandle_{INVALID_HANDLE_VALUE};
    HANDLE readEvent_{nullptr};
    PHIDP_PREPARSED_DATA preparsedData_{nullptr};
    OVERLAPPED readOverlapped_{};
    std::array<std::uint8_t, 64> readBuffer_{};
    DWORD inputReportBytes_{0};
    bool readPending_{false};
    std::int32_t rangeDegrees_{900};
    std::int32_t baseConstant_{0};
    std::int32_t rangeLockState_{0};
    std::int32_t overallGain_{10000};
    std::int32_t boundaryForce_{kRangeLockMagnitude};
    std::int32_t springGain_{10000};
    std::int32_t damperGain_{10000};
    std::int32_t frictionGain_{10000};
    std::int32_t periodicGain_{10000};
    wchar_t activeProfileId_[
        dfgt::ipc::control::ProfileIdCharacters]{L'd', L'e', L's', L'k',
                                                  L't', L'o', L'p', L'\0'};
};

struct BrokerState final {
    // Lock order: stateMutex is never held during OS I/O. deviceMutex only
    // serializes access to HidDevice and must never be acquired while holding
    // stateMutex.
    std::mutex stateMutex;
    std::timed_mutex deviceMutex;
    HidDevice device;
    bool ffbClientConnected{false};
    bool controlClientConnected{false};
    std::uint32_t failSafeCount{0};
    HRESULT lastResult{S_OK};
    dfgt::broker::SafetyState safety;
    std::atomic<std::uint64_t> safetyGeneration{0};
    std::atomic<bool> stopRecoveryRequested{false};
};

void RunStopRecovery(
    BrokerState& state,
    std::stop_token stop) {
    while (!stop.stop_requested()) {
        if (!state.stopRecoveryRequested.exchange(false)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        for (const auto delay : dfgt::broker::StopRecoveryBackoff) {
            std::this_thread::sleep_for(delay);
            if (stop.stop_requested()) return;
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                result = state.device.StopAll();
            }
            {
                std::scoped_lock lock(state.stateMutex);
                state.lastResult = result;
                if (SUCCEEDED(result)) {
                    state.safety.ConfirmAllStopped();
                } else {
                    state.safety.MarkStopUnconfirmed(result);
                }
            }
            if (SUCCEEDED(result)) {
                state.stopRecoveryRequested = false;
                break;
            }
        }
    }
}

bool ReadMessageWithTimeout(
    HANDLE pipe,
    const dfgt::broker::ClientProcessWatch& clientProcess,
    dfgt::ipc::Message& message,
    bool forceActive,
    ULONGLONG leaseDeadline,
    BrokerState& state) {
    for (;;) {
        if (clientProcess.Terminated()) {
            SetLastError(ERROR_PROCESS_ABORTED);
            return false;
        }
        HRESULT rangeLockResult = S_OK;
        {
            std::scoped_lock lock(state.deviceMutex);
            rangeLockResult = state.device.PollRangeLock();
        }
        if (FAILED(rangeLockResult)) {
            {
                std::scoped_lock lock(state.deviceMutex);
                state.device.StopAll();
                state.device.SetRange(900);
            }
            {
                std::scoped_lock lock(state.stateMutex);
                state.lastResult = rangeLockResult;
                ++state.failSafeCount;
            }
            std::wcout
                << L"{\"event\":\"range-lock-read-failed\","
                   L"\"result\":" << rangeLockResult
                << L",\"win32\":" << HRESULT_CODE(rangeLockResult)
                << L"}\n"
                << std::flush;
            SetLastError(HRESULT_CODE(rangeLockResult));
            return false;
        }
        if (forceActive && GetTickCount64() >= leaseDeadline) {
            HRESULT result = S_OK;
            {
                result = dfgt::broker::TryEmergencyIo(
                    state.deviceMutex,
                    std::chrono::milliseconds(5),
                    [&]() noexcept { return state.device.StopAll(); });
            }
            {
                std::scoped_lock lock(state.stateMutex);
                if (SUCCEEDED(result)) state.safety.ConfirmAllStopped();
                else {
                    state.safety.MarkStopUnconfirmed(result);
                    state.stopRecoveryRequested = true;
                }
                state.lastResult = result;
                ++state.failSafeCount;
            }
            std::wcout
                << L"{\"event\":\"lease-expired-stop-all\",\"tickMs\":"
                << GetTickCount64()
                << L",\"result\":" << result << L"}\n"
                << std::flush;
            SetLastError(ERROR_TIMEOUT);
            return false;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available >= dfgt::ipc::wire::MessageSize) {
            dfgt::ipc::wire::EncodedMessage encoded{};
            const auto read = dfgt::io::ReadExact(
                pipe, encoded.data(),
                static_cast<DWORD>(encoded.size()),
                kPipeIoTimeoutMs);
            if (!read.Succeeded(
                    static_cast<DWORD>(encoded.size())) ||
                !dfgt::ipc::wire::Decode(encoded, message)) {
                SetLastError(HRESULT_CODE(read.result));
                if (SUCCEEDED(read.result)) {
                    SetLastError(ERROR_INVALID_DATA);
                }
                return false;
            }
            return true;
        }
        if (!forceActive && GetTickCount64() >= leaseDeadline) {
            SetLastError(
                available == 0 ? ERROR_TIMEOUT : ERROR_INVALID_DATA);
            return false;
        }
        Sleep(5);
    }
}

HRESULT ProcessMessage(
    const dfgt::ipc::Message& message,
    BrokerState& state,
    bool& opened,
    bool& forceActive,
    std::uint64_t& sessionGeneration,
    std::uint64_t expectedNonce,
    bool suppressForceWrites) {
    using dfgt::ipc::Command;
    if (message.magic != dfgt::ipc::Magic ||
        message.version != dfgt::ipc::Version ||
        message.size != sizeof(message)) {
        return E_INVALIDARG;
    }
    if (message.capabilities !=
            dfgt::ipc::RequiredCapabilities ||
        message.buildId != dfgt::ipc::BuildId ||
        (message.command == Command::Open
            ? message.sessionNonce != 0
            : message.sessionNonce != expectedNonce)) {
        return E_ACCESSDENIED;
    }

    switch (message.command) {
    case Command::Open: {
        if (message.devicePath[dfgt::ipc::DevicePathCharacters - 1] != L'\0') {
            return E_INVALIDARG;
        }
        auto result = S_OK;
        {
            std::scoped_lock lock(state.stateMutex);
            state.safety.BeginInitialize();
        }
        {
            std::scoped_lock lock(state.deviceMutex);
            if (!state.device.IsOpen()) {
                result = state.device.Open(message.devicePath);
            }
        }
        opened = SUCCEEDED(result);
        sessionGeneration = state.safetyGeneration.load();
        forceActive = false;
        {
            std::scoped_lock lock(state.stateMutex);
            state.ffbClientConnected = opened;
            state.lastResult = result;
            if (opened &&
                state.safety.Mode() ==
                    dfgt::broker::SafetyMode::Initializing) {
                state.safety.ConfirmNeutralReady();
                result = state.safety.Arm();
                state.lastResult = result;
            } else if (FAILED(result)) {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
        }
        return result;
    }
    case Command::Constant:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (sessionGeneration != state.safetyGeneration.load()) {
            return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
        }
        forceActive =
            message.value != 0 || message.auxiliary != 0;
        if (suppressForceWrites) return S_OK;
        {
            if (forceActive) {
                std::scoped_lock lock(state.stateMutex);
                const auto allowed = state.safety.CanAcceptForce();
                if (FAILED(allowed)) return allowed;
            }
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                if (sessionGeneration !=
                    state.safetyGeneration.load()) {
                    return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
                }
                result = forceActive
                    ? state.device.Constant(
                        message.value, message.auxiliary)
                    : state.device.StopSoftwareSlot();
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                if (forceActive) {
                    state.safety.Activate(
                        dfgt::broker::PhysicalSlot::Software);
                } else {
                    state.safety.ConfirmSlotStopped(
                        dfgt::broker::PhysicalSlot::Software);
                }
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::StopSoftwareSlot:
        forceActive = false;
        {
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                result = state.device.StopSoftwareSlot();
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.ConfirmSlotStopped(
                    dfgt::broker::PhysicalSlot::Software);
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::StopAll:
        forceActive = false;
        {
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                result = state.device.StopAll();
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::SetRange:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (sessionGeneration != state.safetyGeneration.load()) {
            return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
        }
        {
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                if (sessionGeneration !=
                    state.safetyGeneration.load()) {
                    return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
                }
                result = state.device.SetRange(message.value);
            }
            std::scoped_lock lock(state.stateMutex);
            state.lastResult = result;
            return result;
        }
    case Command::Spring:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (sessionGeneration != state.safetyGeneration.load()) {
            return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
        }
        forceActive = true;
        if (suppressForceWrites) return S_OK;
        {
            {
                std::scoped_lock lock(state.stateMutex);
                const auto allowed = state.safety.CanAcceptForce();
                if (FAILED(allowed)) return allowed;
            }
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                if (sessionGeneration !=
                    state.safetyGeneration.load()) {
                    return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
                }
                result = state.device.Spring(
                    message.value, message.auxiliary,
                    message.value3, message.value4,
                    message.value5, message.value6);
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.Activate(
                    dfgt::broker::PhysicalSlot::Spring);
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::Damper:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (sessionGeneration != state.safetyGeneration.load()) {
            return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
        }
        forceActive = true;
        if (suppressForceWrites) return S_OK;
        {
            {
                std::scoped_lock lock(state.stateMutex);
                const auto allowed = state.safety.CanAcceptForce();
                if (FAILED(allowed)) return allowed;
            }
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                if (sessionGeneration !=
                    state.safetyGeneration.load()) {
                    return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
                }
                result = state.device.Damper(
                    message.value, message.auxiliary,
                    message.value5, message.value6);
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.Activate(
                    dfgt::broker::PhysicalSlot::DamperOrInertia);
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::Friction:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (sessionGeneration != state.safetyGeneration.load()) {
            return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
        }
        forceActive = true;
        if (suppressForceWrites) return S_OK;
        {
            {
                std::scoped_lock lock(state.stateMutex);
                const auto allowed = state.safety.CanAcceptForce();
                if (FAILED(allowed)) return allowed;
            }
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                if (sessionGeneration !=
                    state.safetyGeneration.load()) {
                    return dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
                }
                result = state.device.Friction(
                    message.value, message.auxiliary,
                    message.value5, message.value6);
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.Activate(
                    dfgt::broker::PhysicalSlot::Friction);
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
            return result;
        }
    case Command::Heartbeat:
        if (!opened) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        return sessionGeneration == state.safetyGeneration.load()
            ? S_OK
            : dfgt::broker::DFGT_E_EMERGENCY_LATCHED;
    case Command::Close:
        forceActive = false;
        opened = false;
        {
            bool resetRange = false;
            {
                std::scoped_lock lock(state.stateMutex);
                state.ffbClientConnected = false;
                resetRange = !state.controlClientConnected;
            }
            HRESULT result = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                result = state.device.StopAll();
                if (resetRange) state.device.SetRange(900);
            }
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
            state.lastResult = result;
        }
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

bool WriteResponse(
    HANDLE pipe,
    std::uint32_t sequence,
    HRESULT result,
    std::uint64_t sessionNonce,
    BrokerState& state) {
    dfgt::ipc::Response response{};
    response.sequence = sequence;
    response.result = result;
    response.capabilities = dfgt::ipc::RequiredCapabilities;
    response.sessionNonce = sessionNonce;
    response.buildId = dfgt::ipc::BuildId;
    {
        std::scoped_lock lock(state.stateMutex);
        response.safetyMode =
            static_cast<std::uint32_t>(state.safety.Mode());
        response.activeSlots =
            static_cast<std::uint32_t>(
                state.safety.ActiveSlots());
        response.connected =
            state.safety.Mode() !=
                dfgt::broker::SafetyMode::Disconnected
            ? 1u
            : 0u;
        response.stopConfirmed =
            state.safety.StopConfirmed() ? 1u : 0u;
    }
    const auto encoded = dfgt::ipc::wire::Encode(response);
    const auto write = dfgt::io::WriteExact(
        pipe, encoded.data(),
        static_cast<DWORD>(encoded.size()),
        kPipeIoTimeoutMs);
    return write.Succeeded(
        static_cast<DWORD>(encoded.size()));
}

SECURITY_ATTRIBUTES MakePipeSecurity(PSECURITY_DESCRIPTOR& descriptor) {
    // Limit ordinary access to the account that launched the broker. Using the
    // broad Interactive Users SID would let another local login inject force.
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        descriptor = nullptr;
        return {};
    }
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(token);
        descriptor = nullptr;
        return {};
    }
    std::vector<std::uint8_t> tokenBuffer(bytes);
    if (!GetTokenInformation(
            token,
            TokenUser,
            tokenBuffer.data(),
            bytes,
            &bytes)) {
        CloseHandle(token);
        descriptor = nullptr;
        return {};
    }
    CloseHandle(token);

    const auto* tokenUser =
        reinterpret_cast<const TOKEN_USER*>(tokenBuffer.data());
    LPWSTR userSid = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &userSid)) {
        descriptor = nullptr;
        return {};
    }
    const std::wstring sddl =
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;" +
        std::wstring(userSid) +
        L")";
    LocalFree(userSid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(),
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        descriptor = nullptr;
        return {};
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return attributes;
}

int RunClient(
    HANDLE pipe,
    BrokerState& state,
    bool diagnostics,
    bool suppressForceWrites) {
    ULONG clientPid = 0;
    ULONG clientSession = 0;
    GetNamedPipeClientProcessId(pipe, &clientPid);
    GetNamedPipeClientSessionId(pipe, &clientSession);
    dfgt::broker::ClientProcessWatch clientProcess(clientPid);
    std::uint64_t sessionNonce = 0;
    const auto nonceResult = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(&sessionNonce),
        sizeof(sessionNonce),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(nonceResult)) sessionNonce = 0;
    if (clientPid == 0 || !clientProcess.Valid() ||
        sessionNonce == 0) {
        std::scoped_lock lock(state.stateMutex);
        state.lastResult = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        ++state.failSafeCount;
        return 1;
    }
    std::wcout
        << L"{\"event\":\"ffb-session-open\",\"pid\":"
        << clientPid << L",\"session\":" << clientSession
        << L",\"nonce\":" << sessionNonce << L"}\n"
        << std::flush;
    bool opened = false;
    bool forceActive = false;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t messageCount = 0;
    std::uint64_t constantCount = 0;
    std::uint64_t nonzeroConstantCount = 0;
    std::int32_t maximumAbsoluteConstant = 0;
    std::uint64_t stopAllCount = 0;
    std::uint64_t heartbeatCount = 0;
    std::uint32_t lastSequence = 0;
    dfgt::broker::LeaseWatchdog watchdog{
        std::chrono::milliseconds(kReadTimeoutMs)};
    const auto watchdogStart = std::chrono::steady_clock::now();
    watchdog.Reset(watchdogStart);
    ULONGLONG leaseDeadline = GetTickCount64() + kReadTimeoutMs;
    for (;;) {
        dfgt::ipc::Message message{};
        if (!ReadMessageWithTimeout(
                pipe,
                clientProcess,
                message,
                forceActive,
                leaseDeadline,
                state)) {
            if (GetLastError() == ERROR_TIMEOUT) {
                forceActive = false;
                leaseDeadline = GetTickCount64() + kReadTimeoutMs;
                continue;
            }
            HRESULT result = S_OK;
            bool resetRange = false;
            {
                std::scoped_lock lock(state.stateMutex);
                resetRange = !state.controlClientConnected;
            }
            {
                std::scoped_lock lock(state.deviceMutex);
                result = state.device.StopAll();
                if (resetRange) state.device.SetRange(900);
            }
            {
                std::scoped_lock lock(state.stateMutex);
                state.ffbClientConnected = false;
                state.lastResult = result;
                ++state.failSafeCount;
                if (SUCCEEDED(result)) state.safety.ConfirmAllStopped();
                else {
                    state.safety.MarkStopUnconfirmed(result);
                    state.stopRecoveryRequested = true;
                }
            }
            std::wcout
                << L"{\"event\":\"client-disconnect-stop-all\",\"tickMs\":"
                << GetTickCount64()
                << L",\"result\":" << result << L"}\n"
                << std::flush;
            return 0;
        }
        const auto sequenceValid =
            message.sequence > lastSequence;
        const auto effectiveResult = sequenceValid
            ? ProcessMessage(
                message,
                state,
                opened,
                forceActive,
                sessionGeneration,
                sessionNonce,
                suppressForceWrites)
            : E_INVALIDARG;
        if (sequenceValid) lastSequence = message.sequence;
        {
            std::scoped_lock lock(state.stateMutex);
            forceActive =
                state.safety.ActiveSlots() !=
                dfgt::broker::PhysicalSlot::None;
        }
        if (diagnostics &&
            message.command != dfgt::ipc::Command::Heartbeat) {
            std::wcout
                << L"{\"event\":\"command\",\"sequence\":"
                << message.sequence
                << L",\"command\":"
                << static_cast<std::uint32_t>(message.command)
                << L",\"value\":" << message.value
                << L",\"result\":" << effectiveResult
                << L"}\n"
                << std::flush;
        }
        ++messageCount;
        if (message.command == dfgt::ipc::Command::Constant) {
            ++constantCount;
            if (message.value != 0) ++nonzeroConstantCount;
            maximumAbsoluteConstant = std::max(
                maximumAbsoluteConstant,
                static_cast<std::int32_t>(std::abs(
                    static_cast<std::int64_t>(message.value))));
        } else if (message.command == dfgt::ipc::Command::StopAll) {
            ++stopAllCount;
        } else if (message.command == dfgt::ipc::Command::Heartbeat) {
            ++heartbeatCount;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto rendererProgressed =
            message.command != dfgt::ipc::Command::Heartbeat ||
            watchdog.ObserveRenderer(message.renderEpoch, now);
        if (SUCCEEDED(effectiveResult) && forceActive && rendererProgressed &&
            (message.command == dfgt::ipc::Command::Constant ||
             message.command == dfgt::ipc::Command::Spring ||
             message.command == dfgt::ipc::Command::Damper ||
             message.command == dfgt::ipc::Command::Friction ||
             message.command == dfgt::ipc::Command::Heartbeat)) {
            if (message.command != dfgt::ipc::Command::Heartbeat) {
                watchdog.ObserveForceCommand(now);
            }
            leaseDeadline = GetTickCount64() +
                static_cast<ULONGLONG>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        watchdog.Deadline() - now).count());
        } else if (!forceActive) {
            leaseDeadline = GetTickCount64() + kReadTimeoutMs;
        }
        if (!WriteResponse(
                pipe, message.sequence, effectiveResult,
                sessionNonce, state)) {
            HRESULT stopResult = S_OK;
            {
                std::scoped_lock lock(state.deviceMutex);
                stopResult = state.device.StopAll();
            }
            {
                std::scoped_lock lock(state.stateMutex);
                state.ffbClientConnected = false;
                ++state.failSafeCount;
                state.lastResult = stopResult;
                if (SUCCEEDED(stopResult)) state.safety.ConfirmAllStopped();
                else {
                    state.safety.MarkStopUnconfirmed(stopResult);
                    state.stopRecoveryRequested = true;
                }
            }
            return 0;
        }
        if (!sequenceValid ||
            effectiveResult == E_ACCESSDENIED) {
            return 0;
        }
        if (message.command == dfgt::ipc::Command::Close) {
            if (diagnostics) {
                std::wcout
                    << L"{\"event\":\"client-summary\",\"messages\":"
                    << messageCount
                    << L",\"constants\":" << constantCount
                    << L",\"nonzeroConstants\":" << nonzeroConstantCount
                    << L",\"maximumAbsoluteConstant\":"
                    << maximumAbsoluteConstant
                    << L",\"stopAll\":" << stopAllCount
                    << L",\"heartbeats\":" << heartbeatCount
                    << L"}\n"
                    << std::flush;
            }
            return 0;
        }
    }
}

dfgt::ipc::control::Status SnapshotStatus(BrokerState& state) {
    dfgt::ipc::control::Status status{};
    {
        std::scoped_lock lock(state.deviceMutex);
        status = state.device.Status();
    }
    std::scoped_lock lock(state.stateMutex);
    status.ffbClientConnected =
        state.ffbClientConnected ? 1u : 0u;
    status.controlClientConnected =
        state.controlClientConnected ? 1u : 0u;
    status.failSafeCount = state.failSafeCount;
    status.lastResult = state.lastResult;
    status.safetyMode =
        static_cast<std::uint32_t>(state.safety.Mode());
    status.activeSlots =
        static_cast<std::uint32_t>(state.safety.ActiveSlots());
    status.stopConfirmed = state.safety.StopConfirmed() ? 1u : 0u;
    status.lastSafetyError = state.safety.LastSafetyError();
    status.safetyGeneration = state.safetyGeneration.load();
    if (FAILED(state.lastResult)) {
        status.state = dfgt::ipc::control::DeviceState::Faulted;
    } else if (!status.connected) {
        status.state = dfgt::ipc::control::DeviceState::Disconnected;
    } else if (state.ffbClientConnected) {
        status.state = dfgt::ipc::control::DeviceState::GameActive;
    } else if (status.activeProfileId[0] != L'\0') {
        status.state = dfgt::ipc::control::DeviceState::ProfileActive;
    } else {
        status.state = dfgt::ipc::control::DeviceState::Ready;
    }
    return status;
}

HRESULT ProcessControlRequest(
    const dfgt::ipc::control::Request& request,
    BrokerState& state,
    std::uint64_t expectedNonce) {
    using dfgt::ipc::control::Command;
    if (request.magic != dfgt::ipc::control::Magic ||
        request.version != dfgt::ipc::control::Version ||
        request.size != sizeof(request)) {
        return E_INVALIDARG;
    }
    if (request.capabilities !=
            dfgt::ipc::RequiredCapabilities ||
        request.buildId != dfgt::ipc::BuildId ||
        (request.command ==
                dfgt::ipc::control::Command::Ping &&
            request.sessionNonce == 0
            ? false
            : request.sessionNonce != expectedNonce)) {
        return E_ACCESSDENIED;
    }

    HRESULT result = S_OK;
    switch (request.command) {
    case Command::Ping:
    case Command::GetStatus:
        break;
    case Command::AttachDevice:
        if (request.devicePath[
                dfgt::ipc::DevicePathCharacters - 1] != L'\0') {
            result = E_INVALIDARG;
        } else {
            {
                std::scoped_lock lock(state.stateMutex);
                state.safety.BeginInitialize();
            }
            {
                std::scoped_lock lock(state.deviceMutex);
                if (!state.device.IsOpen()) {
                    result = state.device.Open(request.devicePath);
                }
            }
            {
                std::scoped_lock lock(state.stateMutex);
                if (SUCCEEDED(result) &&
                    state.safety.Mode() ==
                        dfgt::broker::SafetyMode::Initializing) {
                    state.safety.ConfirmNeutralReady();
                    result = state.safety.Arm();
                } else if (FAILED(result)) {
                    state.safety.MarkStopUnconfirmed(result);
                    state.stopRecoveryRequested = true;
                }
            }
        }
        break;
    case Command::ApplyProfile: {
        {
            std::scoped_lock lock(state.stateMutex);
            const auto allowed = state.safety.CanAcceptForce();
            if (FAILED(allowed)) {
                result = allowed;
                break;
            }
        }
        std::scoped_lock lock(state.deviceMutex);
        result = state.device.ApplyProfile(request.profile);
        break;
    }
    case Command::EmergencyStop:
        // Latch and invalidate the current FFB session before touching HID.
        {
            HRESULT latchResult = S_OK;
            {
                std::scoped_lock lock(state.stateMutex);
                latchResult = state.safety.LatchEmergency();
                ++state.safetyGeneration;
            }
            {
                result = dfgt::broker::TryEmergencyIo(
                    state.deviceMutex,
                    std::chrono::milliseconds(5),
                    [&]() noexcept { return state.device.StopAll(); });
            }
            {
                std::scoped_lock lock(state.stateMutex);
                if (SUCCEEDED(result)) {
                    state.safety.ConfirmAllStopped();
                    result = latchResult;
                } else {
                    state.safety.MarkStopUnconfirmed(result);
                    state.stopRecoveryRequested = true;
                }
                ++state.failSafeCount;
            }
        }
        break;
    case Command::AcknowledgeAndRearm: {
        bool deviceOpen = false;
        {
            std::scoped_lock lock(state.deviceMutex);
            deviceOpen = state.device.IsOpen();
        }
        std::scoped_lock lock(state.stateMutex);
        result = state.safety.AcknowledgeAndRearm(
            deviceOpen,
            state.safety.StopConfirmed());
        break;
    }
    case Command::DetachDevice: {
        bool closeDevice = false;
        {
            std::scoped_lock lock(state.stateMutex);
            closeDevice = !state.ffbClientConnected;
        }
        {
            std::scoped_lock lock(state.deviceMutex);
            result = state.device.StopAll();
            state.device.SetRange(900);
            if (closeDevice) state.device.Close();
        }
        {
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
                state.safety.Disconnect();
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
        }
        break;
    }
    case Command::PrepareForMaintenance: {
        {
            std::scoped_lock lock(state.stateMutex);
            if (state.ffbClientConnected) {
                result = HRESULT_FROM_WIN32(ERROR_BUSY);
                break;
            }
        }
        HRESULT stopResult = E_FAIL;
        HRESULT rangeResult = E_FAIL;
        {
            std::scoped_lock lock(state.deviceMutex);
            stopResult = state.device.StopAll();
            rangeResult = SUCCEEDED(stopResult)
                ? state.device.SetRange(900)
                : stopResult;
            if (SUCCEEDED(stopResult) &&
                SUCCEEDED(rangeResult)) {
                state.device.Close();
            }
        }
        result = FAILED(stopResult) ? stopResult : rangeResult;
        {
            std::scoped_lock lock(state.stateMutex);
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
                state.safety.EnterMaintenanceSafe();
            } else {
                state.safety.MarkStopUnconfirmed(result);
                state.stopRecoveryRequested = true;
            }
        }
        break;
    }
    default:
        result = E_INVALIDARG;
        break;
    }
    {
        std::scoped_lock lock(state.stateMutex);
        state.lastResult = result;
    }
    return result;
}

void RunControlServer(
    BrokerState& state,
    SECURITY_ATTRIBUTES security,
    bool diagnostics,
    std::stop_token stop) {
    while (!stop.stop_requested()) {
        const auto pipe = CreateNamedPipeW(
            dfgt::ipc::ControlPipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
                FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1,
            4096,
            4096,
            0,
            &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::wcerr
                << L"CreateNamedPipeW(control) failed: "
                << GetLastError() << L"\n";
            return;
        }

        const auto connectResult =
            dfgt::io::ConnectPipe(pipe, kPipeIoTimeoutMs);
        if (FAILED(connectResult)) {
            CloseHandle(pipe);
            if (stop.stop_requested()) return;
            continue;
        }
        ULONG clientPid = 0;
        ULONG clientSession = 0;
        DWORD brokerSession = 0;
        GetNamedPipeClientProcessId(pipe, &clientPid);
        GetNamedPipeClientSessionId(pipe, &clientSession);
        ProcessIdToSessionId(
            GetCurrentProcessId(), &brokerSession);
        dfgt::broker::ClientProcessWatch clientProcess(clientPid);
        std::uint64_t sessionNonce = 0;
        const auto nonceReady = BCRYPT_SUCCESS(BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(&sessionNonce),
            sizeof(sessionNonce),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        if (clientPid == 0 || !clientProcess.Valid() ||
            clientSession != brokerSession ||
            !nonceReady || sessionNonce == 0) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
        }
        {
            std::scoped_lock lock(state.stateMutex);
            state.controlClientConnected = true;
        }
        std::uint32_t lastSequence = 0;

        for (;;) {
            dfgt::ipc::control::Request request{};
            dfgt::ipc::control::wire::EncodedRequest encodedRequest{};
            const auto read = dfgt::io::ReadExact(
                pipe, encodedRequest.data(),
                static_cast<DWORD>(encodedRequest.size()),
                kPipeIoTimeoutMs);
            if (!read.Succeeded(
                    static_cast<DWORD>(encodedRequest.size())) ||
                !dfgt::ipc::control::wire::Decode(
                    encodedRequest, request)) {
                if (read.result ==
                        HRESULT_FROM_WIN32(ERROR_TIMEOUT) &&
                    !stop.stop_requested()) {
                    DWORD available = 0;
                    if (PeekNamedPipe(
                            pipe, nullptr, 0, nullptr,
                            &available, nullptr) &&
                        available == 0 &&
                        !clientProcess.Terminated()) {
                        continue;
                    }
                }
                break;
            }
            const auto sequenceValid =
                request.sequence > lastSequence;
            const auto firstPing =
                lastSequence == 0 &&
                request.command ==
                    dfgt::ipc::control::Command::Ping &&
                request.sessionNonce == 0;
            const auto nonceValid =
                firstPing ||
                request.sessionNonce == sessionNonce;
            const auto result =
                sequenceValid && nonceValid
                ? ProcessControlRequest(
                    request, state, sessionNonce)
                : E_ACCESSDENIED;
            if (sequenceValid) lastSequence = request.sequence;
            dfgt::ipc::control::Response response{};
            response.sequence = request.sequence;
            response.result = result;
            response.status = SnapshotStatus(state);
            response.capabilities =
                dfgt::ipc::RequiredCapabilities;
            response.sessionNonce = sessionNonce;
            response.buildId = dfgt::ipc::BuildId;
            const auto encodedResponse =
                dfgt::ipc::control::wire::Encode(response);
            const auto write = dfgt::io::WriteExact(
                pipe, encodedResponse.data(),
                static_cast<DWORD>(encodedResponse.size()),
                kPipeIoTimeoutMs);
            if (!write.Succeeded(
                    static_cast<DWORD>(encodedResponse.size()))) {
                break;
            }
            if (diagnostics) {
                std::wcout
                    << L"{\"event\":\"control-command\","
                       L"\"sequence\":" << request.sequence
                    << L",\"command\":"
                    << static_cast<std::uint32_t>(request.command)
                    << L",\"result\":" << result << L"}\n"
                    << std::flush;
            }
            if (!sequenceValid || !nonceValid ||
                result == E_ACCESSDENIED) {
                break;
            }
        }

        {
            std::scoped_lock lock(state.stateMutex);
            state.controlClientConnected = false;
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool diagnostics =
        argc == 2 &&
        (wcscmp(argv[1], L"--diagnostics") == 0 ||
         wcscmp(argv[1], L"--diagnostics-no-force") == 0);
    const bool suppressForceWrites =
        argc == 2 && wcscmp(argv[1], L"--diagnostics-no-force") == 0;
    if (argc > 2 || (argc == 2 && !diagnostics)) {
        std::wcerr
            << L"usage: DfgtFfbBroker "
               L"[--diagnostics|--diagnostics-no-force]\n";
        return 2;
    }
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto security = MakePipeSecurity(descriptor);
    if (descriptor == nullptr) {
        std::wcerr << L"Could not create the broker pipe security descriptor: "
                   << GetLastError() << L"\n";
        return 10;
    }

    BrokerState state;
    std::jthread recoveryThread(
        [&state](std::stop_token stop) {
            RunStopRecovery(state, stop);
        });
    std::jthread controlThread(
        [&state, security, diagnostics](std::stop_token stop) {
            RunControlServer(
                state,
                security,
                diagnostics,
                stop);
        });
    for (;;) {
        const auto pipe = CreateNamedPipeW(
            dfgt::ipc::PipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
                FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            4096,
            4096,
            0,
            &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::wcerr << L"CreateNamedPipeW failed: " << GetLastError() << L"\n";
            LocalFree(descriptor);
            return 11;
        }

        const bool ready = SUCCEEDED(
            dfgt::io::ConnectPipe(pipe, kPipeIoTimeoutMs));

        if (ready) {
            RunClient(
                pipe,
                state,
                diagnostics,
                suppressForceWrites);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
