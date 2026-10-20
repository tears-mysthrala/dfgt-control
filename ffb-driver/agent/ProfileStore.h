#pragma once

#include <string>
#include <vector>

#include "../shared/BrokerProtocol.h"

namespace dfgt::agent {

enum class ProfileLoadResult {
    Loaded,
    Missing,
    InvalidJson,
    UnsupportedSchema,
    InvalidData,
    AccessDenied,
    SharingViolation,
    PartialRead,
    TransientFailure,
};

struct ProfileModel {
    dfgt::ipc::control::Profile hardware;
    std::wstring name;
    std::wstring executablePath;
};

class ProfileStore final {
public:
    ProfileStore();
    explicit ProfileStore(std::wstring path);

    ProfileLoadResult Load(std::vector<ProfileModel>& profiles);
    bool Save(const std::vector<ProfileModel>& profiles);
    bool QuarantineInvalid();
    [[nodiscard]] const std::wstring& Path() const noexcept;

    static ProfileModel DesktopProfile();
    static bool Validate(const ProfileModel& profile) noexcept;
    static bool ValidateDocument(
        const std::vector<ProfileModel>& profiles) noexcept;

private:
    std::wstring path_;
    std::int32_t revision_{};
};

} // namespace dfgt::agent
