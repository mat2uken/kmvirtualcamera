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

    # Pinned at v0.22.4. The patch and its rationale are documented with the
    # same patch in the top-level CMakeLists.txt; SRTP/RTCP must not be
    # dispatched while doRecv() holds the SSL mutex. ExternalProject re-runs the
    # patch step on later configures, so the helper restores the fetched tree to
    # the pin before applying.
    # The cache holds FETCHCONTENT_SOURCE_DIR_<UCNAME> empty by default, so the
    # value must be tested for content, not for being defined.
    if(FETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL)
        set(KM_LIBDATACHANNEL_SRC_DIR "${FETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL}")
    else()
        if(NOT FETCHCONTENT_BASE_DIR)
            set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
        endif()
        set(KM_LIBDATACHANNEL_SRC_DIR "${FETCHCONTENT_BASE_DIR}/libdatachannel-src")
    endif()
    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.22.4
        GIT_SHALLOW TRUE
        PATCH_COMMAND "${CMAKE_COMMAND}"
                      "-DREPO=${KM_LIBDATACHANNEL_SRC_DIR}"
                      "-DPATCH=${CMAKE_CURRENT_LIST_DIR}/../patches/libdatachannel-0.22.4-defer-demux.patch"
                      -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/apply_libdatachannel_patch.cmake"
    )
    FetchContent_MakeAvailable(libdatachannel)
endif()

# -------------------------------------------------------------
# 1. Virtual Camera Media Source DLL (0 dependencies on WebRTC)
# -------------------------------------------------------------
add_library(VirtualCameraMediaSource SHARED
    virtual-camera/media-source/dll_main.cpp
    virtual-camera/media-source/webrtc_bridge_activate.cpp
    virtual-camera/media-source/webrtc_bridge_media_source.cpp
    virtual-camera/media-source/webrtc_bridge_media_stream.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
    receiver/media/test_pattern_generator.cpp
    virtual-camera/media-source/VirtualCameraMediaSource.def
    virtual-camera/media-source/virtual_camera.rc
)

target_include_directories(VirtualCameraMediaSource PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
)

target_link_libraries(VirtualCameraMediaSource PRIVATE
    mfplat
    mfuuid
    mf
    mfsensorgroup
    d3d11
    dxgi
    ole32
    advapi32
    winmm
    avrt
)

if(MSVC)
    target_link_options(VirtualCameraMediaSource PRIVATE /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /OPT:REF /OPT:ICF)
endif()

# -------------------------------------------------------------
# 2. Windows Native Receiver Application
# -------------------------------------------------------------
add_executable(Receiver WIN32
    receiver/app/main.cpp
    receiver/app/app_controller.cpp
    receiver/signaling/win_http_client.cpp
    receiver/rtc/peer_connection_manager.cpp
    receiver/rtc/bandwidth_estimator.cpp
    receiver/rtc/twcc_receiver.cpp
    receiver/rtc/enhanced_rtcp_session.cpp
    receiver/codec/h264_decoder.cpp
    receiver/codec/h264_rtp_depacketizer.cpp
    receiver/codec/dc_video_depacketizer.cpp
    receiver/media/nv12_converter.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/audio/audio_device_enumerator.cpp
    receiver/audio/wasapi_audio_renderer.cpp
    receiver/vcam/virtual_camera_registrar.cpp
    receiver/ui/main_window.cpp
    receiver/ui/d3d11_preview.cpp
    receiver/ui/qr_view.cpp
    third_party/qr/qrcodegen.cpp
    receiver/app/receiver.rc
    receiver/app/receiver.manifest
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
    avrt
)

if(MSVC)
    target_link_options(Receiver PRIVATE /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /OPT:REF /OPT:ICF)
endif()

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
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
)
target_include_directories(test_pipe_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_pipe_integration PRIVATE
    d3d11
    dxgi
    advapi32
)
add_test(NAME PipeIntegrationTest COMMAND test_pipe_integration)

add_executable(test_win_http_live
    tests/test_win_http_live.cpp
    receiver/signaling/win_http_client.cpp
)
target_include_directories(test_win_http_live PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/signaling
)
target_link_libraries(test_win_http_live PRIVATE
    winhttp
)

# Failure-path coverage for the signaling transport (in-process WinSock mock
# server plus an expired-certificate host); registered so ctest records it.
add_executable(test_win_http_paths
    tests/test_win_http_paths.cpp
    receiver/signaling/win_http_client.cpp
)
target_include_directories(test_win_http_paths PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/signaling
)
target_link_libraries(test_win_http_paths PRIVATE
    winhttp
    ws2_32
)
add_test(NAME WinHttpPathsTest COMMAND test_win_http_paths)

add_executable(test_qr_generator
    tests/test_qr_generator.cpp
    third_party/qr/qrcodegen.cpp
)
target_include_directories(test_qr_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qr
)
add_test(NAME QrGeneratorTest COMMAND test_qr_generator)

add_executable(test_vcam_registration
    tests/test_vcam_registration.cpp
    receiver/vcam/virtual_camera_registrar.cpp
)
target_include_directories(test_vcam_registration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
)
target_link_libraries(test_vcam_registration PRIVATE
    mfplat
    mfuuid
    mf
    ole32
    advapi32
    shell32
    user32
)
add_test(NAME VcamRegistrationTest COMMAND test_vcam_registration)

add_executable(test_h264_decoder
    tests/test_h264_decoder.cpp
    receiver/codec/h264_decoder.cpp
)
target_include_directories(test_h264_decoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/codec
)
target_link_libraries(test_h264_decoder PRIVATE
    mfplat
    mfuuid
    mf
    ole32
)
add_test(NAME H264DecoderTest COMMAND test_h264_decoder)

add_executable(test_h264_stream_inspector
    tests/test_h264_stream_inspector.cpp
    receiver/codec/h264_decoder.cpp
)
target_include_directories(test_h264_stream_inspector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/codec
)
target_link_libraries(test_h264_stream_inspector PRIVATE
    mfplat
    mfuuid
    mf
    ole32
    d3d11
    dxgi
)

add_executable(test_h264_depacketizer
    tests/test_h264_depacketizer.cpp
    receiver/codec/h264_rtp_depacketizer.cpp
)
target_include_directories(test_h264_depacketizer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/codec
)
add_test(NAME H264DepacketizerTest COMMAND test_h264_depacketizer)

add_executable(test_receiver_startup
    tests/test_receiver_startup.cpp
    receiver/app/app_controller.cpp
    receiver/signaling/win_http_client.cpp
    receiver/rtc/peer_connection_manager.cpp
    receiver/rtc/bandwidth_estimator.cpp
    receiver/rtc/twcc_receiver.cpp
    receiver/rtc/enhanced_rtcp_session.cpp
    receiver/codec/h264_decoder.cpp
    receiver/codec/h264_rtp_depacketizer.cpp
    receiver/media/nv12_converter.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/audio/audio_device_enumerator.cpp
    receiver/audio/wasapi_audio_renderer.cpp
    receiver/vcam/virtual_camera_registrar.cpp
    receiver/ui/main_window.cpp
    receiver/ui/d3d11_preview.cpp
    receiver/ui/qr_view.cpp
    third_party/qr/qrcodegen.cpp
)
target_include_directories(test_receiver_startup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qr
    ${mbedtls_SOURCE_DIR}/include
)
target_link_directories(test_receiver_startup PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/$<CONFIG>"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/Release"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library"
)
target_link_libraries(test_receiver_startup PRIVATE
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
    ws2_32
)

# Test Target 6: test_webrtc_dtls
add_executable(test_webrtc_dtls
    tests/test_webrtc_dtls.cpp
    receiver/rtc/peer_connection_manager.cpp
    receiver/rtc/bandwidth_estimator.cpp
    receiver/rtc/twcc_receiver.cpp
    receiver/rtc/enhanced_rtcp_session.cpp
    receiver/codec/h264_rtp_depacketizer.cpp
)
target_include_directories(test_webrtc_dtls PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${mbedtls_SOURCE_DIR}/include
)
target_link_directories(test_webrtc_dtls PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/$<CONFIG>"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library/Release"
    "${CMAKE_CURRENT_BINARY_DIR}/_deps/mbedtls-build/library"
)
target_link_libraries(test_webrtc_dtls PRIVATE
    datachannel-static
    mbedtls
    mbedcrypto
    mbedx509
    ws2_32
)

add_executable(test_twcc_receiver
    tests/test_twcc_receiver.cpp
    receiver/rtc/twcc_receiver.cpp
)
target_include_directories(test_twcc_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
)
add_test(NAME TwccReceiverTest COMMAND test_twcc_receiver)

add_executable(test_bandwidth_estimator
    receiver/test_bandwidth_estimator.cpp
    receiver/rtc/bandwidth_estimator.cpp
)
target_include_directories(test_bandwidth_estimator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
)
add_test(NAME BandwidthEstimatorTest COMMAND test_bandwidth_estimator)

# Test Target 8: test_virtual_camera_e2e
add_executable(test_virtual_camera_e2e
    tests/test_virtual_camera_e2e.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/vcam/virtual_camera_registrar.cpp
    virtual-camera/media-source/webrtc_bridge_media_source.cpp
    virtual-camera/media-source/webrtc_bridge_media_stream.cpp
    virtual-camera/media-source/webrtc_bridge_activate.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
)
target_include_directories(test_virtual_camera_e2e PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_virtual_camera_e2e PRIVATE
    d3d11
    dxgi
    mfplat
    mfuuid
    mf
    mfsensorgroup
    mfreadwrite
    ole32
    advapi32
    winmm
    avrt
)
add_test(NAME VirtualCameraE2ETest COMMAND test_virtual_camera_e2e)

# Test Target 9: test_vcam_pixel_fidelity
add_executable(test_vcam_pixel_fidelity
    tests/test_vcam_pixel_fidelity.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    virtual-camera/media-source/webrtc_bridge_media_source.cpp
    virtual-camera/media-source/webrtc_bridge_media_stream.cpp
    virtual-camera/media-source/webrtc_bridge_activate.cpp
    virtual-camera/media-source/pipe_frame_receiver.cpp
)
target_include_directories(test_vcam_pixel_fidelity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_vcam_pixel_fidelity PRIVATE
    d3d11
    dxgi
    mfplat
    mfuuid
    mf
    mfsensorgroup
    mfreadwrite
    ole32
    advapi32
    winmm
    avrt
)
add_test(NAME VirtualCameraPixelFidelityTest COMMAND test_vcam_pixel_fidelity)

# Test Target 10: test_system_vcam_capture (Windows Device Enumeration Capture)
add_executable(test_system_vcam_capture
    tests/test_system_vcam_capture.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/vcam/virtual_camera_registrar.cpp
)
target_include_directories(test_system_vcam_capture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_system_vcam_capture PRIVATE
    d3d11
    dxgi
    mfplat
    mfuuid
    mf
    mfsensorgroup
    mfreadwrite
    ole32
    advapi32
)
add_test(NAME SystemVirtualCameraCaptureTest COMMAND test_system_vcam_capture)

# Test Target 11: test_vcam_isolated (Minimal Standalone VCam Feeder & Capture Test)
add_executable(test_vcam_isolated
    tests/test_vcam_isolated.cpp
    receiver/media/d3d11_video_processor.cpp
    receiver/media/pipe_publisher.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/vcam/virtual_camera_registrar.cpp
)
target_include_directories(test_vcam_isolated PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_vcam_isolated PRIVATE
    d3d11
    dxgi
    mfplat
    mfuuid
    mf
    mfsensorgroup
    mfreadwrite
    ole32
    advapi32
)
add_test(NAME VirtualCameraIsolatedTest COMMAND test_vcam_isolated)

# Test Target 12: test_vcam_e2e_capture (Real Windows FrameServer Capture Test)
add_executable(test_vcam_e2e_capture
    tests/test_vcam_e2e_capture.cpp
    receiver/media/test_pattern_generator.cpp
    receiver/vcam/virtual_camera_registrar.cpp
)
target_include_directories(test_vcam_e2e_capture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_vcam_e2e_capture PRIVATE
    mfplat
    mfuuid
    mf
    mfreadwrite
    ole32
    advapi32
    winmm
)
add_executable(test_vcam_smoothness_verifier
    tests/test_vcam_smoothness_verifier.cpp
    receiver/media/test_pattern_generator.cpp
)
target_include_directories(test_vcam_smoothness_verifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_vcam_smoothness_verifier PRIVATE
    mfplat
    mfuuid
    mf
    mfreadwrite
    ole32
    advapi32
    winmm
)
add_test(NAME VirtualCameraSmoothnessTest COMMAND test_vcam_smoothness_verifier)

add_executable(test_vcam_dxgi_zero_copy
    tests/test_vcam_dxgi_zero_copy.cpp
    receiver/media/test_pattern_generator.cpp
)
target_include_directories(test_vcam_dxgi_zero_copy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver/media
    ${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/media-source
)
target_link_libraries(test_vcam_dxgi_zero_copy PRIVATE
    d3d11
    dxgi
    mfplat
    mfuuid
    mf
    mfreadwrite
    ole32
    advapi32
    winmm
)
add_test(NAME VirtualCameraDxgiZeroCopyTest COMMAND test_vcam_dxgi_zero_copy)
