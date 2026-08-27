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
# fired on first sight loses the race with "Permission denied". Two other
# failure modes seen in practice: the mount vanishes sub-second and comes
# back (bouncy USB during bootloader entry), and a stale mountpoint makes
# macOS remount under "GO60RHBOOT 1" — so retry fast (0.25s) and re-resolve
# the target by glob on every attempt. Never let a failed copy kill the
# watcher (set -e would).
flash_half() {
    local vol="$1" fw="$2" err="" target=""
    echo "$vol detected — flashing $(basename "$fw")..."
    for _ in $(seq 1 80); do
        target=$(ls -d "/Volumes/$vol"* 2>/dev/null | head -1)
        if [ -n "$target" ] && err=$(cp "$fw" "$target/" 2>&1); then
            echo "  OK — waiting for $target to unmount (board reboots on success)"
            # Without this the next poll finds the volume still mounted and
            # flashes again; the UF2 bootloader unmounts once it has the image.
            for _ in $(seq 1 30); do
                [ -d "$target" ] || { echo "  $vol unmounted — flashed."; return 0; }
                sleep 1
            done
            echo "  WARNING: $target still mounted after 30s — flash may not have applied" >&2
            return 1
        fi
        sleep 0.25
    done
    echo "  ERROR: could not write to $vol after 20s (last error: ${err:-volume never mounted})" >&2
    return 1
}

echo "Watching for Go60 bootloader volumes..."
echo "  LH firmware: $LH_FW"
echo "  RH firmware: $RH_FW"
echo "Press Ctrl+C to stop."

while true; do
    ls -d /Volumes/GO60LHBOOT* >/dev/null 2>&1 && flash_half GO60LHBOOT "$LH_FW" || true
    ls -d /Volumes/GO60RHBOOT* >/dev/null 2>&1 && flash_half GO60RHBOOT "$RH_FW" || true
    sleep 1
done
