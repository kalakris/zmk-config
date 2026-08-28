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

## b. Re-land the LinearMouse cleanup, item by item

`~/src/linearmouse@go60-inputscale`: the review-pass cleanup batch
`a204c40` caused a live regression (first touch dead, then added
latency) within a minute of deploy and was reverted wholesale in
`29a8f88` — root cause never isolated; the archive building clean proved
nothing. The revert commit lists the seven items. Re-land them **one at
a time, with a scroll test between deploys**, and announce every deploy.

## c. LH pointer choppiness — root-caused: the WIRED split transport

**Measurement DONE 2026-08-28.** Captured LH frame timing in a 2×2
matrix (split link × host link) with the new passive monitor
(`scripts/raw-touch-monitor.swift` + `scripts/analyze-touch-timing.py`):

| Split link | Host link | Dropped | Cadence at host |
|---|---|---|---|
| Wire  | USB | **45%** | 2–3-frame bursts every ~22.5 ms |
| Wire  | BLE | 0% | same 22.5 ms bursts, re-batched by the ~15 ms host BLE interval |
| Radio | BLE | 0% | ~7.5/15 ms alternation, small batches |
| Radio | USB | 0% | ~7.5/15 ms alternation, no batching — cleanest |

The RH pad is a clean 10 ms metronome in every configuration. **Root
cause: the wired split transport's batched delivery** — the Go60's
wired split is *half-duplex polled*: the peripheral only transmits when
the central sends a `POLL_EVENTS` command, and the central schedules
the next poll `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT`
(default 20, applied as **ms** in `publish_events_work`,
`app/src/split/wired/central.c`) after the previous response — plus
~2.5 ms of turnaround, giving the measured ~22.5 ms burst period. See
raw-touch.md → "Split-link timing" for the full mechanism. Immediate
mitigation: **run the halves wireless** — the user confirms LH pointer
feels significantly better with the wire unplugged. Captures live in
`/tmp/claude-501/capture[2-5].csv` (wire+USB, wire+BLE, radio+BLE,
radio+USB) — but `/tmp` is volatile; re-capture if gone.

Remaining work:

1. **Tune the wired transport** (no fork needed — plain Kconfig, central
   side only, so `config/go60_rh.conf`): try
   `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT=3` and
   `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_TIMEOUT=5` → ~5 ms poll
   cadence, ≤1 frame per poll. Risks: line-turnaround collisions on the
   single half-duplex wire if the margin is too thin (CRC catches them,
   but colliding envelopes are *dropped* — including key events; watch
   for "Prefix mismatch" behavior, i.e. missed keys), and more UART/CPU
   wakeups on both halves (each half runs on its own battery — the wire
   carries data, not power). Upside beyond the pads: LH **key** latency
   rides the same poll loop, so it improves too.
2. **Downgraded-priority hardening** (only matters under wire-burst
   delivery): a small USB TX ring in the module's `src/usb_hid.c`
   (today a single in-flight slot guarded by `hid_sem` drops
   back-to-back frames with `-EAGAIN` — the 45% above), and
   central-side timestamp reconstruction so LH timestamps become honest
   sample times (residual error ±4 ms over radio vs ±22 ms over wire;
   the split relay carries no timestamps, so this must be inferred).
3. **Host pointer synthesis from stream frames** — the elegant fix,
   viable **now over radio**: frames get the device-clock treatment for
   free, pointer would too.

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
