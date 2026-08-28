#!/usr/bin/env python3
"""Analyze a raw-touch-monitor.swift capture: per-pad frame cadence.

Usage: analyze-touch-timing.py capture.csv

For each pad, compares host *arrival* spacing (what pointer motion is timed
by) against *device* sample spacing (the v3 100 µs timestamps, what scroll
is timed by). Bursty arrival with clean device spacing = the split-relay
batching that makes LH pointer motion choppy.
"""

import sys
import csv
import statistics
from collections import defaultdict

IDLE_GAP_S = 0.150  # frame silence longer than this = separate touch
TICK_S = 100e-6     # device timestamp unit
TS_WRAP = 65536     # device timestamp wraps at 6.5536 s
BURST_S = 0.002     # arrival deltas below this = same transport batch


def percentile(sorted_values, p):
    if not sorted_values:
        return float("nan")
    k = (len(sorted_values) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (k - lo)


def stats_ms(deltas):
    if not deltas:
        return "  (no data)"
    ms = sorted(d * 1000 for d in deltas)
    return (f"  n={len(ms)}  mean={statistics.mean(ms):6.2f}  sd={statistics.pstdev(ms):6.2f}  "
            f"p50={percentile(ms, 50):6.2f}  p90={percentile(ms, 90):6.2f}  "
            f"p99={percentile(ms, 99):6.2f}  max={max(ms):7.2f} ms")


def histogram(deltas, edges_ms=(0, 1, 2, 5, 8, 12, 16, 25, 50, 150)):
    if not deltas:
        return
    ms = [d * 1000 for d in deltas]
    buckets = []
    for i, lo in enumerate(edges_ms):
        hi = edges_ms[i + 1] if i + 1 < len(edges_ms) else float("inf")
        count = sum(1 for v in ms if lo <= v < hi)
        buckets.append((lo, hi, count))
    peak = max(count for _, _, count in buckets) or 1
    for lo, hi, count in buckets:
        if count == 0:
            continue
        label = f"{lo:>4g}-{hi:<4g}" if hi != float("inf") else f"{lo:>4g}+   "
        bar = "#" * max(1, round(40 * count / peak))
        print(f"    {label} ms {count:6d} {bar}")


def main(path):
    frames = defaultdict(list)  # pad -> [(host_s, seq, ts_ticks, touched)]
    with open(path) as f:
        for row in csv.DictReader(f):
            frames[int(row["pad"])].append((
                int(row["host_ns"]) / 1e9,
                int(row["seq"]),
                int(row["ts_ticks"]),
                int(row["flags"]) & 1,
            ))

    if not frames:
        sys.exit("no frames in capture")

    for pad in sorted(frames):
        rows = frames[pad]
        span = rows[-1][0] - rows[0][0]
        touches = sum(1 for i, r in enumerate(rows) if not r[3] and (i == 0 or rows[i - 1][3]))
        print(f"\npad {pad}: {len(rows)} frames over {span:.1f} s, ~{touches} lift-offs")

        arrival, device, dropped, burst_sizes = [], [], 0, []
        current_burst = 1
        for (h0, s0, t0, _), (h1, s1, t1, _) in zip(rows, rows[1:]):
            dh = h1 - h0
            if dh > IDLE_GAP_S:  # between touches: close burst, skip deltas
                burst_sizes.append(current_burst)
                current_burst = 1
                continue
            arrival.append(dh)
            device.append(((t1 - t0) % TS_WRAP) * TICK_S)
            dropped += (s1 - s0 - 1) % 256
            if dh < BURST_S:
                current_burst += 1
            else:
                burst_sizes.append(current_burst)
                current_burst = 1
        burst_sizes.append(current_burst)

        print(f"  dropped frames (seq gaps): {dropped}")
        print("  host arrival spacing:")
        print(stats_ms(arrival))
        print("  device sample spacing:")
        print(stats_ms(device))
        if arrival:
            batched = sum(1 for d in arrival if d < BURST_S)
            print(f"  batching: {100 * batched / len(arrival):.0f}% of frames arrive <{BURST_S * 1000:.0f} ms "
                  f"after the previous; mean batch size {statistics.mean(burst_sizes):.2f}, "
                  f"max {max(burst_sizes)}")
        print("  arrival spacing histogram:")
        histogram(arrival)
        print("  device spacing histogram:")
        histogram(device)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
