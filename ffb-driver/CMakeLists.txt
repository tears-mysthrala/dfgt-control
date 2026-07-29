cmake_minimum_required(VERSION 3.24)
project(DfgtDirectInputFfb LANGUAGES CXX)
enable_testing()

option(DFGT_STATIC_ANALYSIS "Enable MSVC static analysis" OFF)
if(MSVC)
    add_compile_options(/sdl)
    if(DFGT_STATIC_ANALYSIS)
        add_compile_options(/analyze)
    endif()
endif()

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
add_test(
    NAME com-abi-activation
    COMMAND DfgtFfbComSmoke $<TARGET_FILE:DfgtDirectInputFfb>)

add_executable(DfgtDirectInputContractTests
    tests/DirectInputContractTests.cpp)
target_compile_features(DfgtDirectInputContractTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtDirectInputContractTests PRIVATE
    UNICODE _UNICODE DIRECTINPUT_VERSION=0x0800
    WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtDirectInputContractTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtDirectInputContractTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtDirectInputContractTests PRIVATE dxguid)
set_target_properties(DfgtDirectInputContractTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(
    NAME directinput-contract
    COMMAND DfgtDirectInputContractTests
        $<TARGET_FILE:DfgtDirectInputFfb>)

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

add_executable(DfgtEmergencyIoGateTests
    tests/EmergencyIoGateTests.cpp)
target_compile_features(DfgtEmergencyIoGateTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtEmergencyIoGateTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtEmergencyIoGateTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtEmergencyIoGateTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtEmergencyIoGateTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME emergency-io-gate COMMAND DfgtEmergencyIoGateTests)

add_executable(DfgtEffectTimingTests tests/EffectTimingTests.cpp)
target_compile_features(DfgtEffectTimingTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtEffectTimingTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtEffectTimingTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtEffectTimingTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtEffectTimingTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME effect-timing COMMAND DfgtEffectTimingTests)

add_executable(DfgtBrokerProtocolIntegrationTests
    tests/BrokerProtocolIntegrationTests.cpp)
target_compile_features(
    DfgtBrokerProtocolIntegrationTests PRIVATE cxx_std_20)
target_compile_definitions(
    DfgtBrokerProtocolIntegrationTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(
    DfgtBrokerProtocolIntegrationTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(
    DfgtBrokerProtocolIntegrationTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(
    DfgtBrokerProtocolIntegrationTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(
    NAME broker-framing-integration
    COMMAND DfgtBrokerProtocolIntegrationTests
        $<TARGET_FILE:DfgtFfbBroker>)
set_tests_properties(
    broker-framing-integration PROPERTIES RUN_SERIAL TRUE)

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

add_executable(DfgtProfileValidationTests
    tests/ProfileValidationTests.cpp
    agent/ProfileStore.cpp)
target_compile_features(DfgtProfileValidationTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtProfileValidationTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtProfileValidationTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtProfileValidationTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
target_link_libraries(DfgtProfileValidationTests PRIVATE
    shell32 windowsapp)
set_target_properties(DfgtProfileValidationTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(
    NAME profile-document-validation
    COMMAND DfgtProfileValidationTests)

add_executable(DfgtTimingJitterTests tests/TimingJitterTests.cpp)
target_compile_features(DfgtTimingJitterTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtTimingJitterTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtTimingJitterTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtTimingJitterTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtTimingJitterTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME timing-jitter COMMAND DfgtTimingJitterTests)

add_executable(DfgtAgentRecoveryPolicyTests
    tests/AgentRecoveryPolicyTests.cpp)
target_compile_features(DfgtAgentRecoveryPolicyTests PRIVATE cxx_std_20)
target_compile_definitions(DfgtAgentRecoveryPolicyTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(DfgtAgentRecoveryPolicyTests PRIVATE
    /W4 /WX /permissive- /guard:cf)
target_link_options(DfgtAgentRecoveryPolicyTests PRIVATE
    /guard:cf /DYNAMICBASE /NXCOMPAT)
set_target_properties(DfgtAgentRecoveryPolicyTests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(
    NAME agent-recovery-policy
    COMMAND DfgtAgentRecoveryPolicyTests)
