#include <windows.h>
#include <shellapi.h>

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
        if (!ReloadProfiles()) {
            store_.QuarantineInvalid();
            profiles_.push_back(
                dfgt::agent::ProfileStore::DesktopProfile());
            store_.Save(profiles_);
        }
        return ConnectDevice();
    }

    bool ReloadProfiles() {
        std::vector<dfgt::agent::ProfileModel> loaded;
        if (!store_.Load(loaded) || loaded.empty()) return false;
        profiles_ = std::move(loaded);
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(
                store_.Path().c_str(),
                GetFileExInfoStandard,
                &attributes)) {
            profileWriteTime_ = attributes.ftLastWriteTime;
        }
        return true;
    }

    bool ConnectDevice() {
        if (FAILED(EnsureBroker())) return false;
        const auto path = dfgt::agent::FindDfgtDevicePath();
        if (!path || FAILED(control_.AttachDevice(*path))) {
            UpdateTray(L"DFGT Control · volante desconectado");
            return true;
        }
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
            ReloadProfiles()) {
            activeProfile_.clear();
            ApplyForegroundProfile();
        }

        dfgt::ipc::control::Status status{};
        if (FAILED(control_.GetStatus(status))) {
            control_.Close();
            activeProfile_.clear();
            ConnectDevice();
        }
    }

    HRESULT EnsureBroker() {
        auto result = control_.Connect();
        if (SUCCEEDED(result)) return result;
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
            if (SUCCEEDED(result)) return result;
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

    void Shutdown() {
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
                control_.EmergencyStop();
                UpdateTray(L"DFGT Control · fuerza detenida");
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
    NOTIFYICONDATAW tray_{};
    dfgt::agent::ControlClient control_;
    dfgt::agent::ProfileStore store_;
    std::vector<dfgt::agent::ProfileModel> profiles_;
    std::wstring activeProfile_;
    FILETIME profileWriteTime_{};
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
