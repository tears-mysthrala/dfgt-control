#include "../agent/AgentRecoveryPolicy.h"

#include <iostream>

int wmain() {
    using dfgt::agent::NextBrokerBackoff;
    using dfgt::agent::RequiresReconnect;
    using dfgt::agent::ResumeMayAutomaticallyRearm;
    using dfgt::ipc::SafetyMode;
    using dfgt::ipc::control::DeviceState;

    bool ok = true;
    ok &= RequiresReconnect(DeviceState::Disconnected);
    ok &= RequiresReconnect(DeviceState::Faulted);
    ok &= !RequiresReconnect(DeviceState::Ready);
    ok &= NextBrokerBackoff(0) == 1000;
    ok &= NextBrokerBackoff(500) == 1000;
    ok &= NextBrokerBackoff(20000) == 30000;
    ok &= NextBrokerBackoff(30000) == 30000;
    ok &= !ResumeMayAutomaticallyRearm(
        SafetyMode::EmergencyLatched);
    ok &= !ResumeMayAutomaticallyRearm(
        SafetyMode::StopUnconfirmed);
    ok &= ResumeMayAutomaticallyRearm(SafetyMode::ReadyDisarmed);
    if (!ok) std::cerr << "agent recovery policy failed\n";
    return ok ? 0 : 1;
}
