#!/usr/bin/env bash
# Watch for a Go60 half to enter bootloader via USB and flash the correct firmware.
# Usage: ./scripts/flash-go60.sh [firmware-dir]
#   firmware-dir defaults to firmware/main/firmware/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
FW_DIR="${1:-$REPO_DIR/firmware/main/firmware}"

LH_FW="$FW_DIR/go60_lh-zmk.uf2"
RH_FW="$FW_DIR/go60_rh-zmk.uf2"

for f in "$LH_FW" "$RH_FW"; do
    if [ ! -f "$f" ]; then
        echo "Error: firmware not found: $f" >&2
        exit 1
    fi
done

# The volume appears in /Volumes a moment before it is writable, so a copy
# fired on first sight loses the race with "Permission denied". Retry until it
# takes, and never let a failed copy kill the watcher (set -e would).
flash_half() {
    local vol="$1" fw="$2"
    echo "$vol detected — flashing $(basename "$fw")..."
    for _ in $(seq 1 15); do
        if cp "$fw" "/Volumes/$vol/" 2>/dev/null; then
            echo "  OK — waiting for $vol to unmount (board reboots on success)"
            # Without this the next poll finds the volume still mounted and
            # flashes again; the UF2 bootloader unmounts once it has the image.
            for _ in $(seq 1 30); do
                [ -d "/Volumes/$vol" ] || { echo "  $vol unmounted — flashed."; return 0; }
                sleep 1
            done
            echo "  WARNING: $vol still mounted after 30s — flash may not have applied" >&2
            return 1
        fi
        sleep 1
    done
    echo "  ERROR: could not write to /Volumes/$vol after 15 attempts" >&2
    return 1
}

echo "Watching for Go60 bootloader volumes..."
echo "  LH firmware: $LH_FW"
echo "  RH firmware: $RH_FW"
echo "Press Ctrl+C to stop."

while true; do
    [ -d /Volumes/GO60LHBOOT ] && flash_half GO60LHBOOT "$LH_FW" || true
    [ -d /Volumes/GO60RHBOOT ] && flash_half GO60RHBOOT "$RH_FW" || true
    sleep 1
done
