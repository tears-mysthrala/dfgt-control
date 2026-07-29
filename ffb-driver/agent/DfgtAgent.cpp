#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <hidsdi.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <winrt/base.h>

#include "ControlClient.h"
#include "DeviceDiscovery.h"
#include "ProfileStore.h"

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT_PTR kForegroundTimer = 1;
constexpr UINT_PTR kMaintenanceTimer = 2;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuEmergencyStop = 1002;
constexpr UINT kMenuExit = 1003;
constexpr UINT kMenuRearm = 1004;
constexpr wchar_t kWindowClass[] = L"DfgtControl.Agent.Window";

std::wstring ModuleDirectory() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path().wstring();
}

std::wstring ForegroundExecutable() {
    const auto window = GetForegroundWindow();
    if (window == nullptr) return {};
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) return {};
    const auto process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId);
    if (process == nullptr) return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const auto ok = QueryFullProcessImageNameW(
        process,
        0,
        path.data(),
        &size);
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size);
    std::ranges::transform(
        path,
        path.begin(),
        [](wchar_t value) {
            return static_cast<wchar_t>(towlower(value));
        });
    return path;
}

class AgentApp final {
public:
    int Run(HINSTANCE instance) {
        instance_ = instance;
        if (!InitialiseWindow()) return 10;
        RegisterForDeviceChanges();
        if (!InitialiseControl()) {
            UpdateTray(L"DFGT Control · broker no disponible");
        }
        hook_ = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            &AgentApp::ForegroundHook,
            0,
            0,
                WINEVENT_OUTOFCONTEXT |
                WINEVENT_SKIPOWNPROCESS);
        SetTimer(window_, kMaintenanceTimer, 2000, nullptr);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Shutdown();
        return static_cast<int>(message.wParam);
    }

private:
    bool InitialiseWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = instance_;
        windowClass.lpfnWndProc = &AgentApp::WindowProcedure;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(
            0,
            kWindowClass,
            L"DFGT Control Agent",
            0,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) return false;

        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = window_;
        tray_.uID = 1;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP |
            NIF_SHOWTIP;
        tray_.uCallbackMessage = kTrayMessage;
        tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(tray_.szTip, L"DFGT Control · iniciando");
        if (Shell_NotifyIconW(NIM_ADD, &tray_) != TRUE) {
            return false;
        }
        tray_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray_);
        return true;
    }

    bool InitialiseControl() {
        const auto loadResult = ReloadProfiles();
        if (loadResult != dfgt::agent::ProfileLoadResult::Loaded) {
            if (loadResult == dfgt::agent::ProfileLoadResult::InvalidJson ||
                loadResult == dfgt::agent::ProfileLoadResult::InvalidData) {
                store_.QuarantineInvalid();
            } else if (
                loadResult != dfgt::agent::ProfileLoadResult::Missing) {
                UpdateTray(
                    L"DFGT Control · perfiles no disponibles; "
                    L"fichero preservado");
                return false;
            }
            profiles_.push_back(
                dfgt::agent::ProfileStore::DesktopProfile());
            if (!store_.Save(profiles_)) {
                UpdateTray(
                    L"DFGT Control · no se pudieron crear los perfiles");
                return false;
            }
        }
        return ConnectDevice();
    }

    dfgt::agent::ProfileLoadResult ReloadProfiles() {
        std::vector<dfgt::agent::ProfileModel> loaded;
        const auto result = store_.Load(loaded);
        if (result != dfgt::agent::ProfileLoadResult::Loaded ||
            loaded.empty()) {
            return result;
        }
        profiles_ = std::move(loaded);
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(
                store_.Path().c_str(),
                GetFileExInfoStandard,
                &attributes)) {
            profileWriteTime_ = attributes.ftLastWriteTime;
        }
        return dfgt::agent::ProfileLoadResult::Loaded;
    }

    bool ConnectDevice() {
        if (GetTickCount64() < nextAttachAttempt_) return false;
        if (FAILED(EnsureBroker())) return false;
        const auto path = dfgt::agent::FindDfgtDevicePath();
        if (!path || FAILED(control_.AttachDevice(*path))) {
            UpdateTray(L"DFGT Control · volante desconectado");
            nextAttachAttempt_ = GetTickCount64() + 2000;
            return true;
        }
        nextAttachAttempt_ = 0;
        brokerBackoffMs_ = 500;
        ApplyProfile(profiles_.front());
        return true;
    }

    void MaintainConnectionAndProfiles() {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(
                store_.Path().c_str(),
                GetFileExInfoStandard,
                &attributes) &&
            CompareFileTime(
                &attributes.ftLastWriteTime,
                &profileWriteTime_) != 0 &&
            ReloadProfiles() ==
                dfgt::agent::ProfileLoadResult::Loaded) {
            activeProfile_.clear();
            ApplyForegroundProfile();
        }

        dfgt::ipc::control::Status status{};
        if (FAILED(control_.GetStatus(status))) {
            control_.Close();
            activeProfile_.clear();
            ConnectDevice();
        } else if (
            status.state ==
                dfgt::ipc::control::DeviceState::Disconnected ||
            status.state ==
                dfgt::ipc::control::DeviceState::Faulted) {
            activeProfile_.clear();
            ConnectDevice();
        } else if (
            status.safetyMode ==
                static_cast<std::uint32_t>(
                    dfgt::ipc::SafetyMode::EmergencyLatched)) {
            UpdateTray(L"DFGT Control · PARADA ENCLAVADA");
        } else if (
            status.safetyMode ==
                static_cast<std::uint32_t>(
                    dfgt::ipc::SafetyMode::StopUnconfirmed)) {
            UpdateTray(
                L"DFGT Control · PARADA NO CONFIRMADA: "
                L"DESCONECTA EL VOLANTE");
        }
        if (hook_ == nullptr) {
            ApplyForegroundProfile();
        }
    }

    HRESULT EnsureBroker() {
        auto result = control_.Connect();
        if (SUCCEEDED(result)) {
            brokerBackoffMs_ = 500;
            nextBrokerAttempt_ = 0;
            return result;
        }
        const auto now = GetTickCount64();
        if (now < nextBrokerAttempt_) return result;
        nextBrokerAttempt_ = now + brokerBackoffMs_;
        brokerBackoffMs_ = std::min<DWORD>(
            brokerBackoffMs_ * 2, 30000);
        const auto broker =
            ModuleDirectory() + L"\\DfgtFfbBroker.exe";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + broker + L"\"";
        if (!CreateProcessW(
                broker.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &process)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        const auto deadline = GetTickCount64() + 3000;
        do {
            Sleep(100);
            result = control_.Connect();
            if (SUCCEEDED(result)) {
                brokerBackoffMs_ = 500;
                nextBrokerAttempt_ = 0;
                return result;
            }
        } while (GetTickCount64() < deadline);
        return result;
    }

    void ApplyProfile(
        const dfgt::agent::ProfileModel& profile) {
        const auto result =
            control_.ApplyProfile(profile.hardware);
        if (FAILED(result)) {
            UpdateTray(L"DFGT Control · error al aplicar perfil");
            return;
        }
        activeProfile_ = profile.hardware.id;
        UpdateTray(
            L"DFGT Control · " + profile.name);
    }

    void ApplyForegroundProfile() {
        const auto executable = ForegroundExecutable();
        const auto match = std::ranges::find_if(
            profiles_,
            [&executable](const auto& profile) {
                if (profile.executablePath.empty()) return false;
                auto configured = profile.executablePath;
                std::ranges::transform(
                    configured,
                    configured.begin(),
                    [](wchar_t value) {
                        return static_cast<wchar_t>(towlower(value));
                    });
                return configured == executable;
            });
        const auto& profile = match == profiles_.end()
            ? profiles_.front()
            : *match;
        if (activeProfile_ != profile.hardware.id) {
            ApplyProfile(profile);
        }
    }

    void OpenUserInterface() {
        const auto application =
            ModuleDirectory() + L"\\DfgtControl.exe";
        ShellExecuteW(
            window_,
            L"open",
            application.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
    }

    void ShowMenu(POINT point) {
        const auto menu = CreatePopupMenu();
        if (menu == nullptr) return;
        AppendMenuW(menu, MF_STRING, kMenuOpen, L"Abrir DFGT Control");
        AppendMenuW(
            menu,
            MF_STRING,
            kMenuEmergencyStop,
            L"Parada de emergencia");
        AppendMenuW(
            menu,
            MF_STRING,
            kMenuRearm,
            L"Confirmar y rearmar");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Salir");
        SetForegroundWindow(window_);
        TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON,
            point.x,
            point.y,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);
    }

    void UpdateTray(const std::wstring& text) {
        wcsncpy_s(
            tray_.szTip,
            text.c_str(),
            _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &tray_);
    }

    void RegisterForDeviceChanges() {
        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = hidGuid;
        deviceNotification_ = RegisterDeviceNotificationW(
            window_,
            &filter,
            DEVICE_NOTIFY_WINDOW_HANDLE);
    }

    void Shutdown() {
        if (deviceNotification_ != nullptr) {
            UnregisterDeviceNotification(deviceNotification_);
            deviceNotification_ = nullptr;
        }
        if (hook_ != nullptr) UnhookWinEvent(hook_);
        control_.DetachDevice();
        control_.Close();
        if (tray_.hWnd != nullptr) {
            Shell_NotifyIconW(NIM_DELETE, &tray_);
        }
    }

    LRESULT HandleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        switch (message) {
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVICEARRIVAL ||
                wParam == DBT_DEVICEREMOVECOMPLETE) {
                nextAttachAttempt_ = 0;
                activeProfile_.clear();
                ConnectDevice();
            }
            return TRUE;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) {
                control_.DetachDevice();
                activeProfile_.clear();
            } else if (
                wParam == PBT_APMRESUMEAUTOMATIC ||
                wParam == PBT_APMRESUMESUSPEND) {
                nextAttachAttempt_ = 0;
                ConnectDevice();
            }
            return TRUE;
        case WM_QUERYENDSESSION:
            control_.DetachDevice();
            return TRUE;
        case WM_ENDSESSION:
            if (wParam != FALSE) {
                control_.DetachDevice();
                control_.Close();
            }
            return 0;
        case kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                OpenUserInterface();
            } else if (
                LOWORD(lParam) == WM_RBUTTONUP ||
                LOWORD(lParam) == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                ShowMenu(point);
            }
            return 0;
        case kForegroundChanged:
            KillTimer(window_, kForegroundTimer);
            SetTimer(window_, kForegroundTimer, 500, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == kForegroundTimer) {
                KillTimer(window_, kForegroundTimer);
                ApplyForegroundProfile();
            } else if (wParam == kMaintenanceTimer) {
                MaintainConnectionAndProfiles();
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case kMenuOpen:
                OpenUserInterface();
                break;
            case kMenuEmergencyStop:
                if (SUCCEEDED(control_.EmergencyStop())) {
                    UpdateTray(
                        L"DFGT Control · PARADA ENCLAVADA");
                } else {
                    UpdateTray(
                        L"DFGT Control · PARADA NO CONFIRMADA: "
                        L"DESCONECTA EL VOLANTE");
                }
                break;
            case kMenuRearm:
                if (MessageBoxW(
                        window_,
                        L"Confirma que el volante está libre y que deseas "
                        L"rearmar el force feedback.",
                        L"Rearmar DFGT",
                        MB_ICONWARNING | MB_OKCANCEL |
                            MB_DEFBUTTON2) == IDOK) {
                    const auto result =
                        control_.AcknowledgeAndRearm();
                    UpdateTray(SUCCEEDED(result)
                        ? L"DFGT Control · rearmado"
                        : L"DFGT Control · rearme rechazado");
                }
                break;
            case kMenuExit:
                PostQuitMessage(0);
                break;
            default:
                break;
            }
            return 0;
        default:
            return DefWindowProcW(
                window_,
                message,
                wParam,
                lParam);
        }
    }

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        AgentApp* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AgentApp*>(
                create->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<AgentApp*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return app != nullptr
            ? app->HandleMessage(message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    static void CALLBACK ForegroundHook(
        HWINEVENTHOOK,
        DWORD,
        HWND,
        LONG,
        LONG,
        DWORD,
        DWORD) {
        if (instanceApp_ != nullptr &&
            instanceApp_->window_ != nullptr) {
            PostMessageW(
                instanceApp_->window_,
                kForegroundChanged,
                0,
                0);
        }
    }

public:
    AgentApp() {
        instanceApp_ = this;
    }

    ~AgentApp() {
        if (instanceApp_ == this) instanceApp_ = nullptr;
    }

private:
    inline static AgentApp* instanceApp_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    HWINEVENTHOOK hook_{nullptr};
    HDEVNOTIFY deviceNotification_{nullptr};
    NOTIFYICONDATAW tray_{};
    dfgt::agent::ControlClient control_;
    dfgt::agent::ProfileStore store_;
    std::vector<dfgt::agent::ProfileModel> profiles_;
    std::wstring activeProfile_;
    FILETIME profileWriteTime_{};
    ULONGLONG nextBrokerAttempt_{};
    ULONGLONG nextAttachAttempt_{};
    DWORD brokerBackoffMs_{500};
};

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {
    const auto mutex = CreateMutexW(
        nullptr,
        TRUE,
        L"Local\\DfgtControl.Agent");
    if (mutex == nullptr ||
        GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex != nullptr) CloseHandle(mutex);
        return 0;
    }
    winrt::init_apartment(
        winrt::apartment_type::single_threaded);
    AgentApp application;
    const auto result = application.Run(instance);
    CloseHandle(mutex);
    return result;
}
