cmake_minimum_required(VERSION 3.20)
project(WebRtcBridgeWindows VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Unicode, UTF-8 source/execution charset, and MbedTLS custom config
add_compile_options(/utf-8)
add_compile_definitions(
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    MBEDTLS_USER_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_custom_config.h"
    MBEDTLS_SSL_DTLS_SRTP
)

include(FetchContent)

# Option to fetch MbedTLS and libdatachannel
option(USE_EXTERNAL_DATACHANNEL "Fetch libdatachannel from GitHub" ON)

if(USE_EXTERNAL_DATACHANNEL)
    # 1. Fetch MbedTLS (v3.6.2 LTS)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(INSTALL_MBEDTLS_HEADERS ON CACHE BOOL "" FORCE)
    set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_custom_config.h" CACHE STRING "" FORCE)

    FetchContent_Declare(
        MbedTLS
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v3.6.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(MbedTLS)

    # 2. Configure and Fetch libdatachannel
    set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)

    # Set include paths and library targets for FindMbedTLS in libdatachannel
    set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(MbedTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
    set(MbedCrypto_LIBRARY mbedcrypto CACHE STRING "" FORCE)
    set(MbedX509_LIBRARY mbedx509 CACHE STRING "" FORCE)

    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.22.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libdatachannel)
endif()

# -------------------------------------------------------------
# 1. Virtual Camera Media Source DLL (0 dependencies on WebRTC)
# -------------------------------------------------------------
add_library(VirtualCameraMediaSource SHARED
    virtual-camera/media-source/dll_main.cpp
    virtual-camera/media-source/webrtc_bridge_media_source.cpp
    virtual-camera/media-source/webrtc_bridge_media_stream.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
    virtual-camera/media-source/VirtualCameraMediaSource.def
)

target_include_directories(VirtualCameraMediaSource PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)

target_link_libraries(VirtualCameraMediaSource PRIVATE
    mfplat
    mfuuid
    mf
    ole32
    advapi32
)

# -------------------------------------------------------------
# 2. Windows Native Receiver Application
# -------------------------------------------------------------
add_executable(Receiver WIN32
    receiver/app/main.cpp
    receiver/app/app_controller.cpp
    receiver/signaling/win_http_client.cpp
    receiver/rtc/peer_connection_manager.cpp
    receiver/media/nv12_converter.cpp
    receiver/media/pipe_publisher.cpp
    receiver/audio/audio_device_enumerator.cpp
    receiver/audio/wasapi_audio_renderer.cpp
    receiver/vcam/virtual_camera_registrar.cpp
    receiver/ui/main_window.cpp
    receiver/ui/d3d11_preview.cpp
    receiver/ui/qr_view.cpp
    third_party/qr/qrcodegen.cpp
)

target_include_directories(Receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qr
    ${mbedtls_SOURCE_DIR}/include
)

target_link_directories(Receiver PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/$<CONFIG>"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/Release"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library"
)

target_link_libraries(Receiver PRIVATE
    datachannel-static
    mbedtls
    mbedcrypto
    mbedx509
    d3d11
    dxgi
    winhttp
    mfplat
    mfuuid
    mf
    ole32
    gdi32
    user32
    advapi32
    ws2_32
)

# -------------------------------------------------------------
# 3. Unit & Integration Tests
# -------------------------------------------------------------
enable_testing()

add_executable(test_frame_pipe_protocol
    tests/test_frame_pipe_protocol.cpp
)
target_include_directories(test_frame_pipe_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)
add_test(NAME FramePipeProtocolTest COMMAND test_frame_pipe_protocol)

add_executable(test_nv12_converter
    tests/test_nv12_converter.cpp
    receiver/media/nv12_converter.cpp
)
target_include_directories(test_nv12_converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
)
add_test(NAME Nv12ConverterTest COMMAND test_nv12_converter)

add_executable(test_pipe_integration
    tests/test_pipe_integration.cpp
    receiver/media/pipe_publisher.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
)
target_include_directories(test_pipe_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_pipe_integration PRIVATE
    advapi32
)
add_test(NAME PipeIntegrationTest COMMAND test_pipe_integration)
