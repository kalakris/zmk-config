#!/usr/bin/env bash
# Build the patched LinearMouse fork locally and install it.
#
# Fork lives at ~/src/linearmouse (github.com/kalakris/linearmouse,
# branch go60-inputscale). Requires full Xcode with license accepted.
#
# Builds are signed with the local "Apple Development" certificate
# (team 7WBD7URF58), so the Accessibility grant persists across
# rebuilds — only the very first certificate-signed install needs a
# (final) re-grant.

set -euo pipefail

FORK_DIR="$HOME/src/linearmouse"
ARCHIVE="$FORK_DIR/build/LinearMouse.xcarchive"
APP="$ARCHIVE/Products/Applications/LinearMouse.app"
TEAM_ID="7WBD7URF58"

cd "$FORK_DIR"

# Regenerate signing config each run so we always build with the real
# certificate (the repo template ships with an empty team, and agents'
# unsigned test builds may leave it that way).
cp Signing.xcconfig.tpl Signing.xcconfig
sed -i '' "s/DEVELOPMENT_TEAM =/DEVELOPMENT_TEAM = $TEAM_ID/" Signing.xcconfig
echo "CODE_SIGN_IDENTITY = Apple Development" >> Signing.xcconfig

[ -f Version.xcconfig ] || ./Scripts/configure-version

rm -rf "$ARCHIVE"
xcodebuild archive -project LinearMouse.xcodeproj -scheme LinearMouse \
    -archivePath "$ARCHIVE" \
    | tail -3

codesign --verify --deep "$APP"

osascript -e 'quit app "LinearMouse"' 2>/dev/null || true
sleep 1
rm -rf /Applications/LinearMouse.app
ditto "$APP" /Applications/LinearMouse.app

open -a LinearMouse
echo
echo "Installed (signed, team $TEAM_ID). Accessibility grant persists"
echo "across rebuilds; grant once if this is the first signed install."
