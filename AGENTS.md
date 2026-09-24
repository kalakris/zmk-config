# AGENTS.md

## Repository Overview

ZMK firmware configuration for two split keyboards — the **Eyelash Sofle** and the **MoErgo Go60** — sharing a single keymap via C preprocessor macros. The actual ZMK firmware source is pulled in via West (Zephyr's package manager). Branch map for the Go60 (see "Raw touch scrolling" below):

- `main` — **what the user's Go60 runs.** Stock MoErgo ZMK (pinned SHA, no ZMK fork) + the out-of-tree `zmk-raw-touch` module (vendored under `vendor/` while its repo is private) + Zephyr main's in-tree Pinnacle driver via `cirque-input-module@intree-driver`. Hardware-verified 2026-08-27 over USB and BLE (promoted from `module-port-intree`). Both trackpads stream (protocol v4): RH = pad-id 0, LH = pad-id 1 via the `raw_touch_lh` node; sensitivity is deliberately asymmetric — "1x" LH / "2x" RH (gain is per-pad; 2x tames RH baseline-drift jitter but caused light-touch dropouts on LH).
- `raw-touch`, `module-port`, `module-port-intree` — historical stages of the module port; superseded by `main`. `raw-touch` still points at the deletable `kalakris/zmk` fork (its one PR-worthy commit is salvaged in `patches/`).

## Key Files

- `config/shared.dtsi` — All behaviors, macros, combos, and layers (shared between both keyboards)
- `config/positions_sofle.dtsi` — Sofle position names, position groups, ROW/THUMB macros
- `config/positions_go60.dtsi` — Go60 position names, position groups, ROW/THUMB macros
- `config/eyelash_sofle.keymap` — Thin wrapper: includes + Sofle-specific config (encoder, mouse input)
- `config/go60.keymap` — Thin wrapper: includes + Go60-specific config (dual Cirque trackpads)
- `config/eyelash_sofle.conf` — Sofle Kconfig (RGB, sleep, Bluetooth, mouse, encoder, etc.)
- `config/go60.conf` — Go60 Kconfig (RGB, sleep, trackpad, full consumer HID)
- `config/west.yml` — West manifest: stock moergo (SHA-pinned) + Cirque driver override + the touch module (commented-out west entry; vendored until the repo is public)
- `build.yaml` — Build targets: 3 Sofle (left+studio, right, settings_reset) + 2 Go60 (lh, rh). The two Go60 targets carry `-DZMK_EXTRA_MODULES` cmake-args for the vendored module
- `vendor/zmk-raw-touch/` + `scripts/sync-raw-touch-module.sh` — Vendored copy of the (private) module repo, because CI's `west update` can't clone it. Temporary until the repo goes public; see the brief
- `boards/` — Custom board definitions for the Eyelash Sofle
- `config/go60-layouts.dtsi` — Go60 physical layout, for keymap-drawer only (not the firmware build)
- `ubersicht-widget/` — macOS desktop overlay showing the current keymap drawing

## Building and Downloading Firmware

Firmware is built via GitHub Actions. Every push triggers the `Build and Draw` workflow.

### Build + Download workflow

```bash
# 1. Push changes
git push

# 2. Watch the build (optional — it takes ~2 min)
gh run list --limit 1
gh run watch          # live stream of build progress

# 3. Download firmware
#    ./scripts/download-firmware.sh        — current branch
#    ./scripts/download-firmware.sh --all  — all remote branches
#
# Firmware is organized by branch: firmware/<branch>/firmware/
#   Sofle:
#     eyelash_sofle_studio_left.uf2        — left half (with ZMK Studio enabled)
#     nice_view_adapter nice_view_battery-eyelash_sofle_right-zmk.uf2  — right half
#     settings_reset-eyelash_sofle_left-zmk.uf2  — settings reset
#   Go60:
#     go60_lh-zmk.uf2  — left half
#     go60_rh-zmk.uf2  — right half
```

The workflow also auto-commits keymap drawings via keymap-drawer for both boards — `keymap-drawer/eyelash_sofle.svg` and `keymap-drawer/go60.svg` (commit message prefixed with `[Draw]`). This means after pushing, the remote may have one extra commit — use `git pull --rebase` before pushing again.

## Keymap Drawings and Desktop Overlay

An Übersicht widget (`ubersicht-widget/keymap.widget/`) renders one of the committed SVGs on the macOS desktop, pulling it from `origin/<branch>` so a push is enough to update it. The Go60 needs a vendored physical layout (`config/go60-layouts.dtsi`) passed via `draw_args`, because its board lives in MoErgo's ZMK, which the draw job does not fetch. See [docs/keymap-overlay.md](docs/keymap-overlay.md) for the layout-ordering gotcha, local rendering, and widget knobs.

```bash
./ubersicht-widget/sync.sh    # reinstall the widget after editing it
```

## Shared Keymap Architecture

The keymap is shared between Sofle and Go60 using preprocessor macros that handle the physical differences:

- **ROW macro** — Rows 0-3 take 13 params (6L + encoder + 6R). Sofle keeps all 13; Go60 drops the encoder column, keeping 12.
- **THUMB macro** — Row 4 takes 14 params (union of both keyboards). Each board picks its 12: Sofle drops Go60-only inner thumb keys (lht1/rht1); Go60 drops Sofle-only encoder press and mid key.
- **Named positions** (POS_Q, POS_W, etc.) — Let combos and hold-trigger-key-positions be shared despite different position numbering.
- **Position groups** (RIGHT_HAND, LEFT_HAND, THUMB_KEYS, etc.) — Used in HRM hold-trigger-key-positions.
- **`#ifdef HAS_ENCODER`** — Guards Sofle-specific encoder/sensor config.

### Layers
0. **Base** — QWERTY with home-row mods (urob timerless HRM pattern)
1. **Graphite** — Graphite alpha overlay on Base
2. **Nav** — Navigation, function keys, mouse keys. While held, the Go60's RIGHT trackpad becomes a scroller: its `nav_scroll` listener overlay carries the `&zip_raw_touch_scroll` scroll-context marker plus a two-axis ÷24 wheel fallback chain. The LEFT pad needs no layer — since 2026-09-15 the marker lives in its BASE chain, making it a dedicated two-axis scroll pad (no pointer, no tap, ÷24 wheel fallback, its own `zip_raw_touch_idle_filter_lh` — one filter instance per listener)
3. **System** — Bluetooth, system controls, bootloader
4. **Numpad** — Number pad layout, RGB controls
5. **Tmux** — Tmux tab switching via `tmux_tab` macro (Ctrl+A then number)
6. **Vim** — Vim split navigation via `vim_split` macro (Ctrl+W then direction)
7. **Magic** — Media, RGB, Bluetooth, bootloader. Currently unreachable: RH T2
   (its old hold trigger) is now permanently `&mkp RCLK`; everything critical is
   duplicated on System. Kept in case a binding returns (removing it would
   renumber Spaces, which `&r_spaces 8` references)
8. **Spaces** — macOS workspace navigation

Mouse clicks are plain base-layer bindings: RH T1 = `&mkp LCLK`, RH T2 =
`&mkp RCLK` (2026-08-28; T1's old Esc and T2's old Magic-hold/sticky-shift were
unused). The former trackpad-activated Mouse layer (layer 9) and its
`&zip_temp_layer` wiring are gone.

### Behaviors
- `hml` / `hmr` — Left/right home-row mods using positional hold-tap with `require-prior-idle-ms`
- `lth` — Layer-tap for thumb keys. Like HRMs but uses `hold-trigger-key-positions` (all keys) and `hold-trigger-on-release` instead of `require-prior-idle-ms`, so layer-hold works reliably after quick keypresses. Also `hold-while-undecided` (2026-08-28): trackpad input events can't resolve a hold-tap, so without it a Nav-held flick starting inside the 280 ms tapping term streams in pointer context — the layer must be live during the undecided window for the pad listeners
- `z_tmux` — Hold Z for Tmux layer, tap for Z
- `v_vim` — Hold V for Vim layer, tap for V
- `esc_grave_tilde` — Tap for Esc, Shift for ~, other mods for `

### Key Constants
- `QUICK_TAP_MS = 175` — Quick-tap window used across behaviors

## Raw Touch Scrolling (RawTouch)

Magic-Trackpad-quality scrolling for the Go60's Cirque pads on macOS —
**both pads**. The firmware puts the pads in absolute mode and streams raw
touch frames over a vendor HID report (usage page 0xFF00/0x01 — decided,
fixed defines; report ID 0x04; **protocol v4** (bumped from v3
2026-09-22, no backwards compatibility — see next-steps item dd):
11-byte frames with
pad_id/contact_id/seq/100 µs timestamp at ~100 Hz, feature report of
16 + 8 × pads bytes with per-pad geometry slots — 32 on the Go60 (33 over
USB with the report-ID prefix), hosts
must not hard-code 32. Body bytes 4-7 are the fixed ASCII **magic
`RAWT`** (0x52 0x41 0x57 0x54) and a host MUST reject a body whose magic,
protocol byte or 16 + 8N length does not match and MUST NOT lease such a
device — 0xFF00/0x01 is a generic vendor pair, so this is protocol
identification, not authentication. Bytes 8-15 are the **`device_id`**,
the SoC's `hwinfo_get_device_id()` (nRF52 FICR DEVICEID) verbatim,
all-zero = unknown and NEVER a reason to reject: it is the one identifier
that is the same over USB and BLE, so a host can tell that two rows are
one keyboard. The Go60's USB serial is built from the same eight bytes by
the board itself (`moergo.com:GO60-A856ED2AC49F3E97`), so CONFIG_HWINFO
was already on and the module's `select HWINFO` changes nothing there.
Spec = the module README's appendix, authoritative). Release frames are retained on both transports (BLE since
2026-09-04: spinlocked ring, motion-only eviction, head-of-line retry,
queue default 8; USB since 2026-09-05: queued, drained on transfer
completion), but the host's 150 ms silence watchdog stays REQUIRED for
link loss, bus reset, endpoint switch mid-touch and the bounded give-up
cases. **The host is RawTouch** (`~/src/rawtouch`, standalone
SwiftPM daemon; since 2026-08-30). Two scrolling modes — this naming is
canonical (2026-08-31; never "legacy/basic/fallback mode"): **Standard
mode** — no host software; the firmware scrolls on its own (pointer,
tap, ÷24 two-axis wheel) and the touch stream is silent; and **RawTouch
mode** — RawTouch holds the stream **lease** (SET feature report,
renewed, endpoint-scoped; vocabulary since 2026-09-23 — never "claim"
or "gate"), the firmware emits frames only while
the lease is held (since 2026-08-31; flags bit 2 = `lease_held`, implied-set)
and suppresses the ÷24 wheel, and RawTouch synthesizes scroll — with
real gesture phases, lift-off momentum, ballistics, device-time
reconstruction, and cross-pad-catch arbitration — posting at the session
event tap (composes with any mouse tool; never post at the HID tap). A
lease lapsing mid-touch gets one trailing bit-2-clear release frame;
the host answers by canceling that pad's series WITHOUT momentum. The
keyboard reverts to Standard mode whenever the lease lapses (timeout,
release, endpoint switch, host death). Since 2026-09-05 the lease is also
conditional on **posting readiness**: `RawTouchService` polls the Accessibility
trust check (1 s ungranted / 5 s granted, never the prompting API) and an
ungranted or revoked app releases the lease, so the keyboard keeps
scrolling in Standard mode instead of going dead; the user's `enabled`
preference is never rewritten. Lease writes carry a generation
(`LeaseState`) so an acquire in flight cannot land after a release.
Firmware side (same date): one shared transmit ring
(`src/raw_touch_txq.h`) queues frames on BOTH transports — USB drains from
`in_ready_cb` (`CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE`, default 4), BLE
entries are bound to their profile and flushed on endpoint switch /
disconnect — so releases survive a busy endpoint and never cross hosts.
Hardware-verified 2026-09-14 (checklist + results: next-steps item r).
**Split stamp (2026-09-20, both halves flashed; hardware-verified
2026-09-21: LH timestamp spread 1.44 → 0.36 ms = RH parity — next-steps
item y):**
the LH pad's frames now carry the LH's own sample time — the module's
peripheral-only `zip_raw_touch_split_stamp` processor on the LH's
`&cirque_split` relay sends one extra vendor-typed input event per frame
ahead of its sync, and `raw_touch_process_frame()` on the central prefers
it over its own clock; the host needs no change (one clock per source).
Both halves must be flashed from the same build. Over a BLE split the
LH needs `CONFIG_BT_*_TX_*=8` (go60_lh.conf) or the 4th notification
per frame blocks the input thread and delays the next stamp. Honest
stamps expose split-hop delivery lateness to the resampler (LH over USB
needs ~5 ms at p90) — answered by the host's **adaptive per-source
latency** (next-steps item z: implemented 2026-09-21 in rawtouch
`d2b4571` + review fixes `577f519`, deployed 2026-09-21; feel verdict
still open).
Two behaviours to know when testing: a USB unplug mid-touch is a
*handover* to BLE, not a stop (the app leases both endpoints); and a BT
profile switch mid-touch discards the trailing release into the empty
profile, so the host's watchdog closes that gesture — test switches in
pointer context (profile keys + System + Nav need three hands).
The scroll fallback is simply quitting RawTouch → Standard mode (no
software needed). The old LinearMouse touch-stream fork is **obsolete
as a fallback** since lease-conditional emission (2026-08-31: it never
acquires a lease, so it gets a silent stream while suppressing wheel events
host-side = no scrolling at all) and **will not be released publicly**;
RawTouch is the release vehicle. Since 2026-08-31,
/Applications/LinearMouse.app is the **stock-inputscale** build
(upstream + the 2 inputScale commits, NO touch-stream code) — it runs
ALONGSIDE RawTouch for pointer processing; the never-run-both rule
applied to the fork build only. macOS
quirk: USB feature-report GETs arrive report-ID-prefixed, BLE bare.
Tap-to-click is firmware-side; the pads' chains must NOT contain
`&zip_button_behaviors`, which would eat the injected BTN_0.
**Per-pad roles (2026-09-15):** the LEFT pad is a **dedicated two-axis
scroll pad** — `&zip_raw_touch_scroll` sits in its BASE listener chain, so
every touch is scroll context on every layer, with no pointer motion; a
tap RIGHT-clicks (since 2026-09-23: `tap-click` +
`tap-click-while-scrolling` on `raw_touch_lh`, mapper in the chain; in
RawTouch mode the firmware parks the tap and the host confirms it unless
the touch caught a coast — module README "Tap confirm"). The RIGHT pad is unchanged — pointer + tap on
the base layer, scroll while Nav is held. Both fallback chains are
two-axis (X→`REL_HWHEEL`, Y→`REL_WHEEL`, `INPUT_TRANSFORM_Y_INVERT` only);
the host is two-axis too (`axes`, `pads.<id>.axes`,
`pads.<id>.invertHorizontal`). **RH flashed + fully hardware-verified 2026-09-15** (USB and BLE) —
see next-steps item s.

Two gotchas that have each cost real debugging time: (1) macOS caches the
BLE HOGP report map — ANY report-layout change needs forget + re-pair, and
the failure is deceptively partial (keys/USB/feature report fine, only BLE
frame parsing dead); (2) the in-tree driver's force-recalibrate patch had
a boot race that could leave a pad dead (SW_CC stuck, DR never fires)
while keys work — **fixed 2026-08-28** (amended patch 3/3, pinned in
`config/west.yml`), so a dead pad on boot is now a REAL bug to
investigate and report, not the known race; power-cycling the half is
still the way to get typing again. Also: announce every host deploy. The
flash watcher enforces its own single-instance rule (see the build loop).

Repos (`v0-prototype` tag on the older four = validated prototype; binaries
in `firmware/raw-touch-v0-prototype/`):
- this repo, `main` — the daily driver (module architecture, v3, both pads)
- `~/src/go60-rawtouch-config` (`kalakris/go60-rawtouch-config`, **private**
  until the flip) — the **public starter** for newcomers: MoErgo's west
  template + six adoption commits, STOCK split roles (left half central;
  the reverse of this keyboard, so hardware-untested — its
  `docs/hardware-checklist.md` says what). `main` cannot build in Actions
  while the module is private; the throwaway branch `ci-vendored` vendors
  it (delete after the flip). Its west.yml carries TEMPORARY pins marked
  for the release tags. State: publish brief, "Public starter configuration"
- `~/src/zmk-raw-touch` (`kalakris/zmk-raw-touch@main`) — **the module**: private HID report descriptor, second USB HID interface + second BLE HIDS instance, frame handler, `zip_raw_touch_scroll` marker, `zip_raw_touch_idle_filter`. Name final (renamed from `-wip`); still **private** — vendored into `vendor/` for CI
- `~/src/zmk` (`kalakris/zmk@raw-touch`) — the old ZMK core patch. **Dead; safe to delete** — `cfc4b3e6` is salvaged as `patches/zmk-skip-empty-mouse-report-syncs.patch`
- `~/src/cirque-input-module` — `@intree-driver` (tip `89a0896` since 2026-09-23: Zephyr main's driver vendored pristine + 3 patches, ALL Pete Johanson's own code from his module (`0759bf6`) that stock Go60 firmware runs — kept for that reason; the unused sample-rate commit was dropped and the branch force-pushed; patch 2 is inert on every Go60 (nobody sets `x/y-axis-z-min`), patch 3 recalibrates at the configured ADC gain after the driver's own reset calibration; patch 3/3's dead-pad boot race was **fixed 2026-08-28** — wait for SW_CC to assert before clearing, re-check DR after arming the edge interrupt — which cleared the must-fix gate before upstreaming) and the historical `@raw-touch` fork. Never PR abs-mode anywhere — see [docs/pinnacle-driver-landscape.md](docs/pinnacle-driver-landscape.md)
- `~/src/rawtouch` (`kalakris/rawtouch`, **private** since 2026-09-04; push over HTTPS) — **RawTouch, the host**: menubar app over `RawTouchCore` (the CLI daemon was DROPPED 2026-09-23; the flock instance lock now guards two app copies). **The menubar app IS the live host since 2026-08-30** (`~/Applications/RawTouch.app`; the iTerm-tab CLI arrangement is retired). Config `~/.config/rawtouch/config.json` live-reloads via file watcher; flock instance lock stops a second app copy. Scroll synthesis is display-rate resampled (`FrameResampler` + CVDisplayLink vsync ticks, carry semantics at stops). **Latency is adaptive per source since 2026-09-21** (`d2b4571` + review fixes `577f519`, item z; deployed 2026-09-21, feel verdict open): `RawTouchLatencyEstimator` keeps each source's last 200 frames' arrival lateness against their own device timestamps (epoch-min-anchored; epochs under 10 frames — taps — are left out of the percentile) and the pipeline adopts p90 + 0.5 ms at every touch-down (same-pad catches included), held for the gesture; 15 ms seed until 20 frames; the measurement survives a claim lapse; measured levels ≈ 1 ms local/USB, 5 ms relayed/wired split, 10 ms relayed/BLE split, 14–16 ms any BLE host link — the bench's replay sweep (`scroll-bench --offline --sweep --replay bench/captures/*.csv`) put the knee at or just under p90 on every path. The manual override (`resampling.adaptive` / `latencyMs` / `bluetoothLatencyMs`) was DELETED 2026-09-23 (feel verdict accepted); the Advanced tab shows a read-only measured-latency row per pad and connection. The Settings "Last gesture" strip gained `Latency X ms · measured Y · held N of M frames` (held = display ticks that had to hold for a late frame = the stutter count). **Lift-off since 2026-09-21 (item aa, deployed, user: "so much better"):** `momentum.liftStrengthFloor` 0.6 (Advanced slider, the ONE switch, 0 = off) marks the finger leaving when strength drops below 0.6 × the touch's own p75 strength; a flick (would coast + not braking, a/v ≥ −5/s on raw counts) is carried through the fade at its firm velocity and coasts from it, anything else is followed as reported — one decision, no gate/cap cascade (the user dislikes conditional cascades; review such logic as pseudocode first). The momentum seed = raw finger velocity fit × gain at lift (was fitted on gain-scaled positions since the 2026-08-25 ballistics commit, which baked in the gain's 40 ms EMA lag: seeds at 0.69 of peak × gain, now 0.96). Readout lift-off line ends "carried N frames"; since 2026-09-23 every speed/distance is PHYSICAL — "Peak finger N mm/s" (on the pad) vs "Lift-off N mm/s on screen · coasted N mm" (display pt/mm from the gesture's display, fallback main display, then 4.3) — and the config keys are mm/s too: `acceleration.referenceFingerSpeed` (40 = old 1,500 counts/s at 38 counts/mm), `momentum.minimumFlickSpeed` (23), `momentum.maximumCoastSpeed` (4,600); old keys convert once on load (skipped when the load reported an error). `bench/lift-strength.py <capture.csv>` re-validates the floor on any pad; `bench/plot-sweep.py` renders `--sweep` output as small multiples with the knee. `scale` is a **gain relative to physical 1:1** (pt/count derived per gesture from the pad's counts/mm and the display's pt/mm; both panels here ≈4.3 pt/mm), and the shipped defaults are the user's hardware-tuned Apple-like curve (acceleration on, exponent 0.9, minGain 1, maxGain 16, decay 0.55 s). Bench tooling: `scroll-bench --offline` (deterministic 120 Hz cadence table — the real validation) and `bench/safari-bounce/` (Safari integration; note Safari's page-side rAF runs at 60 Hz even on ProMotion, so the page log cannot observe 120 Hz cadence — use `--offline` for that; `record.py` captures a LIVE session, page log + pad frames, for felt-but-not-benched problems). WebKit rule learned 2026-09-04: Safari reads the INTEGER point delta, so a scroll `began` must carry ≥ 1 pt or it is a zero-delta began that never reaches the rubber-band controller — the poster defers the began until a whole point has accumulated. Extracted from the LinearMouse fork; 349 unit tests. **Multi-keyboard since 2026-09-15** (`ddfdd31`, deployed, next-steps item t): every HID endpoint is its own source — (endpoint generation × pad) keys clock/seq/watchdog/geometry/claim, one gesture at a time arbitrated across all sources; persistent key `id:<device_id hex>` since 2026-09-23 (item ee: USB and BLE of one keyboard share ONE config entry, one Keyboards-tab block, one menu line; legacy `usb:`/`bt:` keys only for firmware without a device id, migrated non-destructively in `RawTouchService` behind a 1 s settle timer; firmware/protocol/pad-half versions are a per-KEYBOARD record), config `devices.<key>.pads.<id>` > global; two physical keyboards NOT hardware-tested. App logs: `/usr/bin/log show --last 2m --info --predicate 'subsystem == "io.github.kalakris.RawTouch"'` (needs `--info`; run unsandboxed). Two-axis (vertical + horizontal) since 2026-09-15: config `axes` / `pads.<id>.axes` / `pads.<id>.invertHorizontal`, orientation-derived signs hardware-verified, direction edits apply live, axis/axes edits end the gesture. Since 2026-09-04: types carry the `RawTouch*` prefix (no `TouchStream*`), `RawTouchService` owns the daemon lifecycle (the CLI daemon was dropped 2026-09-23), IOKit report transfers run on a per-endpoint serial queue off main, and a `RawTouchTestSupport` target holds the shared test helpers. Bundle ID / LaunchAgent label / log subsystem = `io.github.kalakris.RawTouch` (since 2026-09-04; the first deploy after that change needs a fresh Accessibility grant regardless of signing). TCC: the app bundle holds its OWN Accessibility grant; `make-app.sh` auto-finds the user's **Developer ID Application** cert (Team `7WBD7URF58`, since 2026-09-16; before that the Apple Development cert of the free team `L2ZD95Z272` — that switch cost one re-grant), so rebuilds keep the grant. Do NOT judge signing from a SANDBOXED `security find-identity`: it sees no identities and wrongly suggests ad-hoc. CLI-under-terminal attributes to the terminal instead
- `~/src/linearmouse` (`kalakris/linearmouse`) — the old host consumer. `main` = the frozen touch-stream fork (never released; obsolete even as a fallback post-claim-gated-emission). **`stock-inputscale`** (2026-08-31) = upstream `9843332` + the 2 `inputScale` commits (UI commit amended: `fieldRange:` dropped — that param was fork-only plumbing) — this is what's INSTALLED at /Applications/LinearMouse.app (pointer processing + input scaling, coexists with RawTouch) and the cleanest upstream-PR base (item g). `inputscale` = the old fork-stacked variant, superseded

Build loops:
- **Firmware**: edit on `main` → push → `./scripts/download-firmware.sh` (waits for the branch-tip run) → `./scripts/flash-go60.sh firmware/main/firmware [--halves both|lh|rh]` run in the background (bootloader: RH T3 + `/`; right half only for scroll/stream changes, both halves for driver or left-pad-config changes). The watcher **exits 0 by itself once every requested half has flashed** — so a backgrounded run notifies the agent when flashing is done, no polling or killing; it waits indefinitely (so leaving the desk mid-flash is fine; `--timeout SECONDS` opts into an idle timeout, exit 2), exit 3 = another watcher already holds the lock (never start a second one; wait for or kill the owner it names).
- **Module edits**: the module repo is private, so CI cannot fetch it — edit `~/src/zmk-raw-touch`, then `./scripts/sync-raw-touch-module.sh` and commit `vendor/zmk-raw-touch/`. Pushing the module repo alone changes nothing.
- **Host (menubar app — the live host)**: edit `~/src/rawtouch` → user quits the app (releases the lease) → `./scripts/make-app.sh` assembles + signs `~/Applications/RawTouch.app` (signs with the keychain's Developer ID cert automatically, universal arm64+x86_64; `RAWTOUCH_SIGN_ID` only overrides) → relaunch. Launching/quitting is the user's move unless they ask for a deploy; when they do, the sequence that works is `osascript -e 'quit app "RawTouch"'` (releases the lease), `./scripts/make-app.sh`, `open ~/Applications/RawTouch.app`, and announce it. Bigger host features have been delegated to an opus subagent with a precise brief, then independently built/tested/reviewed before deploy. **Settings UI since 2026-09-16** (next-steps item u): `Settings` scene with toolbar tabs Scrolling / Advanced / Keyboards, one confirmed Restore Defaults per tab, per-pad orientation behind a disclosure row, plain labels + config keys in tooltips, and a live "Last gesture" readout (`RawTouchGestureSummary`) for tuning; since 2026-09-23 keyboard and pad names are edited IN their section headers (persistent tertiary pencil = a real "Rename" button, click the text = caret, Return commits, Escape reverts — `RenameField` state machine; `devices.<key>.name` / `pads.<id>.name`, 24 chars), the Keyboard picker is hidden for a single keyboard, and the tab's one action is "Remove Settings…";
since 2026-09-22 (item bb, third critique) the lift-off strength floor
sits in Scrolling › Momentum and the menu's master switch is "Use RawTouch
Scrolling" (never "Enable Scrolling"). **Versioning (item cc, 2026-09-22):**
app = SemVer `0.y.z` from the `v*` tag (`make-app.sh` stamps
`CFBundleShortVersionString`, `CFBundleVersion` = commit count,
`RawTouchGitCommit`; untagged builds read `0.0.0-dev`), module = its own
`v0.y.z` tags with `include/zmk/raw_touch/version.h` as the source, and
the protocol integer (4) is the ONLY compatibility contract. "About
RawTouch" in the menu opens the system About panel; the Keyboards tab's
"Firmware" row reads the module version from the feature report's two
former reserved bytes (body byte 3 = central, slot byte 7 = the pad's
half; 0 = unknown — a split peripheral announces its version before the
first frame of each touch, and the host re-reads the report on that frame
(`RawTouchFirmwareVersionProbe`), so a relayed pad's version appears after
its first touch; power-cycle the RIGHT half to reproduce "unknown").
Hardware-verified USB + BLE 2026-09-22 (515 tests as of 2026-09-23). The three 2026-09-16
pipeline-test releases v0.1.0–v0.1.2 were DELETED; the first real release
will be v0.1.0 (tags not yet pushed). To see the native UI without computer use: AppleScript opens menu/Settings, `screencapture -l <CG window id>` captures it (both unsandboxed); judge accessibility with the AX API, NOT System Events `name` (SwiftUI buttons label via AXDescription only). There is no CLI any more (dropped 2026-09-23). Legacy LinearMouse loop: `./linearmouse/build-and-install.sh`, config live-reloads.
- **Host release** (since 2026-09-16, next-steps item f done): `cd ~/src/rawtouch && git tag vX.Y.Z && git push origin vX.Y.Z` — the tag-only `Release` workflow (`.github/workflows/release.yml`, `release` environment secrets) builds, signs with Developer ID, notarizes, staples and attaches a **DMG + zip** to a GitHub release in ~1 min, version stamped from the tag; provenance attestation is gated on the repo being public. Local equivalent: `./scripts/make-app.sh && ./scripts/notarize.sh` (keychain profile `rawtouch-notary`; Apple's first-ever submission took 30 min, later ones seconds). Verify a download with `spctl --assess --type open --context context:primary-signature -v X.dmg`. App icon: `resources/RawTouch.icns`, regenerated from `resources/icon/*.svg` by `scripts/make-icon.sh` (needs `rsvg-convert`). **Secrets never pass through chat** — the user runs `gh secret set` / `notarytool store-credentials` at a prompt; the .p12 backup is in `~/Documents`.
- **CI in both release repos** (since 2026-09-21): rawtouch `.github/workflows/ci.yml` on `macos-26` (swift build, swift test, `scroll-bench --offline`, `make-app.sh` ad-hoc signed, `nm -u` event-tap check; push to main / PR / dispatch; ~80 s, no cache) — `release.yml` `needs` it. Module: `examples/{trackpad,split}/` ARE the README's config blocks (`<!-- example: path -->` / `<!-- example-diff: name before after -->` markers; `scripts/check-readme-examples.py --write` regenerates the README from the files, plain run = the CI check, orphan example files fail it) and `ci/` is a ZMK config dir that compiles the after-files on both Go60 halves via ZMK's `build-user-config.yml@v0.3` against the pinned trees in `ci/west.yml` (bump with the README's compatibility table; the per-half keymaps `#define` the README's generic labels onto the board's nodes and reach `examples/` through `-DDTS_EXTRA_CPPFLAGS=-I$GITHUB_WORKSPACE`). After editing an example: run the script with `--write`, commit both. No upstream-ZMK-main canary by design.
- **UI verification without computer use**: the tools live in `/tmp/claude/rt-critique/` (`capture.sh <dir>` = menu + every tab as PNG + AX dumps, `ax dump|all|press|focused`, `axactions` = AX action names per heading/tab, `locked` = check the screen lock FIRST — a locked screen makes every capture look like a hang). System Events can wedge (every AppleEvent -1712 after ~60 s): `killall "System Events"`. Hidden ⌘-shortcut Buttons become AppKit's initial first responder → `.focusable(false)`. The user does NOT want synthetic pointer clicks on the app (System Events `click at`) — leave click tests to them. Sixth `/impeccable critique` 2026-09-23: 32/40 (trend 23→30→27→30→27→32); snapshots in rawtouch `.impeccable/critique/`.
- **Vocabulary (2026-09-23)**: the host's hold on the stream is a **lease** (acquire / renew / release / lapse) — never "claim", never "gate". Bench: `scripts/lease.swift` (acquire|release|hold|raw), `raw-touch-monitor.swift --lease`.
- **TCC rule**: the user must NEVER grant Accessibility prompts raised during `xcodebuild test` runs (they bind to the DerivedData test-host copy and lock the real app out — the "accessibility loop"). Grant only right after a deploy. Recovery recipe: docs/raw-touch.md → "THE ACCESSIBILITY-LOOP TRAP".

Full state doc (architecture, tuning knobs, gotchas, rollback):
[docs/raw-touch.md](docs/raw-touch.md). **Resumable next-steps list
(start here in a fresh session):** [docs/next-steps.md](docs/next-steps.md).
Pre-upstreaming punch list:
[docs/upstreaming-todo.md](docs/upstreaming-todo.md). Multi-finger successor
evaluation — Azoteq TPS43 on a Sofle bench rig, researched but **not started**,
electrical design closed: [docs/tps43-bench.md](docs/tps43-bench.md). Prior art and the
claims to avoid making publicly: [docs/prior-art-survey.md](docs/prior-art-survey.md).
Driver-ecosystem state: [docs/pinnacle-driver-landscape.md](docs/pinnacle-driver-landscape.md).
Zephyr 4.1 / ZMK 0.4 migration decision (currently: **wait**, with named
triggers): [docs/zephyr-41-migration.md](docs/zephyr-41-migration.md).
**The module — what was built, the temporary vendoring workaround, and
what remains before flipping the repo public (host cleanup re-land, demo
video, un-vendor):**
[docs/module-publish-brief.md](docs/module-publish-brief.md).

## Go60 Layout Editor Export

The keymap can also be exported to MoErgo's Go60 Layout Editor format. See [docs/go60-export.md](docs/go60-export.md) for details.

```bash
python3 scripts/generate-go60-layout.py
# Import sofle-eyelash-go60-layout.json at https://my.moergo.com/go60/
```
