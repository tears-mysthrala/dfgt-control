#include "FakeHidTransport.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using dfgt::broker::TransportFailure;
    using dfgt::broker::TransportResult;
    using dfgt::tests::FakeHidTransport;

    FakeHidTransport transport;
    constexpr std::array<std::uint8_t, 8> report{
        0x00, 0xF3, 0, 0, 0, 0, 0, 0};
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(10);

    bool ok = true;
    transport.Queue({
        HRESULT_FROM_WIN32(ERROR_TIMEOUT),
        TransportFailure::Timeout,
        0});
    const auto timeout = transport.Write(report, deadline);
    ok &= Expect(!timeout.Succeeded(), "timeout must fail");
    ok &= Expect(
        timeout.failure == TransportFailure::Timeout,
        "timeout classification was lost");

    transport.Queue({
        HRESULT_FROM_WIN32(ERROR_WRITE_FAULT),
        TransportFailure::ShortWrite,
        4});
    const auto shortWrite = transport.Write(report, deadline);
    ok &= Expect(!shortWrite.Succeeded(), "short write must fail");
    ok &= Expect(
        shortWrite.bytesTransferred == 4,
        "short-write byte count was lost");

    transport.Queue({
        HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
        TransportFailure::Disconnected,
        0});
    const auto disconnected = transport.Write(report, deadline);
    ok &= Expect(
        disconnected.failure == TransportFailure::Disconnected,
        "disconnect classification was lost");

    transport.Queue({
        HRESULT_FROM_WIN32(ERROR_BUSY),
        TransportFailure::Blocked,
        0});
    const auto blocked = transport.Write(report, deadline);
    ok &= Expect(
        blocked.failure == TransportFailure::Blocked,
        "blocked classification was lost");

    transport.Close();
    ok &= Expect(!transport.IsOpen(), "close did not clear open state");
    transport.Queue({
        S_OK,
        TransportFailure::None,
        0});
    const auto recovered = transport.ReopenNeutral(deadline);
    ok &= Expect(recovered.Succeeded(), "recovery should succeed");
    ok &= Expect(transport.IsOpen(), "recovery did not reopen transport");

    transport.Cancel();
    ok &= Expect(transport.Cancelled(), "cancel was not recorded");
    ok &= Expect(
        transport.Operations().size() == 4,
        "fake did not record every report");
    return ok ? 0 : 1;
}
