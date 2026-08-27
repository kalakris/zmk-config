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
reports (and, while the Nav overlay is active, ÷8 wheel events) so any host
works driverless; a stream-aware host additionally opens the vendor HID
device and takes over scrolling.

**Where the firmware lives (changed 2026-08-26).** The device side is now an
out-of-tree ZMK module, `kalakris/zmk-raw-touch-wip`, built on top of *stock*
`moergo-sc/zmk` — no ZMK fork. The **binary running on the user's keyboard
is still the old `raw-touch` fork build** until the bench pass; the module
build sits on branches `module-port` / `module-port-intree`. The wire format
is byte-identical across the two, and the host software is unchanged, so the
diagram below describes both. Names differ: the fork's
`&zip_touch_stream_scroll` is the module's `&zip_raw_touch_scroll`, and
`CONFIG_ZMK_TOUCH_STREAM` is `CONFIG_ZMK_RAW_TOUCH`. See
[module-publish-brief.md](module-publish-brief.md) for the port itself.

```
 Go60 right half (central)
┌────────────────────────────────────────────────────────────┐
│ Cirque Pinnacle (abs-mode, ~100 Hz, single-touch)          │
│   │ absolute frames (x, y, z)                              │
│   ▼                                                        │
│ raw touch module (kalakris/zmk-raw-touch-wip)              │
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
│        scroll-mode flag set by the &zip_raw_touch_scroll   │
│        marker when the chain that actually handles this    │
│        pad's events (the nav_scroll overlay) reaches it    │
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

- **Scroll context is declared in the keymap**, not the host: the marker
  input processor sits in the `nav_scroll` listener overlay (layers = Nav,
  layer 2) in `config/go60_rh.keymap`. It passes events through untouched;
  its *presence* in the chain that actually handles the pad's events is what
  sets the scroll-mode flag on streamed frames. How that presence is
  detected differs between the two builds: the `raw-touch` fork walked the
  listener's overlays from patched ZMK core, while the module has the marker
  latch a flag per input device that the pad's frame handler consumes — more
  faithful, because the marker only runs if its chain really handled the
  event, so overlay ordering and `process-next` shadowing come for free.
  Escape hatch on the module: `scroll-layers = <2>;` on the pad node
  switches to plain layer-state evaluation.
- **Physical-identity matching**: the host suppresses fallback wheel events
  per physical device (`HIDPhysicalDeviceIdentity`), because the user's
  Eyelash Sofle shares ZMK's default VID/PID (0x16C0/0x27D9) with the Go60
  — VID/PID matching would wrongly suppress the Sofle's encoder scroll.
- **Tap-to-click is firmware-side** (the Pinnacle's hardware taps are a
  relative-mode feature lost in abs-mode). On `raw-touch` it is
  `stream-tap-click` on `&glidepoint`; on the module branches it is
  `tap-click` on the `raw_touch_rh` node, off the driver binding entirely.
  The injected BTN_0 flows down the right pad's listener
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

Five repos, one of them (`kalakris/zmk`) now dead weight. The
`v0-prototype` tag on the original four marks the validated
single-sided-config prototype (matched binaries kept in
`firmware/raw-touch-v0-prototype/`). Note: in the local `~/src/zmk` and
`~/src/cirque-input-module` clones the tag exists on `origin` but may not
be fetched locally — `git fetch --tags` if you need it.

| Repo | Location | Branch | Contents |
|---|---|---|---|
| [kalakris/zmk-config](https://github.com/kalakris/zmk-config) | `~/zmk-config` | `main` | Daily keymap config, scripts, docs, LinearMouse config snapshot (`linearmouse/linearmouse.json`) and deploy script (`linearmouse/build-and-install.sh`). `west.yml` still points at moergo upstream (`moergo-sc/zmk:go60-zmk0.3.0`) — the stable fallback. |
| | | `raw-touch` | **What the user's Go60 is running today.** `west.yml` → `kalakris/zmk@raw-touch`; `go60_rh.keymap` adds `abs-mode` + `stream-tap-click` on `&glidepoint`, the `nav_scroll` marker overlay, and drops the right pad to `&zip_xy_scaler 1 1`; `go60_rh.conf` sets `CONFIG_ZMK_TOUCH_STREAM=y`; carries `docs/raw-touch-protocol.md`. Superseded by `module-port` once that is benched. |
| | | `module-port` (`13a68eb`) | **The intended replacement.** Stock `moergo-sc/zmk` @ `57a7b8e0` + the same Cirque fork + the module. `go60_rh.keymap` keeps `abs-mode` on `&glidepoint` but moves tap/geometry to a `raw_touch_rh` node; `go60_rh.conf` sets `CONFIG_ZMK_RAW_TOUCH=y` and `CONFIG_USB_HID_DEVICE_COUNT=2`. CI-green on all 5 targets; **`go60_lh` comes out byte-identical to the `raw-touch` build**, so only the right half needs reflashing. |
| | | `module-port-intree` (`b2bf28c`) | The same plus Zephyr's in-tree Pinnacle driver (`kalakris/cirque-input-module@intree-driver`) — the full end state. Also CI-green. Here the left half's driver really changes, so flash both halves. |
| [kalakris/linearmouse](https://github.com/kalakris/linearmouse) | `~/src/linearmouse` | `go60-inputscale` | 25 commits over upstream. The first two (`4b45bfb`, `df55cc2`) are generic `scrolling.smoothed.inputScale` + GUI slider — a self-contained upstream-PR candidate. The rest is the touch-stream feature (`LinearMouse/TouchStream/`, config model, Raw Touch UI). Unit suite: ~640 tests. |
| [kalakris/zmk-raw-touch-wip](https://github.com/kalakris/zmk-raw-touch-wip) | `~/src/zmk-raw-touch-wip` | `main` (`5199d34`) | **The device side, as of 2026-08-26.** Private; the working name is provisional. Out-of-tree ZMK module, 22 files / ~1450 lines C: private HID report descriptor, second USB HID interface (`HID_1`), second BLE HIDS instance (with the feature-report characteristic), the frame handler, and two input processors — `zip_raw_touch_scroll` (scroll-context marker) and `zip_raw_touch_idle_filter`. Its `zmk,raw-touch-pad` binding is the whole config surface, including the tap and geometry props that used to live on the Cirque driver. |
| [kalakris/zmk](https://github.com/kalakris/zmk) | `~/src/zmk` | `raw-touch` | Fork of `moergo-sc/zmk` (base `go60-zmk0.3.0`) carrying the 219-line ZMK core patch: vendor HID plumbing, `app/src/pointing/touch_stream.c`, `input_processor_touch_stream_scroll.c`, a listener-config routing iterator, and zero-mouse-report suppression (`cfc4b3e6`). ⚠️ **No longer load-bearing** — the module above replaces all of it, and the `module-port` branch builds from stock MoErgo ZMK. Still the source of the binary currently flashed; delete it once the bench pass confirms parity. |
| [kalakris/cirque-input-module](https://github.com/kalakris/cirque-input-module) | `~/src/cirque-input-module` | `raw-touch` / `intree-driver` | `raw-touch` is the fork of petejohanson/cirque-input-module: `abs-mode` absolute reporting, 3 Z-idle packets on lift-off (redundancy), and the now-removed `stream-tap-*` binding properties. `intree-driver` is Zephyr **main**'s `input_pinnacle.c` vendored pristine from `27150c9d` (`7d6f543`) plus three labelled patches — 0xFF/SW_DR guard (`66897c3`), per-axis ERA edge sensitivity (`995e9e0`), force-recalibrate-on-init (`b5c2365`); SW-reset-on-init was already upstream. ⚠️ **The fork is transitional.** Zephyr's in-tree `input_pinnacle` driver already has absolute mode (since 2024) and reached ZMK main via the Zephyr 4.1 bump; pete's module is EOL. Plan is to drop this fork and migrate — see [docs/pinnacle-driver-landscape.md](pinnacle-driver-landscape.md). |

## Status: validated vs pending

**As of 2026-08-26 the system is complete and in daily use.** Working and
hardware-validated: raw-touch scrolling with ballistics, real lift-off
momentum and touch-to-catch; firmware tap-to-click; the "Raw Touch"
scrolling mode in LinearMouse with live-responding settings; dual-mode
wheel fallback; direction semantics unified with the system Natural
Scrolling preference.

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

Still pending (user tests, not known bugs):
- **Sofle + Go60 simultaneously**: confirm the Sofle's encoder scroll works
  while the Go60 stream is open (physical-identity suppression exists
  precisely for this).
- **A dedicated BLE session**: the BLE HOG transport is implemented but a
  focused Bluetooth-only test hasn't been run — **on any build, ever**. Keep
  that in mind when benching the module: a BLE failure there is a first
  test, not a regression.
- **The module port**: `module-port` is built and CI-green but has never
  been flashed. Bench it against the checklist in
  [module-publish-brief.md](module-publish-brief.md) — scroll, momentum,
  catch, tap-to-click, ÷8 fallback, pointer speed, then BLE. Only the right
  half needs flashing (the left half's binary is byte-identical to what is
  running).

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

**The module rewrite is done** (2026-08-26) — the device side no longer
needs a ZMK fork. See [module-publish-brief.md](module-publish-brief.md) for
the shape as built, the CI evidence, the temporary vendoring workaround, and
what is left before anything can go public.

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

Same knobs on both firmware branches; the names moved when the code did.

| Knob | `raw-touch` (running) | `module-port` | Current |
|---|---|---|---|
| Absolute mode | `abs-mode` on `&glidepoint` | same (`data-mode = "absolute"` on `-intree`) | on |
| `sensitivity` | `&glidepoint` | same | "2x" (baseline-drift fix, see MEMORY) |
| Tap-to-click | `stream-tap-click` / `stream-tap-max-ms` / `stream-tap-max-movement` on `&glidepoint` | `tap-click` / `tap-max-ms` / `tap-max-movement` on the `raw_touch_rh` node | on (defaults: 180 ms, 30 counts) |
| Pad geometry / orientation | driver's `rotate-90`, `y-invert` | same names, on `raw_touch_rh`, plus `x-max` / `y-max` / `resolution` / `pad-id` | rotate-90, y-invert |
| Enable | `CONFIG_ZMK_TOUCH_STREAM=y` | `CONFIG_ZMK_RAW_TOUCH=y` **plus `CONFIG_USB_HID_DEVICE_COUNT=2`** — mandatory, and it fails at *runtime*, not build time: without it `device_get_binding("HID_1")` returns NULL and the stream silently does not exist | on |
| Scroll context | `&zip_touch_stream_scroll` in the `nav_scroll` overlay | `&zip_raw_touch_scroll`, same place | Nav layer (2) |
| Zero-report suppression | patched into ZMK core | `&zip_raw_touch_idle_filter`, **last** in every chain (base and overlay) | on |
| Pointer scale | `&zip_xy_scaler 1 1` (right pad; abs-derived deltas run larger than relative-mode, so not the old 3:1) | same | 1:1 |

## Operational loops

### Firmware loop (build → download → flash)

```bash
cd ~/zmk-config
git checkout raw-touch          # or module-port / module-port-intree
# ...edit, commit...
git push                        # triggers "Build and Draw" (~2 min)
./scripts/download-firmware.sh  # matches the branch-tip SHA and waits
                                # for THAT run (a stale-download race
                                # previously served the prior build)
./scripts/flash-go60.sh firmware/raw-touch/firmware   # or firmware/module-port/firmware
git checkout main               # ALWAYS return to main (see gotchas)
```

- Bootloader entry: hold **RH T3 + `/`**. Only the right half (central)
  needs reflashing for scroll/stream changes.
- `flash-go60.sh` is glob + fast-retry hardened (bouncy mounts,
  "GO60RHBOOT 1" stale-mountpoint remounts). **Beware stray watchers**
  from old sessions racing the flash with the wrong firmware — check
  `pgrep -fl flash-go60` whenever a flash behaves oddly.
- On `raw-touch`, ZMK-core changes go in `~/src/zmk` (push to
  `kalakris/zmk@raw-touch`). On the module branches there is no ZMK fork:
  stream changes go in `~/src/zmk-raw-touch-wip`, and because that repo is
  private (CI's `west update` authenticates anonymously) they must be
  re-vendored — `./scripts/sync-raw-touch-module.sh`, then commit
  `vendor/zmk-raw-touch/`. **Pushing the module repo alone changes
  nothing**; CI builds the vendored copy.
- Cirque-driver changes go in `~/src/cirque-input-module` (push to
  `@raw-touch` or `@intree-driver`, then bump the revision pinned in
  `config/west.yml`). CI rebuilds pull branch tips.

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

- The `~/zmk-config` working tree gets switched between `main` and the
  firmware branches for CI triggers and downloads, and the branches have
  **diverged copies of `scripts/` and `linearmouse/`** — `main`'s are
  newest, and `raw-touch`'s `linearmouse.json` is a stale v0 snapshot.
  `module-port*` took `main`'s `scripts/` (plus
  `sync-raw-touch-module.sh`) but still carry `raw-touch`'s stale
  `linearmouse/`. Run scripts from a `main` checkout when possible, and
  always return the working tree to `main`.
- The Build and Draw workflow auto-commits `[Draw]` keymap SVGs — `git
  pull --rebase` before pushing again.
- **BLE, second HIDS instance** (module branches only). The module adds a
  second HID-over-GATT service, so the GATT database changes and Zephyr's
  `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION` (`default y`) means the host's CCC
  subscription for the new input characteristic is *not* inherited from an
  existing bond. **Expect to re-pair after the first flash that carries it**
  — if the host does not re-discover and subscribe, `bt_gatt_notify_cb()`
  refuses to send and the stream is silently dead, with everything else
  working normally.
- **On macOS, disconnect and reconnect once before judging a fresh
  pairing.** macOS enumerates both HIDS instances but mis-binds their report
  maps on the very first pairing — a known, still-open macOS bug
  ([mooltipass/minible#126](https://github.com/mooltipass/minible/issues/126));
  one reconnect corrects it. If BLE turns out to be unworkable anyway, set
  `CONFIG_ZMK_RAW_TOUCH_BLE=n`: the stream becomes USB-only and the
  relative-delta wheel fallback still works over BLE.

## Rollback recipes

Four levels, mildest first:

0. **Back out the module port**: flash the `raw-touch` build
   (`./scripts/download-firmware.sh` on `raw-touch`, then
   `./scripts/flash-go60.sh firmware/raw-touch/firmware`). Same wire format,
   same host software, no host-side change needed. Right half only — the
   left half's binary is identical between `raw-touch` and `module-port`.
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
