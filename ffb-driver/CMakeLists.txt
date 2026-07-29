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
target_link_libraries(DfgtFfbBroker PRIVATE hid advapi32)
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
