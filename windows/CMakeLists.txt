cmake_minimum_required(VERSION 3.20)
project(WebRtcBridgeWindows VERSION 1.0.0 LANGUAGES C CXX)
# Preserve the existing target definitions in one included file, without a second copy.
# USE_NICE may be supplied to libdatachannel with -DUSE_NICE=ON; install libnice first.
option(KM_ENABLE_OPUS "Decode browser Opus audio with pinned libopus" ON)
include(${CMAKE_CURRENT_LIST_DIR}/legacy_targets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/opus.cmake)
foreach(target IN ITEMS Receiver test_receiver_startup test_webrtc_dtls)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared/receiver/audio/opus_rtp_decoder.cpp)
    target_compile_definitions(${target} PRIVATE KM_TURN_TCP_TLS=$<BOOL:${USE_NICE}>)
    km_configure_audio(${target})
endforeach()
# The older test targets omitted the DataChannel translation unit.
foreach(target IN ITEMS test_receiver_startup test_webrtc_dtls)
    target_sources(${target} PRIVATE receiver/codec/dc_video_depacketizer.cpp)
endforeach()
target_link_libraries(Receiver PRIVATE avrt shell32)
target_link_libraries(test_receiver_startup PRIVATE avrt shell32)
target_include_directories(test_h264_depacketizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
