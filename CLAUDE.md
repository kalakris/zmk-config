# CLAUDE.md

## Repository Overview

ZMK firmware configuration for two split keyboards — the **Eyelash Sofle** and the **MoErgo Go60** — sharing a single keymap via C preprocessor macros. The actual ZMK firmware source is pulled in via West (Zephyr's package manager). Branch map for the Go60 (see "Raw touch scrolling" below):

- `main` — **what the user's Go60 runs.** Stock MoErgo ZMK (pinned SHA, no ZMK fork) + the out-of-tree `zmk-raw-touch` module (vendored under `vendor/` while its repo is private) + Zephyr main's in-tree Pinnacle driver via `cirque-input-module@intree-driver`. Hardware-verified 2026-08-27 over USB and BLE (promoted from `module-port-intree`). Both trackpads stream (protocol v3): RH = pad-id 0, LH = pad-id 1 via the `raw_touch_lh` node; sensitivity is deliberately asymmetric — "1x" LH / "2x" RH (gain is per-pad; 2x tames RH baseline-drift jitter but caused light-touch dropouts on LH).
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
2. **Nav** — Navigation, function keys, mouse keys. While held, the Go60's trackpads become scrollers: each pad's `nav_scroll` listener overlay carries the `&zip_raw_touch_scroll` scroll-context marker plus a ÷24 wheel fallback chain (the LH overlay uses its own `zip_raw_touch_idle_filter_lh` — one filter instance per listener)
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
fixed defines; report ID 0x04; **protocol v3**: 11-byte frames with
pad_id/contact_id/seq/100 µs timestamp at ~100 Hz, feature report of
4 + 8 × pads bytes with per-pad geometry slots — 20 on the Go60, hosts
must not hard-code 20 (since 2026-09-04); spec = the module README's
appendix, authoritative). BLE release frames are undroppable since
2026-09-04 (spinlocked ring, evicts motion frames only, head-of-line
retry; queue default 8), so the host's 150 ms silence watchdog is a
safety net, not a correctness requirement. **The host is RawTouch** (`~/src/rawtouch`, standalone
SwiftPM daemon; since 2026-08-30). Two scrolling modes — this naming is
canonical (2026-08-31; never "legacy/basic/fallback mode"): **Standard
mode** — no host software; the firmware scrolls on its own (pointer,
tap, Nav-layer ÷24 wheel) and the touch stream is silent; and **RawTouch
mode** — RawTouch holds the stream claim (SET feature report,
refreshed, endpoint-scoped), the firmware emits frames only while
claimed (since 2026-08-31; flags bit 2 = `host_claimed`, implied-set)
and suppresses the ÷24 wheel, and RawTouch synthesizes scroll — with
real gesture phases, lift-off momentum, ballistics, device-time
reconstruction, and cross-pad-catch arbitration — posting at the session
event tap (composes with any mouse tool; never post at the HID tap). A
claim clearing mid-touch gets one trailing bit-2-clear release frame;
the host answers by canceling that pad's series WITHOUT momentum. The
keyboard reverts to Standard mode whenever the claim lapses (timeout,
release, endpoint switch, host death).
The scroll fallback is simply quitting RawTouch → Standard mode (no
software needed). The old LinearMouse touch-stream fork is **obsolete
as a fallback** since claim-gated emission (2026-08-31: it never
claims, so it gets a silent stream while suppressing wheel events
host-side = no scrolling at all) and **will not be released publicly**;
RawTouch is the release vehicle. Since 2026-08-31,
/Applications/LinearMouse.app is the **stock-inputscale** build
(upstream + the 2 inputScale commits, NO touch-stream code) — it runs
ALONGSIDE RawTouch for pointer processing; the never-run-both rule
applied to the fork build only. macOS
quirk: USB feature-report GETs arrive report-ID-prefixed, BLE bare.
Tap-to-click is firmware-side; the pads' chains must NOT contain
`&zip_button_behaviors`, which would eat the injected BTN_0.

Two gotchas that have each cost real debugging time: (1) macOS caches the
BLE HOGP report map — ANY report-layout change needs forget + re-pair, and
the failure is deceptively partial (keys/USB/feature report fine, only BLE
frame parsing dead); (2) the in-tree driver's force-recalibrate patch has
a boot race that can leave a pad dead (SW_CC stuck, DR never fires) while
keys work — power-cycle the half. Also: announce every host deploy. The
flash watcher enforces its own single-instance rule (see the build loop).

Repos (`v0-prototype` tag on the older four = validated prototype; binaries
in `firmware/raw-touch-v0-prototype/`):
- this repo, `main` — the daily driver (module architecture, v3, both pads)
- `~/src/zmk-raw-touch` (`kalakris/zmk-raw-touch@main`) — **the module**: private HID report descriptor, second USB HID interface + second BLE HIDS instance, frame handler, `zip_raw_touch_scroll` marker, `zip_raw_touch_idle_filter`. Name final (renamed from `-wip`); still **private** — vendored into `vendor/` for CI
- `~/src/zmk` (`kalakris/zmk@raw-touch`) — the old ZMK core patch. **Dead; safe to delete** — `cfc4b3e6` is salvaged as `patches/zmk-skip-empty-mouse-report-syncs.patch`
- `~/src/cirque-input-module` — `@intree-driver` (Zephyr main's driver vendored + 3 patches; patch 3/3 carries the dead-pad boot race, must-fix before upstreaming) and the historical `@raw-touch` fork. Never PR abs-mode anywhere — see [docs/pinnacle-driver-landscape.md](docs/pinnacle-driver-landscape.md)
- `~/src/rawtouch` (`kalakris/rawtouch`, **private** since 2026-09-04; push over HTTPS) — **RawTouch, the host**: menubar app + CLI daemon over shared `RawTouchCore`. **The menubar app IS the live host since 2026-08-30** (`~/Applications/RawTouch.app`; the iTerm-tab CLI arrangement is retired). Config `~/.config/rawtouch/config.json` live-reloads via file watcher; flock instance lock stops app/CLI double-runs; LaunchAgent plist in `resources/` for headless use. Scroll synthesis is display-rate resampled (`FrameResampler` + CVDisplayLink vsync ticks, carry semantics at stops; latency is per transport — `resampling.latencyMs` 0 for USB, `bluetoothLatencyMs` 10 for BLE — adopted at each gesture's touch-down from the frame's IOHIDDevice transport). `scale` is a **gain relative to physical 1:1** (pt/count derived per gesture from the pad's counts/mm and the display's pt/mm; both panels here ≈4.3 pt/mm), and the shipped defaults are the user's hardware-tuned Apple-like curve (acceleration on, exponent 0.9, minGain 1, maxGain 16, decay 0.55 s). Bench tooling: `scroll-bench --offline` (deterministic 120 Hz cadence table — the real validation) and `bench/safari-bounce/` (Safari integration; note Safari's page-side rAF runs at 60 Hz even on ProMotion, so the page log cannot observe 120 Hz cadence — use `--offline` for that). Extracted from the LinearMouse fork; 256 unit tests. Since 2026-09-04: types carry the `RawTouch*` prefix (no `TouchStream*`), `RawTouchService` owns the daemon lifecycle for both CLI and app, IOKit report transfers run on a serial queue off main, and a `RawTouchTestSupport` target holds the shared test helpers. Bundle ID / LaunchAgent label / log subsystem = `io.github.kalakris.RawTouch` (since 2026-09-04; the first deploy after that change needs a fresh Accessibility grant regardless of signing). TCC: the app bundle holds its OWN Accessibility grant (ad-hoc signing = re-grant per rebuild; `RAWTOUCH_SIGN_ID` = stable); CLI-under-terminal attributes to the terminal instead
- `~/src/linearmouse` (`kalakris/linearmouse`) — the old host consumer. `main` = the frozen touch-stream fork (never released; obsolete even as a fallback post-claim-gated-emission). **`stock-inputscale`** (2026-08-31) = upstream `9843332` + the 2 `inputScale` commits (UI commit amended: `fieldRange:` dropped — that param was fork-only plumbing) — this is what's INSTALLED at /Applications/LinearMouse.app (pointer processing + input scaling, coexists with RawTouch) and the cleanest upstream-PR base (item g). `inputscale` = the old fork-stacked variant, superseded

Build loops:
- **Firmware**: edit on `main` → push → `./scripts/download-firmware.sh` (waits for the branch-tip run) → `./scripts/flash-go60.sh firmware/main/firmware [--halves both|lh|rh]` run in the background (bootloader: RH T3 + `/`; right half only for scroll/stream changes, both halves for driver or left-pad-config changes). The watcher **exits 0 by itself once every requested half has flashed** — so a backgrounded run notifies the agent when flashing is done, no polling or killing; it waits indefinitely (so leaving the desk mid-flash is fine; `--timeout SECONDS` opts into an idle timeout, exit 2), exit 3 = another watcher already holds the lock (never start a second one; wait for or kill the owner it names).
- **Module edits**: the module repo is private, so CI cannot fetch it — edit `~/src/zmk-raw-touch`, then `./scripts/sync-raw-touch-module.sh` and commit `vendor/zmk-raw-touch/`. Pushing the module repo alone changes nothing.
- **Host (menubar app — the live host)**: edit `~/src/rawtouch` → user quits the app (releases the gate) → `./scripts/make-app.sh` assembles + signs `~/Applications/RawTouch.app` (`RAWTOUCH_SIGN_ID` for a TCC-stable signature, else re-grant Accessibility per rebuild) → relaunch. Launching/quitting is the user's move unless they ask for a deploy; when they do, the sequence that works is `osascript -e 'quit app "RawTouch"'` (releases the claim), `./scripts/make-app.sh`, `open ~/Applications/RawTouch.app`, and announce it. Bigger host features have been delegated to an opus subagent with a precise brief, then independently built/tested/reviewed before deploy. **Host (CLI, headless fallback)**: `swift build -c release && cp .build/release/rawtouch ~/bin/`; the flock instance lock (`~/.config/rawtouch/rawtouch.pid`, exit 3) stops app/CLI double-runs. Config live-reloads for both. Legacy LinearMouse loop: `./linearmouse/build-and-install.sh`, config live-reloads.
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
