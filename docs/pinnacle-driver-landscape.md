# The "new Pinnacle driver on main" — research report (2026-08-26)

Opus research agent findings on the ZMK-Discord chatter, and what it means
for the raw-touch stack. Supersedes the cirque-module upstreaming plan in
docs/upstreaming-todo.md (see that file's updated section).

## What it is

Not a new ZMK driver: **upstream Zephyr's in-tree `input_pinnacle` driver**,
arriving in ZMK `main` as a side effect of the Zephyr 3.5 → 4.1 upgrade. ZMK
now tells users to drop `petejohanson/cirque-input-module` and use in-tree.

| Fact | Detail |
|---|---|
| Driver | `drivers/input/input_pinnacle.c` in zephyrproject-rtos/zephyr |
| Origin | Ilia Kharin (akscram), zephyr#69438, merged 2024-02-25 |
| Reached ZMK | ZMK main west.yml pins `zephyr @ v4.1.0+zmk-fixes` |
| Announcement | ZMK blog "Zephyr 4.1 Update", 2025-12-09 |
| Release status | **main only** — latest ZMK tag v0.3.0; v0.4 not cut yet |
| Recent commits | SW reset on init (`fa7037ca`, 2025-10-28, by Peter Johanson — **on Zephyr main, i.e. AFTER the v4.1.0 tag ZMK pins**), invert x/y rel, `sleep-mode-enable` |

**petejohanson/cirque-input-module is effectively EOL**: last commit
0de55f36 (2025-02-18 — the exact SHA our fork branches from), 7 PRs
untouched for 7–18 months, and Pete now lands fixes in Zephyr upstream.
Abs-mode PRs already rotting there unmerged: halfdane #5 (since 2025-02),
geeksville #6/#7.

## Feature comparison (same `cirque,pinnacle` compat — collides with the module, migration is swap not side-by-side)

| Capability | Zephyr in-tree (ZMK main) | pete's module | Our raw-touch fork |
|---|---|---|---|
| Absolute mode | ✅ `data-mode = "absolute"` | ❌ | ✅ `abs-mode` |
| Z reporting | ✅ INPUT_ABS_Z | ❌ | ✅ |
| Z-idle packets | ✅ DT prop `idle-packets-count` | hardcoded 5 | hardcoded 3 |
| Clip/scale/active-range | ✅ full DT props | ❌ | ❌ |
| Invert in abs mode | ✅ software invert-x/y | rel only | rel only |
| Swap XY | ✅ `swap-xy` (rel) | `rotate-90` | `rotate-90` |
| Sensitivity | ✅ 1x–4x (default 4x!) | default 1x | default 1x (we use 2x) |
| Primary tap | opt-in `primary-tap-enable` | opt-out `no-taps` | opt-out |
| Secondary-tap control | ❌ none | ✅ | ✅ |
| Taps in abs mode | ❌ | n/a | ✅ our stream-tap-* |
| 0xFF STATUS1 glitch guard | ❌ | ✅ | ✅ |
| ERA Z-min tuning | ❌ | ✅ | ✅ |
| SW reset on init | ⚠️ Zephyr main only, NOT in v4.1.0 | ❌ | ✅ (ours, from Cirque sample code) |
| ZMK-activity idle sleeper | ❌ | ✅ | ✅ |

**Headline: absolute X/Y/Z — the thing we forked to add — has been upstream
since Feb 2024, more configurable than ours.**

## What it means for our stack

- **No path delivers it to us today**: moergo-sc/zmk (go60-zmk0.3.0 AND
  main) pins zephyr v3.5.0+zmk-fixes.
- **MoErgo already migrated on a side branch**: `moergo-sc/zmk@zephyr-4-1`,
  commit 1e61f57b "use in-tree cirque driver" (2026-01-11) — deletes the
  module, rewrites Go60 DTS (`dr-gpios`→`data-ready-gpios`,
  `rotate-90`→`swap-xy`, `y-invert`→`invert-y`,
  `no-secondary-tap`→`primary-tap-enable` — the last is a semantic change,
  upstream has no secondary-tap knob). Unreleased/unblessed.
- **ZMK main's input listener still never converts ABS→mouse motion** — a
  consumer module (ours) remains mandatory for abs-mode cursor. The new
  driver does not obsolete our touch-stream module, only our driver patch.

### Revised plan (supersedes the old "PR abs-mode to pete's module")

1. **Cancel the abs-mode upstream PR entirely** — redundant; it exists
   upstream, and pete's module is where abs-mode PRs go to die.
2. **Refactor our ZMK side to be driver-independent**: move `stream-tap-*`
   props off the trackpad node onto our own module node; consume standard
   `INPUT_ABS_X/Y/Z`. (This was already a punch-list item for other
   reasons — now it's also the migration enabler.) After that, the driver
   swap is a west/DTS change.
3. **Rebase onto moergo-sc/zmk@zephyr-4-1 when MoErgo blesses it.** Gains:
   `idle-packets-count` DT prop (drop our hardcoded Z_IDLE=3), hw
   clipping/scaling, one less west project.
   ⚠️ **Corrected 2026-08-26:** this previously credited the in-tree
   driver with SW-reset-on-init as a likely fix for our baseline-drift
   jitter. **Backwards** — the *fork* does the software reset; Zephyr
   v4.1.0 does not. (A reset commit `fa7037ca` did land on Zephyr **main**
   2025-10-28, i.e. after the v4.1.0 tag ZMK pins — so vendoring from
   Zephyr main rather than the tag may supply it for free; verify.)
   Must port/re-add to any in-tree base: SW reset, `force_recalibrate()`
   (our documented jitter escalation path), ERA Z-min edge tuning, the
   0xFF STATUS1 glitch guard, secondary-tap control if wanted, and an
   activity-tied sleeper (the in-tree driver has **no PM at all** — minor
   sleep-current risk).
4. **If upstreaming driver work, target Zephyr, not the module**: 0xFF
   guard, ERA Z-min, secondary-tap control are small well-scoped PRs and
   Pete reviews that tree now. Bonus goodwill: ZMK's pointing.mdx docs on
   main still show the old module props — trivial doc PR.

## Azoteq IQS5xx (TPS43/TPS65) bonus

**Nothing upstream, no path forming** — Zephyr has zero iqs5xx code (two
driver PRs closed unmerged 2022/2023); ZMK PR #3201 (2026-01) was closed
same-day. Community modules are the only game: de-facto standard
`AYM1607/zmk-driver-azoteq-iqs5xx` (73★, active through 2025-12); newest
activity `finestedm/zmk-driver-azoteq-iqs5xx` (2026-08, explicitly
TPS43+TPS65, I2C-retry robustness). For the multi-touch roadmap: start from
AYM1607 for reach, read finestedm for the robustness fixes; expect
out-of-tree to persist.

## Confidence

High on everything merged (read from source trees/commits/blog); the one
untested inference is that SW-reset-on-init helps our specific drift jitter.
ZMK's pointing docs on main contradict the blog's migration table (docs show
old module props) — trust the Zephyr binding pages during migration.


## Update 2026-08-26: the in-tree driver runs on Zephyr 3.5 today

Vendoring experiment: upstream `input_pinnacle.c` compiles **verbatim**
against Zephyr 3.5 (no PM code to port; input/DT/SPI/GPIO APIs identical;
event stream byte-identical), green CI on all 5 targets —
https://github.com/kalakris/zmk-config/actions/runs/32945803339
Test branches `intree-driver-test` in zmk-config, kalakris/zmk and
kalakris/cirque-input-module; nothing merged.

So the EOL fork can be dropped **now**, without migrating to Zephyr 4.1 —
which removes the "can't continue this work until we migrate" blocker.
Two-step (~2h + bench) because three Cirque-sample-code features are absent
in-tree (`force_recalibrate()`, ERA edge tuning, 0xFF STATUS1 guard). SW
reset is already upstream (`fa7037ca`, merged 2026-01-18) — vendor from
Zephyr **main** rather than the v4.1.0 tag and it comes free. Plan and behavioural deltas: docs/zephyr-41-migration.md.
