#!/bin/sh
# Builds the static libraries the Xcode project links, in two trees:
#   build/macos-foundation - extension target deps (also the gate tree; no RTC)
#   build/macos-rtc        - app target deps: the same km_macos/km_signaling
#                            libraries plus km_rtc_shared and the pinned RTC
#                            stack (mbedTLS/libdatachannel; the first configure
#                            needs network for FetchContent).
# Split so the gate (test_macos_foundation.sh, 10 tests) never sees RTC flags.
# No tests, no install.
set -eu
if [ "$(uname -s)" != Darwin ]; then
    echo 'Requires macOS and a selected Xcode SDK.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
foundation=${1:-"$root/build/macos-foundation"}
rtc=${2:-"$root/build/macos-rtc"}
xcrun --sdk macosx --show-sdk-path >/dev/null
if [ ! -f "$foundation/CMakeCache.txt" ] || grep -q '^KM_BUILD_RTC_SHARED:BOOL=ON' "$foundation/CMakeCache.txt"; then
    cmake -S "$root" -B "$foundation" -DCMAKE_BUILD_TYPE=Debug \
        -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.3 \
        -DKM_BUILD_RTC_SHARED=OFF -DKM_FETCH_DATACHANNEL=OFF
fi
cmake --build "$foundation" --parallel
if [ ! -f "$rtc/CMakeCache.txt" ] || ! grep -q '^KM_BUILD_RTC_SHARED:BOOL=ON' "$rtc/CMakeCache.txt"; then
    cmake -S "$root" -B "$rtc" -DCMAKE_BUILD_TYPE=Debug \
        -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.3 \
        -DKM_BUILD_RTC_SHARED=ON -DKM_FETCH_DATACHANNEL=ON
fi
cmake --build "$rtc" --parallel
