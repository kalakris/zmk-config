# Go60 raw touch stream — project state

Magic-Trackpad-quality scrolling for the MoErgo Go60's right-hand Cirque
Pinnacle trackpad on macOS. The firmware streams raw absolute touch frames
to the host over a vendor HID report; a patched LinearMouse fork consumes
them and synthesizes continuous scroll events with real gesture phases,
measured lift-off momentum, touch-to-catch, and a velocity-gain ballistics
curve. This document is the full project state: architecture, repo map,
what's validated, where every knob lives, the operational loops, and how to
roll back. It assumes no prior context.

Companion documents:

- **Protocol spec** — [`docs/raw-touch-protocol.md` on the `raw-touch`
  branch](https://github.com/kalakris/zmk-config/blob/raw-touch/docs/raw-touch-protocol.md)
  (wire format, feature report, scroll-mode semantics, dual-mode hygiene).
  The spec lives on `raw-touch` because that's the branch that ships it.
- **Pre-upstreaming punch list** —
  [`docs/upstreaming-todo.md`](upstreaming-todo.md) (on `main`): everything
  that must happen before PRing the pieces upstream, plus the post-upstream
  roadmap. Not duplicated here.

## Architecture

Dual-mode design: the firmware always emits standard relative pointer
reports (and, while the Nav overlay is active, ÷8 wheel events) so any host
works driverless; a stream-aware host additionally opens the vendor HID
device and takes over scrolling.

```
 Go60 right half (central)
┌────────────────────────────────────────────────────────────┐
│ Cirque Pinnacle (abs-mode, ~100 Hz, single-touch)          │
│   │ absolute frames (x, y, z)                              │
│   ▼                                                        │
│ touch_stream.c (kalakris/zmk @ raw-touch)                  │
│   ├─ derives REL_X/REL_Y ──► input listener chain          │
│   │                            ├─ pointer reports (0x03)   │
│   │                            └─ Nav overlay: ÷8 wheel    │
│   │                               (fallback scrolling)     │
│   ├─ firmware tap-to-click ──► BTN_0 into listener chain   │
│   └─ raw frames ──► vendor HID input report                │
│        usage page 0xFF00, report ID 0x04, 7-byte frames    │
│        + 8-byte feature report (self-describing: version=2,│
│          pads, resolution ~38 counts/mm, orientation bits, │
│          x/y extents)                                      │
│        scroll-mode flag set while the nav_scroll overlay   │
│        (with &zip_touch_stream_scroll marker) would handle │
│        the pad's events                                    │
└──────────────┬─────────────────────────────────────────────┘
               │ USB HID / BLE HID-over-GATT
               ▼
 macOS host — patched LinearMouse (kalakris/linearmouse
              @ go60-inputscale)
┌────────────────────────────────────────────────────────────┐
│ TouchStreamManager (IOHIDManager, matches usage pair       │
│ 0xFF00/0x01, reads feature report, requires protocol v2,   │
│ keys devices on HIDPhysicalDeviceIdentity — NOT VID/PID)   │
│   │ scroll-flagged frames                                  │
│   ▼                                                        │
│ TouchScrollEngine (gesture phases, velocity tracking,      │
│ ballistics gain curve, lift-off momentum, touch-to-catch)  │
│   ▼                                                        │
│ GestureScrollSeriesPoster ──► CGEvent scroll series with   │
│                               real gesture phases          │
│ + suppression of the same physical device's fallback wheel │
│   events while its stream is open                          │
└────────────────────────────────────────────────────────────┘
```

Key design points:

- **Scroll context is declared in the keymap**, not the host: the
  `&zip_touch_stream_scroll` marker input processor sits in the
  `nav_scroll` listener overlay (layers = Nav, layer 2) in
  `config/go60_rh.keymap` on `raw-touch`. It passes events through
  untouched; its *presence* in the chain that would currently handle the
  pad's events is what sets the scroll-mode flag on streamed frames.
- **Physical-identity matching**: the host suppresses fallback wheel events
  per physical device (`HIDPhysicalDeviceIdentity`), because the user's
  Eyelash Sofle shares ZMK's default VID/PID (0x16C0/0x27D9) with the Go60
  — VID/PID matching would wrongly suppress the Sofle's encoder scroll.
- **Tap-to-click is firmware-side** (`stream-tap-click` DT property on
  `&glidepoint`; the Pinnacle's hardware taps are a relative-mode feature
  lost in abs-mode). The injected BTN_0 flows down the right pad's listener
  chain, which must **not** contain `&zip_button_behaviors` — that
  processor maps BTN_0 to `&none` (it exists to mute the left pad's
  hardware taps) and would eat the firmware tap. This was a real bug, fixed
  in zmk-config `raw-touch` commit `308ba52`. A deprecated *host-side* tap
  path also exists in the LinearMouse fork; it is slated for deletion (see
  the punch list) — both enabled at once would double-click.
- **Known limits**: the Pinnacle is single-touch, so two-finger gestures
  are impossible, ever. Driverless hosts get pointer, click, typing, and
  ÷8-wheel fallback scrolling; firmware taps work everywhere. The left pad
  (`pad_id` 1) is reserved in the protocol but not streamed.

## Repo / branch / tag map

Four repos. The `v0-prototype` tag on all four marks the validated
single-sided-config prototype (matched binaries kept in
`firmware/raw-touch-v0-prototype/`). Note: in the local `~/src/zmk` and
`~/src/cirque-input-module` clones the tag exists on `origin` but may not
be fetched locally — `git fetch --tags` if you need it.

| Repo | Location | Branch | Contents |
|---|---|---|---|
| [kalakris/zmk-config](https://github.com/kalakris/zmk-config) | `~/zmk-config` | `main` | Daily keymap config, scripts, docs, LinearMouse config snapshot (`linearmouse/linearmouse.json`) and deploy script (`linearmouse/build-and-install.sh`). `west.yml` still points at moergo upstream (`moergo-sc/zmk:go60-zmk0.3.0`) — the stable fallback. |
| | | `raw-touch` | **The active firmware config — the user's daily Go60 firmware is built from this branch.** `west.yml` → `kalakris/zmk@raw-touch`; `go60_rh.keymap` adds `abs-mode` + `stream-tap-click` on `&glidepoint`, the `nav_scroll` marker overlay, and drops the right pad to `&zip_xy_scaler 1 1`; `go60_rh.conf` sets `CONFIG_ZMK_TOUCH_STREAM=y`; carries `docs/raw-touch-protocol.md`. |
| [kalakris/linearmouse](https://github.com/kalakris/linearmouse) | `~/src/linearmouse` | `go60-inputscale` | 25 commits over upstream. The first two (`4b45bfb`, `df55cc2`) are generic `scrolling.smoothed.inputScale` + GUI slider — a self-contained upstream-PR candidate. The rest is the touch-stream feature (`LinearMouse/TouchStream/`, config model, Raw Touch UI). Unit suite: ~640 tests. |
| [kalakris/zmk](https://github.com/kalakris/zmk) | `~/src/zmk` | `raw-touch` | Fork of `moergo-sc/zmk` (base `go60-zmk0.3.0`). Vendor input + feature HID report plumbing, `app/src/pointing/touch_stream.c`, the marker processor (`input_processor_touch_stream_scroll.c`), a unified listener-config routing iterator, and ungated zero-mouse-report suppression (`cfc4b3e6`, deliberately upstream-shaped). Pulls the Cirque driver from the fork below via its west manifest. |
| [kalakris/cirque-input-module](https://github.com/kalakris/cirque-input-module) | `~/src/cirque-input-module` | `raw-touch` | Fork of petejohanson/cirque-input-module: `abs-mode` absolute reporting, 3 Z-idle packets on lift-off (redundancy), `stream-tap-*` binding properties (consumed by ZMK, not the driver — must move before upstreaming). |

## Status: validated vs pending

Everything is hardware-validated on the user's machine as of 2026-08-25
**except**:

- **Sofle + Go60 simultaneously**: confirm the Sofle's encoder scroll works
  while the Go60 stream is open (the physical-identity suppression exists
  precisely for this; needs a check with both keyboards connected).
- **A dedicated BLE session**: the BLE HOG transport (feature-report
  characteristic, notification-only payload frames) is implemented but a
  focused Bluetooth-only test session hasn't been run.

Both are pending user tests, not known bugs.

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

### Firmware — devicetree + Kconfig (zmk-config `raw-touch`)

| Knob | Where | Current |
|---|---|---|
| `abs-mode` | `&glidepoint`, `config/go60_rh.keymap` | on |
| `sensitivity` | same | "2x" (baseline-drift fix, see MEMORY) |
| `stream-tap-click` | same | on (defaults: 180 ms, 30 counts max movement; tune via `stream-tap-max-ms` / `stream-tap-max-movement`) |
| `CONFIG_ZMK_TOUCH_STREAM=y` | `config/go60_rh.conf` | on |
| Scroll context | `nav_scroll` overlay in `config/go60_rh.keymap`: `&zip_touch_stream_scroll` marker + `÷8` wheel fallback chain | Nav layer (2) |
| Pointer scale | `&zip_xy_scaler 1 1` (right pad; abs-derived deltas run larger than relative-mode, so not the old 3:1) | 1:1 |

## Operational loops

### Firmware loop (build → download → flash)

```bash
cd ~/zmk-config
git checkout raw-touch          # the active firmware branch
# ...edit, commit...
git push                        # triggers "Build and Draw" (~2 min)
./scripts/download-firmware.sh  # matches the branch-tip SHA and waits
                                # for THAT run (a stale-download race
                                # previously served the prior build)
./scripts/flash-go60.sh firmware/raw-touch/firmware
git checkout main               # ALWAYS return to main (see gotchas)
```

- Bootloader entry: hold **RH T3 + `/`**. Only the right half (central)
  needs reflashing for scroll/stream changes.
- `flash-go60.sh` is glob + fast-retry hardened (bouncy mounts,
  "GO60RHBOOT 1" stale-mountpoint remounts). **Beware stray watchers**
  from old sessions racing the flash with the wrong firmware — check
  `pgrep -fl flash-go60` whenever a flash behaves oddly.
- ZMK-core changes go in `~/src/zmk` (push to `kalakris/zmk@raw-touch`);
  Cirque-driver changes in `~/src/cirque-input-module` (push to
  `kalakris/cirque-input-module@raw-touch`, then bump the revision in the
  zmk fork's west manifest if pinned). CI rebuilds pull branch tips.

### Host loop (LinearMouse)

```bash
cd ~/src/linearmouse            # branch go60-inputscale
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
- **GUI mode switches can wipe mode-specific config sections** (the UI
  keeps only an in-memory cache of the section you switched away from) —
  restore from the snapshot if a section disappears.
- **Tests**: `xcodebuild test` launches the app as its own test host and
  has historically triggered TCC prompts. `TouchStreamManager.start()`
  now no-ops under tests (`ProcessEnvironment.isRunningApp` guard). The
  exact historical trigger was never 100% pinned — if prompts recur
  during test runs, instrument rather than guess.

### Cross-cutting gotchas

- The `~/zmk-config` working tree gets switched between `main` and
  `raw-touch` for CI triggers and downloads. The two branches have
  **diverged copies of the scripts** (and of `linearmouse/`) — `main`'s
  are newest; `raw-touch`'s `linearmouse.json` is a stale v0 snapshot.
  Run scripts from a `main` checkout when possible, and always return the
  working tree to `main`.
- The Build and Draw workflow auto-commits `[Draw]` keymap SVGs — `git
  pull --rebase` before pushing again.

## Rollback recipes

Three levels, mildest first:

1. **Re-flash a known-good stream build**: the `v0-prototype` binaries are
   kept in `firmware/raw-touch-v0-prototype/` —
   `./scripts/flash-go60.sh firmware/raw-touch-v0-prototype/firmware`.
   Source state for all four repos is the `v0-prototype` tag (on origin;
   `git fetch --tags` in `~/src/zmk` / `~/src/cirque-input-module`).
   Note v0 predates dual-mode/feature-report/firmware-taps; pair it with a
   v0-era host build (`git checkout v0-prototype` in `~/src/linearmouse`,
   then `build-and-install.sh`).
2. **Full firmware revert (stream off)**: flash `main`'s firmware
   (`./scripts/download-firmware.sh` on `main`, then
   `./scripts/flash-go60.sh` — its default dir is `firmware/main/firmware`).
   `main` builds from moergo upstream: relative mode, hardware taps,
   Nav-layer ÷8 wheel scrolling, no vendor report. Then disable the host
   side: set `scrolling.touchStream.enabled` to `false` (or pick a
   non-Raw-Touch scrolling mode) so LinearMouse stops expecting a stream
   and stops suppressing wheel events.
3. **Host-only revert**: reinstall stock LinearMouse (or `git checkout
   v0.11.4 && build-and-install.sh`) — firmware keeps streaming harmlessly
   (frames are ignored by hosts that don't open the vendor device) and the
   ÷8 wheel fallback provides scrolling.
