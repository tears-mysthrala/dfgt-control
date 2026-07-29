#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../shared/BrokerProtocol.h"
#include "../shared/DfgtReports.h"
#include "SafetyState.h"
#include "EffectCoordinator.h"

namespace {

constexpr DWORD kReadTimeoutMs = 350;
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
            0,
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
            Close();
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
            Close();
            return result;
        }
        if (!HidD_GetPreparsedData(readHandle_, &preparsedData_)) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            Close();
            return result;
        }
        HIDP_CAPS capabilities{};
        if (HidP_GetCaps(preparsedData_, &capabilities) !=
                HIDP_STATUS_SUCCESS ||
            capabilities.InputReportByteLength == 0 ||
            capabilities.InputReportByteLength > readBuffer_.size()) {
            Close();
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        inputReportBytes_ = capabilities.InputReportByteLength;
        const auto readResult = BeginRead();
        if (FAILED(readResult)) {
            Close();
            return readResult;
        }
        return StopAll();
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
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        rangeDegrees_ = 900;
        baseConstant_ = 0;
        rangeLockState_ = 0;
    }

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
        if (handle_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        if (HidD_SetOutputReport(
                handle_,
                const_cast<std::uint8_t*>(report.data()),
                static_cast<ULONG>(report.size()))) {
            return S_OK;
        }
        DWORD written = 0;
        if (WriteFile(
                handle_,
                report.data(),
                static_cast<DWORD>(report.size()),
                &written,
                nullptr) &&
            written == report.size()) {
            return S_OK;
        }
        return HRESULT_FROM_WIN32(GetLastError());
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
    std::mutex mutex;
    HidDevice device;
    bool ffbClientConnected{false};
    bool controlClientConnected{false};
    std::uint32_t failSafeCount{0};
    HRESULT lastResult{S_OK};
    dfgt::broker::SafetyState safety;
    std::atomic<std::uint64_t> safetyGeneration{0};
};

bool ReadMessageWithTimeout(
    HANDLE pipe,
    dfgt::ipc::Message& message,
    bool forceActive,
    ULONGLONG leaseDeadline,
    BrokerState& state) {
    for (;;) {
        HRESULT rangeLockResult = S_OK;
        {
            std::scoped_lock lock(state.mutex);
            rangeLockResult = state.device.PollRangeLock();
        }
        if (FAILED(rangeLockResult)) {
            {
                std::scoped_lock lock(state.mutex);
                state.device.StopAll();
                state.device.SetRange(900);
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
                std::scoped_lock lock(state.mutex);
                result = state.device.StopAll();
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
        if (available >= sizeof(message)) {
            DWORD bytesRead = 0;
            return ReadFile(pipe, &message, sizeof(message), &bytesRead, nullptr) &&
                bytesRead == sizeof(message);
        }
        if (!forceActive && GetTickCount64() >= leaseDeadline) {
            SetLastError(ERROR_TIMEOUT);
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
    bool suppressForceWrites) {
    using dfgt::ipc::Command;
    if (message.magic != dfgt::ipc::Magic ||
        message.version != dfgt::ipc::Version ||
        message.size != sizeof(message)) {
        return E_INVALIDARG;
    }

    switch (message.command) {
    case Command::Open: {
        if (message.devicePath[dfgt::ipc::DevicePathCharacters - 1] != L'\0') {
            return E_INVALIDARG;
        }
        std::scoped_lock lock(state.mutex);
        auto result = S_OK;
        if (!state.device.IsOpen()) {
            state.safety.BeginInitialize();
            result = state.device.Open(message.devicePath);
        }
        opened = SUCCEEDED(result);
        state.ffbClientConnected = opened;
        sessionGeneration = state.safetyGeneration.load();
        state.lastResult = result;
        forceActive = false;
        if (opened &&
            state.safety.Mode() ==
                dfgt::broker::SafetyMode::Initializing) {
            state.safety.ConfirmNeutralReady();
            result = state.safety.Arm();
            state.lastResult = result;
        } else if (FAILED(result)) {
            state.safety.MarkStopUnconfirmed(result);
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
            std::scoped_lock lock(state.mutex);
            dfgt::broker::EffectCoordinator effects(
                state.device, state.safety);
            const auto result =
                effects.SetSoftware(message.value, message.auxiliary);
            state.lastResult = result;
            return result;
        }
    case Command::StopSoftwareSlot:
        forceActive = false;
        {
            std::scoped_lock lock(state.mutex);
            dfgt::broker::EffectCoordinator effects(
                state.device, state.safety);
            const auto result = effects.StopSoftware();
            state.lastResult = result;
            return result;
        }
    case Command::StopAll:
        forceActive = false;
        {
            std::scoped_lock lock(state.mutex);
            const auto result = state.device.StopAll();
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
            } else {
                state.safety.MarkStopUnconfirmed(result);
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
            std::scoped_lock lock(state.mutex);
            const auto result = state.device.SetRange(message.value);
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
            std::scoped_lock lock(state.mutex);
            dfgt::broker::EffectCoordinator effects(
                state.device, state.safety);
            const auto result = effects.SetSpring(
                message.value, message.auxiliary,
                message.value3, message.value4,
                message.value5, message.value6);
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
            std::scoped_lock lock(state.mutex);
            dfgt::broker::EffectCoordinator effects(
                state.device, state.safety);
            const auto result = effects.SetDamper(
                message.value, message.auxiliary,
                message.value5, message.value6);
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
            std::scoped_lock lock(state.mutex);
            dfgt::broker::EffectCoordinator effects(
                state.device, state.safety);
            const auto result = effects.SetFriction(
                message.value, message.auxiliary,
                message.value5, message.value6);
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
            std::scoped_lock lock(state.mutex);
            state.ffbClientConnected = false;
            const auto result = state.device.StopAll();
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
            } else {
                state.safety.MarkStopUnconfirmed(result);
            }
            state.lastResult = result;
            if (!state.controlClientConnected) {
                state.device.SetRange(900);
            }
        }
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

bool WriteResponse(HANDLE pipe, std::uint32_t sequence, HRESULT result) {
    dfgt::ipc::Response response{};
    response.sequence = sequence;
    response.result = result;
    DWORD written = 0;
    return WriteFile(pipe, &response, sizeof(response), &written, nullptr) &&
        written == sizeof(response);
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
    bool opened = false;
    bool forceActive = false;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t messageCount = 0;
    std::uint64_t constantCount = 0;
    std::uint64_t nonzeroConstantCount = 0;
    std::int32_t maximumAbsoluteConstant = 0;
    std::uint64_t stopAllCount = 0;
    std::uint64_t heartbeatCount = 0;
    ULONGLONG leaseDeadline = GetTickCount64() + kReadTimeoutMs;
    for (;;) {
        dfgt::ipc::Message message{};
        if (!ReadMessageWithTimeout(
                pipe,
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
            {
                std::scoped_lock lock(state.mutex);
                result = state.device.StopAll();
                state.ffbClientConnected = false;
                state.lastResult = result;
                ++state.failSafeCount;
                if (!state.controlClientConnected) {
                    state.device.SetRange(900);
                }
            }
            std::wcout
                << L"{\"event\":\"client-disconnect-stop-all\",\"tickMs\":"
                << GetTickCount64()
                << L",\"result\":" << result << L"}\n"
                << std::flush;
            return 0;
        }
        const auto result = ProcessMessage(
            message,
            state,
            opened,
            forceActive,
            sessionGeneration,
            suppressForceWrites);
        if (diagnostics &&
            message.command != dfgt::ipc::Command::Heartbeat) {
            std::wcout
                << L"{\"event\":\"command\",\"sequence\":"
                << message.sequence
                << L",\"command\":"
                << static_cast<std::uint32_t>(message.command)
                << L",\"value\":" << message.value
                << L",\"result\":" << result
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
        if (SUCCEEDED(result) && forceActive &&
            (message.command == dfgt::ipc::Command::Constant ||
             message.command == dfgt::ipc::Command::Spring ||
             message.command == dfgt::ipc::Command::Damper ||
             message.command == dfgt::ipc::Command::Friction ||
             message.command == dfgt::ipc::Command::Heartbeat)) {
            leaseDeadline = GetTickCount64() + kReadTimeoutMs;
        } else if (!forceActive) {
            leaseDeadline = GetTickCount64() + kReadTimeoutMs;
        }
        if (!WriteResponse(pipe, message.sequence, result)) {
            std::scoped_lock lock(state.mutex);
            state.device.StopAll();
            state.ffbClientConnected = false;
            ++state.failSafeCount;
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
    std::scoped_lock lock(state.mutex);
    auto status = state.device.Status();
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
    BrokerState& state) {
    using dfgt::ipc::control::Command;
    if (request.magic != dfgt::ipc::control::Magic ||
        request.version != dfgt::ipc::control::Version ||
        request.size != sizeof(request)) {
        return E_INVALIDARG;
    }

    std::scoped_lock lock(state.mutex);
    HRESULT result = S_OK;
    switch (request.command) {
    case Command::Ping:
    case Command::GetStatus:
        break;
    case Command::AttachDevice:
        if (request.devicePath[
                dfgt::ipc::DevicePathCharacters - 1] != L'\0') {
            result = E_INVALIDARG;
        } else if (!state.device.IsOpen()) {
            state.safety.BeginInitialize();
            result = state.device.Open(request.devicePath);
            if (SUCCEEDED(result) &&
                state.safety.Mode() ==
                    dfgt::broker::SafetyMode::Initializing) {
                state.safety.ConfirmNeutralReady();
                result = state.safety.Arm();
            } else if (FAILED(result)) {
                state.safety.MarkStopUnconfirmed(result);
            }
        }
        break;
    case Command::ApplyProfile:
        result = state.device.ApplyProfile(request.profile);
        break;
    case Command::EmergencyStop:
        // Latch and invalidate the current FFB session before touching HID.
        {
            const auto latchResult = state.safety.LatchEmergency();
            ++state.safetyGeneration;
            result = state.device.StopAll();
            if (SUCCEEDED(result)) {
                state.safety.ConfirmAllStopped();
                result = latchResult;
            } else {
                state.safety.MarkStopUnconfirmed(result);
            }
        }
        ++state.failSafeCount;
        break;
    case Command::AcknowledgeAndRearm:
        result = state.safety.AcknowledgeAndRearm(
            state.device.IsOpen(),
            state.safety.StopConfirmed());
        break;
    case Command::DetachDevice:
        result = state.device.StopAll();
        if (SUCCEEDED(result)) {
            state.safety.ConfirmAllStopped();
        } else {
            state.safety.MarkStopUnconfirmed(result);
        }
        state.device.SetRange(900);
        if (!state.ffbClientConnected) state.device.Close();
        if (SUCCEEDED(result)) state.safety.Disconnect();
        break;
    default:
        result = E_INVALIDARG;
        break;
    }
    state.lastResult = result;
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
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
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

        const auto connected = ConnectNamedPipe(pipe, nullptr);
        const auto error = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && error != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (stop.stop_requested()) return;
            continue;
        }
        {
            std::scoped_lock lock(state.mutex);
            state.controlClientConnected = true;
        }

        for (;;) {
            dfgt::ipc::control::Request request{};
            DWORD bytesRead = 0;
            if (!ReadFile(
                    pipe,
                    &request,
                    sizeof(request),
                    &bytesRead,
                    nullptr) ||
                bytesRead != sizeof(request)) {
                break;
            }
            const auto result = ProcessControlRequest(request, state);
            dfgt::ipc::control::Response response{};
            response.sequence = request.sequence;
            response.result = result;
            response.status = SnapshotStatus(state);
            DWORD written = 0;
            if (!WriteFile(
                    pipe,
                    &response,
                    sizeof(response),
                    &written,
                    nullptr) ||
                written != sizeof(response)) {
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
        }

        {
            std::scoped_lock lock(state.mutex);
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
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
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

        const auto connected = ConnectNamedPipe(pipe, nullptr);
        const auto error = connected ? ERROR_SUCCESS : GetLastError();
        const bool ready = connected || error == ERROR_PIPE_CONNECTED;

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
