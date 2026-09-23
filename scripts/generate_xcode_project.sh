#!/bin/sh
# Regenerates macos/KMVirtualCamera.xcodeproj from macos/project.yml (xcodegen).
set -eu
if [ "$(uname -s)" != Darwin ]; then
    echo 'Requires macOS.' >&2
    exit 2
fi
if ! command -v xcodegen >/dev/null 2>&1; then
    echo 'xcodegen not found (brew install xcodegen).' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
xcodegen generate --spec "$root/macos/project.yml" --project "$root/macos"
