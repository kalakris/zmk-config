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

echo "Watching for Go60 bootloader volumes..."
echo "  LH firmware: $LH_FW"
echo "  RH firmware: $RH_FW"
echo "Press Ctrl+C to stop."

while true; do
    if [ -d /Volumes/GO60LHBOOT ]; then
        echo "GO60LHBOOT detected — flashing left half..."
        cp "$LH_FW" /Volumes/GO60LHBOOT/
        echo "Done. Left half flashed."
    fi
    if [ -d /Volumes/GO60RHBOOT ]; then
        echo "GO60RHBOOT detected — flashing right half..."
        cp "$RH_FW" /Volumes/GO60RHBOOT/
        echo "Done. Right half flashed."
    fi
    sleep 1
done
