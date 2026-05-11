# Go60 per-layer RGB underglow (darknao/zmk PR36)

## What it is

[moergo-sc/zmk#36](https://github.com/moergo-sc/zmk/pull/36) — "Per key/layer
RGB underglow for 25.11" by @darknao. An open PR on `darknao/zmk` branch
`rgb-layer-24.12`. Adds per-key, per-layer RGB underglow as a new effect
mode (`UNDERGLOW_EFFECT_LAYER_INDICATORS`, effect index `4`).

## Why we're on this fork

We've replaced `moergo-sc/zmk:go60-zmk0.3.0` with `darknao/zmk:rgb-layer-24.12`
in `config/west.yml` to get this feature. Trade-offs:

- The PR is unmerged, so we don't get future MoErgo updates without rebasing.
- The `&ug` binding syntax has already broken once during the PR's lifetime.
  If darknao force-pushes a breaking change, our `bindings = LAYER_RGB(...)`
  rows in `config/go60.keymap` may need touch-ups.
- The PR includes a Go60 RH-thumb pixel-lookup fix (T1/T3 swap), so the
  built-in `underglow-indicators` work correctly on RH. The fix landed
  on the branch in March 2026.
- The Sofle build is also affected — it now compiles against darknao's
  fork too. The Sofle keymap doesn't use any new behaviors, so it should
  be unchanged in behaviour.

## What's wired up (minimal)

A single corner LED on each half lights a distinct colour for each non-base
layer. Base layer = LEDs off.

| Layer | Name     | Colour  | Hex      |
|-------|----------|---------|----------|
| 0     | Base     | (off)   | —        |
| 1     | Graphite | cyan    | `0x00FFFF` |
| 2     | Nav      | green   | `0x00FF00` |
| 3     | System   | red     | `0xFF0000` |
| 4     | Numpad   | yellow  | `0xFFFF00` |
| 5     | Tmux     | magenta | `0xFF00FF` |
| 6     | Vim      | orange  | `0xFF8000` |
| 7     | Magic    | white   | `0xFFFFFF` |
| 8     | Spaces   | blue    | `0x0000FF` |

Indicator positions:

- `0` — LH outer-top corner (Esc / `~` key on Base)
- `11` — RH outer-top corner (Backspace key on Base)

When multiple layers are stacked, the highest active layer wins (other
layers' bindings are `&trans`, falling through).

## Files touched

- `config/west.yml` — points `zmk` project at `darknao/zmk:rgb-layer-24.12`.
- `config/go60.conf` — adds `CONFIG_EXPERIMENTAL_RGB_LAYER=y` and sets
  `CONFIG_ZMK_RGB_UNDERGLOW_EFF_START=4` (was `3` = SWIRL).
- `config/go60.keymap` — adds an `underglow-layer { ... }` block (extends
  the node already declared in `app/boards/arm/go60/go60_lh.dts` and
  `go60_rh.dts`) with one `*_rgb` child per layer. Plus `IND_*` colour
  macros and a `LAYER_RGB(color)` helper that emits the 60-entry bindings
  array.

## How `&ug` works

- `compatible = "zmk,behavior-underglow-color"`, `#binding-cells = <1>`.
- The cell is a packed 24-bit `0xRRGGBB` value (NOT HSB — the `RGB_COLOR_HSB`
  macro from `<dt-bindings/zmk/rgb.h>` is for the 2-cell `&rgb_ug` behaviour
  and won't fit here).
- Pixel brightness = `state.color.b * channel / 255`. With our
  `BRT_MAX=90` cap and default `BRT_START`, even `0xFF`-channel values
  display at moderate intensity.
- `&trans` returns `ZMK_BEHAVIOR_TRANSPARENT`; the engine continues to
  the next active layer's binding.
- A position with no `bindings` entry (NULL behavior_dev) is treated the
  same as `&trans`.

## How activation works

- `state.layer_enabled` is initialised to `false` on every boot, regardless
  of `EFF_START`. With `ON_START=n` (our existing setting), RGB is off
  on cold boot.
- The first time you toggle RGB on (`&rgb_ug RGB_TOG` — already bound on
  the Magic layer), if `current_effect == LAYER_INDICATORS` (which it is,
  thanks to `EFF_START=4`), `layer_enabled` flips to `true`.
- Once `layer_enabled = true`, settings are persisted to flash, so future
  boots restore it automatically.
- Auto-off-idle still applies. After `ZMK_IDLE_TIMEOUT` ms (default 30 s
  on Go60 from `Kconfig.defconfig`) of no activity, RGB powers down.
  It re-enables on next layer change.

## First-boot procedure

1. Flash both halves with the new firmware.
2. Hold the Magic-layer key, tap `RGB TOG` (the LED-toggle binding on
   Magic). Both indicator LEDs should light up to reflect the active layer.
3. Activate any non-base layer to confirm: e.g. tap-hold for Nav and the
   corner LEDs should turn green.

## Adding more colour to a layer

Replace `&trans` entries in the relevant `*_rgb` child node with explicit
`<&ug 0x...>` values. Index = keymap position number (see
`config/positions_go60.dtsi` for the position grid). Example: to light the
hjkl cluster on Vim layer (positions 30, 31, 32, 33), edit `vim_rgb`:

```
vim_rgb {
    layer-id = <6>;
    bindings = <... position 30 = &ug 0xFF8000 ... etc ...>;
};
```

Note that the `LAYER_RGB(color)` macro is just for the corner-only style
— for richer per-layer maps you'll want to write the 60 entries out.

## Rolling back

If the fork breaks something, revert `config/west.yml` to:

```yaml
- name: zmk
  remote: moergo-sc
  revision: go60-zmk0.3.0
  import: app/west.yml
```

…and remove `CONFIG_EXPERIMENTAL_RGB_LAYER=y` from `config/go60.conf`,
restore `CONFIG_ZMK_RGB_UNDERGLOW_EFF_START=3`, and delete the
`underglow-layer { ... }` block from `config/go60.keymap` (along with the
`IND_*` macros and `LAYER_RGB`). The remaining keymap is unchanged.

## Pre-built firmware

darknao's CI built artifacts on 2026-03-24:
[run 23491528405](https://github.com/darknao/zmk/actions/runs/23491528405)
contains `go60.uf2` (320.6 KB, no expiry). That's the stock keymap — it
won't reflect this repo's per-layer RGB config; for that you need to push
this branch and let our own GH Actions build.
