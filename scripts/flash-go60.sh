#!/usr/bin/env bash
# Flash Go60 halves: watch for a half to enter bootloader via USB and copy
# the matching firmware. Exits on its own once every requested half has
# flashed, so a backgrounded run tells its caller (an agent, a shell &&)
# exactly when flashing is done.
#
# Usage: ./scripts/flash-go60.sh [firmware-dir] [--halves both|lh|rh]
#                                [--timeout SECONDS] [--loop]
#   firmware-dir   defaults to firmware/main/firmware/
#   --halves       which halves to wait for (default both). Each is flashed
#                  once; the watcher exits when all requested halves are done.
#   --timeout      optional idle timeout in seconds: exit 2 if a requested
#                  half has not shown up in that long since the last event.
#                  Off by default - the watcher waits indefinitely, so
#                  leaving the desk mid-flash and coming back just works.
#   --loop         old behaviour: keep watching forever (Ctrl+C to stop).
#
# Exit codes: 0 all requested halves flashed; 2 idle timeout (only with
# --timeout); 3 another
# watcher is already running (never run two - they race for the volume);
# 64 usage; 66 firmware missing.
#
# Only one watcher can run at a time: a lock directory at /tmp/flash-go60.lock
# holds the owner's PID and is cleaned up on exit (stale locks from dead
# processes are reclaimed).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
FW_DIR="$REPO_DIR/firmware/main/firmware"
HALVES="both"
TIMEOUT=0
LOOP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --halves) HALVES="${2:-}"; shift 2 ;;
        --timeout) TIMEOUT="${2:-}"; shift 2 ;;
        --loop) LOOP=1; shift ;;
        -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --*) echo "unknown option: $1" >&2; exit 64 ;;
        *) FW_DIR="$1"; shift ;;
    esac
done
case "$HALVES" in both|lh|rh) ;; *) echo "--halves must be both, lh or rh" >&2; exit 64 ;; esac
case "$TIMEOUT" in ''|*[!0-9]*) echo "--timeout must be a whole number of seconds (0 = none)" >&2; exit 64 ;; esac

LH_FW="$FW_DIR/go60_lh-zmk.uf2"
RH_FW="$FW_DIR/go60_rh-zmk.uf2"
for f in "$LH_FW" "$RH_FW"; do
    if [ ! -f "$f" ]; then
        echo "Error: firmware not found: $f" >&2
        exit 66
    fi
done

# Single-instance lock. Two watchers race each other for the bootloader
# volume, which has cost real flashes; refuse rather than warn.
LOCK_DIR="/tmp/flash-go60.lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    owner="$(cat "$LOCK_DIR/pid" 2>/dev/null || echo "")"
    if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
        echo "Error: another flash watcher is already running (pid $owner). Never run two; wait for it or kill it." >&2
        exit 3
    fi
    # Stale lock from a dead watcher: reclaim it.
    rm -rf "$LOCK_DIR"
    mkdir "$LOCK_DIR"
fi
echo $$ > "$LOCK_DIR/pid"
trap 'rm -rf "$LOCK_DIR"' EXIT

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
                [ -d "$target" ] || { echo "  $vol unmounted — flashed at $(date +%H:%M:%S)."; return 0; }
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

want_lh=0; want_rh=0
[ "$HALVES" != "rh" ] && want_lh=1
[ "$HALVES" != "lh" ] && want_rh=1
done_lh=0; done_rh=0

echo "Watching for Go60 bootloader volumes (halves: $HALVES; $( [ "$TIMEOUT" -gt 0 ] && echo "idle timeout ${TIMEOUT}s" || echo "no timeout - waits until you get to it"))..."
[ $want_lh = 1 ] && echo "  LH firmware: $LH_FW"
[ $want_rh = 1 ] && echo "  RH firmware: $RH_FW"
echo "Put each half into bootloader over USB; this exits when every requested half has flashed."

last_event=$(date +%s)
while true; do
    if [ $want_lh = 1 ] && [ $LOOP = 1 -o $done_lh = 0 ] && ls -d /Volumes/GO60LHBOOT* >/dev/null 2>&1; then
        if flash_half GO60LHBOOT "$LH_FW"; then done_lh=1; fi
        last_event=$(date +%s)
    fi
    if [ $want_rh = 1 ] && [ $LOOP = 1 -o $done_rh = 0 ] && ls -d /Volumes/GO60RHBOOT* >/dev/null 2>&1; then
        if flash_half GO60RHBOOT "$RH_FW"; then done_rh=1; fi
        last_event=$(date +%s)
    fi

    if [ $LOOP = 0 ] && [ $done_lh -ge $want_lh ] && [ $done_rh -ge $want_rh ]; then
        summary=""
        [ $want_lh = 1 ] && summary="LH"
        [ $want_rh = 1 ] && summary="${summary:+$summary + }RH"
        echo "DONE: $summary flashed. Watcher exiting."
        exit 0
    fi

    if [ "$TIMEOUT" -gt 0 ] && [ $(( $(date +%s) - last_event )) -ge "$TIMEOUT" ]; then
        remaining=""
        [ $want_lh = 1 ] && [ $done_lh = 0 ] && remaining="LH"
        [ $want_rh = 1 ] && [ $done_rh = 0 ] && remaining="${remaining:+$remaining + }RH"
        echo "TIMEOUT: no bootloader volume for ${TIMEOUT}s; still waiting for: $remaining. Watcher exiting." >&2
        exit 2
    fi
    sleep 1
done
