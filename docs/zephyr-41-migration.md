# Zephyr 4.1 / ZMK 0.4 migration — decision record (2026-08-26)

Research report on migrating from the current pin (MoErgo fork,
Zephyr 3.5, ZMK v0.3.0-era) to Zephyr 4.1 / ZMK main.

**Decision: WAIT.** Gains outside the in-tree Cirque driver are thin (189
upstream commits in a year, mostly Dependabot); costs land on our two
riskiest surfaces (a hand-rolled HWMv1 split board, and a Go60 board we
don't own); and three open regressions hit our exact config.

## Why not now

**MoErgo is the hard gate.** `moergo-sc/zmk@zephyr-4-1` is an abandoned
proposal branch, not a release:
- Head `0f473e18` (2026-01-11), untouched 7.5 months, unsquashed `fixup!`
  commits. Its PR to main ([#40](https://github.com/moergo-sc/zmk/pull/40))
  has 0 comments, 0 reviews, `mergeable: false`.
- It *is* functionally complete (Go60 → HWMv2 at `app/boards/moergo/go60/`,
  in-tree Cirque wired up, CI green on 2026-01-11) — but it predates every
  4.1 bugfix since.
- **Killer signal:** MoErgo kept patching the 3.5 line for seven months
  after pushing it — most recently `5b43b3f8` (2026-08-21) fixing the Go60
  physical layout key order, *the same edit we made by hand in
  `config/go60-layouts.dtsi`*. That fix is on `main`, NOT on `zephyr-4-1`.
  Migrating today would regress it.
- Latest MoErgo release: `v25.11` (2025-11-26), Zephyr 3.5.

**Open regressions that hit us specifically:**
1. 🔴 [#3207](https://github.com/zmkfirmware/zmk/issues/3207) deep-sleep
   hang + full battery drain in ~24h on split centrals (Lily58/nice!nano
   v2/nice!view — near-identical to our Sofle). Fixed in ZMK's Zephyr fork
   2026-06-24, confirmed closed 2026-08-23. **MoErgo's branch has the bug.**
2. 🟠 [#3195](https://github.com/zmkfirmware/zmk/issues/3195) Studio +
   sleep on the central won't wake — exactly our
   `eyelash_sofle_studio_left` target. Speculative fix unverified for 2
   months. In the v0.4.0 milestone.
3. 🟠 [#3341](https://github.com/zmkfirmware/zmk/issues/3341) `&mkp` held
   button released by trackpad events — drag-select breaks mid-drag with a
   Cirque on a split central. Our Mouse layer is `&mkp LCLK` on RH T1.
   Open since 2026-05, zero comments. Reporter: v0.3 works, main doesn't.
4. 🟡 [#3282](https://github.com/zmkfirmware/zmk/issues/3282) multiple
   `zip_temp_layer` instances — still unfixed on main (we already work
   around it).
5. 🟡 [#3212](https://github.com/zmkfirmware/zmk/issues/3212) input-thread
   stack overrun with processor chains — set
   `CONFIG_INPUT_THREAD_STACK_SIZE=2048` prophylactically whenever we go.

## What we'd actually gain (and not)

Real: the in-tree Cirque driver (abs mode, `idle-packets-count`,
SW-reset-on-init — plausibly the proper fix for our baseline-drift
jitter); USB spurious-disconnect fix (#3070); nice!view VCOM inversion
(#3294) and LVGL-memory render fix (#3243); split TX-buffer sizing
(#3216); `d_scroll_x` uninitialized (#3196).

**NOT gained** (common assumptions, all checked): battery life (zero
claims; `battery.c` unchanged since v0.3.0), BLE stability/reconnect (LE
Subrating and Power Control are unavailable on the open-source split link
layer ZMK uses), USB stack (`USB_DEVICE_STACK_NEXT` still experimental —
deferred to Zephyr 4.3), **input processors (bindings byte-identical
v0.3.0↔main — the whole `zip_*` family is unchanged)**, home-row
mods/hold-tap. LVGL 9.3 on a 1-bpp panel is neutral-to-worse.

**Free:** the entire keymap ports verbatim — all 73 `&`-references in
`shared.dtsi`, `positions_*.dtsi` and both keymaps resolve identically on
main. keymap-drawer needs no changes (it never builds ZMK).

## What it would cost

| Work item | Effort | Blocking |
|---|---|---|
| HWMv2 conversion of `boards/arm/eyelash_sofle/` (Zephyr's converter script explicitly does not handle splits) | ~2h — **already done and green on `origin/copilot/upgrade-zmk-to-0-4`** | yes |
| `&bootloader` boot retention (`#include <common/nordic/nrf52840_uf2_boot_mode.dtsi>`) — **the existing PR branch omits this and silently breaks the behavior** | 15 min | yes |
| DC/DC → devicetree (`&reg0`/`&reg1`) — **also omitted on that branch; silent battery regression that builds clean** | 15 min | yes |
| `CONFIG_WS2812_STRIP` removed | 5 min | yes |
| `infely/nice-view-battery` is dead (last commit 2024-10, open LVGL-9 issue since 2025-12) — drop for stock `nice_view`, or fork `geratrevino115/nice-view-battery` | 30 min – half day | yes |
| Rebase our `raw-touch` ZMK fork onto 4.1; re-port 0xFF guard, ERA Z-min, activity-tied sleep; redo `stream-tap-*` | **days — the real cost** | yes |
| CI `build-user-config.yml@v0.3` → `@main` in the same commit as the west bump | 5 min | yes |
| Verify `storage_partition` offset/size unchanged across the board rewrite or lose BLE bonds | 10 min | yes |

## Migration triggers (watch these)

1. **Primary/hard:** MoErgo merges PR #40 or tags a release on a 4.1 base.
2. **Secondary:** [zmkfirmware/zmk#3024 "chore(main): release 0.4.0"](https://github.com/zmkfirmware/zmk/pull/3024)
   merges (that PR's merge *is* the v0.4.0 cut; body regenerated
   2026-08-21). Note v0.3.0 has been "Latest" for ~13 months.
3. **Tertiary:** #3195 closed as verified fixed.

## Do NOW (makes the eventual migration cheap)

- [ ] **Driver-independent refactor of the touch-stream module** (already
  in upstreaming-todo) — converts the migration from "rebase a driver
  fork" into "a west/DTS change". Highest leverage by far.
- [ ] **Pin `config/west.yml` to a SHA** (`57a7b8e0`) — `go60-zmk0.3.0` is
  a *branch*, not a tag: a moving target under a daily driver.
- [ ] **Cherry-pick [zmk#3483 "declare scroll resolution in the report
  descriptor"](https://github.com/zmkfirmware/zmk/pull/3483) onto our
  fork.** Touches only `hid.h`, `pointing/Kconfig`, docs — zero Zephyr
  coupling, applies to a 3.5 base. Its problem statement is verbatim our
  scroll-divisor saga ("slow scrolling or fine scrolling, but not both").
  **May let us retire the LinearMouse `inputScale` patch entirely.**
- [ ] Fix and keep warm the `origin/copilot/upgrade-zmk-to-0-4` branch
  (add boot retention + DC/DC) as a free regression canary; the Sofle half
  of the migration is then pre-validated.
- [ ] Decide the `nice-view-battery` question while it's cheap.
- [ ] Unrelated free win: keymap-drawer v0.23.0 ships a built-in Go60
  layout (`resources/extra_layouts/go60.json`) — likely lets us delete
  `config/go60-layouts.dtsi` and the `draw_args` workaround.

## Separable sub-decision

The Sofle *could* migrate alone today (the PR branch proves all three
targets build green). Recommendation: still no — it gains a VCOM fix and a
USB fix, and costs the nice-view-battery widget, an unverified Studio+sleep
regression on the half that uses Studio, and a second firmware line to
maintain.
