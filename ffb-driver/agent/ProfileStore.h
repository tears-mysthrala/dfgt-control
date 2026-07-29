#pragma once

#include <string>
#include <vector>

#include "../shared/BrokerProtocol.h"

namespace dfgt::agent {

struct ProfileModel {
    dfgt::ipc::control::Profile hardware;
    std::wstring name;
    std::wstring executablePath;
};

class ProfileStore final {
public:
    ProfileStore();

    bool Load(std::vector<ProfileModel>& profiles);
    bool Save(const std::vector<ProfileModel>& profiles);
    bool QuarantineInvalid();
    [[nodiscard]] const std::wstring& Path() const noexcept;

    static ProfileModel DesktopProfile();
    static bool Validate(const ProfileModel& profile) noexcept;

private:
    std::wstring path_;
};

} // namespace dfgt::agent
