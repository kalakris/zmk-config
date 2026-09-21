#!/usr/bin/env python3
"""Per-pad timing stats for a raw-touch-monitor CSV: device-timestamp
spacing (are the stamps honest?), host-arrival spacing (how batched is
delivery?), and lateness of arrival against stamped time, min-anchored per
touch (the resampling latency a pad needs). Only same-touch consecutive-seq
frame pairs are counted; 16-bit timestamps are unwrapped.

Usage: analyze-touch-lateness.py capture.csv [more.csv ...]
"""
import csv, statistics as st, collections, sys

def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(p * len(v)))]

def analyze(f):
    rows = list(csv.DictReader(open(f)))
    per = {}
    for r in rows: per.setdefault((r['dev'], r['pad']), []).append(r)
    print(f"== {f} ({len(rows)} frames)")
    for (dev, pad), rs in sorted(per.items()):
        dts = []; hts = []; res = []; series = []; prev = None; last = None; unw = 0; prevunw = 0
        h = collections.Counter(); gaps = 0
        for r in rs:
            t = int(r['ts_ticks']); unw = t if last is None else unw + (t - last) % 65536; last = t
            off = int(r['host_ns']) / 1e6 - unw * 0.1
            touched = int(r['flags']) & 1
            both = prev is not None and int(prev['flags']) & 1 and touched
            cons = both and (int(prev['seq']) + 1) % 256 == int(r['seq'])
            if both and not cons: gaps += 1
            if cons:
                d = (unw - prevunw) * 0.1
                if d <= 300:
                    dts.append(d); hts.append((int(r['host_ns']) - int(prev['host_ns'])) / 1e6); h[round(d)] += 1
                series.append(off)
            else:
                if len(series) >= 10: m = min(series); res += [o - m for o in series]
                series = [off] if touched else []
            prev = r; prevunw = unw
        if len(series) >= 10: m = min(series); res += [o - m for o in series]
        if len(dts) < 20: print(f"  dev={dev} pad={pad}: only {len(dts)} pairs"); continue
        print(f"  dev={dev} pad={pad} n={len(dts)} seq gaps while touched={gaps}")
        print("     device-ts spacing: sd=%.2f ms  hist(ms:%%): " % st.pstdev(dts)
              + ", ".join(f"{k}:{100*v/len(dts):.1f}" for k, v in sorted(h.items()) if 100 * v / len(dts) >= 0.5))
        print(f"     host-arrival spacing: sd={st.pstdev(hts):.2f} p5={pct(hts,.05):.1f} p95={pct(hts,.95):.1f} ms")
        print(f"     lateness vs stamped time: p50={pct(res,.5):.1f} p90={pct(res,.9):.1f} p95={pct(res,.95):.1f} p99={pct(res,.99):.1f} max={max(res):.1f} ms")

for f in sys.argv[1:]: analyze(f)
