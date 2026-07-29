#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <numbers>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../shared/BrokerProtocol.h"

// {806A045D-E7E8-491E-A607-31495A7BF4A3}
static constexpr GUID CLSID_DfgtDirectInputFfb{
    0x806a045d, 0xe7e8, 0x491e, {0xa6, 0x07, 0x31, 0x49, 0x5a, 0x7b, 0xf4, 0xa3}};

namespace {

std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};

constexpr DWORD kConstantEffectId = 0;
constexpr DWORD kRampEffectId = 1;
constexpr DWORD kSquareEffectId = 2;
constexpr DWORD kSineEffectId = 3;
constexpr DWORD kTriangleEffectId = 4;
constexpr DWORD kSawtoothUpEffectId = 5;
constexpr DWORD kSawtoothDownEffectId = 6;
constexpr DWORD kSpringEffectId = 7;
constexpr DWORD kDamperEffectId = 8;
constexpr DWORD kInertiaEffectId = 9;
constexpr DWORD kFrictionEffectId = 10;
constexpr DWORD kCustomForceEffectId = 0x100;
constexpr DWORD kInvalidEffectHandle = 0;
constexpr DWORD kFirstEffectHandle = 1;
constexpr std::size_t kMaximumEffects = 16;
constexpr DWORD kFirmwareRevision = 0x1322;
constexpr DWORD kDriverVersion = 0x00010000;

void LogDiagnostic(const char* format, ...) noexcept {
    wchar_t enabled[2]{};
    if (GetEnvironmentVariableW(
            L"DFGT_FFB_DIAGNOSTICS",
            enabled,
            static_cast<DWORD>(std::size(enabled))) == 0 ||
        enabled[0] != L'1') {
        return;
    }

    char line[320]{};
    va_list arguments;
    va_start(arguments, format);
    const auto written = vsprintf_s(line, format, arguments);
    va_end(arguments);
    if (written <= 0) return;
    strcat_s(line, "\r\n");

    wchar_t path[MAX_PATH]{};
    const auto pathLength = GetTempPathW(
        static_cast<DWORD>(std::size(path)),
        path);
    if (pathLength == 0 || pathLength >= std::size(path) ||
        wcscat_s(path, L"dfgt-provider-diagnostic.log") != 0) {
        return;
    }
    const auto file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD bytesWritten = 0;
    WriteFile(
        file,
        line,
        static_cast<DWORD>(strlen(line)),
        &bytesWritten,
        nullptr);
    CloseHandle(file);
}

class BrokerTransport final {
public:
    BrokerTransport()
        : heartbeat_([this](std::stop_token stop) { HeartbeatLoop(stop); }) {}

    ~BrokerTransport() {
        activeSlots_ = 0;
        heartbeat_.request_stop();
        heartbeatWake_.notify_all();
        StopAll();
        Close();
    }

    HRESULT Open(const wchar_t* path) noexcept {
        if (path == nullptr || path[0] == L'\0') return E_INVALIDARG;
        Close();
        if (!WaitNamedPipeW(dfgt::ipc::PipeName, 2000)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        {
            std::scoped_lock lock(mutex_);
            pipe_ = CreateFileW(
                dfgt::ipc::PipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (pipe_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
                const auto result = HRESULT_FROM_WIN32(GetLastError());
                CloseHandle(pipe_);
                pipe_ = INVALID_HANDLE_VALUE;
                return result;
            }
        }

        dfgt::ipc::Message message{};
        message.command = dfgt::ipc::Command::Open;
        if (wcsncpy_s(
                message.devicePath,
                dfgt::ipc::DevicePathCharacters,
                path,
                _TRUNCATE) != 0) {
            Close();
            return E_INVALIDARG;
        }
        return Send(message);
    }

    void Close() noexcept {
        activeSlots_ = 0;
        std::scoped_lock lock(mutex_);
        if (pipe_ == INVALID_HANDLE_VALUE) return;
        dfgt::ipc::Message message{};
        message.command = dfgt::ipc::Command::Close;
        SendLocked(message);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

    HRESULT Constant(
        std::int32_t magnitude,
        std::int32_t periodicMagnitude = 0) noexcept {
        dfgt::ipc::Message message{};
        message.command = dfgt::ipc::Command::Constant;
        message.value = std::clamp(magnitude, -10000, 10000);
        message.auxiliary =
            std::clamp(periodicMagnitude, -10000, 10000);
        const auto result = Send(message);
        if (SUCCEEDED(result)) {
            SetSlot(kSoftwareSlot,
                magnitude != 0 || periodicMagnitude != 0);
        }
        heartbeatWake_.notify_all();
        return result;
    }

    HRESULT StopSlotOne() noexcept {
        SetSlot(kSoftwareSlot, false);
        dfgt::ipc::Message message{};
        message.command = dfgt::ipc::Command::StopSoftwareSlot;
        return Send(message);
    }

    HRESULT StopAll() noexcept {
        activeSlots_ = 0;
        dfgt::ipc::Message message{};
        message.command = dfgt::ipc::Command::StopAll;
        return Send(message);
    }

    HRESULT Condition(
        dfgt::ipc::Command command,
        LONG negativeCoefficient,
        LONG positiveCoefficient,
        LONG center,
        DWORD deadBand,
        DWORD negativeSaturation,
        DWORD positiveSaturation) noexcept {
        if (command != dfgt::ipc::Command::Spring &&
            command != dfgt::ipc::Command::Damper &&
            command != dfgt::ipc::Command::Friction) {
            return E_INVALIDARG;
        }
        dfgt::ipc::Message message{};
        message.command = command;
        message.value = std::clamp<LONG>(negativeCoefficient, -10000, 10000);
        message.auxiliary = std::clamp<LONG>(positiveCoefficient, -10000, 10000);
        message.value3 = std::clamp<LONG>(center, -10000, 10000);
        message.value4 = static_cast<std::int32_t>(std::min<DWORD>(deadBand, 10000));
        message.value5 = static_cast<std::int32_t>(
            std::min<DWORD>(negativeSaturation, 10000));
        message.value6 = static_cast<std::int32_t>(
            std::min<DWORD>(positiveSaturation, 10000));
        const auto result = Send(message);
        if (SUCCEEDED(result)) {
            const auto slot = command == dfgt::ipc::Command::Spring
                ? kSpringSlot
                : command == dfgt::ipc::Command::Friction
                    ? kFrictionSlot
                    : kDamperSlot;
            SetSlot(slot, true);
        }
        heartbeatWake_.notify_all();
        return result;
    }

    void UpdateRenderEpoch(std::uint64_t epoch) noexcept {
        renderEpoch_.store(epoch, std::memory_order_release);
    }

private:
    static constexpr std::uint32_t kSoftwareSlot = 1u << 0;
    static constexpr std::uint32_t kSpringSlot = 1u << 1;
    static constexpr std::uint32_t kDamperSlot = 1u << 2;
    static constexpr std::uint32_t kFrictionSlot = 1u << 3;

    void SetSlot(std::uint32_t slot, bool active) noexcept {
        if (active) {
            activeSlots_.fetch_or(slot);
        } else {
            activeSlots_.fetch_and(~slot);
        }
    }

    HRESULT Send(dfgt::ipc::Message& message) noexcept {
        std::scoped_lock lock(mutex_);
        return SendLocked(message);
    }

    HRESULT SendLocked(dfgt::ipc::Message& message) noexcept {
        if (pipe_ == INVALID_HANDLE_VALUE) return DIERR_NOTINITIALIZED;
        message.sequence = ++sequence_;
        DWORD written = 0;
        if (!WriteFile(pipe_, &message, sizeof(message), &written, nullptr) ||
            written != sizeof(message)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        dfgt::ipc::Response response{};
        DWORD read = 0;
        if (!ReadFile(pipe_, &response, sizeof(response), &read, nullptr) ||
            read != sizeof(response)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (response.magic != dfgt::ipc::Magic ||
            response.version != dfgt::ipc::Version ||
            response.size != sizeof(response) ||
            response.sequence != message.sequence) {
            return E_FAIL;
        }
        return response.result;
    }

    void HeartbeatLoop(std::stop_token stop) noexcept {
        std::mutex waitMutex;
        std::unique_lock waitLock(waitMutex);
        while (!stop.stop_requested()) {
            heartbeatWake_.wait_for(waitLock, std::chrono::milliseconds(100));
            if (stop.stop_requested() || activeSlots_.load() == 0) continue;
            dfgt::ipc::Message message{};
            message.command = dfgt::ipc::Command::Heartbeat;
            message.renderEpoch =
                renderEpoch_.load(std::memory_order_acquire);
            if (FAILED(Send(message))) activeSlots_ = 0;
        }
    }

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::mutex mutex_;
    std::atomic<std::uint32_t> activeSlots_{0};
    std::atomic<std::uint64_t> renderEpoch_{0};
    std::uint32_t sequence_{};
    std::condition_variable heartbeatWake_;
    std::jthread heartbeat_;
};

enum class EffectKind {
    Constant,
    Ramp,
    Square,
    Sine,
    Triangle,
    SawtoothUp,
    SawtoothDown,
    Spring,
    Damper,
    Friction,
    Inertia,
    Custom,
};

struct EffectState {
    EffectKind kind{EffectKind::Constant};
    LONG magnitude{};
    LONG offset{};
    LONG startMagnitude{};
    LONG endMagnitude{};
    DWORD phase{};
    DWORD periodUs{100000};
    DWORD gain{10000};
    DWORD durationUs{INFINITE};
    DWORD startDelayUs{};
    DIENVELOPE envelope{};
    bool hasEnvelope{};
    LONG direction{1};
    DICONDITION condition{};
    std::vector<LONG> customSamples;
    DWORD customSamplePeriodUs{};
    std::chrono::steady_clock::time_point startsAt{};
    std::chrono::steady_clock::time_point deadline{};
    bool hasDeadline{};
    bool conditionStarted{};
    bool playing{};
};

class EffectDriver final : public IDirectInputEffectDriver {
public:
    EffectDriver()
        : timer_([this](std::stop_token stop) { TimerLoop(stop); }) {
        ++g_objects;
    }
    ~EffectDriver() {
        timer_.request_stop();
        timerWake_.notify_all();
        if (timer_.joinable()) timer_.join();
        transport_.StopAll();
        transport_.Close();
        --g_objects;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDirectInputEffectDriver) {
            *object = static_cast<IDirectInputEffectDriver*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++references_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }

    HRESULT STDMETHODCALLTYPE DeviceID(
        DWORD,
        DWORD externalId,
        DWORD begin,
        DWORD,
        void* initInfo) override {
        LogDiagnostic(
            "DeviceID begin=%lu externalId=%lu",
            static_cast<unsigned long>(begin),
            static_cast<unsigned long>(externalId));
        std::scoped_lock lock(stateMutex_);
        if (begin == 0) {
            transport_.StopAll();
            lastRendered_ = 0;
            transport_.Close();
            effects_.clear();
            externalId_ = 0;
            return S_OK;
        }
        if (initInfo == nullptr) return E_INVALIDARG;
        const auto* info = static_cast<const DIHIDFFINITINFO*>(initInfo);
        if (info->dwSize < sizeof(DIHIDFFINITINFO) || info->pwszDeviceInterface == nullptr) {
            return E_INVALIDARG;
        }
        const auto result = transport_.Open(info->pwszDeviceInterface);
        if (FAILED(result)) return result;
        externalId_ = externalId;
        actuatorsEnabled_ = true;
        paused_ = false;
        return transport_.StopAll();
    }

    HRESULT STDMETHODCALLTYPE GetVersions(DIDRIVERVERSIONS* versions) override {
        if (versions == nullptr || versions->dwSize < sizeof(DIDRIVERVERSIONS)) {
            return E_INVALIDARG;
        }
        versions->dwFirmwareRevision = kFirmwareRevision;
        versions->dwHardwareRevision = kFirmwareRevision;
        versions->dwFFDriverVersion = kDriverVersion;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Escape(DWORD, DWORD, DIEFFESCAPE*) override {
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetGain(DWORD externalId, DWORD gain) override {
        LogDiagnostic(
            "SetGain externalId=%lu gain=%lu",
            static_cast<unsigned long>(externalId),
            static_cast<unsigned long>(gain));
        if (!Matches(externalId) || gain > 10000) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        globalGain_ = gain;
        return RenderEverythingLocked(std::chrono::steady_clock::now());
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD externalId, DWORD command) override {
        LogDiagnostic(
            "SendForceFeedbackCommand externalId=%lu command=%lu",
            static_cast<unsigned long>(externalId),
            static_cast<unsigned long>(command));
        if (!Matches(externalId)) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        switch (command) {
        case DISFFC_RESET:
            effects_.clear();
            paused_ = false;
            lastRendered_ = 0;
            return transport_.StopAll();
        case DISFFC_STOPALL:
            for (auto& [_, effect] : effects_) {
                effect.playing = false;
                effect.conditionStarted = false;
            }
            paused_ = false;
            lastRendered_ = 0;
            return transport_.StopAll();
        case DISFFC_PAUSE:
            paused_ = true;
            lastRendered_ = 0;
            return transport_.StopAll();
        case DISFFC_CONTINUE:
            paused_ = false;
            return RenderEverythingLocked(std::chrono::steady_clock::now());
        case DISFFC_SETACTUATORSON:
            actuatorsEnabled_ = true;
            return RenderEverythingLocked(std::chrono::steady_clock::now());
        case DISFFC_SETACTUATORSOFF:
            actuatorsEnabled_ = false;
            lastRendered_ = 0;
            return transport_.StopAll();
        default:
            return E_INVALIDARG;
        }
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(
        DWORD externalId,
        DIDEVICESTATE* state) override {
        if (!Matches(externalId) || state == nullptr || state->dwSize < sizeof(DIDEVICESTATE)) {
            return E_INVALIDARG;
        }
        std::scoped_lock lock(stateMutex_);
        state->dwState = 0;
        if (paused_) state->dwState |= DIGFFS_PAUSED;
        if (actuatorsEnabled_) state->dwState |= DIGFFS_ACTUATORSON;
        else state->dwState |= DIGFFS_ACTUATORSOFF;
        state->dwState |= DIGFFS_POWERON | DIGFFS_SAFETYSWITCHOFF | DIGFFS_USERFFSWITCHON;
        state->dwLoad = std::min<DWORD>(100, static_cast<DWORD>(effects_.size()) * 6);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DownloadEffect(
        DWORD externalId,
        DWORD effectId,
        DWORD* handle,
        const DIEFFECT* effect,
        DWORD flags) override {
        if (!Matches(externalId) || handle == nullptr || effect == nullptr) return E_INVALIDARG;
        LogDiagnostic(
            "DownloadEffect externalId=%lu effectId=%lu flags=%lu handle=%lu",
            static_cast<unsigned long>(externalId),
            static_cast<unsigned long>(effectId),
            static_cast<unsigned long>(flags),
            static_cast<unsigned long>(*handle));
        EffectState candidate{};
        const auto parseResult = ParseEffect(effectId, *effect, candidate);
        LogDiagnostic(
            "DownloadEffect parse=0x%08lX magnitude=%ld gain=%lu duration=%lu",
            static_cast<unsigned long>(parseResult),
            static_cast<long>(candidate.magnitude),
            static_cast<unsigned long>(candidate.gain),
            static_cast<unsigned long>(candidate.durationUs));
        if (FAILED(parseResult)) return parseResult;
        if ((flags & DIEP_NODOWNLOAD) != 0) return S_OK;

        std::scoped_lock lock(stateMutex_);
        if (*handle == kInvalidEffectHandle) {
            if (effects_.size() >= kMaximumEffects) return DIERR_DEVICEFULL;
            while (nextHandle_ == kInvalidEffectHandle ||
                   effects_.contains(nextHandle_)) {
                ++nextHandle_;
            }
            *handle = nextHandle_++;
            effects_.emplace(*handle, EffectState{});
        } else if (!effects_.contains(*handle)) {
            return DIERR_NOTDOWNLOADED;
        }
        auto& stored = effects_.at(*handle);
        candidate.playing = stored.playing;
        candidate.hasDeadline = stored.hasDeadline;
        candidate.startsAt = stored.startsAt;
        candidate.deadline = stored.deadline;
        candidate.conditionStarted = stored.conditionStarted;
        stored = candidate;
        if ((flags & DIEP_START) != 0) {
            stored.playing = true;
            ArmDeadline(stored, 1);
            timerWake_.notify_all();
            return RenderEverythingLocked(std::chrono::steady_clock::now());
        }
        if (stored.playing) {
            return RenderEverythingLocked(std::chrono::steady_clock::now());
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DestroyEffect(DWORD externalId, DWORD handle) override {
        if (!Matches(externalId) || handle == kInvalidEffectHandle) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return DIERR_NOTDOWNLOADED;
        const bool wasPlaying = found->second.playing;
        effects_.erase(found);
        return wasPlaying
            ? RenderEverythingLocked(std::chrono::steady_clock::now())
            : S_OK;
    }

    HRESULT STDMETHODCALLTYPE StartEffect(
        DWORD externalId,
        DWORD handle,
        DWORD mode,
        DWORD iterations) override {
        LogDiagnostic(
            "StartEffect externalId=%lu handle=%lu mode=%lu iterations=%lu",
            static_cast<unsigned long>(externalId),
            static_cast<unsigned long>(handle),
            static_cast<unsigned long>(mode),
            static_cast<unsigned long>(iterations));
        if (!Matches(externalId)) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return DIERR_NOTDOWNLOADED;
        if (iterations == 0) return E_INVALIDARG;
        found->second.playing = true;
        ArmDeadline(found->second, iterations);
        timerWake_.notify_all();
        return RenderEverythingLocked(std::chrono::steady_clock::now());
    }

    HRESULT STDMETHODCALLTYPE StopEffect(DWORD externalId, DWORD handle) override {
        LogDiagnostic(
            "StopEffect externalId=%lu handle=%lu",
            static_cast<unsigned long>(externalId),
            static_cast<unsigned long>(handle));
        if (!Matches(externalId)) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return DIERR_NOTDOWNLOADED;
        found->second.playing = false;
        found->second.hasDeadline = false;
        found->second.conditionStarted = false;
        return RenderEverythingLocked(std::chrono::steady_clock::now());
    }

    HRESULT STDMETHODCALLTYPE GetEffectStatus(
        DWORD externalId,
        DWORD handle,
        DWORD* status) override {
        if (!Matches(externalId) || status == nullptr) return E_INVALIDARG;
        std::scoped_lock lock(stateMutex_);
        const auto found = effects_.find(handle);
        if (found == effects_.end()) return DIERR_NOTDOWNLOADED;
        *status = found->second.playing && !paused_ && actuatorsEnabled_
            ? DIEGES_PLAYING
            : 0;
        return S_OK;
    }

private:
    static HRESULT ParseEffect(
        DWORD effectId,
        const DIEFFECT& source,
        EffectState& target) noexcept {
        if (source.dwSize < sizeof(DIEFFECT) ||
            source.dwGain > 10000 ||
            source.lpvTypeSpecificParams == nullptr ||
            source.cAxes != 1 ||
            source.rgdwAxes == nullptr ||
            source.rglDirection == nullptr ||
            (source.dwFlags & DIEFF_CARTESIAN) == 0) {
            return DIERR_INVALIDPARAM;
        }

        target.gain = source.dwGain;
        target.durationUs = source.dwDuration;
        target.startDelayUs = source.dwStartDelay;
        target.direction = source.rglDirection[0] < 0 ? -1 : 1;
        if (source.lpEnvelope != nullptr) {
            if (source.lpEnvelope->dwSize < sizeof(DIENVELOPE) ||
                source.lpEnvelope->dwAttackLevel > 10000 ||
                source.lpEnvelope->dwFadeLevel > 10000) {
                return DIERR_INVALIDPARAM;
            }
            target.envelope = *source.lpEnvelope;
            target.hasEnvelope = true;
        }

        switch (effectId) {
        case kConstantEffectId: {
            if (source.cbTypeSpecificParams != sizeof(DICONSTANTFORCE)) {
                return DIERR_INVALIDPARAM;
            }
            const auto& parameters =
                *static_cast<const DICONSTANTFORCE*>(source.lpvTypeSpecificParams);
            if (parameters.lMagnitude < -10000 || parameters.lMagnitude > 10000) {
                return DIERR_INVALIDPARAM;
            }
            target.kind = EffectKind::Constant;
            target.magnitude = parameters.lMagnitude;
            return S_OK;
        }
        case kRampEffectId: {
            if (source.cbTypeSpecificParams != sizeof(DIRAMPFORCE)) {
                return DIERR_INVALIDPARAM;
            }
            const auto& parameters =
                *static_cast<const DIRAMPFORCE*>(source.lpvTypeSpecificParams);
            if (parameters.lStart < -10000 || parameters.lStart > 10000 ||
                parameters.lEnd < -10000 || parameters.lEnd > 10000) {
                return DIERR_INVALIDPARAM;
            }
            target.kind = EffectKind::Ramp;
            target.startMagnitude = parameters.lStart;
            target.endMagnitude = parameters.lEnd;
            return S_OK;
        }
        case kSquareEffectId:
        case kSineEffectId:
        case kTriangleEffectId:
        case kSawtoothUpEffectId:
        case kSawtoothDownEffectId: {
            if (source.cbTypeSpecificParams != sizeof(DIPERIODIC)) {
                return DIERR_INVALIDPARAM;
            }
            const auto& parameters =
                *static_cast<const DIPERIODIC*>(source.lpvTypeSpecificParams);
            if (parameters.dwMagnitude > 10000 ||
                parameters.lOffset < -10000 ||
                parameters.lOffset > 10000 ||
                parameters.dwPhase >= 36000 ||
                parameters.dwPeriod == 0) {
                return DIERR_INVALIDPARAM;
            }
            target.kind = effectId == kSquareEffectId ? EffectKind::Square
                : effectId == kSineEffectId ? EffectKind::Sine
                : effectId == kTriangleEffectId ? EffectKind::Triangle
                : effectId == kSawtoothUpEffectId ? EffectKind::SawtoothUp
                : EffectKind::SawtoothDown;
            target.magnitude = static_cast<LONG>(parameters.dwMagnitude);
            target.offset = parameters.lOffset;
            target.phase = parameters.dwPhase;
            target.periodUs = parameters.dwPeriod;
            return S_OK;
        }
        case kSpringEffectId:
        case kDamperEffectId:
        case kInertiaEffectId:
        case kFrictionEffectId: {
            if (source.cbTypeSpecificParams != sizeof(DICONDITION)) {
                return DIERR_INVALIDPARAM;
            }
            const auto& parameters =
                *static_cast<const DICONDITION*>(source.lpvTypeSpecificParams);
            if (parameters.lOffset < -10000 || parameters.lOffset > 10000 ||
                parameters.lPositiveCoefficient < -10000 ||
                parameters.lPositiveCoefficient > 10000 ||
                parameters.lNegativeCoefficient < -10000 ||
                parameters.lNegativeCoefficient > 10000 ||
                parameters.dwPositiveSaturation > 10000 ||
                parameters.dwNegativeSaturation > 10000 ||
                parameters.lDeadBand < 0 ||
                parameters.lDeadBand > 10000) {
                return DIERR_INVALIDPARAM;
            }
            target.kind = effectId == kSpringEffectId ? EffectKind::Spring
                : effectId == kDamperEffectId ? EffectKind::Damper
                : effectId == kInertiaEffectId ? EffectKind::Inertia
                : EffectKind::Friction;
            target.condition = parameters;
            return S_OK;
        }
        case kCustomForceEffectId: {
            if (source.cbTypeSpecificParams != sizeof(DICUSTOMFORCE)) {
                return DIERR_INVALIDPARAM;
            }
            const auto& parameters =
                *static_cast<const DICUSTOMFORCE*>(source.lpvTypeSpecificParams);
            constexpr DWORD kMaximumCustomSamples = 4096;
            if (parameters.cChannels != 1 ||
                parameters.dwSamplePeriod == 0 ||
                (source.dwSamplePeriod != 0 &&
                 source.dwSamplePeriod != parameters.dwSamplePeriod) ||
                parameters.cSamples == 0 ||
                parameters.cSamples > kMaximumCustomSamples ||
                parameters.rglForceData == nullptr) {
                return DIERR_INVALIDPARAM;
            }
            target.customSamples.assign(
                parameters.rglForceData,
                parameters.rglForceData + parameters.cSamples);
            if (std::ranges::any_of(
                    target.customSamples,
                    [](LONG sample) { return sample < -10000 || sample > 10000; })) {
                return DIERR_INVALIDPARAM;
            }
            target.kind = EffectKind::Custom;
            target.customSamplePeriodUs = parameters.dwSamplePeriod;
            return S_OK;
        }
        default:
            return DIERR_UNSUPPORTED;
        }
    }

    bool Matches(DWORD externalId) const noexcept {
        return externalId_ == externalId;
    }

    static std::int32_t EvaluateEffect(
        const EffectState& effect,
        std::chrono::steady_clock::time_point now) noexcept {
        if (now < effect.startsAt) return 0;
        const auto elapsedUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - effect.startsAt).count());
        const auto localUs = effect.durationUs != INFINITE && effect.durationUs != 0
            ? elapsedUs % effect.durationUs
            : elapsedUs;

        double value = 0;
        switch (effect.kind) {
        case EffectKind::Constant:
            value = effect.magnitude;
            break;
        case EffectKind::Ramp: {
            const auto denominator = effect.durationUs == 0 || effect.durationUs == INFINITE
                ? 1.0
                : static_cast<double>(effect.durationUs);
            const auto progress = std::clamp(localUs / denominator, 0.0, 1.0);
            value = effect.startMagnitude +
                (effect.endMagnitude - effect.startMagnitude) * progress;
            break;
        }
        case EffectKind::Square:
        case EffectKind::Sine:
        case EffectKind::Triangle:
        case EffectKind::SawtoothUp:
        case EffectKind::SawtoothDown: {
            const auto cycle = std::fmod(
                static_cast<double>(elapsedUs) / effect.periodUs +
                    static_cast<double>(effect.phase) / 36000.0,
                1.0);
            double wave = 0;
            if (effect.kind == EffectKind::Square) wave = cycle < 0.5 ? 1.0 : -1.0;
            else if (effect.kind == EffectKind::Sine) {
                wave = std::sin(cycle * 2.0 * std::numbers::pi);
            } else if (effect.kind == EffectKind::Triangle) {
                wave = 1.0 - 4.0 * std::abs(cycle - 0.5);
            } else if (effect.kind == EffectKind::SawtoothUp) {
                wave = 2.0 * cycle - 1.0;
            } else {
                wave = 1.0 - 2.0 * cycle;
            }
            value = effect.offset + effect.magnitude * wave;
            break;
        }
        case EffectKind::Spring:
        case EffectKind::Damper:
        case EffectKind::Friction:
        case EffectKind::Inertia:
            return 0;
        case EffectKind::Custom: {
            if (effect.customSamples.empty() || effect.customSamplePeriodUs == 0) {
                return 0;
            }
            const auto index =
                (elapsedUs / effect.customSamplePeriodUs) %
                effect.customSamples.size();
            value = effect.customSamples[static_cast<std::size_t>(index)];
            break;
        }
        }

        if (effect.hasEnvelope) {
            double envelopeGain = 1.0;
            if (effect.envelope.dwAttackTime > 0 &&
                localUs < effect.envelope.dwAttackTime) {
                const auto progress =
                    static_cast<double>(localUs) / effect.envelope.dwAttackTime;
                envelopeGain =
                    effect.envelope.dwAttackLevel / 10000.0 +
                    (1.0 - effect.envelope.dwAttackLevel / 10000.0) * progress;
            } else if (effect.durationUs != INFINITE &&
                       effect.envelope.dwFadeTime > 0 &&
                       localUs + effect.envelope.dwFadeTime >= effect.durationUs) {
                const auto remaining = effect.durationUs > localUs
                    ? effect.durationUs - localUs
                    : 0;
                const auto progress =
                    static_cast<double>(remaining) / effect.envelope.dwFadeTime;
                envelopeGain =
                    effect.envelope.dwFadeLevel / 10000.0 +
                    (1.0 - effect.envelope.dwFadeLevel / 10000.0) * progress;
            }
            value *= std::clamp(envelopeGain, 0.0, 1.0);
        }

        value *= effect.direction;
        value *= effect.gain / 10000.0;
        return std::clamp(
            static_cast<std::int32_t>(std::lround(value)),
            -10000,
            10000);
    }

    HRESULT RenderAllLocked(std::chrono::steady_clock::time_point now) noexcept {
        if (paused_ || !actuatorsEnabled_) return S_OK;
        std::int64_t sum = 0;
        std::int64_t periodicSum = 0;
        for (auto& [_, effect] : effects_) {
            if (!effect.playing) continue;
            const auto value = EvaluateEffect(effect, now);
            if (effect.kind == EffectKind::Square ||
                effect.kind == EffectKind::Sine ||
                effect.kind == EffectKind::Triangle ||
                effect.kind == EffectKind::SawtoothUp ||
                effect.kind == EffectKind::SawtoothDown) {
                periodicSum += value;
            } else {
                sum += value;
            }
        }
        const auto scaled = std::clamp<std::int64_t>(
            sum * globalGain_ / 10000,
            -10000,
            10000);
        const auto periodicScaled = std::clamp<std::int64_t>(
            periodicSum * globalGain_ / 10000,
            -10000,
            10000);
        const auto rendered = static_cast<std::int32_t>(scaled);
        const auto periodicRendered =
            static_cast<std::int32_t>(periodicScaled);
        if (rendered == lastRendered_ &&
            periodicRendered == lastPeriodicRendered_) return S_OK;
        const auto result =
            transport_.Constant(rendered, periodicRendered);
        LogDiagnostic(
            "RenderConstant value=%ld periodic=%ld result=0x%08lX",
            static_cast<long>(rendered),
            static_cast<long>(periodicRendered),
            static_cast<unsigned long>(result));
        if (SUCCEEDED(result)) {
            lastRendered_ = rendered;
            lastPeriodicRendered_ = periodicRendered;
        }
        return result;
    }

    HRESULT RenderConditionsLocked(
        std::chrono::steady_clock::time_point now) noexcept {
        if (paused_ || !actuatorsEnabled_) return S_OK;
        for (auto& [_, effect] : effects_) {
            if (!effect.playing || now < effect.startsAt) continue;
            dfgt::ipc::Command command{};
            if (effect.kind == EffectKind::Spring) command = dfgt::ipc::Command::Spring;
            else if (effect.kind == EffectKind::Damper) command = dfgt::ipc::Command::Damper;
            else if (effect.kind == EffectKind::Inertia) {
                // The original Windows Logitech provider and new-lg4ff map
                // unsupported native inertia to the wheel's damper slot.
                command = dfgt::ipc::Command::Damper;
            }
            else if (effect.kind == EffectKind::Friction) {
                command = dfgt::ipc::Command::Friction;
            } else {
                continue;
            }

            const auto scaleSigned = [&](LONG value) {
                return static_cast<LONG>(std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(value) *
                        effect.gain * globalGain_ /
                        10000 / 10000,
                    -10000,
                    10000));
            };
            const auto scaleUnsigned = [&](DWORD value) {
                return static_cast<DWORD>(std::clamp<std::uint64_t>(
                    static_cast<std::uint64_t>(value) *
                        effect.gain * globalGain_ /
                        10000 / 10000,
                    0,
                    10000));
            };
            const auto result = transport_.Condition(
                command,
                scaleSigned(effect.condition.lNegativeCoefficient),
                scaleSigned(effect.condition.lPositiveCoefficient),
                effect.condition.lOffset,
                static_cast<DWORD>(effect.condition.lDeadBand),
                scaleUnsigned(effect.condition.dwNegativeSaturation),
                scaleUnsigned(effect.condition.dwPositiveSaturation));
            if (FAILED(result)) return result;
            effect.conditionStarted = true;
        }
        return S_OK;
    }

    HRESULT RenderEverythingLocked(
        std::chrono::steady_clock::time_point now) noexcept {
        const auto stopResult = transport_.StopAll();
        lastRendered_ = 0;
        lastPeriodicRendered_ = 0;
        if (FAILED(stopResult)) return stopResult;
        for (auto& [_, effect] : effects_) effect.conditionStarted = false;
        const auto softwareResult = RenderAllLocked(now);
        if (FAILED(softwareResult)) return softwareResult;
        return RenderConditionsLocked(now);
    }

    static void ArmDeadline(EffectState& effect, DWORD iterations) noexcept {
        effect.startsAt = std::chrono::steady_clock::now() +
            std::chrono::microseconds(effect.startDelayUs);
        effect.conditionStarted = false;
        effect.hasDeadline = effect.durationUs != INFINITE;
        if (!effect.hasDeadline) return;
        const auto boundedIterations = iterations == INFINITE
            ? static_cast<std::uint64_t>(UINT32_MAX)
            : static_cast<std::uint64_t>(iterations);
        const auto totalUs = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(effect.durationUs) * boundedIterations,
            static_cast<std::uint64_t>(std::chrono::microseconds::max().count()));
        effect.deadline = effect.startsAt +
            std::chrono::microseconds(totalUs);
    }

    void TimerLoop(std::stop_token stop) noexcept {
        std::mutex waitMutex;
        std::unique_lock waitLock(waitMutex);
        while (!stop.stop_requested()) {
            timerWake_.wait_for(waitLock, std::chrono::milliseconds(2));
            if (stop.stop_requested()) break;

            {
                std::scoped_lock lock(stateMutex_);
                const auto now = std::chrono::steady_clock::now();
                bool rerenderConditions = false;
                for (auto& [_, effect] : effects_) {
                    if (!effect.playing) continue;
                    if (effect.hasDeadline && now >= effect.deadline) {
                        rerenderConditions = rerenderConditions ||
                            effect.kind == EffectKind::Spring ||
                            effect.kind == EffectKind::Damper ||
                            effect.kind == EffectKind::Inertia ||
                            effect.kind == EffectKind::Friction;
                        effect.playing = false;
                        effect.hasDeadline = false;
                        effect.conditionStarted = false;
                    } else if (!effect.conditionStarted &&
                               now >= effect.startsAt &&
                               (effect.kind == EffectKind::Spring ||
                                effect.kind == EffectKind::Damper ||
                                effect.kind == EffectKind::Inertia ||
                                effect.kind == EffectKind::Friction)) {
                        rerenderConditions = true;
                    }
                }
                if (rerenderConditions) RenderEverythingLocked(now);
                else RenderAllLocked(now);
                transport_.UpdateRenderEpoch(++renderEpoch_);
            }
        }
    }

    std::atomic<long> references_{1};
    BrokerTransport transport_;
    std::mutex stateMutex_;
    std::unordered_map<DWORD, EffectState> effects_;
    std::atomic<DWORD> externalId_{};
    DWORD nextHandle_{kFirstEffectHandle};
    DWORD globalGain_{10000};
    bool actuatorsEnabled_{true};
    bool paused_{};
    std::int32_t lastRendered_{};
    std::int32_t lastPeriodicRendered_{};
    std::uint64_t renderEpoch_{};
    std::condition_variable timerWake_;
    std::jthread timer_;
};

class ClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++references_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown* outer,
        REFIID iid,
        void** object) override {
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        auto* driver = new (std::nothrow) EffectDriver();
        if (driver == nullptr) return E_OUTOFMEMORY;
        const auto result = driver->QueryInterface(iid, object);
        driver->Release();
        return result;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++g_locks;
        else --g_locks;
        return S_OK;
    }

private:
    std::atomic<long> references_{1};
};

} // namespace

STDAPI DllGetClassObject(
    REFCLSID clsid,
    REFIID iid,
    void** object) {
    if (!IsEqualCLSID(clsid, CLSID_DfgtDirectInputFfb)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) return E_OUTOFMEMORY;
    const auto result = factory->QueryInterface(iid, object);
    factory->Release();
    return result;
}

STDAPI DllCanUnloadNow() {
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}
