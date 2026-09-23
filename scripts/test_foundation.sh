#!/bin/sh
# Portable common-core verification. No downloads, registration or signing.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/foundation"}
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DKM_BUILD_TESTS=ON
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
