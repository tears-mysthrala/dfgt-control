cmake_minimum_required(VERSION 3.24)
project(DfgtDirectInputFfb LANGUAGES CXX)
enable_testing()

add_library(DfgtDirectInputFfb SHARED
    src/DfgtDirectInputFfb.cpp
    src/DfgtDirectInputFfb.def)

target_compile_features(DfgtDirectInputFfb PRIVATE cxx_std_20)
target_compile_definitions(DfgtDirectInputFfb PRIVATE
    UNICODE
    _UNICODE
    DIRECTINPUT_VERSION=0x0800
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtDirectInputFfb PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtDirectInputFfb PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtDirectInputFfb PRIVATE dxguid)

set_target_properties(DfgtDirectInputFfb PROPERTIES
    OUTPUT_NAME "DfgtDirectInputFfb"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DfgtFfbComSmoke tests/ComAbiSmoke.cpp)
target_compile_features(DfgtFfbComSmoke PRIVATE cxx_std_20)
target_compile_definitions(DfgtFfbComSmoke PRIVATE
    UNICODE
    _UNICODE
    DIRECTINPUT_VERSION=0x0800
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtFfbComSmoke PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtFfbComSmoke PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtFfbComSmoke PRIVATE dxguid)
set_target_properties(DfgtFfbComSmoke PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DfgtFfbBroker broker/DfgtFfbBroker.cpp)
target_compile_features(DfgtFfbBroker PRIVATE cxx_std_20)
target_compile_definitions(DfgtFfbBroker PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtFfbBroker PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtFfbBroker PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtFfbBroker PRIVATE hid advapi32 bcrypt)
set_target_properties(DfgtFfbBroker PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DfgtProtocolTests tests/ProtocolTests.cpp)
target_compile_features(DfgtProtocolTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtProtocolTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtProtocolTests PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtProtocolTests PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtProtocolTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME protocol-vectors COMMAND DfgtProtocolTests)

add_executable(DfgtTransportFakeTests tests/TransportFakeTests.cpp)
target_compile_features(DfgtTransportFakeTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtTransportFakeTests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtTransportFakeTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtTransportFakeTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtTransportFakeTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME transport-fake-injections COMMAND DfgtTransportFakeTests)

add_executable(DfgtSafetyStateTests tests/SafetyStateTests.cpp)
target_compile_features(DfgtSafetyStateTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtSafetyStateTests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtSafetyStateTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtSafetyStateTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtSafetyStateTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME safety-state-machine COMMAND DfgtSafetyStateTests)

add_executable(DfgtEffectCoordinatorTests
    tests/EffectCoordinatorTests.cpp)
target_compile_features(DfgtEffectCoordinatorTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtEffectCoordinatorTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtEffectCoordinatorTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtEffectCoordinatorTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtEffectCoordinatorTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME effect-slot-isolation COMMAND DfgtEffectCoordinatorTests)

add_executable(DfgtLeaseWatchdogTests tests/LeaseWatchdogTests.cpp)
target_compile_features(DfgtLeaseWatchdogTests PRIVATE cxx_std_20)
target_compile_options(DfgtLeaseWatchdogTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtLeaseWatchdogTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtLeaseWatchdogTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME renderer-lease-watchdog COMMAND DfgtLeaseWatchdogTests)

add_executable(DfgtClientProcessWatchTests
    tests/ClientProcessWatchTests.cpp)
target_compile_features(DfgtClientProcessWatchTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtClientProcessWatchTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtClientProcessWatchTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtClientProcessWatchTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtClientProcessWatchTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME client-process-watch COMMAND DfgtClientProcessWatchTests)

add_executable(DfgtBoundedIoTests tests/BoundedIoTests.cpp)
target_compile_features(DfgtBoundedIoTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtBoundedIoTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtBoundedIoTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtBoundedIoTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtBoundedIoTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME bounded-pipe-io COMMAND DfgtBoundedIoTests)

add_executable(DfgtBrokerRangeSmoke tests/BrokerRangeSmoke.cpp)
target_compile_features(DfgtBrokerRangeSmoke PRIVATE cxx_std_20)
target_compile_definitions(DfgtBrokerRangeSmoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtBrokerRangeSmoke PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtBrokerRangeSmoke PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtBrokerRangeSmoke PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DfgtControlSmoke tests/ControlSmoke.cpp)
target_compile_features(DfgtControlSmoke PRIVATE cxx_std_20)
target_compile_definitions(DfgtControlSmoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtControlSmoke PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtControlSmoke PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtControlSmoke PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(DfgtAgent WIN32
    agent/DfgtAgent.cpp
    agent/ControlClient.cpp
    agent/DeviceDiscovery.cpp
    agent/ProfileStore.cpp)
target_compile_features(DfgtAgent PRIVATE cxx_std_20)
target_compile_definitions(DfgtAgent PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(DfgtAgent PRIVATE /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtAgent PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtAgent PRIVATE
    hid
    setupapi
    shell32
    user32
    windowsapp)
set_target_properties(DfgtAgent PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
