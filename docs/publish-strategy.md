# Publish strategy — what to ship where, in what order (2026-08-26)

Landability research. Blunt version: **ZMK core is effectively closed to
outside code; Zephyr and LinearMouse are open and fast; and the core ZMK
work we thought was fork-forever is actually module-able.**

## The numbers that drive everything

ZMK, last 2 months: 42 merged PRs, **31 of them dependabot**; of 11 human
PRs, 9 were docs. Since Feb 2026: ~50 human merges, of which **6–8 are
outside-contributor core changes** — about one a month. 96 open PRs, **63%
older than 6 months**. Stale bot marks at 10 months, closes 14 days later.
Reference case: [zmk#3345](https://github.com/zmkfirmware/zmk/pull/3345),
a core HID PR that closes a filed issue and was hardware-tested — **zero
comments in 3.5 months**. Maintainer nmunnich, in writing: *"account for
Pete's very limited review capacity."*

Zephyr and LinearMouse are the opposite. Push there first.

## Ranked publish order

| # | Item | Target | Odds | Effort | Notes |
|---|---|---|---|---|---|
| 1 | Cirque `STATUS1==0xFF` / SW_DR guard | **Zephyr** | ~75% | ~1h | Frame as **bugfix** to dodge the ~2026-09-28 v4.5 feature freeze. Don't add a bus round-trip per sample — check 0xFF on already-fetched bytes or fold STATUS1 into the existing burst read |
| 2 | ERA edge sensitivity (`x/y-axis-z-min`) | **Zephyr** | **~85%** | ~2h | Structurally identical to merged [#100717](https://github.com/zephyrproject-rtos/zephyr/pull/100717). Pre-empt Fabio Baltieri's known objection to undocumented registers: name the Cirque app note, the addresses, the revisions tested. Submit before ~Sep 14 for v4.5 |
| 3 | `scrolling.smoothed.inputScale` | **LinearMouse** | ~25% as-is, **~55–65% reframed** | ~2h | See below |
| 4 | **Touch stream as an out-of-tree ZMK module** | GitHub | n/a — this is the real publish | 1–2d + bench | See B1 verdict |
| 5 | Zero-mouse-report suppression | ZMK | ~20–30%/yr | ~2h | Add a test (`app/tests/pointing/` exists) — doubles the odds. Nobody has asked for it, which is the problem |
| 6 | `pinnacle_force_recalibrate()` | Zephyr | ~55–65% | — | Ship as `include/zephyr/input/input_pinnacle.h`, **not** a DT property (precedent: `paw32xx_force_awake()`). Feature → freeze applies → realistically v4.6 (~Feb 2027). **But see the reframing below — it may qualify as a bugfix and beat the freeze.** |
| 7 | Protocol spec | README of the module repo | — | — | **Not** a standalone versioned spec repo. See below |
| 8 | Input-listener routing refactor | ZMK | **~5%** | — | Don't submit standalone. That file has 4 commits ever, 3 by petejohanson. Outside refactors with no user-visible change stall |
| 9 | Touch-stream pipeline (host) | LinearMouse | ~5% as one PR | — | 4,207 insertions / 26 files. Ship as a fork with a downloadable build; revisit in year two |

## The B1 verdict: module-able, not fork-forever

**No extension point exists in ZMK core** (single static `zmk_hid_report_desc`,
one `usb_hid_register_device()` call, hand-counted GATT attribute indices,
two-case `zmk_endpoint_send_report` switch) — and maintainers have ruled on
putting vendor payloads there. [zmk#962](https://github.com/zmkfirmware/zmk/pull/962)
(Plover HID, vendor page 0xFF50) has been **open five years**. nmunnich,
2026-02-01:

> - Unlikely to be suitable for main…
> - **Would be fantastic as a module.**
> - **Additional work on refactoring the endpoints code to allow this to be
>   a module would be great. That sort of work definitely belongs in main.**

That third bullet is a maintainer *inviting* the extension point — nobody
has started it. Large, unscoped, high-risk core refactor; not a first
contribution, but a real long-game option.

**Meanwhile the module route works today, both transports**, and two
projects prove it by vendoring a parallel copy of the transport layer:

| | `badjeff/zmk-hid-io` | `zzeneg/zmk-raw-hid` |
|---|---|---|
| Usage page | 0xFF0C | 0xFF60/0x61 (QMK's) |
| Traction | 15★ | **46★, pushed 2026-08-24** |
| Ships | `usb_hid.c`, `hog.c`, `endpoints.c` | `usb_hid.c` (98 L), `hog.c` (178 L), `events.c` |
| USB | `device_get_binding("HID_1")` + `CONFIG_USB_HID_DEVICE_COUNT=2` | same |
| BLE | own `BT_GATT_SERVICE_DEFINE` — a second HIDS instance | same |
| ZMK fork needed | **no** | **no** |

`petercpark/zmk-hid-io-plover-hid` is how the 5-year Plover saga actually
resolved: a fork of badjeff's module.

**Cost: ~280–350 lines of vendored transport vs our current 219-line core
patch.** `touch_stream.c` and the marker processor port unchanged. Verdict:
**rewrite B1 as a module, delete the core patch, don't PR it.**

Two bench checks before committing:
1. **A second BLE HIDS instance** — HOGP permits it; real-host support is
   the one genuine unknown. Test on macOS over BLE first.
2. A second USB HID interface is likely a **win** on macOS (separate
   `IOHIDInterface` nubs — see prior-art survey §3) and leaves room for the
   optional Linux digitizer collection later.

Neither module implements a BLE feature-report characteristic — we already
wrote that, ~30 lines to carry over.

## Reframing inputScale for LinearMouse

lujjjh is fast (typical latency 1 day, merges outsiders freely) but
explicitly anti-knob: *"There used to be a `preserveNativeMiddleClick`
option, but I later removed it… I'd prefer not to bring it back for
simplicity."*

- **Strongest argument:** `LogitechHighResolutionWheelNormalizer` is
  *already* a pre-smoothing input normalizer — just vendor-gated. Frame as
  **generalizing an existing mechanism**, not adding a knob.
- **Pre-empt "how is this different from `smoothed.speed`?"** with a
  CGEventTap log, not reasoning. The one stalled PR in that repo
  ([#1344](https://github.com/linearmouse/linearmouse/pull/1344)) stalled
  for exactly that reason.
- **Ship schema + engine + tests first; hold the GUI slider for a
  follow-up** (that's what #1337 did — merged in a day).
- `Documentation/Configuration.json` is **generated** — regenerate, never
  hand-edit.

## On the spec: formal specs are anti-correlated with adoption here

Succeeded **without** a formal spec: Plover HID (a README, unversioned,
MIT — now in three firmwares, a commercial vendor, and mainline Plover),
QMK Raw HID (two C functions), VIA (no published spec at all).
Failed **with** one: XAP (versioned, 8 subsystems, 5 years, still draft;
both reference clients archived), HID-IO (spec'd 2015, one implementation
in 11 years).

Plover HID's author shipped host plugin + sample firmware in the initial
commit, **was told no by ZMK core, and the protocol won anyway.** Upstream
merge is the ratification step, not the strategy.

The formality that *does* pay is the one we already have: the
**self-describing feature report**. Make the descriptor the product and the
prose an appendix.

**Lead with a 30-second video of the gesture working — never with "a vendor
HID protocol specification."** In this community the latter reads as XAP.

## Distribution mechanics

There is **no official ZMK module registry** — `zmk.dev/docs/features/modules`
describes no publishing process. What exists: the
[`zmk-module` GitHub topic](https://github.com/topics/zmk-module) (94 repos)
and [`mctechnology17/awesome-zmk`](https://github.com/mctechnology17/awesome-zmk)
(PR yourself in). Ship one repo containing the ZMK module, the required
devicetree overlay, a prebuilt macOS binary, the video, and the protocol in
the README.

**Deliberate decision to make:** 0xFF00/0x01 vs QMK's 0xFF60/0x61. ZMK's
vendor-HID ecosystem has converged on QMK's page (zzeneg got 3× badjeff's
adoption partly by choosing it). Counter: our own kext scan showed
0xFF00/0x01 free on macOS while Apple's `MTUserDevice` squats 0xFF60/0x07,
and no existing host tool would consume touch frames anyway. Not a slam
dunk — decide, don't drift.

## Norms — hard requirements

- **Zephyr: DCO `Signed-off-by:` with legal name** (no pseudonyms; author
  email must match; enforced by required CI). No CLA. Our commits have no
  sign-off today — `git commit -s`.
- **Zephyr Gitlint**: `input: pinnacle: <subject>`, <72 chars, blank line,
  **non-empty body** (empty = hard fail), lines ≤75. Run
  `./scripts/ci/check_compliance.py` locally.
- **Zephyr four-eyes**: 2 approvals incl. the assignee, ≥2 business days,
  and driver changes need the *merger* from a different org. Stalled?
  Ping assignee at 1 week, **#pr-help** on Discord at 2 weeks.
- **ZMK**: conventional commits + release-please (`hid`, `usb`, `ble`,
  `pointing` are valid scopes). No CLA/DCO. PR template says discuss on
  Discord first — **do that before any core PR**.
- **LinearMouse**: MIT, SwiftLint/SwiftFormat, no CLA/DCO. Lowest friction.

## Things that would earn a bad reception

- **The name** — FingerWorks TouchStream collision (see prior-art survey §5).
- **`.github/workflows/build-unsigned.yml` is in the inputScale diff** —
  fork infrastructure in an upstream PR is the clearest "fork dump" tell we
  have. Drop it before PRing.
- **`Co-Authored-By: Claude …` trailers on every commit.** No project here
  has a stated policy, but Zephyr's DCO requires a real legal identity for
  sign-off and an AI co-author trailer beside it draws attention we don't
  want on a first patch. **Decide deliberately per-PR; don't let tooling
  decide.**
- Any claim of **abs-mode novelty** (see the correction in
  pinnacle-driver-landscape.md).
- **Bundling** the three Cirque patches — Zephyr's one-logical-change rule
  splits them anyway, and bundling makes the slowest gate the fastest.
- **Pointing anyone at `kalakris/zmk@raw-touch`** — a fork of MoErgo's fork
  on Zephyr 3.5. Archetypal drive-by fork dump.

## Correction: SW reset is already upstream

`fa7037ca` "input: pinnacle: Perform software reset on init" (Peter
Johanson, [PR #98452](https://github.com/zephyrproject-rtos/zephyr/pull/98452))
**merged 2026-01-18** and is in Zephyr main today. Our
`zephyr-41-migration.md` guessed this ("vendoring from Zephyr main may
supply it for free — verify") — **confirmed.** So:

- **Drop our local SW-reset patch**; rebase onto upstream's, which also
  clears STATUS1 first and adds `k_usleep(50)`. Resubmitting would be
  closed as duplicate.
- **The vendoring plan is now three patches, not four**: 0xFF guard, ERA
  Z-min, `force_recalibrate()`.

Latency signal for planning: that 17-line patch from a well-known
contributor took **82 days**. `input_pinnacle.c` has 8 commits ever, median
~38 days vs ~3 days for `drivers/input` overall. **Nobody with the hardware
is a listed maintainer** — Fabio Baltieri is sole Input maintainer; Peter
Johanson isn't listed. `akscram` (Ilia Kharin, original driver author) is
the de-facto reviewer whose approval unblocked both of Pete's PRs — **CC
him.**

## Higher-value alternative first ZMK contribution

[zmk#3341](https://github.com/zmkfirmware/zmk/issues/3341) — "`&mkp` held
button released by trackpad input events", open since 2026-05-02, **zero
comments**, same file as our refactor, and **we own the exact hardware**
(Cirque on a split central). It has a reporter, a repro, and demand — all
the things our unrequested zero-report fix lacks. Diagnosing it would be a
far better first ZMK contribution.


## Reframing patch 3 (`force_recalibrate`) as a bugfix

Our fork calls `pinnacle_force_recalibrate()` **exactly once**, at the end
of `pinnacle_init()`, immediately after `set_adc_tracking_sensitivity()`
and `tune_edge_sensitivity()`:

```c
pinnacle_set_adc_tracking_sensitivity(dev);   /* change ADC gain      */
pinnacle_tune_edge_sensitivity(dev);          /* change edge Z-min    */
pinnacle_force_recalibrate(dev);              /* re-measure baseline  */
```

That ordering is load-bearing. The Pinnacle's stored baseline ("what does
an untouched pad look like") is only meaningful relative to the gain it was
measured at, so changing ADC sensitivity or edge thresholds staleness the
baseline. Recalibrating afterwards is the **companion** to those writes,
not an independent feature.

Consequences:

1. **Independent of the touch-stream work.** The protocol needs absolute
   X/Y/Z frames and a reliable lift-off packet — nothing more.
2. **Patches 2 and 3 are coupled.** Carrying the ERA edge-sensitivity patch
   without the recalibrate means writing new thresholds against a baseline
   calibrated for the old ones. Take both or neither.
3. **Plausible cause of our original baseline-drift jitter.** Upstream's
   in-tree driver writes `sensitivity` at init (default 4x) and, per the
   landscape research, never recalibrates. Our jitter appeared at maximum
   sensitivity and disappeared when gain was reduced to 2x — exactly the
   signature of a stale baseline being amplified. **Testable**: if it
   holds, recalibrate-after-gain-change is the real fix and we could
   restore higher sensitivity (better light-touch response for tap-to-click
   and touch onset — see docs/raw-touch.md tuning notes).
4. **Upstreaming angle.** Framed as *"the driver writes ADC sensitivity at
   init but never recalibrates, leaving the baseline calibrated for the
   previous gain"*, this is a **bugfix**, not a feature — so it is not
   gated behind the ~2026-09-28 v4.5 feature freeze. Verify against
   upstream's init path before claiming it.
