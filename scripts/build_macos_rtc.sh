#!/bin/sh
# RTC integration build: fetch pinned libdatachannel+mbedTLS (same pins as the
# Windows build), compile km_rtc_shared and the offline rtc_load smoke, then run
# the full test set. First configure needs network for FetchContent.
# Does NOT install a virtual camera and does NOT claim a connection.
set -eu
if [ "$(uname -s)" != Darwin ]; then
    echo 'Requires macOS and a selected Xcode SDK.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/macos-rtc"}
xcrun --sdk macosx --show-sdk-path >/dev/null
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.3 \
    -DKM_BUILD_RTC_SHARED=ON -DKM_FETCH_DATACHANNEL=ON
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
