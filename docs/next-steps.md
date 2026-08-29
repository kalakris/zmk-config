# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-28
(end of day — items a, b, c, i all closed today).
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

**In flight / housekeeping (check these first in a fresh session):**

- **Flash pending?** The `hold-while-undecided` keymap fix (`e631ce8`;
  fast Nav-held flicks streamed as pointer because trackpad input events
  cannot resolve a hold-tap — capture 7 showed frames flipping p→S
  mid-touch at the 280 ms term boundary) built green; both halves need
  it, LH first. Validate: hold Nav + immediately fast-flick, repeatedly —
  every flick must scroll (re-run `scripts/raw-touch-monitor.swift` and
  check flags=3 from frame one if unsure).
- **cirque-input-module branch names**: the boot-race fix lives on
  `intree-driver-fix` (89a08962, pinned in west.yml) because the agent
  can't force-push; the stale `intree-driver` (b5c23650) is still on
  GitHub. From a human terminal: fast-forward `intree-driver` to
  89a08962 (`git push origin +89a08962:refs/heads/intree-driver`),
  repoint west.yml's comment if desired, delete `intree-driver-fix`.

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

## j. Device-side mode gate (firmware + spec) — IMPLEMENTED 2026-08-28, bench pending

**Code in place, hardware bench NOT run** (blocked behind the pending
`hold-while-undecided` flash). Wire contract + branch map:
[mode-gate-plan.md](mode-gate-plan.md). Module repo branch `mode-gate`
@ `e6b27c1` (pushed; repo left checked out on `main`); zmk-config
branch `mode-gate` @ `9b2213b` (vendored sync, pushed, **CI green
first attempt**, all 5 targets). Gate state is one claim slot per
endpoint instance (`src/gate.c`); wheel suppression sits at the REL
delta injection point in `raw_touch.c` (`gate_engaged && scroll_mode`
skips `input_report_rel` — zero keymap changes, bit 2 and suppression
driven by the same per-frame value). USB set_report accepts bare and
ID-prefixed claim payloads; BLE feature char is now write-enabled
(GATT DB changed → **flashing this build requires forget + re-pair**).
Bench checklist: `BENCH-mode-gate.md` in the module repo (8 sections,
incl. fork-compat bit-2-stays-0 and two-host handoff). Run it before
merging `mode-gate` into either `main`.

The v3 spec **reserves** a mode gate but does not implement it: the host
claims the stream by writing a SET feature report, and while claimed the
firmware suppresses the ÷24 fallback wheel events for scroll-context
frames (pointer reports keep flowing — only the wheel duplicates the
stream). Today the duplicate wheel is suppressed **host-side** by
`TouchStreamWheelSuppressionTransformer`, which is the single reason the
host code must live inside something that owns a CGEventTap. Implementing
the gate:

- removes the last structural coupling to LinearMouse (unblocks item k);
- fixes the dual-emit hygiene concern (prior-art survey §6.8 — PTP
  forbids emitting two collections at once);
- helps every host, including ones we don't write (Scroll Reverser / Mos
  misclassification landmines, survey §6.9).

Design notes for a cold start (issues surveyed 2026-08-28):

- **The host→device path does not exist yet, at all** (verified in
  source): USB explicitly declines SET_REPORT
  (`vendor/zmk-raw-touch/src/usb_hid.c:120` — "the raw touch protocol
  has no host-to-device path"), and the BLE feature characteristic is
  read-only (`hog.c:150`). The gate is the module's first writable
  surface: a USB `set_report` handler plus `BT_GATT_PERM_WRITE` on the
  feature characteristic. Changing GATT characteristic properties
  changes the DB → **the macOS HOGP-cache gotcha fires: forget +
  re-pair required**, with the deceptively-partial failure mode (keys
  and USB fine, BLE stream silently dead). Budget a bench flash for it.
- **Claim scoping**: the stream and the wheel both follow
  `zmk_endpoints_selected()` (`endpoints.c:50`). The claim must be
  scoped to the **endpoint instance** (transport + BLE profile),
  suppress the wheel only while the claiming endpoint is the selected
  one, and clear on disconnect / profile switch. A global boolean is a
  latent multi-host bug (host A over USB muting host B's fallback on a
  BLE profile) that single-host bench testing will never catch.
- **"Gate engaged" bit in the frame flags** — the anti-double-scroll
  fix. After sleep/reboot the claim is gone; if the user scrolls before
  the host's re-assert watchdog fires, firmware emits wheel *and* the
  host consumes the stream → double scroll (the exact bug the host-side
  suppression transformer existed to kill). One flag bit makes firmware
  the single source of truth: the host synthesizes scroll **only when
  the bit says the wheel is suppressed**, so wheel and stream are
  mutually exclusive by construction, no host-side racing.
- **Liveness**: clear the claim on USB disconnect / BLE unbond, and
  expire it on a timeout the host refreshes (a dead host must not leave
  the wheel fallback dead). Host re-asserts after resume/reconnect and
  on device (re)enumeration — the mode re-assert watchdog pattern,
  survey §3.4; `hid-magicmouse` and `bcm5974` both do this because
  devices silently revert. Watch BLE battery: each refresh write wakes
  the radio; pick a lazy interval.
- Spec change goes in the module README's wire-format appendix; bump
  the feature-report minor rather than the protocol version if the
  frame layout is untouched (the flag bit lives in the existing flags
  byte). **Scheduling**: even though the implementation is post-v1,
  consider landing the gate in the *spec* (flag bit, claim layout,
  capability bit) **before the public flip** — spec churn is free now
  and versioned pain once the README is public.

## k. Standalone host agent (post-v1) — IMPLEMENTED 2026-08-28, never run live

**Built and green, daemon never executed** (needs an Accessibility
grant + gate firmware on hardware). New local repo
`~/src/raw-touch-agent` @ `ef2ff6f` (no remote; name is a placeholder
pending the PadWire decision). SwiftPM: thin `raw-touch-agent`
executable over a `RawTouchAgentCore` library + a small C SPI target
for `CGEventCopyIOHIDEvent`. `swift build` (debug+release) and
`swift test` pass: **87 tests** (48 ported from the fork — engine
physics pinned byte-identical — 39 new: gate contract, pipeline,
config). Key shapes: `TouchStreamManager` split into an injectable
`TouchScrollPipeline` + slim IOHID/claim manager; gate filter keys on
flags **bit 2 alone** (gated pointer-mode frames still close out
gestures); gate lapse mid-gesture resolved by the 150 ms stale-touch
watchdog; per-pad config overrides (`pads.<id>`), each pad using its
own reported orientation; claim writes try bare then ID-prefixed
(firmware accepts both — confirm which lands at the bench). The
fork-detection check was deliberately dropped (fork and stock
LinearMouse share a bundle id); README documents mutual exclusion
instead. Config: `~/.config/raw-touch-agent/config.json`
(load-at-start; live-reload not yet). First live run: after the gate
bench passes, grant Accessibility right after installing — the
accessibility-loop trap applies.

Decision 2026-08-28: **ship v1 as the LinearMouse fork** (built, tested,
deployed), then extract the host pipeline into a small standalone
menubar/LaunchAgent daemon as the second release. The fork is a poor
long-term vehicle: ~5% upstreamable as one PR (publish-strategy #9),
permanent rebase against a moving app, and "replace your LinearMouse
with my unsigned fork" is high adoption friction versus "run this 3 MB
agent alongside whatever you already use."

Key architectural finding (verified in the fork's source): the stream
pipeline and LinearMouse are **parallel, not serial** — even in-process
today. `TouchStreamScrollPoster` posts at `.cgSessionEventTap`
(`TouchStreamScrollPoster.swift:28`), *downstream* of LinearMouse's own
tap at `.cghidEventTap` (`EventTap.swift:86`), so the transformer chain
never sees the synthesized gestures. A standalone agent posting at the
session tap therefore composes with **stock** LinearMouse (or no
LinearMouse) with zero cooperation. Do NOT post at the HID tap: synthetic
events would be resolved via `deviceFromCGEvent`'s last-active-device
fallback and inherit the *mouse's* scheme (e.g. its reverse-scrolling).

Extraction inventory: `TouchStreamFrame`, `TouchStreamDeviceClock`,
`TouchStreamCapabilities`, `TouchStreamScrollPoster`,
`GestureScrollSeriesPoster` import only Foundation/IOKit — portable
as-is. `TouchScrollEngine` needs its `Scheme`-derived config struct made
plain; `TouchStreamManager` needs its
`ConfigurationState`/`DeviceManager` references replaced (a JSON config
file + its own IOHIDManager device bookkeeping). The wheel-suppression
transformer is **not** ported — item j replaces it. Until extraction:
keep the fork's `TouchStream/` free of new LinearMouse types so the port
stays a day's work.

Known issues to design around (surveyed 2026-08-28):

- **Version-compat matrix.** New agent + gateless firmware → double
  scroll: the agent must require the gate capability bit in the feature
  report and refuse to run without it. Fork build + agent both running
  → double scroll: document mutual exclusion, and have the agent refuse
  to start while the fork's bundle is running.
- **TCC/signing stops being optional.** The agent needs Accessibility
  for `CGEventPost` (no host design escapes that); grants bind to code
  signature, and ad-hoc-signed rebuilds invalidate them — the
  accessibility-loop trap (raw-touch.md) applies to the agent with full
  force, and an unsigned background agent demanding Accessibility is a
  scary install for other users. **Item f (Developer ID + notarization)
  is a prerequisite for the agent's public release**, not optional
  polish.
- **Session-tap posting only kills half the interop landmine.** The
  gate removes the HID-level wheel that Scroll Reverser/Mos could
  misclassify, but the synthesized gestures still flow past *their*
  session-level taps. Prior-art survey §6.9's bench checklist (Scroll
  Reverser inversion, zeroed point-delta fields in Calendar/WebKit)
  survives the architecture change untouched — run it before release.
- **Squatter validation stays.** Post-gate the agent no longer needs
  `HIDPhysicalDeviceIdentity` wheel suppression, but 0xFF00/0x01 is the
  most-squatted vendor pair in the industry — keep the normative
  feature-report validation before treating a matching collection as
  this protocol.
- The fork's settings pane is not ported; the agent is configured by a
  JSON file. Fine for us; note it in the release story.
