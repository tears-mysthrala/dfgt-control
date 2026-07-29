#include "ProfileStore.h"

#include <windows.h>
#include <shlobj.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <string_view>

namespace dfgt::agent {
namespace {

using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;

std::wstring LocalDataDirectory() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &raw))) {
        return {};
    }
    std::wstring path(raw);
    CoTaskMemFree(raw);
    path += L"\\DfgtControl";
    return path;
}

ProfileLoadResult ReadUtf8File(
    const std::wstring& path,
    std::wstring& output) {
    const auto file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return ProfileLoadResult::Missing;
        case ERROR_ACCESS_DENIED:
            return ProfileLoadResult::AccessDenied;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return ProfileLoadResult::SharingViolation;
        default:
            return ProfileLoadResult::TransientFailure;
        }
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 ||
        size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return ProfileLoadResult::InvalidData;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const auto ok = bytes.empty() ||
        (ReadFile(
             file,
             bytes.data(),
             static_cast<DWORD>(bytes.size()),
             &read,
             nullptr) &&
         read == bytes.size());
    CloseHandle(file);
    if (!ok) return ProfileLoadResult::PartialRead;
    if (bytes.empty()) return ProfileLoadResult::InvalidData;
    const auto characters = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (characters <= 0) return ProfileLoadResult::InvalidData;
    output.resize(characters);
    return MultiByteToWideChar(
               CP_UTF8,
               MB_ERR_INVALID_CHARS,
               bytes.data(),
               static_cast<int>(bytes.size()),
               output.data(),
               characters) == characters
        ? ProfileLoadResult::Loaded
        : ProfileLoadResult::InvalidData;
}

bool WriteUtf8Atomically(
    const std::wstring& path,
    std::wstring_view text) {
    const auto bytesRequired = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (bytesRequired <= 0) return false;
    std::string bytes(bytesRequired, '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            bytes.data(),
            bytesRequired,
            nullptr,
            nullptr) != bytesRequired) {
        return false;
    }

    const auto temporary = path + L".tmp";
    const auto file = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const auto wrote = WriteFile(
        file,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        &written,
        nullptr);
    const auto flushed = FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !flushed || written != bytes.size()) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool Integer(
    const JsonObject& object,
    std::wstring_view name,
    std::int32_t& output) {
    const auto value = object.GetNamedNumber(name, NAN);
    if (!std::isfinite(value) ||
        std::floor(value) != value ||
        value < INT32_MIN ||
        value > INT32_MAX) {
        return false;
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

void InsertNumber(
    JsonObject& object,
    std::wstring_view name,
    std::int32_t value) {
    object.Insert(
        name,
        JsonValue::CreateNumberValue(value));
}

} // namespace

ProfileStore::ProfileStore() {
    const auto directory = LocalDataDirectory();
    if (directory.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error) path_ = directory + L"\\profiles.json";
}

ProfileLoadResult ProfileStore::Load(
    std::vector<ProfileModel>& profiles) {
    profiles.clear();
    if (path_.empty()) return ProfileLoadResult::TransientFailure;

    try {
        std::wstring text;
        const auto readResult = ReadUtf8File(path_, text);
        if (readResult != ProfileLoadResult::Loaded) return readResult;
        JsonObject root;
        try {
            root = JsonObject::Parse(text);
        } catch (...) {
            return ProfileLoadResult::InvalidJson;
        }
        std::int32_t schema = 0;
        if (!Integer(root, L"schemaVersion", schema))
            return ProfileLoadResult::InvalidData;
        if (schema != 1) return ProfileLoadResult::UnsupportedSchema;
        std::int32_t revision = 0;
        if (root.HasKey(L"revision") &&
            (!Integer(root, L"revision", revision) || revision < 0)) {
            return ProfileLoadResult::InvalidData;
        }
        const auto values = root.GetNamedArray(L"profiles");
        if (values.Size() == 0 || values.Size() > 256)
            return ProfileLoadResult::InvalidData;
        for (const auto& value : values) {
            const auto object = value.GetObject();
            ProfileModel profile{};
            profile.name = object.GetNamedString(L"name").c_str();
            profile.executablePath =
                object.GetNamedString(L"executablePath", L"").c_str();
            const auto id = object.GetNamedString(L"id");
            if (wcsncpy_s(
                    profile.hardware.id,
                    id.c_str(),
                    _TRUNCATE) != 0 ||
                !Integer(
                    object,
                    L"rangeDegrees",
                    profile.hardware.rangeDegrees) ||
                !Integer(
                    object,
                    L"overallGain",
                    profile.hardware.overallGain) ||
                !Integer(
                    object,
                    L"boundaryForce",
                    profile.hardware.boundaryForce) ||
                !Integer(
                    object,
                    L"springGain",
                    profile.hardware.springGain) ||
                !Integer(
                    object,
                    L"damperGain",
                    profile.hardware.damperGain) ||
                !Integer(
                    object,
                    L"frictionGain",
                    profile.hardware.frictionGain) ||
                !Integer(
                    object,
                    L"periodicGain",
                    profile.hardware.periodicGain) ||
                !Validate(profile)) {
                return ProfileLoadResult::InvalidData;
            }
            profiles.push_back(std::move(profile));
        }
        if (!ValidateDocument(profiles))
            return ProfileLoadResult::InvalidData;
        revision_ = revision;
        return ProfileLoadResult::Loaded;
    } catch (...) {
        return ProfileLoadResult::InvalidData;
    }
}

bool ProfileStore::Save(
    const std::vector<ProfileModel>& profiles) {
    if (path_.empty() ||
        profiles.empty() ||
        profiles.size() > 256 ||
        !ValidateDocument(profiles)) {
        return false;
    }
    try {
        JsonObject root;
        InsertNumber(root, L"schemaVersion", 1);
        if (revision_ == INT32_MAX) return false;
        InsertNumber(root, L"revision", revision_ + 1);
        JsonArray values;
        for (const auto& profile : profiles) {
            JsonObject object;
            object.Insert(
                L"id",
                JsonValue::CreateStringValue(
                    profile.hardware.id));
            object.Insert(
                L"name",
                JsonValue::CreateStringValue(profile.name));
            object.Insert(
                L"executablePath",
                JsonValue::CreateStringValue(
                    profile.executablePath));
            InsertNumber(
                object,
                L"rangeDegrees",
                profile.hardware.rangeDegrees);
            InsertNumber(
                object,
                L"overallGain",
                profile.hardware.overallGain);
            InsertNumber(
                object,
                L"boundaryForce",
                profile.hardware.boundaryForce);
            InsertNumber(
                object,
                L"springGain",
                profile.hardware.springGain);
            InsertNumber(
                object,
                L"damperGain",
                profile.hardware.damperGain);
            InsertNumber(
                object,
                L"frictionGain",
                profile.hardware.frictionGain);
            InsertNumber(
                object,
                L"periodicGain",
                profile.hardware.periodicGain);
            values.Append(object);
        }
        root.Insert(L"profiles", values);
        const auto written = WriteUtf8Atomically(
            path_,
            root.Stringify().c_str());
        if (written) ++revision_;
        return written;
    } catch (...) {
        return false;
    }
}

bool ProfileStore::QuarantineInvalid() {
    if (path_.empty() ||
        GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t suffix[64]{};
    swprintf_s(
        suffix,
        L".invalid-%04u%02u%02u-%02u%02u%02u.json",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);
    const auto quarantined = path_ + suffix;
    return MoveFileExW(
               path_.c_str(),
               quarantined.c_str(),
               MOVEFILE_WRITE_THROUGH) == TRUE;
}

const std::wstring& ProfileStore::Path() const noexcept {
    return path_;
}

ProfileModel ProfileStore::DesktopProfile() {
    ProfileModel profile{};
    wcsncpy_s(
        profile.hardware.id,
        L"desktop",
        _TRUNCATE);
    profile.name = L"Escritorio";
    profile.hardware.rangeDegrees = 900;
    profile.hardware.overallGain = 10000;
    profile.hardware.boundaryForce = 3000;
    profile.hardware.springGain = 10000;
    profile.hardware.damperGain = 10000;
    profile.hardware.frictionGain = 10000;
    profile.hardware.periodicGain = 10000;
    return profile;
}

bool ProfileStore::Validate(
    const ProfileModel& profile) noexcept {
    const std::wstring_view id(profile.hardware.id);
    if (id.empty() ||
        id.size() >=
            dfgt::ipc::control::ProfileIdCharacters ||
        profile.name.empty() ||
        profile.name.size() > 128) {
        return false;
    }
    for (const auto character : id) {
        if (!((character >= L'a' && character <= L'z') ||
              (character >= L'A' && character <= L'Z') ||
              (character >= L'0' && character <= L'9') ||
              character == L'.' ||
              character == L'_' ||
              character == L'-')) {
            return false;
        }
    }
    if (!profile.executablePath.empty() &&
        !std::filesystem::path(
             profile.executablePath).is_absolute()) {
        return false;
    }
    const auto& data = profile.hardware;
    const auto gain = [](std::int32_t value) {
        return value >= 0 && value <= 10000;
    };
    return data.rangeDegrees >= 40 &&
        data.rangeDegrees <= 900 &&
        gain(data.overallGain) &&
        data.boundaryForce >= 0 &&
        data.boundaryForce <= 5000 &&
        gain(data.springGain) &&
        gain(data.damperGain) &&
        gain(data.frictionGain) &&
        gain(data.periodicGain);
}

bool ProfileStore::ValidateDocument(
    const std::vector<ProfileModel>& profiles) noexcept {
    try {
        if (profiles.empty() || profiles.size() > 256) return false;
        std::set<std::wstring, std::less<>> ids;
        std::set<std::wstring, std::less<>> executables;
        std::size_t desktopCount = 0;
        for (const auto& profile : profiles) {
            if (!Validate(profile)) return false;
            std::wstring id(profile.hardware.id);
            std::ranges::transform(id, id.begin(), towlower);
            if (!ids.insert(id).second) return false;
            if (id == L"desktop") {
                ++desktopCount;
                if (!profile.executablePath.empty()) return false;
            }
            if (!profile.executablePath.empty()) {
                auto executable = std::filesystem::path(
                    profile.executablePath).lexically_normal().wstring();
                std::ranges::transform(
                    executable,
                    executable.begin(),
                    towlower);
                if (!executables.insert(executable).second) return false;
            }
        }
        return desktopCount == 1;
    } catch (...) {
        return false;
    }
}

} // namespace dfgt::agent
