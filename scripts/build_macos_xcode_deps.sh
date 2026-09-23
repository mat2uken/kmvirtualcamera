#!/bin/sh
# Builds the km_macos_* static libraries for the Xcode project. No tests, no install.
set -eu
if [ "$(uname -s)" != Darwin ]; then
    echo 'Requires macOS and a selected Xcode SDK.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/macos-foundation"}
xcrun --sdk macosx --show-sdk-path >/dev/null
if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
        -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.3
fi
cmake --build "$build" --parallel
