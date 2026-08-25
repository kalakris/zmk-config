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
cd "$FORK_DIR"

# Regenerate signing config each run (unconditionally — agents' unsigned
# test builds may leave a stale empty-team file behind). The fork's own
# script auto-discovers the "Apple Development" cert and its team ID.
./Scripts/configure-code-signing

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

# Remove the archive copy so Spotlight never offers a stale duplicate
# (multiple on-disk copies cause repeated Accessibility prompts).
rm -rf "$FORK_DIR/build"

open -a LinearMouse
echo
echo "Installed (signed). Accessibility grant persists across rebuilds;"
echo "grant once if this is the first signed install."
