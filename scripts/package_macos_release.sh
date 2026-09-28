#!/bin/sh
# macOS Release packaging: build, sign, notarize, staple, DMG.
# Requires: Developer ID Application cert in keychain,
#           App Store Connect API key (.p8) for notarytool.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=${1:-1.0.0}
outdir=${2:-"$root/dist-macos"}
app_name="KMVirtualCamera"
app_bundle="KMVirtualCamera.app"
dmg_name="KMVirtualCamera-v${version}-macOS.dmg"
dmg_path="$outdir/$dmg_name"

# Notarytool credentials (passed via environment or set here)
NOTARY_KEY_ID=${NOTARY_KEY_ID:-"6YDG75Z5N2"}
NOTARY_ISSUER=${NOTARY_ISSUER:-"69a6de6e-a730-47e3-e053-5b8c7c11a4d1"}
NOTARY_KEY_PATH=${NOTARY_KEY_PATH:-"$HOME/Downloads/AuthKey_6YDG75Z5N2.p8"}

# Signing identity
SIGN_ID="Developer ID Application: Kenichi Matsumoto (K7VNGA9K78)"

mkdir -p "$outdir"

echo "=== [1/6] Building Release ==="
xcodebuild -project "$root/macos/KMVirtualCamera.xcodeproj" \
    -scheme "$app_name" \
    -configuration Release \
    -allowProvisioningUpdates \
    -derivedDataPath "$root/macos/DerivedData" \
    ARCHS=arm64 \
    ONLY_ACTIVE_ARCH=NO \
    build 2>&1 | tail -3

app_path="$root/macos/DerivedData/Build/Products/Release/$app_bundle"
if [ ! -d "$app_path" ]; then
    echo "ERROR: app bundle not found at $app_path" >&2
    exit 1
fi

echo "=== [2/6] Signing extension with Developer ID ==="
ext_path="$app_path/Contents/Library/SystemExtensions/jp.yasagure.kmvirtualcamera.macos.camext.systemextension"
codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
    --entitlements "$root/macos/config/CameraExtension.entitlements" \
    "$ext_path"

echo "=== [3/6] Signing app with Developer ID ==="
codesign --force --deep --sign "$SIGN_ID" --timestamp --options runtime "$app_path"
codesign --verify --deep --strict --verbose=2 "$app_path"

echo "=== [4/6] Creating DMG ==="
tmp_dmg="$outdir/tmp.dmg"
rm -f "$tmp_dmg" "$dmg_path"
hdiutil create -volname "$app_name" -srcfolder "$app_path" -ov -format UDRW "$tmp_dmg"
mount_point=$(hdiutil attach -readwrite -noverify "$tmp_dmg" | grep -o '/Volumes/.*')
ln -sf /Applications "$mount_point/Applications"
hdiutil detach "$mount_point"
hdiutil convert "$tmp_dmg" -format UDZO -o "$dmg_path"
rm -f "$tmp_dmg"

echo "=== [5/6] Signing DMG ==="
codesign --sign "$SIGN_ID" --timestamp "$dmg_path"
codesign --verify --verbose=2 "$dmg_path"

echo "=== [6/6] Notarizing and stapling ==="
xcrun notarytool submit "$dmg_path" \
    --key "$NOTARY_KEY_PATH" \
    --key-id "$NOTARY_KEY_ID" \
    --issuer "$NOTARY_ISSUER" \
    --wait

xcrun stapler staple "$dmg_path"
xcrun stapler validate "$dmg_path"
spctl -a -t open --context context:primary-signature -v "$dmg_path"

echo ""
echo "=== SUCCESS ==="
echo "DMG: $dmg_path"
ls -lh "$dmg_path"
