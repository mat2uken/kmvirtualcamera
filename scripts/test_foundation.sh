#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${KM_BUILD_DIR:-"$root/build/foundation"}
cmake -S "$root" -B "$build" -DKM_BUILD_TESTS=ON -DKM_BUILD_MACOS=OFF -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
