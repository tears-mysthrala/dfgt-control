#include <windows.h>
#include <RestartManager.h>

#include <iostream>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    DWORD session = 0;
    wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    auto result = RmStartSession(&session, 0, key);
    if (result != ERROR_SUCCESS) return 3;
    const auto endSession = [&]() { RmEndSession(session); };
    std::vector<LPCWSTR> resources;
    for (int index = 1; index < argc; ++index)
        resources.push_back(argv[index]);
    result = RmRegisterResources(
        session,
        static_cast<UINT>(resources.size()),
        resources.data(),
        0, nullptr, 0, nullptr);
    if (result != ERROR_SUCCESS) {
        endSession();
        return 4;
    }
    UINT needed = 0;
    UINT count = 0;
    DWORD rebootReasons = 0;
    result = RmGetList(
        session, &needed, &count, nullptr, &rebootReasons);
    if (result == ERROR_SUCCESS && needed == 0) {
        endSession();
        std::wcout << L"{\"lockingProcesses\":[]}\n";
        return 0;
    }
    if (result != ERROR_MORE_DATA) {
        endSession();
        return 5;
    }
    std::vector<RM_PROCESS_INFO> processes(needed);
    count = needed;
    result = RmGetList(
        session, &needed, &count, processes.data(), &rebootReasons);
    endSession();
    if (result != ERROR_SUCCESS) return 6;
    std::wcout << L"{\"lockingProcesses\":[";
    for (UINT index = 0; index < count; ++index) {
        if (index != 0) std::wcout << L",";
        std::wcout
            << L"{\"pid\":" << processes[index].Process.dwProcessId
            << L",\"application\":\""
            << processes[index].strAppName << L"\"}";
    }
    std::wcout << L"]}\n";
    return count == 0 ? 0 : 10;
}
