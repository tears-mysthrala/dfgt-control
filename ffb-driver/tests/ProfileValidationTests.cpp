#include "../agent/ProfileStore.h"

#include <windows.h>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

namespace {

bool Expect(bool value, const char* message) {
    if (value) return true;
    std::cerr << message << '\n';
    return false;
}

dfgt::agent::ProfileModel Game(
    const wchar_t* id,
    const wchar_t* path) {
    auto profile = dfgt::agent::ProfileStore::DesktopProfile();
    wcsncpy_s(profile.hardware.id, id, _TRUNCATE);
    profile.name = id;
    profile.executablePath = path;
    return profile;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    using dfgt::agent::ProfileStore;
    bool ok = true;

    std::vector valid{
        ProfileStore::DesktopProfile(),
        Game(L"game-a", L"C:\\Games\\A.exe")};
    ok &= Expect(
        ProfileStore::ValidateDocument(valid),
        "valid document rejected");

    auto noDesktop = valid;
    noDesktop.erase(noDesktop.begin());
    ok &= Expect(
        !ProfileStore::ValidateDocument(noDesktop),
        "document without desktop accepted");

    auto duplicateDesktop = valid;
    duplicateDesktop.push_back(ProfileStore::DesktopProfile());
    ok &= Expect(
        !ProfileStore::ValidateDocument(duplicateDesktop),
        "duplicate desktop accepted");

    auto duplicateId = valid;
    duplicateId.push_back(Game(L"GAME-A", L"C:\\Games\\B.exe"));
    ok &= Expect(
        !ProfileStore::ValidateDocument(duplicateId),
        "case-insensitive duplicate id accepted");

    auto duplicatePath = valid;
    duplicatePath.push_back(Game(L"game-b", L"c:\\games\\a.EXE"));
    ok &= Expect(
        !ProfileStore::ValidateDocument(duplicatePath),
        "case-insensitive duplicate executable accepted");

    auto relativePath = valid;
    relativePath.push_back(Game(L"game-b", L"Games\\B.exe"));
    ok &= Expect(
        !ProfileStore::ValidateDocument(relativePath),
        "relative executable accepted");

    auto invalidGain = valid;
    invalidGain[1].hardware.overallGain = 10001;
    ok &= Expect(
        !ProfileStore::ValidateDocument(invalidGain),
        "out-of-range gain accepted");

    const auto testRoot = std::filesystem::temp_directory_path() /
        (std::wstring(L"DfgtProfileValidation-") +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(testRoot);
    const auto profilePath = testRoot / L"profiles.json";
    {
        ProfileStore store(profilePath.wstring());
        ok &= Expect(store.Save(valid), "initial atomic save failed");
        ProfileStore concurrent(profilePath.wstring());
        std::vector<dfgt::agent::ProfileModel> concurrentProfiles;
        ok &= Expect(
            concurrent.Load(concurrentProfiles) ==
                dfgt::agent::ProfileLoadResult::Loaded,
            "concurrent store could not load baseline revision");
        valid[1].hardware.overallGain = 7500;
        ok &= Expect(store.Save(valid), "replacement atomic save failed");
        concurrentProfiles[1].hardware.overallGain = 5000;
        ok &= Expect(
            !concurrent.Save(concurrentProfiles),
            "stale profile revision overwrote a newer document");
        ok &= Expect(
            std::filesystem::exists(profilePath.wstring() + L".last-good"),
            "last-good backup was not retained");
        for (const auto& entry :
             std::filesystem::directory_iterator(testRoot)) {
            ok &= Expect(
                entry.path().extension() != L".tmp",
                "owned temporary profile file leaked");
        }
        std::vector<dfgt::agent::ProfileModel> loaded;
        ok &= Expect(
            store.Load(loaded) ==
                dfgt::agent::ProfileLoadResult::Loaded,
            "saved profile document could not be loaded");
        ok &= Expect(
            loaded.size() == valid.size() &&
                loaded[1].hardware.overallGain == 7500,
            "saved profile content did not round trip");
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
    ok &= Expect(!cleanupError, "profile test cleanup failed");

    ok &= Expect(argc == 2, "golden corpus path is missing");
    if (argc == 2) {
        std::ifstream input(
            std::filesystem::path(argv[1]),
            std::ios::binary);
        const std::string bytes{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        const auto root =
            winrt::Windows::Data::Json::JsonObject::Parse(
                winrt::to_hstring(std::string_view(bytes)));
        for (const auto& value :
             root.GetNamedArray(L"cases")) {
            const auto test = value.GetObject();
            const auto expected = test.GetNamedBoolean(L"valid");
            const auto document = test.GetNamedObject(L"document");
            const auto schema =
                static_cast<int>(document.GetNamedNumber(L"schemaVersion"));
            const auto revision =
                static_cast<int>(document.GetNamedNumber(L"revision"));
            std::vector<dfgt::agent::ProfileModel> models;
            for (const auto& profileValue :
                 document.GetNamedArray(L"profiles")) {
                const auto object = profileValue.GetObject();
                dfgt::agent::ProfileModel model{};
                const auto id = object.GetNamedString(L"id");
                wcsncpy_s(
                    model.hardware.id, id.c_str(), _TRUNCATE);
                model.name = object.GetNamedString(L"name").c_str();
                model.executablePath =
                    object.GetNamedString(L"executablePath").c_str();
                model.hardware.rangeDegrees = static_cast<int>(
                    object.GetNamedNumber(L"rangeDegrees"));
                model.hardware.overallGain = static_cast<int>(
                    object.GetNamedNumber(L"overallGain"));
                model.hardware.boundaryForce = static_cast<int>(
                    object.GetNamedNumber(L"boundaryForce"));
                model.hardware.springGain = static_cast<int>(
                    object.GetNamedNumber(L"springGain"));
                model.hardware.damperGain = static_cast<int>(
                    object.GetNamedNumber(L"damperGain"));
                model.hardware.frictionGain = static_cast<int>(
                    object.GetNamedNumber(L"frictionGain"));
                model.hardware.periodicGain = static_cast<int>(
                    object.GetNamedNumber(L"periodicGain"));
                models.push_back(std::move(model));
            }
            const auto actual = schema == 1 && revision >= 0 &&
                ProfileStore::ValidateDocument(models);
            ok &= Expect(
                actual == expected,
                "C++ validator disagrees with shared golden corpus");
        }
    }

    return ok ? 0 : 1;
}
