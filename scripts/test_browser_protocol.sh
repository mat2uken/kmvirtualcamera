#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
# Install existing cloud dev dependencies with npm ci --prefix cloud when needed.
if [ -x cloud/node_modules/.bin/tsc ]; then tsc=cloud/node_modules/.bin/tsc; else tsc=$(command -v tsc); fi
build=${KM_BROWSER_TEST_DIR:-"$root/build/browser-protocol"}
"$tsc" --strict --target ES2022 --module commonjs --lib ES2022,DOM --outDir "$build" \
  cloud/web/src/dc_packetizer.ts cloud/web/src/webcodecs_sender.ts
printf '{"type":"commonjs"}\n' > "$build/package.json"
node tests/browser/test_packetizer.cjs "$build"
