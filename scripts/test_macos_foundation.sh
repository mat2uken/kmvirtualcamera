#!/bin/sh
# Unsigned compile/media smoke only; DOES NOT install a virtual camera.
set -eu
if [ "$(uname -s)" != Darwin ]; then
    echo 'Requires macOS and a selected Xcode SDK.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/macos-foundation"}
xcrun --sdk macosx --show-sdk-path >/dev/null
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.3
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
