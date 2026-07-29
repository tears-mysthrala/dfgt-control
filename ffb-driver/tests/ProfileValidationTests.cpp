#include "../agent/ProfileStore.h"

#include <windows.h>

#include <iostream>
#include <filesystem>
#include <vector>

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

int wmain() {
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
        valid[1].hardware.overallGain = 7500;
        ok &= Expect(store.Save(valid), "replacement atomic save failed");
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

    return ok ? 0 : 1;
}
