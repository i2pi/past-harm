#!/usr/bin/env bash
#
# Build -> bundle -> Developer ID sign -> notarize -> staple "Past Harm.app",
# so it opens on anyone's Mac with a double-click (one "downloaded from the
# internet, open?" prompt, no Gatekeeper override, no network needed at first
# launch -- the ticket is stapled into the app).
#
# Output:
#   build/Past Harm.app
#   build/past-harm-macos.zip     the thing to hand out (unzip, double-click)
#
# The binary is universal (Apple silicon + Intel), macOS 11 or later. Opened
# from the Finder it runs full screen with the cursor hidden; Esc or q quits,
# f toggles full screen.
#
# Uses the Superchromat Developer ID certificate and notary profile set up for
# ~/src/superchromat/scion_controller_app (see its scripts/package_macos.sh for
# the one-time keychain setup).
#
#   SKIP_NOTARIZE=1   sign only, skip the Apple round-trip (NOT distributable)
set -euo pipefail

APP_NAME="Past Harm"
ARTIFACT="past-harm"
BUNDLE_ID="com.superchromat.past-harm"
VERSION="1.0"
IDENTITY="Developer ID Application: Superchromat Pty. Ltd. (DJ7F26GRG8)"
NOTARY_PROFILE="scion-notary"
MIN_OS="11.0"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
APP="build/${APP_NAME}.app"
ZIP="build/${ARTIFACT}-macos.zip"

echo "==> Building universal binary (arm64 + x86_64, macOS ${MIN_OS}+)"
make clean >/dev/null
make p10m1-past CC="clang -arch arm64 -arch x86_64 -mmacosx-version-min=${MIN_OS}"
lipo -info p10m1-past

echo "==> Assembling ${APP}"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/fonts"
cp p10m1-past "$APP/Contents/MacOS/p10m1-past"
cp fonts/D-DIN.otf fonts/D-DIN-Bold.otf "$APP/Contents/Resources/fonts/"
[[ -f fonts/OFL.txt ]] && cp fonts/OFL.txt "$APP/Contents/Resources/fonts/"

ICONSET="build/AppIcon.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s macos/icon-1024.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d macos/icon-1024.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>				<string>${APP_NAME}</string>
	<key>CFBundleDisplayName</key>		<string>${APP_NAME}</string>
	<key>CFBundleIdentifier</key>		<string>${BUNDLE_ID}</string>
	<key>CFBundleExecutable</key>		<string>p10m1-past</string>
	<key>CFBundleIconFile</key>			<string>AppIcon</string>
	<key>CFBundlePackageType</key>		<string>APPL</string>
	<key>CFBundleShortVersionString</key>	<string>${VERSION}</string>
	<key>CFBundleVersion</key>			<string>${VERSION}</string>
	<key>CFBundleInfoDictionaryVersion</key>	<string>6.0</string>
	<key>LSMinimumSystemVersion</key>	<string>${MIN_OS}</string>
	<key>LSApplicationCategoryType</key>	<string>public.app-category.entertainment</string>
	<key>NSHighResolutionCapable</key>	<true/>
	<key>NSHumanReadableCopyright</key>	<string>Joshua Reich, Generative Works 1996-2026</string>
</dict>
</plist>
PLIST
plutil -lint "$APP/Contents/Info.plist"

# Hardened runtime + secure timestamp (required for notarization). No
# entitlements: it only draws with OpenGL and plays audio.
echo "==> Signing"
codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
desc="$(codesign -dv --verbose=2 "$APP" 2>&1 || true)"
case "$desc" in
  *"Authority=Developer ID Application"*) ;;
  *) echo "package_macos: not Developer ID signed" >&2; exit 1 ;;
esac

echo "==> Zipping"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"

if [[ "${SKIP_NOTARIZE:-0}" == "1" ]]; then
  echo "==> SKIP_NOTARIZE=1 -- signed but NOT notarized. Do not distribute."
else
  echo "==> Notarizing (a few minutes)"
  xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
  echo "==> Stapling"
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  rm -f "$ZIP"
  ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
fi

echo "==> Gatekeeper assessment"
spctl -a -vvv --type execute "$APP" || true

# leave a native build behind for the command line
make clean >/dev/null && make >/dev/null

echo
echo "==> Done"
echo "    $APP"
echo "    $ZIP ($(du -h "$ZIP" | cut -f1))"
