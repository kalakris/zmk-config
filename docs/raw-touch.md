# Go60 raw touch stream — project state

Magic-Trackpad-quality scrolling for the MoErgo Go60's Cirque Pinnacle
trackpads on macOS — **both pads since 2026-08-27**. The firmware streams
raw absolute touch frames to the host over a vendor HID report; a host
program consumes them and synthesizes continuous scroll events with real
gesture phases, measured lift-off momentum, touch-to-catch, and a
velocity-gain ballistics curve.

**Since 2026-08-30 the host is RawTouch** (`~/src/rawtouch`, local
SwiftPM daemon extracted from the fork). The keyboard has two scrolling
modes: **Standard mode** — no host software; pointer, tap-to-click and
÷24 wheel scrolling all firmware-side, touch stream silent — and
**RawTouch mode** — RawTouch claims the stream via a SET feature report,
the firmware emits touch frames (only while claimed, since 2026-08-31;
frame flags bit 2 = `host_claimed`) and suppresses the ÷24 wheel
fallback, and RawTouch synthesizes all scrolling — wheel and synthesized
scroll are mutually exclusive by construction. A claim clearing
mid-touch produces one trailing bit-2-clear release frame, which the
host answers by canceling that pad's series without momentum. The
patched LinearMouse fork is the frozen fallback
(quit rawtouch → launch LinearMouse); it will not be released publicly.
Current operational state: next-steps.md items j/k/l/m. Claim spec: the
module README appendix (the module's `BENCH-mode-gate.md` lab notebook was retired 2026-09-02 during release prep; its durable facts moved into the README's Verify section and wire appendix). This document is the full project state:
architecture, repo map, what's validated, where every knob lives, the
operational loops, and how to roll back. It assumes no prior context.

Companion documents:

- **Protocol spec** — the **authoritative spec is now the wire-format
  appendix of the module README** (`~/src/zmk-raw-touch/README.md`,
  protocol **v3**). [`docs/raw-touch-protocol.md`](raw-touch-protocol.md)
  is the historical v2 spec; since 2026-08-28 the host accepts v3 only,
  so the old `raw-touch` fork build scrolls via wheel fallback alone.
- **Next steps** — [`docs/next-steps.md`](next-steps.md): the
  resumable-from-zero work list (host-cleanup re-land, wired-split
  batching fix, demo video, release train).
- **Pre-upstreaming punch list** —
  [`docs/upstreaming-todo.md`](upstreaming-todo.md) (on `main`): everything
  that must happen before PRing the pieces upstream, plus the post-upstream
  roadmap. Not duplicated here.
- **Prior-art survey** — [`docs/prior-art-survey.md`](prior-art-survey.md):
  why vendor HID rather than a standard touchpad descriptor (with the
  evidence that macOS ignores third-party HID touchpads), who else has
  built adjacent things, and the claims to avoid making in public.
- **Pinnacle driver landscape** —
  [`docs/pinnacle-driver-landscape.md`](pinnacle-driver-landscape.md):
  Zephyr's in-tree driver now supersedes our Cirque fork; read before
  touching the driver or planning any driver upstreaming.

## Architecture

Dual-mode design: the firmware always emits standard relative pointer
reports (and, while the Nav overlay is active, ÷24 wheel events) so any host
works driverless; a stream-aware host additionally opens the vendor HID
device and takes over scrolling.

**Where the firmware lives.** The device side is an out-of-tree ZMK
module, `kalakris/zmk-raw-touch`, built on top of *stock* `moergo-sc/zmk`
— no ZMK fork. **This is what `main` builds and what the user's keyboard
runs** (promoted 2026-08-27, hardware-verified over USB and BLE, with the
in-tree Pinnacle driver). The old `raw-touch` fork build and the interim
`module-port` / `module-port-intree` branches are historical stages —
still flashable as rollbacks, but superseded. See
[module-publish-brief.md](module-publish-brief.md) for the port itself.

```
 Go60 left half ──── split transport ────► Go60 right half (central)
 (LH Pinnacle abs frames relayed to             │
  the central's input pipeline)                 ▼
┌────────────────────────────────────────────────────────────┐
│ 2× Cirque Pinnacle (abs mode, ~100 Hz, single-touch each)  │
│   │ absolute frames (x, y, z); RH = pad 0, LH = pad 1      │
│   ▼                                                        │
│ raw touch module (kalakris/zmk-raw-touch)                  │
│   ├─ derives REL_X/REL_Y ──► input listener chains         │
│   │                            ├─ pointer reports (0x03)   │
│   │                            └─ Nav overlay: ÷24 wheel   │
│   │                               (fallback scrolling)     │
│   ├─ firmware tap-to-click ──► BTN_0 into listener chain   │
│   └─ raw frames ──► vendor HID input report                │
│        usage page 0xFF00, report ID 0x04, protocol v3:     │
│        11-byte frames (pad_id, contact_id, x/y u16LE, z,   │
│          flags, seq, timestamp u16LE in 100 µs units)      │
│        + feature report, 4 + 8 × pads bytes (20 on the    │
│          Go60; version=3, pads bitmask, one geometry slot   │
│          per pad); real logical ranges                     │
│          in the report descriptor (macOS-verified)         │
│        scroll-mode flag set by the &zip_raw_touch_scroll   │
│        marker when the chain that actually handles a pad's │
│        events (its nav_scroll overlay) reaches it          │
└──────────────┬─────────────────────────────────────────────┘
               │ USB HID / BLE HID-over-GATT
               ▼
 macOS host — patched LinearMouse (kalakris/linearmouse
              @ inputscale = main + inputScale)
┌────────────────────────────────────────────────────────────┐
│ TouchStreamManager (IOHIDManager, matches usage pair       │
│ 0xFF00/0x01, reads feature report, accepts protocol v3,    │
│ keys devices on HIDPhysicalDeviceIdentity — NOT VID/PID)   │
│   │ scroll-flagged frames · TouchStreamDeviceClock rebuilds│
│   │ device-time from v3 timestamps (fixes BLE-batching     │
│   │ velocity distortion) · 150 ms stale-touch synthesized  │
│   │ lift-off · seq-gap logging                             │
│   ▼                                                        │
│ first-touch-wins pad arbitration (`activeScrollPad`; claim │
│ cleared on gesture end / momentum end / stale / reset)     │
│   ▼                                                        │
│ TouchScrollEngine — pad-agnostic (gesture phases, velocity │
│ tracking, ballistics gain, lift-off momentum, catch)       │
│   ▼                                                        │
│ GestureScrollSeriesPoster ──► CGEvent scroll series with   │
│                               real gesture phases          │
│ + suppression of the same physical device's fallback wheel │
│   events while its stream is open                          │
└────────────────────────────────────────────────────────────┘
```

Key design points:

- **Scroll context is declared in the keymap**, not the host: the marker
  input processor sits in each pad's `nav_scroll` listener overlay
  (layers = Nav, layer 2) in `config/go60_rh.keymap`. It passes events
  through untouched; its *presence* in the chain that actually handles the
  pad's events is what sets the scroll-mode flag on streamed frames. The
  marker latches a flag per input device that the pad's frame handler
  consumes — the marker only runs if its chain really handled the event,
  so overlay ordering and `process-next` shadowing come for free. Both
  pads' overlays share the one `&zip_raw_touch_scroll` marker instance
  (it is stateless per-event), but **each listener needs its own
  `zip_raw_touch_idle_filter` instance** — the filter's state is per
  devicetree node, so the left pad's chains use
  `zip_raw_touch_idle_filter_lh`. (The `scroll-layers` escape hatch was
  removed in the 2026-08-27 review pass; the latch is the only mechanism.)
- **Physical-identity matching**: the host suppresses fallback wheel events
  per physical device (`HIDPhysicalDeviceIdentity`), because the user's
  Eyelash Sofle shares ZMK's default VID/PID (0x16C0/0x27D9) with the Go60
  — VID/PID matching would wrongly suppress the Sofle's encoder scroll.
- **Tap-to-click is firmware-side** (the Pinnacle's hardware taps are a
  relative-mode feature lost in abs-mode): `tap-click` on the
  `raw_touch_rh` / `raw_touch_lh` nodes, off the driver binding entirely.
  The injected BTN_0 flows down the pad's listener chain, which must
  **not** contain `&zip_button_behaviors` — that processor maps BTN_0 to
  `&none` (it existed to mute the left pad's hardware taps) and would eat
  the firmware tap. This was a real bug, fixed in zmk-config `raw-touch`
  commit `308ba52`. The deprecated *host-side* tap path was deleted from
  the LinearMouse fork (`f5e8a33`) — double-click risk gone.
- **Both pads stream.** The left pad's absolute frames ride the split
  transport to the central and stream as `pad_id` 1 via the
  `raw_touch_lh` node (`device = &cirque_split`). Sensitivity is
  **deliberately asymmetric** — LH "1x", RH "2x": ADC gain is per-pad
  hardware, the 2x reduction exists to tame the *right* pad's baseline
  drift jitter, and 2x on the left pad caused light-touch dropouts that
  felt like unreliability. Host-side, one gesture at a time: first touch
  wins the scroll claim.
- **Known limits**: the Pinnacle is single-touch, so two-finger gestures
  are impossible, ever. Driverless hosts get pointer, click, typing, and
  ÷24-wheel fallback scrolling; firmware taps work everywhere. LH
  *pointer* choppiness under the wire was root-caused AND fixed
  2026-08-28 (wired-split poll cadence tuning, see "Split-link timing"
  below). A correction to an earlier claim survives the fix: v3 device
  timestamps are sample-true for the **RH pad only**. Both `seq` and
  `timestamp` are stamped on the central in `raw_touch_process_frame()`
  (module `src/raw_touch.c`), so LH timestamps are
  split-relay-*arrival* times — honest to ±2 ms over the tuned wire,
  ±4 ms over the radio, which feels right for both scroll and pointer.

### Split-link timing (measured 2026-08-28)

The halves can be joined by the wire (data only — each half charges
independently) or by the split radio when unplugged. LH frame cadence at
the host, 2×2 matrix (the RH pad is a clean 10 ms metronome, σ<0.5 ms,
in **every** configuration):

| Split link | Host link | Dropped frames | Cadence at host |
|---|---|---|---|
| Wire (stock 20/15) | USB | **45%** | 2–3-frame bursts every ~22.5 ms |
| Wire (stock 20/15) | BLE | 0% | same 22.5 ms bursts, re-batched by the ~15 ms host BLE conn interval |
| Radio | BLE | 0% | ~7.5/15 ms alternation, small batches |
| Radio | USB | 0% | ~7.5/15 ms alternation, no batching |
| **Wire (tuned 3/5)** | USB | **0%** | **~10 ms per-frame, batch size 1.00, σ≈2 ms — LH ≈ RH parity** |

**RESOLVED same day**: `config/go60_rh.conf` now sets
`..._RX_COMPLETE_TIMEOUT=3` and `..._RX_TIMEOUT=5` (central-side only —
RH reflash suffices). Validated 2026-08-28: zero drops and zero batching
over ~30 s of continuous LH streaming, LH device timestamps honest to
±2 ms, and no missed LH keystrokes while typing (the half-duplex
collision risk did not materialize). The tuned wire is now the *best*
link — it beats the radio. If LH keys ever drop with the wire in,
that's the collision signature: back the timeouts off (e.g. 5/8).

**The wired split transport is the root cause of LH choppiness.** The
Go60's `zmk,wired-split` node sets `half-duplex` (one data line,
`dir-gpios` direction gate; `go60.dtsi`, UART0 at 921600 baud), and
MoErgo/upstream ZMK's half-duplex wired transport is *polled*: the
peripheral never initiates TX (`app/src/split/wired/peripheral.c`,
`begin_tx()` is compiled out of `report_event` under
`IS_HALF_DUPLEX_MODE`) — events queue in a 16-envelope ring until the
central sends a `POLL_EVENTS` command. The central schedules the next
poll only `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT`
(default 20, applied as **milliseconds** in `publish_events_work`,
`app/src/split/wired/central.c` — the Kconfig says "ticks", a units
inconsistency) after the previous response arrives. 20 ms + ~2.5 ms of
poll/turnaround = the measured 22.5 ms burst period, with 2–3 of the
pad's ~10 ms frames queued per poll. (Quirk: the idle poll rate,
`..._HALF_DUPLEX_RX_TIMEOUT` = 15 ms, is *faster* than the
under-load rate.) Both Kconfigs are central-side and tunable from
`config/go60_rh.conf` without forking — plan and risks in
[next-steps.md](next-steps.md) item (c). Also relevant: the wire
prioritizes nothing — key, pointer, sensor, and battery events share
one FIFO ring, so LH *keystrokes* eat the same up-to-~22 ms latency.

Two knock-on effects of the bursts:

- **The 45% wire+USB drop is in the module, not the wire**: the
  module's USB send path (`src/raw_touch_usb_hid.c`,
  `zmk_raw_touch_usb_send_report()`) has a single in-flight report slot
  guarded by `hid_sem`, released only by the host's interrupt-IN poll —
  frames 2..n of a back-to-back burst hit a busy slot and drop
  (`-EAGAIN`, by design). The BLE path has a
  `CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE`-deep queue and drops nothing.
- **LH device timestamps are relay-arrival times**: `seq` and the v3
  `timestamp` are stamped on the central in `raw_touch_process_frame()`
  (`src/raw_touch.c`) — measured 0 ms device-ts deltas within wired
  bursts. The device-clock reconstruction that makes RH velocity immune
  to host-link batching cannot help the LH pad over the wire; over the
  radio the residual error is ±4 ms and it feels fine.

Tooling (both new 2026-08-28, in `scripts/`): `raw-touch-monitor.swift`
is a **passive, read-only** HID monitor for the vendor collection — it
coexists with a running LinearMouse, but must run **unsandboxed** (IOKit
HID access). `analyze-touch-timing.py` chews its CSV into per-pad
cadence/drop stats. Parsing gotcha baked into the monitor: macOS
**prepends the report-ID byte to USB HID callback buffers but not BLE
ones** — account for the offset or every USB frame misparses.

## Repo / branch / tag map

Five repos, one of them (`kalakris/zmk`) now dead weight. The
`v0-prototype` tag on the original four marks the validated
single-sided-config prototype (matched binaries kept in
`firmware/raw-touch-v0-prototype/`). Note: in the local `~/src/zmk` and
`~/src/cirque-input-module` clones the tag exists on `origin` but may not
be fetched locally — `git fetch --tags` if you need it.

| Repo | Location | Branch | Contents |
|---|---|---|---|
| [kalakris/zmk-config](https://github.com/kalakris/zmk-config) | `~/zmk-config` | `main` | **What the user's Go60 is running.** Daily keymap config, scripts, docs, LinearMouse config snapshot (`linearmouse/linearmouse.json`) and deploy script (`linearmouse/build-and-install.sh`). `west.yml`: stock `moergo-sc/zmk` @ SHA `57a7b8e0` + `cirque-input-module@intree-driver` + the vendored module (`vendor/zmk-raw-touch/`, via `cmake-args` in `build.yaml`). Both pads abs-mode; `raw_touch_rh` / `raw_touch_lh` nodes in `go60_rh.keymap`; `go60.conf` sets `CONFIG_INPUT_QUEUE_MAX_MSGS=64` and `CONFIG_INPUT_THREAD_STACK_SIZE=2048` (the 16/512 defaults dropped K_NO_WAIT-injected events under dual-pad load). Salvaged fork commit in `patches/`. |
| | | `raw-touch` | Historical: the ZMK-fork build (protocol v2, right pad only). `west.yml` → `kalakris/zmk@raw-touch`. ⚠️ Since the host dropped v2 (2026-08-28), flashing it as a rollback gets wheel-fallback scrolling only. Carries the v2 `docs/raw-touch-protocol.md`. |
| | | `module-port` / `module-port-intree` | Historical stages of the module port; `module-port-intree` was promoted to `main` on 2026-08-27. |
| [kalakris/linearmouse](https://github.com/kalakris/linearmouse) | `~/src/linearmouse` | `main` (+ `inputscale`) | Restructured 2026-08-28: **`main` = upstream + the touch-stream feature** (32 commits: `LinearMouse/TouchStream/`, config model, Raw Touch UI, v3-only parsing (v2 dropped 2026-08-28), `TouchStreamDeviceClock`, stale-touch lift-off, pad-agnostic engine + manager-level pad arbitration). **`inputscale` = main + 2 generic `scrolling.smoothed.inputScale` commits** (self-contained upstream-PR candidate, kept rebased on main) — **deploys come from `inputscale`** (the user's config uses the feature). Review cleanups re-landed 2026-08-28 (batch exonerated — see the /simplify note below). Fork CI = the unsigned workflow; the signed pipeline is gated upstream-only. Old `go60-inputscale` branch deleted; local tag `pre-restructure-go60-inputscale` anchors its history. Unit suite: ~640 tests. |
| [kalakris/zmk-raw-touch](https://github.com/kalakris/zmk-raw-touch) | `~/src/zmk-raw-touch` | `main` | **The device side.** Renamed from `zmk-raw-touch-wip` (name final), still **private**. Out-of-tree ZMK module: private HID report descriptor (protocol v3, real logical ranges), second USB HID interface (`HID_1`), second BLE HIDS instance (with the feature-report characteristic), the frame handler, and two input processors — `zip_raw_touch_scroll` (scroll-context marker) and `zip_raw_touch_idle_filter`. Its `zmk,raw-touch-pad` binding is the whole config surface, including the tap and geometry props that used to live on the Cirque driver. The README's wire-format appendix is the authoritative protocol spec. |
| [kalakris/zmk](https://github.com/kalakris/zmk) | `~/src/zmk` | `raw-touch` | Fork of `moergo-sc/zmk` (base `go60-zmk0.3.0`) carrying the 219-line ZMK core patch: vendor HID plumbing, `app/src/pointing/touch_stream.c`, `input_processor_touch_stream_scroll.c`, a listener-config routing iterator, and zero-mouse-report suppression (`cfc4b3e6`). ⚠️ **Dead — safe to delete.** The module replaces all of it, `main` builds from stock MoErgo ZMK, and the one PR-worthy commit (`cfc4b3e6`) is salvaged as `patches/zmk-skip-empty-mouse-report-syncs.patch`. |
| [kalakris/cirque-input-module](https://github.com/kalakris/cirque-input-module) | `~/src/cirque-input-module` | `raw-touch` / `intree-driver` | `raw-touch` is the fork of petejohanson/cirque-input-module: `abs-mode` absolute reporting, 3 Z-idle packets on lift-off (redundancy), and the now-removed `stream-tap-*` binding properties. `intree-driver` is Zephyr **main**'s `input_pinnacle.c` vendored pristine from `27150c9d` (`7d6f543`) plus three labelled patches — 0xFF/SW_DR guard (`66897c3`), per-axis ERA edge sensitivity (`995e9e0`), force-recalibrate-on-init (`b5c2365`); SW-reset-on-init was already upstream. ⚠️ **The fork is transitional.** Zephyr's in-tree `input_pinnacle` driver already has absolute mode (since 2024) and reached ZMK main via the Zephyr 4.1 bump; pete's module is EOL. Plan is to drop this fork and migrate — see [docs/pinnacle-driver-landscape.md](pinnacle-driver-landscape.md). |

## Status: validated vs pending

**As of 2026-08-27 the module architecture is the daily driver, on
protocol v3, with both pads streaming.** Working and hardware-validated:
raw-touch scrolling with ballistics, real lift-off momentum and
touch-to-catch, on **both pads** (first-touch-wins arbitration on the
host); firmware tap-to-click; the "Raw Touch" scrolling mode in
LinearMouse with live-responding settings; dual-mode wheel fallback;
direction semantics unified with the system Natural Scrolling preference;
BLE (verified — including v3 device-time reconstruction fixing the
BLE-batching velocity distortion).

Also landed 2026-08-27: the firmware half of the /simplify review pass
(K_NO_WAIT USB send, `scroll-layers` escape hatch removed from module and
binding, fixed usage-pair `#define`s, simplified slot fill). The **host**
half of the same review (`a204c40`, seven cleanup items) was reverted
(`29a8f88`) after an apparent live regression — then **exonerated and
fully re-landed 2026-08-28**: a discriminating redeploy of the exact
batch build ran clean, pinning the original symptom on per-copy
TCC-grant state (the accessibility-loop trap's little sibling), not the
code. Re-landed on `main` as three commits, minus the pad-0 collapse
(obsoleted by pad-1 arbitration); unit suite and all six live scroll
checks green.

Fixed after the initial validation (all deployed):
- Firmware taps were swallowed by `&zip_button_behaviors` on the right
  pad's listener chain (a 2026-03 config change that muted the ASIC's
  hardware taps). Removed for that pad only.
- Every touch-stream setting applied **one change late** — the config
  subscription read `ConfigurationState.configuration` during `willSet`.
  Fixed with a main-queue hop; regression-tested.
- The pane-level "Reverse scrolling" toggle was a no-op in Raw Touch mode
  (the reverse transformer skips synthetic events). It is now the single
  direction control, applied once at the engine, XOR'd with the system
  Natural Scrolling preference — so it means the same thing in every mode.

Still pending (see [next-steps.md](next-steps.md) for the full list):
- **Sofle + Go60 simultaneously**: confirm the Sofle's encoder scroll works
  while the Go60 stream is open (physical-identity suppression exists
  precisely for this).
- ~~**LH pointer choppiness**~~ — root-caused and **FIXED 2026-08-28**:
  the wired split's polled transport batched LH frames 2–3 per ~22.5 ms
  (see "Split-link timing" under Architecture); the poll-cadence tuning
  in `config/go60_rh.conf` (20/15 → 3/5 ms) restored per-frame delivery
  and LH ≈ RH parity, validated on hardware. Optional-polish leftovers
  (USB TX ring, timestamp reconstruction, host pointer synthesis) are
  parked in [next-steps.md](next-steps.md) item (c).
- **Dead-pad boot race** (fix pushed 2026-08-28, hardware verification
  pending): the in-tree driver's force-recalibrate-on-init patch could
  leave SW_CC stuck on some boots, so DR never fired and the pad was
  dead while keys worked fine. The amended patch 3/3 (clear SW_CC and
  re-verify DR) is on `cirque-input-module@intree-driver`
  (`89a08962`), pinned in `config/west.yml`; verify after reflashing
  both halves.

### Strategic decisions taken (2026-08-26)

Research settled several questions; each has a dedicated doc:

| Decision | Verdict | Doc |
|---|---|---|
| Zephyr 4.1 / ZMK 0.4 migration | **Wait** — named triggers; gains thin, three open regressions hit our config, MoErgo's branch stale | [zephyr-41-migration.md](zephyr-41-migration.md) |
| Our Cirque driver fork | **Transitional** — Zephyr's in-tree driver has had abs mode since 2024; ours is EOL-based | [pinnacle-driver-landscape.md](pinnacle-driver-landscape.md) |
| Can we use the in-tree driver now? | **Yes** — it compiles verbatim on Zephyr 3.5, green CI; ~2h + bench to adopt | [zephyr-41-migration.md](zephyr-41-migration.md) |
| Is the vendor-HID work fork-forever? | **No** — it can be an out-of-tree module (proven pattern, no ZMK fork) | [publish-strategy.md](publish-strategy.md) |
| How to publish | **Module first, 3.5, lead with a video**; small patches to Zephyr/LinearMouse first | [publish-strategy.md](publish-strategy.md), [module-publish-brief.md](module-publish-brief.md) |
| Prior art / claims to avoid | vendor HID justified; never claim abs-mode novelty; rename before publishing | [prior-art-survey.md](prior-art-survey.md) |
| Two BLE HIDS instances on one peripheral | **Works, medium-high confidence** — HOGP 1.0 §2.5/§4.5.1 permit and require it; two ZMK modules and Mooltipass Mini BLE ship it. macOS mis-binds report maps on the *first* pairing only | [module-publish-brief.md](module-publish-brief.md) |

**The module rewrite is done, benched, and promoted to `main`**
(2026-08-27) — the device side no longer needs a ZMK fork. Two more
decisions closed the same day: the name is final (**`zmk-raw-touch`**) and
the usage page **stays 0xFF00/0x01** (fixed `#define`s, not Kconfig — the
kext scan showed it free on macOS while Apple squats QMK's 0xFF60/0x07).
See [module-publish-brief.md](module-publish-brief.md) for the shape as
built, the temporary vendoring workaround, and what is left before
anything can go public.

## Tuning: current values and where every knob lives

### Host — LinearMouse (`schemes[].scrolling.touchStream` in `~/.config/linearmouse/linearmouse.json`)

Current tuning (authoritative snapshot: `linearmouse/linearmouse.json` on
`main`; the Go60 scheme matches on VID/PID 0x16C0/0x27D9):

| Knob | Current | Default | Where |
|---|---|---|---|
| `enabled` | true | — | JSON + "Raw Touch" mode in the Scrolling pane |
| `scale` | 0.6 | 0.25 | UI "Scroll speed" slider |
| `acceleration.exponent` | 0.5 | 0.5 | UI "Scroll acceleration" slider (0 = off) |
| `acceleration.referenceSpeed` | 800 | 800 | JSON only (expert) |
| `acceleration.minGain` / `maxGain` | 0.4 / 3.0 | 0.4 / 3.0 | JSON only (expert) |
| `momentum.decayTimeConstant` | unset (0.83 s default) | 0.83 | UI "Scroll inertia" slider |
| `momentum.startThreshold` / `maxSpeed` | unset | — | JSON only (expert) |
| scroll direction | `scrolling.reverse.vertical` = false | — | UI "Reverse scrolling" toggle — the **single** direction control, applied once as the engine's output sign (frames are orientation-mapped from the feature report; there is no touchStream-specific direction key) |
| pointer (same scheme) | `speed` 0.04, `acceleration` 1.2 | — | UI Pointer pane |

UI notes: "Raw Touch" appears in the Scrolling pane's mode picker **only**
when a streaming device is detected (`ScrollingModeSection.swift` /
`TouchStreamScrollingSection.swift`). The pane shows exactly four controls:
Reverse scrolling, Scroll speed, Scroll acceleration, Scroll inertia.
Config defaults/ranges: `LinearMouse/Model/Configuration/Scheme/Scrolling/TouchStream.swift`.

Feel constants that are *not* config live as named constants in
`LinearMouse/TouchStream/TouchScrollEngine.swift` (velocity window 0.1 s,
stop velocity 10 pt/s, speed-smoothing time constant 0.04 s, max plausible
step 512 counts, etc.).

### Firmware — devicetree + Kconfig

All on `main` now; the old `raw-touch`-fork names (`stream-tap-*`,
`CONFIG_ZMK_TOUCH_STREAM`, `&zip_touch_stream_scroll`) exist only on the
historical branches.

| Knob | Where | Current |
|---|---|---|
| Absolute mode | `data-mode = "absolute"` on `&glidepoint`, both halves, with `idle-packets-count = <3>` (**mandatory** — upstream defaults to 0: no lift-off packet means no release frame, no momentum, no tap) | on |
| `sensitivity` | `&glidepoint`, per half — **deliberately asymmetric**: ADC gain is per-pad hardware; "2x" tames the RH pad's baseline-drift jitter (see MEMORY), but on the LH pad it caused light-touch dropouts, so LH stays at stock "1x" | RH "2x", LH "1x" |
| Tap-to-click | `tap-click` / `tap-max-ms` / `tap-max-movement` on the `raw_touch_rh` / `raw_touch_lh` nodes | on (defaults: 180 ms, 30 counts) |
| Pad geometry / orientation | `rotate-90` / `y-invert` / `x-max` / `y-max` / `resolution` / `pad-id` on the `raw_touch_*` nodes (LH: `device = &cirque_split`) | rotate-90, y-invert; RH `pad-id` 0, LH `pad-id` 1 |
| Enable | `CONFIG_ZMK_RAW_TOUCH=y` **plus `CONFIG_USB_HID_DEVICE_COUNT=2`** — mandatory, and it fails at *runtime*, not build time: without it `device_get_binding("HID_1")` returns NULL and the stream silently does not exist | on |
| Scroll context | `&zip_raw_touch_scroll` in each pad's `nav_scroll` overlay | Nav layer (2) |
| Zero-report suppression | `&zip_raw_touch_idle_filter` (RH) / `&zip_raw_touch_idle_filter_lh` (LH), **last** in every chain (base and overlay); one instance per listener — the filter's state is per devicetree node | on |
| Pointer scale | `&zip_xy_scaler 1 1` (abs-derived deltas run larger than relative-mode, so not the old 3:1) | 1:1 |
| Input subsystem headroom | `go60.conf`: `CONFIG_INPUT_QUEUE_MAX_MSGS=64`, `CONFIG_INPUT_THREAD_STACK_SIZE=2048` — the 16-deep / 512-byte defaults silently dropped K_NO_WAIT-injected events (deltas, taps) under dual-pad ~100 Hz load | set |

## Operational loops

### Firmware loop (build → download → flash)

```bash
cd ~/zmk-config                 # main is the daily branch — no checkout dance
# ...edit, commit...
git push                        # triggers "Build and Draw" (~2 min)
./scripts/download-firmware.sh  # matches the branch-tip SHA and waits
                                # for THAT run (a stale-download race
                                # previously served the prior build)
./scripts/flash-go60.sh firmware/main/firmware
```

- Bootloader entry: hold **RH T3 + `/`**. Only the right half (central)
  needs reflashing for scroll/stream changes — but Cirque-driver or
  left-pad-config changes touch the left half too; flash both.
- `flash-go60.sh` is glob + fast-retry hardened (bouncy mounts,
  "GO60RHBOOT 1" stale-mountpoint remounts). Since 2026-09-02 it also
  **exits on its own once every requested half has flashed**
  (`--halves both|lh|rh`, default both) - run it in the background and
  its completion is the signal - **holds a lock so a second watcher
  refuses to start (exit 3)**, and waits indefinitely by default (the user
  may leave the desk mid-flash; `--timeout SECONDS` opts into an idle
  timeout, exit 2). The old "exactly one watcher" discipline is now enforced by
  the script; `--loop` restores the forever-watching behaviour.
- Stream changes go in `~/src/zmk-raw-touch` — there is no ZMK fork.
  Because that repo is private (CI's `west update` authenticates
  anonymously) they must be re-vendored:
  `./scripts/sync-raw-touch-module.sh`, then commit
  `vendor/zmk-raw-touch/`. **Pushing the module repo alone changes
  nothing**; CI builds the vendored copy.
- Cirque-driver changes go in `~/src/cirque-input-module` (push to
  `@raw-touch` or `@intree-driver`, then bump the revision pinned in
  `config/west.yml`). CI rebuilds pull branch tips.

### Host loop (LinearMouse)

```bash
cd ~/src/linearmouse            # branch inputscale (touch-stream edits → main, then rebase inputscale)
# ...edit, commit...
~/zmk-config/linearmouse/build-and-install.sh   # ~2 min
```

- The script archives with Xcode, signs with the user's **Apple
  Development** cert (team 7WBD7URF58) so the Accessibility/TCC grant
  persists across rebuilds, replaces /Applications/LinearMouse.app, and
  deletes the build archive (a second on-disk copy causes repeated
  Accessibility prompts).
- `~/.config/linearmouse/linearmouse.json` **live-reloads** — tune by
  editing it directly. After tuning, snapshot it back to
  `~/zmk-config/linearmouse/linearmouse.json` and commit.
- **Announce every deploy.** A silent mid-session host deploy has already
  cost a misdiagnosis (the behavior change got attributed to the wrong
  layer). Say what was deployed and when, before judging any symptom.
- **GUI mode switches can wipe mode-specific config sections** (the UI
  keeps only an in-memory cache of the section you switched away from) —
  restore from the snapshot if a section disappears.
- **Tests**: `xcodebuild test` launches the app as its own test host and
  has historically triggered TCC prompts. `TouchStreamManager.start()`
  now no-ops under tests (`ProcessEnvironment.isRunningApp` guard). The
  exact historical trigger was never 100% pinned — if prompts recur
  during test runs, instrument rather than guess.
- **THE ACCESSIBILITY-LOOP TRAP** (bitten twice): granting a permission
  prompt raised by a *test host* binds the TCC grant to the ephemeral
  Debug copy in `~/Library/Developer/Xcode/DerivedData/LinearMouse-*/`,
  not the real app — which then keeps asking forever. Rules: (1) NEVER
  grant a prompt that appears while agents are running builds/tests —
  dismiss it; grant only right after a deploy when told the real app is
  asking. (2) Recovery, in order: kill LinearMouse; delete the DerivedData
  Debug app copy (and any other copies —
  `mdfind "kMDItemCFBundleIdentifier == 'com.lujjjh.dev.LinearMouse'"`
  must list ONLY /Applications/LinearMouse.app); `tccutil reset
  Accessibility com.lujjjh.dev.LinearMouse`; relaunch from /Applications;
  grant the single fresh row. (3) Proper fix (punch list): keep the test
  host from touching TCC-protected APIs at all.

### Cross-cutting gotchas

- Daily work happens on `main` now, but the historical branches
  (`raw-touch`, `module-port*`) have **diverged copies of `scripts/` and
  `linearmouse/`** — `main`'s are newest, and `raw-touch`'s
  `linearmouse.json` is a stale v0 snapshot. If the working tree gets
  switched for a rollback build, run scripts from a `main` checkout when
  possible and always return the tree to `main`.
- The Build and Draw workflow auto-commits `[Draw]` keymap SVGs — `git
  pull --rebase` before pushing again.
- **macOS caches the HOGP report map — ANY report-layout change needs a
  forget + re-pair on BLE.** Proven the hard way with the v2→v3 frame
  change: the failure is deceptively partial — keys work, USB works, even
  the feature report reads fine (it is live-read), and only BLE frame
  parsing is dead, because the host parses notifications against the
  cached, stale descriptor. Same treatment for GATT-database changes (the
  second HIDS instance): Zephyr's `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION`
  (`default y`) means the CCC subscription for a new characteristic is not
  inherited from an old bond, and `bt_gatt_notify_cb()` silently refuses
  to send. When in doubt after a report-shape flash: forget the device,
  re-pair.
- **On macOS, disconnect and reconnect once before judging a fresh
  pairing.** macOS enumerates both HIDS instances but mis-binds their report
  maps on the very first pairing — a known, still-open macOS bug
  ([mooltipass/minible#126](https://github.com/mooltipass/minible/issues/126));
  one reconnect corrects it. If BLE turns out to be unworkable anyway, set
  `CONFIG_ZMK_RAW_TOUCH_BLE=n`: the stream becomes USB-only and the
  relative-delta wheel fallback still works over BLE.
- **Dead-pad boot race** (hit once, on the LH pad): the in-tree driver's
  force-recalibrate-on-init patch (patch 3/3 on
  `cirque-input-module@intree-driver`) can leave SW_CC stuck on some
  boots — DR then never fires, the pad is dead, and the keys work fine,
  which makes it look like a config or transport bug. It is not:
  power-cycle the half and it comes back. The fix (clear SW_CC and
  re-verify DR in the patch) shipped 2026-08-28 on
  `cirque-input-module@intree-driver` (`89a08962`, pinned in
  `config/west.yml`) — hardware verification pending; if a pad boots
  dead on older firmware, this is still the first suspect.

## Rollback recipes

Four levels, mildest first:

0. **Back out to the fork build**: flash the `raw-touch` build
   (`./scripts/download-firmware.sh` on `raw-touch`, then
   `./scripts/flash-go60.sh firmware/raw-touch/firmware`). It speaks
   protocol v2, which the current host still accepts; no host-side change
   needed. Flash **both halves** (the left half's driver and config have
   since diverged from that build), and expect a BLE forget + re-pair in
   each direction (the report map changes — see gotchas).
1. **Re-flash a known-good stream build**: the `v0-prototype` binaries are
   kept in `firmware/raw-touch-v0-prototype/` —
   `./scripts/flash-go60.sh firmware/raw-touch-v0-prototype/firmware`.
   Source state for all four repos is the `v0-prototype` tag (on origin;
   `git fetch --tags` in `~/src/zmk` / `~/src/cirque-input-module`).
   Note v0 predates dual-mode/feature-report/firmware-taps; pair it with a
   v0-era host build (`git checkout v0-prototype` in `~/src/linearmouse`,
   then `build-and-install.sh`).
2. **Full firmware revert (stream off)**: since the promotion, no branch
   tip builds the stream-free config any more — it is `main`'s last
   pre-promotion commit, `c036948` (moergo upstream: relative mode,
   hardware taps, Nav-layer ÷8 wheel scrolling, no vendor report). Check
   it out onto a branch, push to trigger a build, download, flash both
   halves. Then disable the host side: set
   `scrolling.touchStream.enabled` to `false` (or pick a non-Raw-Touch
   scrolling mode) so LinearMouse stops expecting a stream and stops
   suppressing wheel events.
3. **Host-only revert**: reinstall stock LinearMouse (or `git checkout
   v0.11.4 && build-and-install.sh`) — firmware keeps streaming harmlessly
   (frames are ignored by hosts that don't open the vendor device) and the
   ÷24 wheel fallback provides scrolling.
