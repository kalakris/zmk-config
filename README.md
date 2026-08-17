# ZMK Firmware — Eyelash Sofle & MoErgo Go60

ZMK firmware configuration for two split keyboards sharing a single keymap via C preprocessor macros.

| Keyboard | Halves | Notable Hardware |
|---|---|---|
| **Eyelash Sofle** | Left (ZMK Studio) + Right | Encoder, nice!view displays |
| **MoErgo Go60** | Left + Right | Dual Cirque trackpads |

## Keymap

<img src="keymap-drawer/go60.svg">

### Layers

| # | Layer | Purpose |
|---|---|---|
| 0 | Base | QWERTY with home-row mods (urob timerless HRM) |
| 1 | Graphite | Graphite alpha overlay |
| 2 | Nav | Navigation, function keys, mouse keys |
| 3 | System | Bluetooth, system controls, bootloader |
| 4 | Numpad | Number pad, RGB controls |
| 5 | Tmux | Tmux tab switching (Ctrl+A → number) |
| 6 | Vim | Vim split navigation (Ctrl+W → direction) |

## Shared Keymap Architecture

Both keyboards include `config/shared.dtsi`, which contains all behaviors, macros, combos, and layers. Physical differences are handled by per-board position files:

- **`config/positions_sofle.dtsi`** / **`config/positions_go60.dtsi`** — define `ROW` and `THUMB` macros that map the shared 13-column row layout onto each board's physical matrix.
- **Named positions** (`POS_Q`, `POS_W`, …) let combos and hold-trigger lists work despite different position numbering.
- **`#ifdef HAS_ENCODER`** guards Sofle-specific encoder config.

## Building Firmware

Firmware is built automatically by GitHub Actions on every push. The workflow also auto-commits a keymap drawing (commit message prefixed with `[Draw]`).

```bash
# Push, then download the built firmware:
git push
./scripts/download-firmware.sh            # current branch
./scripts/download-firmware.sh --all      # all remote branches
```

Firmware artifacts are saved under `firmware/<branch>/firmware/`.

## Keymap Drawings & Desktop Overlay

Each push also redraws `keymap-drawer/eyelash_sofle.svg` and `keymap-drawer/go60.svg`. An Übersicht widget can show either one on the macOS desktop, refreshed from the remote so pushing is enough to update it:

```bash
./ubersicht-widget/setup.sh   # first time
./ubersicht-widget/sync.sh    # after editing the widget
```

See [docs/keymap-overlay.md](docs/keymap-overlay.md).

## Go60 Layout Editor Export

The keymap can be exported to MoErgo's Go60 Layout Editor format:

```bash
python3 scripts/generate-go60-layout.py
# Import sofle-eyelash-go60-layout.json at https://my.moergo.com/go60/
```

## Key Files

| File | Purpose |
|---|---|
| `config/shared.dtsi` | Behaviors, macros, combos, layers (shared) |
| `config/eyelash_sofle.keymap` | Sofle wrapper (encoder, mouse input) |
| `config/go60.keymap` | Go60 wrapper (trackpads) |
| `config/positions_sofle.dtsi` | Sofle position names & ROW/THUMB macros |
| `config/positions_go60.dtsi` | Go60 position names & ROW/THUMB macros |
| `config/eyelash_sofle.conf` | Sofle Kconfig |
| `config/go60.conf` | Go60 Kconfig |
| `config/west.yml` | West manifest (MoErgo's ZMK fork) |
| `build.yaml` | GitHub Actions build targets |
| `boards/` | Custom board definitions for Eyelash Sofle |
