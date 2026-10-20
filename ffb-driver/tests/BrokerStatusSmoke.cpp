#include <windows.h>

#include <iostream>

#include "../shared/AgentUiProtocol.h"
#include "../shared/BoundedIo.h"

int wmain() {
    if (!WaitNamedPipeW(dfgt::agentui::PipeName, 2000)) return 2;
    const auto pipe = CreateFileW(
        dfgt::agentui::PipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 3;
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        CloseHandle(pipe);
        return 4;
    }

    dfgt::agentui::Request request{};
    request.command = dfgt::agentui::Command::GetStatus;
    request.sequence = 1;
    const auto write = dfgt::io::WriteExact(
        pipe, &request, sizeof(request), 500);
    if (!write.Succeeded(sizeof(request))) {
        CloseHandle(pipe);
        return 5;
    }
    dfgt::agentui::Response response{};
    const auto read = dfgt::io::ReadExact(
        pipe, &response, sizeof(response), 500);
    CloseHandle(pipe);
    if (!read.Succeeded(sizeof(response)) ||
        response.magic != dfgt::agentui::Magic ||
        response.version != dfgt::agentui::Version ||
        response.size != sizeof(response) ||
        response.sequence != request.sequence) {
        return 6;
    }

    std::wcout
        << L"{\"agentProtocolVersion\":" << response.version
        << L",\"brokerStatusResult\":" << response.result
        << L",\"deviceState\":" << response.deviceState
        << L",\"connected\":" << response.connected
        << L",\"ffbClientConnected\":"
        << response.ffbClientConnected
        << L",\"safetyMode\":" << response.safetyMode
        << L",\"activeSlots\":" << response.activeSlots
        << L",\"stopConfirmed\":" << response.stopConfirmed
        << L",\"lastSafetyError\":"
        << response.lastSafetyError << L"}\n";
    return SUCCEEDED(response.result) ? 0 : 7;
}
