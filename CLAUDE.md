# CLAUDE.md

## Repository Overview

ZMK firmware configuration for two split keyboards — the **Eyelash Sofle** and the **MoErgo Go60** — sharing a single keymap via C preprocessor macros. The actual ZMK firmware source is pulled in via West (Zephyr's package manager). On `main`, `west.yml` points at MoErgo's fork (`moergo-sc/zmk:go60-zmk0.3.0`) — the stable fallback. **The user's daily Go60 firmware is built from branch `raw-touch`**, whose `west.yml` points at `kalakris/zmk@raw-touch` for the raw touch stream (see "Raw touch scrolling" below).

## Key Files

- `config/shared.dtsi` — All behaviors, macros, combos, and layers (shared between both keyboards)
- `config/positions_sofle.dtsi` — Sofle position names, position groups, ROW/THUMB macros
- `config/positions_go60.dtsi` — Go60 position names, position groups, ROW/THUMB macros
- `config/eyelash_sofle.keymap` — Thin wrapper: includes + Sofle-specific config (encoder, mouse input)
- `config/go60.keymap` — Thin wrapper: includes + Go60-specific config (dual Cirque trackpads)
- `config/eyelash_sofle.conf` — Sofle Kconfig (RGB, sleep, Bluetooth, mouse, encoder, etc.)
- `config/go60.conf` — Go60 Kconfig (RGB, sleep, trackpad, full consumer HID)
- `config/west.yml` — West manifest (moergo upstream on `main`; kalakris/zmk fork on `raw-touch`)
- `build.yaml` — Build targets: 3 Sofle (left+studio, right, settings_reset) + 2 Go60 (lh, rh)
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

An Übersicht widget (`ubersicht-widget/keymap.widget/`) renders one of the committed SVGs on the macOS desktop, pulling it from `origin/<branch>` so a push is enough to update it. The Go60 needs a vendored physical layout (`config/go60-layouts.dtsi`) passed via `draw_args`, because its board lives in the ZMK fork that the draw job does not fetch. See [docs/keymap-overlay.md](docs/keymap-overlay.md) for the layout-ordering gotcha, local rendering, and widget knobs.

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
2. **Nav** — Navigation, function keys, mouse keys. While held, the Go60's right trackpad becomes a scroller: on `raw-touch` the `nav_scroll` listener overlay carries the `&zip_touch_stream_scroll` marker (raw-touch-stream scroll context) plus a ÷8 wheel fallback chain; on `main` it is the plain ÷8 wheel chain
3. **System** — Bluetooth, system controls, bootloader
4. **Numpad** — Number pad layout, RGB controls
5. **Tmux** — Tmux tab switching via `tmux_tab` macro (Ctrl+A then number)
6. **Vim** — Vim split navigation via `vim_split` macro (Ctrl+W then direction)
7. **Magic** — Media, RGB, Bluetooth, bootloader (held via RH T2)
8. **Spaces** — macOS workspace navigation
9. **Mouse** — Go60 only, not bound to any key. Activated automatically by the
   trackpads via `&zip_temp_layer` and dropped `MOUSE_LAYER_MS` after the last
   pointer event. Overrides RH T1 = left click, RH T2 = right click; everything
   else is `&trans`. Wiring lives in `config/go60_rh.keymap`; tuning constants
   (`MOUSE_LAYER`, `MOUSE_LAYER_MS`, `MOUSE_KEEP_KEYS`) in `positions_go60.dtsi`.

### Behaviors
- `hml` / `hmr` — Left/right home-row mods using positional hold-tap with `require-prior-idle-ms`
- `lth` — Layer-tap for thumb keys. Like HRMs but uses `hold-trigger-key-positions` (all keys) and `hold-trigger-on-release` instead of `require-prior-idle-ms`, so layer-hold works reliably after quick keypresses
- `z_tmux` — Hold Z for Tmux layer, tap for Z
- `v_vim` — Hold V for Vim layer, tap for V
- `esc_grave_tilde` — Tap for Esc, Shift for ~, other mods for `

### Key Constants
- `QUICK_TAP_MS = 175` — Quick-tap window used across behaviors

## Raw Touch Scrolling (LinearMouse fork)

Magic-Trackpad-quality scrolling for the Go60's right Cirque pad on macOS.
Firmware (branch `raw-touch`) puts the pad in absolute mode and streams raw
touch frames over a vendor HID report (usage page 0xFF00, report ID 0x04,
7-byte frames at ~100 Hz, plus an 8-byte self-describing feature report,
protocol v2); a patched LinearMouse fork consumes them and synthesizes
scroll events with real gesture phases, lift-off momentum, and a ballistics
curve. Dual-mode: standard pointer + Nav-layer ÷8 wheel events remain as a
driverless fallback; the host suppresses the wheel events per physical
device identity (the Sofle shares ZMK's default VID/PID). Tap-to-click is
firmware-side (`stream-tap-click`); the right pad's chain must NOT contain
`&zip_button_behaviors`, which would eat the injected BTN_0.

Repos (all with tag `v0-prototype` = validated prototype; binaries in
`firmware/raw-touch-v0-prototype/`):
- this repo, branch `raw-touch` — active firmware config + `docs/raw-touch-protocol.md` (wire spec)
- `~/src/zmk` (`kalakris/zmk@raw-touch`) — vendor HID reports, `touch_stream.c`, marker processor
- `~/src/cirque-input-module` (`kalakris/cirque-input-module@raw-touch`) — `abs-mode`, `stream-tap-*` props
- `~/src/linearmouse` (`kalakris/linearmouse@go60-inputscale`) — host consumer; first two commits are a generic `inputScale` upstream-PR candidate

Build loops:
- **Firmware**: `git checkout raw-touch` → push → `./scripts/download-firmware.sh` (waits for the branch-tip run) → `./scripts/flash-go60.sh firmware/raw-touch/firmware` (bootloader: RH T3 + `/`; only the right half needs reflashing for scroll changes) → **return to `main`** (scripts differ between branches; `main`'s are newest).
- **Host**: edit fork → `./linearmouse/build-and-install.sh` (signed, TCC grant persists). Config at `~/.config/linearmouse/linearmouse.json` live-reloads; snapshot to `linearmouse/linearmouse.json` after tuning.

Full state doc (architecture, tuning knobs, gotchas, rollback):
[docs/raw-touch.md](docs/raw-touch.md). Pre-upstreaming punch list:
[docs/upstreaming-todo.md](docs/upstreaming-todo.md).

## Go60 Layout Editor Export

The keymap can also be exported to MoErgo's Go60 Layout Editor format. See [docs/go60-export.md](docs/go60-export.md) for details.

```bash
python3 scripts/generate-go60-layout.py
# Import sofle-eyelash-go60-layout.json at https://my.moergo.com/go60/
```
