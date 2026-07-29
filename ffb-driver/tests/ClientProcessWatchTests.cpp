#include "../broker/ClientProcessWatch.h"

#include <iostream>

int main() {
    wchar_t command[] = L"cmd.exe /d /c exit 0";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr, command, nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::cerr << "could not create test client\n";
        return 1;
    }
    dfgt::broker::ClientProcessWatch watch(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!watch.Valid()) {
        std::cerr << "client process watch did not open\n";
        return 1;
    }
    const auto deadline = GetTickCount64() + 3000;
    while (!watch.Terminated() && GetTickCount64() < deadline) Sleep(5);
    if (!watch.Terminated()) {
        std::cerr << "client termination was not detected\n";
        return 1;
    }
    return 0;
}
