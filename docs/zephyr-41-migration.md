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

Real: the in-tree Cirque driver (abs mode, `idle-packets-count`);
USB spurious-disconnect fix (#3070); nice!view VCOM inversion
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
| ~~Rebase our `raw-touch` ZMK fork onto 4.1; re-port driver patches~~ — **ELIMINATED by vendoring the in-tree driver onto our current 3.5 base** (proven green, see below). Post-vendor the driver cost is: delete the west entry, drop the `/delete-property/` blocks, re-home `stream-tap-*` (~30 min) | ~30 min | no |
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


## Update 2026-08-26: vendoring settles the "can't continue until we migrate" problem

**Upstream Zephyr's `input_pinnacle.c` compiles verbatim against Zephyr
3.5 — zero source shims.** Proven with green CI on all 5 targets:
https://github.com/kalakris/zmk-config/actions/runs/32945803339
(test branches `intree-driver-test` in zmk-config, kalakris/zmk, and
kalakris/cirque-input-module; nothing merged).

The expected blocker didn't exist: the in-tree driver has **no PM support
at all**, so there was nothing to port. `input_report_abs()` and every DT/
SPI/I2C/GPIO macro it uses are identical in 3.5. The event stream is
byte-identical too (X, Y, then Z with sync), so `touch_stream.c` needed
only property renames, not structural change.

The real work is at the devicetree layer: Zephyr 3.5's edtlib **errors**
on undeclared properties, so the Go60 board DTS's `dr-gpios`, `rotate-90`,
`y-invert`, `no-secondary-tap` (set in MoErgo's fork, which we can't edit)
must each be `/delete-property/`'d in our keymap overlay before the
in-tree names are set. Same trick already used for `cirque_split`.

### ⚠️ Correction to an earlier claim in this doc

This doc previously credited the in-tree driver with "SW-reset-on-init —
plausibly the proper fix for our baseline-drift jitter". **That was
wrong**, and it inverted the truth: it is the *fork* (from petejohanson's
base, i.e. Cirque's sample code) that does the software reset. Zephyr
v4.1.0's init path has none.

Nuance worth checking before vendoring: a SW-reset-on-init commit
(`fa7037ca`, 2025-10-28, by Peter Johanson) did land on Zephyr **main** —
after v4.1.0 was cut. So vendoring from Zephyr main rather than the v4.1.0
tag may supply it for free. Verify before hand-porting it.

### Four things the in-tree driver is missing vs our fork

All four come from petejohanson's base (Cirque's own sample code), not
from our additions, and all are self-contained:

1. **Software reset on init** (`SYS_CFG_RESET`) — see the nuance above.
2. **`pinnacle_force_recalibrate()`** — our documented escalation path for
   the baseline-drift jitter. No in-tree equivalent.
3. **Edge-sensitivity ERA writes** (`x-axis-z-min` / `y-axis-z-min`).
4. **`STATUS1 == 0xFF` / SW_DR guard** (petejohanson `70ff465`) — drops
   garbage frames from flaky SPI reads. ~10 lines.

Swapping without these would most likely reintroduce the jitter we fixed
with sensitivity 2x. So vendoring is a **two-step**, not a swap.

### Plan (≈2h + a bench session)

- [ ] Step 1: re-apply the four missing pieces as separate, clearly
  labelled commits *on top of* the pristine upstream file, so the delta
  stays legible and upstreamable to Zephyr later.
- [ ] Step 2 (bench, needs hardware): verify the two axis questions —
  in-tree applies `invert-x/y` in **absolute** mode and `swap-xy` only in
  **relative** mode (the fork does both only in relative), so
  `touch_stream.c` must stop mirroring Y while still mirroring the 90°
  rotation; and the LH pad loses hardware Y-inversion entirely (compensated
  with `&zip_xy_transform INPUT_TRANSFORM_Y_INVERT` in its listener chain).
  Also re-verify tap-to-click and lift-off.
- [ ] Then merge to `raw-touch`.

### Other deltas (no action needed today)

- In-tree reports relative-mode taps as `INPUT_BTN_TOUCH`, not `INPUT_BTN_0`
  — ZMK's button handling only understands `BTN_0..4`, so hardware taps
  would be dropped. Doesn't affect us (LH taps are muted; RH taps come from
  our firmware tap-to-click), but it forecloses hardware taps without a patch.
- No PM/sleep in-tree. ZMK tolerates `-ENOSYS` cleanly and we don't set
  `sleep` on either Go60 node; at most a minor sleep-current regression.
- `idle-packets-count = <3>` is **mandatory**, not cosmetic: the in-tree
  driver writes it to `Z_IDLE` and defaults it to **0**, which emits *no*
  lift-off packets — `touch_stream.c` would never see a release.
- `stream-tap-*` currently rides along by appending to the vendored binding
  yaml. That does **not** survive real Zephyr 4.1 (can't ship a second
  binding for the same compatible), so those props must move to a ZMK-side
  node or Kconfig — which is the driver-independent refactor already planned.

**This does not change the WAIT decision.** MoErgo's branch is still stale
and #3207/#3195/#3341 are still open. Vendoring is orthogonal: it makes the
eventual migration cheap without committing us to it.
