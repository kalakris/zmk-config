# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-28.
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

## a. Fix the dead-pad boot race (firmware, real bug) — DONE 2026-08-28

**Fixed 2026-08-28.** The amended patch 3/3 (clear SW_CC and re-verify
DR after the recalibrate) is pushed as `cirque-input-module` branch
`intree-driver-fix` (SHA `89a08962`); the pinned revision in
`config/west.yml` is bumped and pushed on zmk-config `main`. **Hardware
verification pending**: reflash **both halves** and watch a few
boots. Fixing this was the MUST-FIX gate before upstreaming the
ERA/recalibrate patches to Zephyr (see
[upstreaming-todo.md](upstreaming-todo.md)).

## b. Re-land the LinearMouse cleanup — DONE 2026-08-28

The reverted `a204c40` batch was **exonerated** by the discriminating
deploy from [linearmouse-reland-plan.md](linearmouse-reland-plan.md):
the exact batch build ran clean for 26 min of live use, pinning the
original regression on deploy-environment TCC-grant state, not the
code. All items re-landed on `main` as three commits (frame parser +
guard order; VerifiedDevice merge + capabilities; lazy watchdog +
process-routed release, adapted to the per-pad dict), minus the
obsolete pad-0 collapse (conflicts with pad-1 arbitration). Full unit
suite green, all six live scroll checks passed. The old
`go60-inputscale` branch is deleted (remote); local tag
`pre-restructure-go60-inputscale` keeps the forensic history reachable.

## c. LH pointer choppiness — FIXED 2026-08-28 (wired-split poll cadence)

**Root-caused, fixed, and hardware-validated the same day.** The fix is
the two-line Kconfig change in `config/go60_rh.conf` (poll timeouts
20/15 → 3/5 ms); with it, the wire beats every other configuration:

| Split link | Host link | Dropped | Cadence at host |
|---|---|---|---|
| Wire (stock 20/15) | USB | **45%** | 2–3-frame bursts every ~22.5 ms |
| Wire (stock 20/15) | BLE | 0% | same 22.5 ms bursts, re-batched by the ~15 ms host BLE interval |
| Radio | BLE | 0% | ~7.5/15 ms alternation, small batches |
| Radio | USB | 0% | ~7.5/15 ms alternation, no batching |
| **Wire (tuned 3/5)** | USB | **0%** | **~10 ms per-frame, batch size 1.00, σ≈2 ms — LH ≈ RH parity** |

Validated over ~30 s of continuous LH streaming (zero drops, zero
batching, LH device timestamps now honest to ±2 ms) plus typing with no
missed LH keystrokes (the collision risk of the tighter half-duplex
turnaround did not materialize; margin math said ~1 ms per exchange at
921600 baud). LH key latency improves too — keys ride the same poll
loop.

The RH pad is a clean 10 ms metronome in every configuration. **Root
cause: the wired split transport's batched delivery** — the Go60's
wired split is *half-duplex polled*: the peripheral only transmits when
the central sends a `POLL_EVENTS` command, and the central schedules
the next poll `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT`
(default 20, applied as **ms** in `publish_events_work`,
`app/src/split/wired/central.c`) after the previous response — plus
~2.5 ms of turnaround, giving the measured ~22.5 ms burst period. See
raw-touch.md → "Split-link timing" for the full mechanism. (Running the
halves wireless was the interim mitigation before the tuning landed; it
is no longer needed.) Captures lived in
`/tmp/claude-501/capture[2-6].csv` (wire+USB, wire+BLE, radio+BLE,
radio+USB, tuned-wire+USB) — `/tmp` is volatile; re-capture if gone.

Residual items, all **optional polish** now (the fixes they were
designed for no longer occur under the tuned wire):

1. A small USB TX ring in the module's `src/usb_hid.c` — the
   single-slot `hid_sem` drop path only bites on multi-frame bursts,
   which the tuned poll cadence no longer produces. Worth doing only as
   robustness for users running stock poll timings.
2. Central-side timestamp reconstruction (LH stamps are relay-arrival,
   not sample time — but the error is now ±2 ms over the tuned wire,
   ±4 ms over radio; the split relay carries no timestamps, so this
   must be inferred). Only worth it if scroll feel ever regresses.
3. Host pointer synthesis from stream frames — refinement, not a fix;
   pointer deltas now arrive per-frame anyway.

Watch-item: battery on both halves (more UART/CPU wakeups from the 3×
faster idle poll; each half runs on its own battery — the wire carries
data, not power). If LH keystrokes ever start dropping with the wire
in, that's the half-duplex collision signature — back off to e.g. 5/8.

## d. Demo video

Shot list agreed: **30-second cut, cold-open on the catch, a contrast
cut with LinearMouse quit** (÷24 wheel fallback vs the stream), **hands
cam tight on the right half**. The module README is written demo-first;
the release leads with this clip.

## e. Public release train

In order: host cleanup re-landed (b) → video (d) → flip
`kalakris/zmk-raw-touch` public → un-vendor (uncomment the west entry in
`config/west.yml`, delete `vendor/`, drop the two `cmake-args` from
`build.yaml`) → announce.

## f. Notarized releases (optional)

The LinearMouse fork inherits upstream's release pipeline; making it
produce installable signed builds needs a paid Apple Developer Program
membership, ~7 repo secrets (signing cert, notarization credentials,
etc.), and a tag push. Optional extra: build provenance via
`actions/attest-build-provenance`.

## g. Upstream patches

- Cirque driver patches to **Zephyr** (0xFF/SW_DR guard, ERA edge
  sensitivity — after fixing (a)): **message Pete Johanson first**; the
  patches are his unfinished migration.
- `inputScale` PR to **LinearMouse** (first two commits of the fork,
  self-contained), framed as generalizing
  `LogitechHighResolutionWheelNormalizer`.

## h. Delete the `kalakris/zmk` fork

Nothing depends on it; `cfc4b3e6` is already salvaged as
`patches/zmk-skip-empty-mouse-report-syncs.patch` on `main`. Just
delete the GitHub repo and `~/src/zmk`.

## i. Drop protocol v2 from the LinearMouse fork — DONE 2026-08-28

Landed on the fork's `main` (net −142 lines): fixed 11-byte frame
layout, capability validation admits v3 alone, v2 tests deleted and
the capability tests re-anchored on the v3 fixture (ground-truth
direction table assertions unchanged). Unit suite green, live scroll
test passed. **Standing caveat**: the `v0-prototype` rollback binaries
(and the historical `raw-touch` branch build) speak v2 — rolling back
to them now gets wheel-fallback scrolling only.
