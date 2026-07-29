#pragma once

#include <windows.h>

#include <array>
#include <string_view>

namespace dfgt::ipc {

inline bool SameUserToken(DWORD processId) noexcept {
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return false;
    HANDLE peerToken = nullptr;
    HANDLE currentToken = nullptr;
    const auto tokensOpened =
        OpenProcessToken(process, TOKEN_QUERY, &peerToken) &&
        OpenProcessToken(
            GetCurrentProcess(), TOKEN_QUERY, &currentToken);
    CloseHandle(process);
    if (!tokensOpened) {
        if (peerToken != nullptr) CloseHandle(peerToken);
        if (currentToken != nullptr) CloseHandle(currentToken);
        return false;
    }
    DWORD peerBytes = 0;
    DWORD currentBytes = 0;
    GetTokenInformation(
        peerToken, TokenUser, nullptr, 0, &peerBytes);
    GetTokenInformation(
        currentToken, TokenUser, nullptr, 0, &currentBytes);
    std::array<std::byte, 1024> peer{};
    std::array<std::byte, 1024> current{};
    if (peerBytes > peer.size() ||
        currentBytes > current.size()) {
        CloseHandle(peerToken);
        CloseHandle(currentToken);
        return false;
    }
    const auto queried =
        GetTokenInformation(
            peerToken, TokenUser, peer.data(),
            peerBytes, &peerBytes) &&
        GetTokenInformation(
            currentToken, TokenUser, current.data(),
            currentBytes, &currentBytes);
    CloseHandle(peerToken);
    CloseHandle(currentToken);
    if (!queried) return false;
    const auto* peerUser =
        reinterpret_cast<const TOKEN_USER*>(peer.data());
    const auto* currentUser =
        reinterpret_cast<const TOKEN_USER*>(current.data());
    return EqualSid(
        peerUser->User.Sid,
        currentUser->User.Sid) != FALSE;
}

inline bool ValidatePipeServer(
    HANDLE pipe,
    HMODULE clientModule,
    std::wstring_view expectedFilename) noexcept {
    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid) ||
        serverPid == 0 ||
        !SameUserToken(serverPid)) {
        return false;
    }
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        serverPid);
    if (process == nullptr) return false;
    std::array<wchar_t, 32768> serverPath{};
    DWORD serverLength =
        static_cast<DWORD>(serverPath.size());
    const auto serverOk = QueryFullProcessImageNameW(
        process, 0, serverPath.data(), &serverLength);
    CloseHandle(process);
    if (!serverOk) return false;
    std::array<wchar_t, 32768> clientPath{};
    const auto clientLength = GetModuleFileNameW(
        clientModule,
        clientPath.data(),
        static_cast<DWORD>(clientPath.size()));
    if (clientLength == 0 ||
        clientLength >= clientPath.size()) {
        return false;
    }
    const std::wstring_view server(
        serverPath.data(), serverLength);
    const std::wstring_view client(
        clientPath.data(), clientLength);
    const auto serverSlash = server.find_last_of(L"\\/");
    const auto clientSlash = client.find_last_of(L"\\/");
    if (serverSlash == std::wstring_view::npos ||
        clientSlash == std::wstring_view::npos) {
        return false;
    }
    const auto serverDirectory =
        server.substr(0, serverSlash);
    const auto clientDirectory =
        client.substr(0, clientSlash);
    const auto serverFilename =
        server.substr(serverSlash + 1);
    auto directoriesMatch =
        serverDirectory.size() == clientDirectory.size() &&
        _wcsnicmp(
            serverDirectory.data(),
            clientDirectory.data(),
            serverDirectory.size()) == 0;
    if (!directoriesMatch) {
        const auto serverParentSlash =
            serverDirectory.find_last_of(L"\\/");
        const auto clientParentSlash =
            clientDirectory.find_last_of(L"\\/");
        if (serverParentSlash != std::wstring_view::npos &&
            clientParentSlash != std::wstring_view::npos) {
            const auto serverParent =
                serverDirectory.substr(0, serverParentSlash);
            const auto clientParent =
                clientDirectory.substr(0, clientParentSlash);
            const auto serverLeaf =
                serverDirectory.substr(serverParentSlash + 1);
            const auto clientLeaf =
                clientDirectory.substr(clientParentSlash + 1);
            directoriesMatch =
                serverParent.size() == clientParent.size() &&
                _wcsnicmp(
                    serverParent.data(),
                    clientParent.data(),
                    serverParent.size()) == 0 &&
                serverLeaf.size() == 3 &&
                _wcsnicmp(
                    serverLeaf.data(), L"x64", 3) == 0 &&
                clientLeaf.size() == 3 &&
                (_wcsnicmp(
                    clientLeaf.data(), L"x64", 3) == 0 ||
                 _wcsnicmp(
                    clientLeaf.data(), L"x86", 3) == 0);
        }
    }
    return directoriesMatch &&
        serverFilename.size() == expectedFilename.size() &&
        _wcsnicmp(
            serverFilename.data(),
            expectedFilename.data(),
            serverFilename.size()) == 0;
}

} // namespace dfgt::ipc
