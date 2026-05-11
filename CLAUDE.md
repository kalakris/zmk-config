# CLAUDE.md

## Repository Overview

ZMK firmware configuration for two split keyboards — the **Eyelash Sofle** and the **MoErgo Go60** — sharing a single keymap via C preprocessor macros. The actual ZMK firmware source is pulled in via West (Zephyr's package manager) from MoErgo's fork (`moergo-sc/zmk:go60-zmk0.3.0`).

## Key Files

- `config/shared.dtsi` — All behaviors, macros, combos, and layers (shared between both keyboards)
- `config/positions_sofle.dtsi` — Sofle position names, position groups, ROW/THUMB macros
- `config/positions_go60.dtsi` — Go60 position names, position groups, ROW/THUMB macros
- `config/eyelash_sofle.keymap` — Thin wrapper: includes + Sofle-specific config (encoder, mouse input)
- `config/go60.keymap` — Thin wrapper: includes + Go60-specific config (dual Cirque trackpads)
- `config/eyelash_sofle.conf` — Sofle Kconfig (RGB, sleep, Bluetooth, mouse, encoder, etc.)
- `config/go60.conf` — Go60 Kconfig (RGB, sleep, trackpad, full consumer HID)
- `config/west.yml` — West manifest pointing to MoErgo's ZMK fork
- `build.yaml` — Build targets: 3 Sofle (left+studio, right, settings_reset) + 2 Go60 (lh, rh)
- `boards/` — Custom board definitions for the Eyelash Sofle

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

The workflow also auto-commits a keymap drawing via keymap-drawer (Sofle only; commit message prefixed with `[Draw]`). This means after pushing, the remote may have one extra commit — use `git pull --rebase` before pushing again.

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
2. **Nav** — Navigation, function keys, mouse keys
3. **System** — Bluetooth, system controls, bootloader
4. **Numpad** — Number pad layout, RGB controls
5. **Tmux** — Tmux tab switching via `tmux_tab` macro (Ctrl+A then number)
6. **Vim** — Vim split navigation via `vim_split` macro (Ctrl+W then direction)

### Behaviors
- `hml` / `hmr` — Left/right home-row mods using positional hold-tap with `require-prior-idle-ms`
- `lth` — Layer-tap for thumb keys. Like HRMs but uses `hold-trigger-key-positions` (all keys) and `hold-trigger-on-release` instead of `require-prior-idle-ms`, so layer-hold works reliably after quick keypresses
- `z_tmux` — Hold Z for Tmux layer, tap for Z
- `v_vim` — Hold V for Vim layer, tap for V
- `esc_grave_tilde` — Tap for Esc, Shift for ~, other mods for `

### Key Constants
- `QUICK_TAP_MS = 175` — Quick-tap window used across behaviors

## Go60 Layout Editor Export

The keymap can also be exported to MoErgo's Go60 Layout Editor format. See [docs/go60-export.md](docs/go60-export.md) for details.

```bash
python3 scripts/generate-go60-layout.py
# Import sofle-eyelash-go60-layout.json at https://my.moergo.com/go60/
```

## Go60 Per-Layer RGB Underglow

`west.yml` points the `zmk` project at `darknao/zmk:rgb-layer-24.12` (an open PR adding per-key/per-layer RGB on top of 25.11) instead of the stock MoErgo fork. Per-layer indicator colours live in the `underglow-layer { ... }` block in `config/go60.keymap`. See [docs/go60-rgb-pr36.md](docs/go60-rgb-pr36.md) for the colour map, activation procedure, and how to roll back.
