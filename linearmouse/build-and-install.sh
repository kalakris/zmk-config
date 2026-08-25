#!/usr/bin/env bash
# Build the patched LinearMouse fork locally and install it.
#
# Fork lives at ~/src/linearmouse (github.com/kalakris/linearmouse,
# branch go60-inputscale). Requires full Xcode with license accepted.
#
# NOTE: the app is ad-hoc signed, so macOS ties the Accessibility grant
# to the exact binary — after every install you must re-grant it in
# System Settings > Privacy & Security > Accessibility.

set -euo pipefail

FORK_DIR="$HOME/src/linearmouse"
ARCHIVE="$FORK_DIR/build/LinearMouse.xcarchive"
APP="$ARCHIVE/Products/Applications/LinearMouse.app"

cd "$FORK_DIR"
[ -f Signing.xcconfig ] || ./Scripts/configure-code-signing
[ -f Version.xcconfig ] || ./Scripts/configure-version

rm -rf "$ARCHIVE"
xcodebuild archive -project LinearMouse.xcodeproj -scheme LinearMouse \
    -archivePath "$ARCHIVE" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= DEVELOPMENT_TEAM= \
    | tail -3

codesign --force --deep -s - "$APP"

osascript -e 'quit app "LinearMouse"' 2>/dev/null || true
sleep 1
rm -rf /Applications/LinearMouse.app
ditto "$APP" /Applications/LinearMouse.app

# Clear the now-stale accessibility grant so the app re-prompts cleanly.
tccutil reset Accessibility com.lujjjh.dev.LinearMouse || true

open -a LinearMouse
echo
echo "Installed. Re-grant Accessibility when prompted"
echo "(System Settings > Privacy & Security > Accessibility)."
