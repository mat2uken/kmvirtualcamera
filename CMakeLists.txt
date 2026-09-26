cmake_minimum_required(VERSION 3.20)
project(KMVirtualCameraFoundation VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KM_BUILD_TESTS "Build common contract and regression tests" ON)
option(KM_BUILD_MACOS "Build unsigned macOS media components (not an installed camera)" OFF)
option(KM_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(KM_ENABLE_OPUS "Build real Opus audio decoding (fetch pinned libopus)" OFF)
option(KM_BUILD_RTC_SHARED "Compile RTC integration using an installed LibDataChannel" OFF)
option(KM_FETCH_DATACHANNEL "Fetch pinned libdatachannel+mbedTLS source like windows/legacy_targets.cmake (first configure needs network)" OFF)
find_package(Threads REQUIRED)
add_library(km_media_core STATIC
    shared/receiver/codec/dc_video_depacketizer.cpp
    shared/receiver/codec/h264_rtp_depacketizer.cpp)
target_include_directories(km_media_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(km_media_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(km_media_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(km_media_core PRIVATE -Wall -Wextra -Wpedantic)
    if(KM_ENABLE_SANITIZERS)
        target_compile_options(km_media_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(km_media_core PUBLIC -fsanitize=address,undefined)
    endif()
endif()
include(cmake/opus.cmake)
add_library(km_audio STATIC shared/receiver/audio/opus_rtp_decoder.cpp)
target_link_libraries(km_audio PUBLIC km_media_core)
km_configure_audio(km_audio)
add_library(km_receiver_engine STATIC
    shared/receiver/engine/receiver_engine.cpp)
target_link_libraries(km_receiver_engine PUBLIC km_media_core)
if(MSVC)
    target_compile_options(km_receiver_engine PRIVATE /W4 /utf-8)
else()
    target_compile_options(km_receiver_engine PRIVATE -Wall -Wextra -Wpedantic)
    if(KM_ENABLE_SANITIZERS)
        target_compile_options(km_receiver_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(km_receiver_engine PUBLIC -fsanitize=address,undefined)
    endif()
endif()
# M4-a signaling worker: SessionClient create/poll/answer on one dedicated thread,
# mirroring the Windows SignalingWorkerProc. Common C++ only (IHttpTransport seam,
# no Win32/NSURLSession/RTC types) so Mac wiring stays a transport choice.
add_library(km_signaling_worker STATIC
    shared/receiver/signaling/signaling_worker.cpp)
target_include_directories(km_signaling_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(km_signaling_worker PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(km_signaling_worker PRIVATE /W4 /utf-8)
else()
    target_compile_options(km_signaling_worker PRIVATE -Wall -Wextra -Wpedantic)
    if(KM_ENABLE_SANITIZERS)
        target_compile_options(km_signaling_worker PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(km_signaling_worker PUBLIC -fsanitize=address,undefined)
    endif()
endif()
if(KM_BUILD_TESTS)
    enable_testing()
    foreach(pair IN ITEMS "km_core_tests|test_core|common_contracts" "km_dc_tests|test_dc|datachannel_regression" "km_review_tests|test_review|review_regressions" "km_session_tests|test_session|session_protocol")
        string(REPLACE "|" ";" parts "${pair}")
        list(GET parts 0 target)
        list(GET parts 1 source)
        list(GET parts 2 test)
        add_executable(${target} tests/shared/${source}.cpp)
        target_link_libraries(${target} PRIVATE km_media_core)
        add_test(NAME ${test} COMMAND ${target})
    endforeach()
    add_executable(km_engine_tests tests/shared/test_engine.cpp)
    target_link_libraries(km_engine_tests PRIVATE km_receiver_engine)
    add_test(NAME receiver_engine COMMAND km_engine_tests)
    add_executable(km_signaling_worker_tests tests/shared/test_signaling_worker.cpp)
    target_link_libraries(km_signaling_worker_tests PRIVATE km_signaling_worker)
    add_test(NAME signaling_worker COMMAND km_signaling_worker_tests)
    if(KM_ENABLE_OPUS)
        add_executable(km_opus_tests tests/shared/test_opus.cpp)
        target_link_libraries(km_opus_tests PRIVATE km_audio)
        add_test(NAME opus_decode COMMAND km_opus_tests)
    endif()
endif()
if(KM_BUILD_MACOS)
    if(NOT APPLE)
        message(FATAL_ERROR "KM_BUILD_MACOS requires macOS and Xcode SDK")
    endif()
    enable_language(OBJCXX)
    add_subdirectory(macos)
endif()
if(KM_BUILD_RTC_SHARED)
    # RTC integration: PeerConnectionManager plus the shared rtc helpers.
    # LibDataChannel supply: KM_FETCH_DATACHANNEL=ON fetches the exact pins the
    # Windows build uses (windows/legacy_targets.cmake: mbedTLS v3.6.2,
    # libdatachannel v0.22.4, static, mbedTLS backend); OFF keeps find_package.
    # TURN: the fetched libjuice carries UDP TURN only, so KM_TURN_TCP_TLS stays 0
    # unless a libnice build is installed — TCP/TLS TURN endpoints are skipped and
    # relay policy without a usable TURN fails with a reason instead of pretending.
    if(KM_FETCH_DATACHANNEL)
        include(FetchContent)
        # Pinned deps (e.g. libdatachannel's vendored plog) declare pre-3.5
        # cmake_minimum_required values; CMake 4 removed that compatibility and
        # documents this override for fetched trees.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(INSTALL_MBEDTLS_HEADERS ON CACHE BOOL "" FORCE)
        set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/windows/mbedtls_custom_config.h" CACHE STRING "" FORCE)
        # The user config header defines MBEDTLS_SSL_DTLS_SRTP itself; passing it as
        # -D too would be a macro redefinition warning on clang, so only the file is
        # exported as a compile definition. CMake applies it to the whole tree at
        # generation time, which is harmless here: only sources that include mbedtls
        # headers (the fetched ones) consume the macro.
        add_compile_definitions(MBEDTLS_USER_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/windows/mbedtls_custom_config.h")
        FetchContent_Declare(
            MbedTLS
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG v3.6.2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(MbedTLS)
        set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
        set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
        set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
        set(NO_MEDIA OFF CACHE BOOL "" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        # libsrtp (vendored by libdatachannel) defaults TEST_APPS ON, which would
        # register its drivers in this build's ctest; our gate must stay ours.
        set(TEST_APPS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
        # FindMbedTLS resolution: targets come from the fetched mbedTLS above.
        set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
        set(MbedTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
        set(MbedCrypto_LIBRARY mbedcrypto CACHE STRING "" FORCE)
        set(MbedX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
        # Pinned at v0.22.4: the stock source dispatches SRTP/RTCP from
        # DtlsTransport::ReadCallback while doRecv() still holds the SSL mutex.
        # An application callback that sends from inside that dispatch takes the
        # same mutex the other way round (DtlsTransport::send), which deadlocks.
        # patches/libdatachannel-0.22.4-defer-demux.patch defers the dispatch
        # until doRecv() has released the mutex, so the pin itself is untouched.
        # ExternalProject re-runs the patch step on later configures, so the
        # helper restores the fetched tree to the pin before applying.
        # The cache holds FETCHCONTENT_SOURCE_DIR_<UCNAME> empty by default, so
        # the value must be tested for content, not for being defined.
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
                          "-DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/patches/libdatachannel-0.22.4-defer-demux.patch"
                          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_libdatachannel_patch.cmake"
        )
        FetchContent_MakeAvailable(libdatachannel)
        set(KM_RTC_LIBS datachannel-static)
    else()
        find_package(LibDataChannel CONFIG REQUIRED)
        set(KM_RTC_LIBS LibDataChannel::LibDataChannel)
    endif()
    add_library(km_rtc_shared STATIC
        shared/receiver/rtc/bandwidth_estimator.cpp
        shared/receiver/rtc/twcc_receiver.cpp
        shared/receiver/rtc/enhanced_rtcp_session.cpp
        windows/receiver/rtc/peer_connection_manager.cpp)
    target_include_directories(km_rtc_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    # peer_connection_manager delegates the video path to the shared engine.
    target_link_libraries(km_rtc_shared PUBLIC km_media_core km_receiver_engine km_audio ${KM_RTC_LIBS})
    if(KM_FETCH_DATACHANNEL)
        # Same as windows/legacy_targets.cmake: name the fetched mbedTLS targets on
        # the consumer (resolves to real files and fixes build order) and add their
        # output directory, because FindMbedTLS binds bare libnames that reach the
        # linker as -l entries only searchable through -L.
        target_link_directories(km_rtc_shared PUBLIC ${mbedtls_BINARY_DIR}/library)
        target_link_libraries(km_rtc_shared PUBLIC mbedtls mbedcrypto mbedx509)
    endif()
    option(KM_INSTALLED_RTC_USES_LIBNICE "Installed LibDataChannel was built with libnice" OFF)
    target_compile_definitions(km_rtc_shared PRIVATE KM_TURN_TCP_TLS=$<BOOL:${KM_INSTALLED_RTC_USES_LIBNICE}>)
    if(MSVC)
        target_compile_options(km_rtc_shared PRIVATE /W4 /utf-8)
    else()
        target_compile_options(km_rtc_shared PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(KM_BUILD_TESTS)
        # Offline load smoke: builds the manager against libdatachannel and checks
        # configuration gating. No offer/answer, no ICE gathering, no connection.
        add_executable(km_rtc_load_tests tests/shared/test_rtc_load.cpp)
        target_include_directories(km_rtc_load_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/receiver/rtc)
        target_link_libraries(km_rtc_load_tests PRIVATE km_rtc_shared)
        add_test(NAME rtc_load COMMAND km_rtc_load_tests)
        # Offline makeAnswer wiring: worker -> manager (Initialize with the
        # session's RTC configuration -> answer generation). Synthetic transport,
        # local candidates only, no connection attempted or asserted.
        add_executable(km_rtc_answer_tests tests/shared/test_rtc_answer.cpp)
        target_include_directories(km_rtc_answer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/receiver/rtc)
        target_link_libraries(km_rtc_answer_tests PRIVATE km_rtc_shared km_signaling_worker)
        add_test(NAME rtc_answer COMMAND km_rtc_answer_tests)
    endif()
endif()
# Production Windows build remains: cmake -S windows -B build/windows.
