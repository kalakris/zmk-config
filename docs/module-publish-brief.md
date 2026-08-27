# Brief: republish the raw touch stream as a standalone ZMK module

Status: **planned, not started** (2026-08-26). Prerequisite reading:
[publish-strategy.md](publish-strategy.md) (why a module, why now, what
else to publish first), [raw-touch.md](raw-touch.md) (what exists today),
[prior-art-survey.md](prior-art-survey.md) (claims to avoid making).

## Why

The system works and is in daily use, but it is **unpublishable in its
current shape**: the firmware lives in `kalakris/zmk@raw-touch`, a fork of
*MoErgo's fork* of ZMK. Anyone with a different keyboard would have to
adopt MoErgo's ZMK to try it — at any Zephyr version. That is the
archetypal "drive-by fork dump" and it would define the first impression.

ZMK core has no extension point for custom HID reports (single static
`zmk_hid_report_desc`, one `usb_hid_register_device()` call, hand-counted
GATT attribute indices), and maintainers have declined to add vendor
payloads there — [zmk#962](https://github.com/zmkfirmware/zmk/pull/962)
(Plover HID) has been open **five years**. But two projects prove the
out-of-tree route works by shipping a **parallel copy of the transport
layer** inside a module:

- [`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) — 46★,
  283 lines total (`usb_hid.c` 98, `hog.c` 178, `events.c`), USB + BLE
- [`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io) — 15★,
  same approach on usage page 0xFF0C

Cost: ~280–350 lines of vendored transport, versus our current 219-line
patch to ZMK core. **`touch_stream.c` and the marker input processor port
unchanged.** The result needs no ZMK fork at all.

## Target shape

One repo — working name TBD (**must not contain "TouchStream"**; see the
rename note below). Contents:

```
<module>/
  zephyr/module.yml            # makes it a Zephyr/ZMK module
  CMakeLists.txt, Kconfig
  dts/bindings/                # the scroll-context marker processor
  src/
    touch_stream.c             # ported unchanged from our ZMK fork
    input_processor_touch_stream_scroll.c   # ported unchanged
    hid.c / usb_hid.c / hog.c  # vendored parallel transport (the new work)
  boards/…/example.overlay     # a copy-pasteable devicetree example
  host/                        # prebuilt macOS app + link to the fork
  README.md                    # video first, protocol as an appendix
```

## It works with the MoErgo fork — and lets us drop `kalakris/zmk`

Verified 2026-08-26. Two facts:

1. **West resolves duplicate project names in favour of the top-level
   manifest** — *"If the same name occurs in multiple manifests, the first
   definition is used"*, and the top-level file is processed before project
   imports. So `config/west.yml` can override anything MoErgo's ZMK pulls.
2. **MoErgo's `app/west.yml` @ `go60-zmk0.3.0` pins the driver by name**:
   `cirque-input-module`, remote `petejohanson`, revision `0de55f36`.

So the end state needs **no ZMK fork at all**:

```yaml
# config/west.yml
projects:
  - name: zmk
    remote: moergo-sc          # stock MoErgo, unforked
    revision: go60-zmk0.3.0
    import: app/west.yml
  - name: cirque-input-module  # top-level wins over MoErgo's pin
    remote: kalakris           # our fork, or the vendored in-tree driver
    revision: <sha>
  - name: <touch-module>       # the new module, added alongside
    remote: kalakris
    revision: <sha>
```

**Payoff beyond publishability:** `kalakris/zmk` disappears. No fork-of-a-
fork to rebase, and MoErgo's upstream fixes flow to us for free (e.g. the
Go60 physical-layout key-order fix `5b43b3f8`, 2026-08-21, which we had
patched by hand in `config/go60-layouts.dtsi`).

Caveats to prove in stage 1:
- The vendored transport must be compatible with **MoErgo's** ZMK at Zephyr
  3.5, not just upstream ZMK. MoErgo's deltas are mostly Go60 board
  support, so this is expected to be fine — but it is the thing to check.
- One driver override remains either way (abs-mode isn't in petejohanson's
  module) — use the vendored in-tree driver, per the ~2h plan in
  zephyr-41-migration.md.
- `CONFIG_USB_HID_DEVICE_COUNT=2` and the second BLE HIDS instance must
  work on MoErgo's build.

## Work items

1. **Vendor the transport.** Start from `zzeneg/zmk-raw-hid`'s structure.
   USB: `device_get_binding("HID_1")` + `usb_hid_register_device()` with
   `CONFIG_USB_HID_DEVICE_COUNT=2`. BLE: a second
   `BT_GATT_SERVICE_DEFINE(…, BT_UUID_HIDS, …)` instance. Carry over our
   BLE **feature-report characteristic** (~30 lines) — neither reference
   module implements one, and the self-describing feature report is the
   part of our design most worth keeping.
2. **Port `touch_stream.c` + the marker processor** — expected to be
   near-verbatim; they consume standard `INPUT_ABS_X/Y/Z` and emit through
   the transport we now own.
3. **Move `stream-tap-*` off the Cirque driver binding** onto our own node
   (already a punch-list item — it is also what makes the module
   driver-agnostic).
4. **Delete the ZMK core patch** from `kalakris/zmk@raw-touch` once the
   module reaches parity. The fork stops being load-bearing.
5. **Host side**: delete the deprecated host tap-to-click path (~560 lines
   — both paths enabled = double-click), and drop the hardcoded ZMK
   VID/PID from `TouchStreamManager`'s matching dictionary so the feature
   is *keyboards-with-touchpads*, not *Go60*.
6. **README + video.** Lead with a 30-second clip of the gesture working.
   Protocol is an appendix. Never present this as "a vendor HID protocol
   specification" — in this community that reads as XAP.

## Two bench checks before committing to the shape

1. **A second BLE HIDS instance.** HOGP permits it; real-host support is
   the one genuine unknown (our own prior-art doc rates it MEDIUM
   confidence). **Test on macOS over BLE before rewriting anything.**
2. **A second USB HID interface** is likely a *win* on macOS (separate
   `IOHIDInterface` nubs — see prior-art survey §3) and leaves room for the
   optional single-slot Linux digitizer collection later.

## Target version: build against what we can test

Build against **ZMK v0.3.0 / Zephyr 3.5** — what our hardware actually
runs. Note in the README that the 4.1 port is confined to the vendored
transport files and invite PRs. Rationale: the official ZMK config template
tracks `main` (Zephyr 4.1), so 3.5 serves a shrinking audience — but
**never publish what you cannot run**; targeting 4.1 for reach would mean
shipping firmware we cannot reproduce bugs in. Marking the delta turns our
blocker into a contribution opportunity.

## Blockers before anything goes public

- [ ] **Rename.** "touch stream" collides with **FingerWorks TouchStream**
  — the 1998–2005 multitouch split keyboard that streamed finger contacts
  to a host, acquired by Apple to build the iPhone. r/ErgoMechKeyboards
  knows this history. Survey suggests **PadWire** (clean across npm/PyPI/
  crates/GitHub) or **ZipTouch** (ZMK-flavoured), with "touch frame" as the
  unit noun. One-shot first impression.
- [ ] **Protocol v3 or an explicit "provisional" label.** Breaking the wire
  format under early adopters costs more than a two-week delay. The v3
  items: per-frame device timestamp (HID Scan Time, 100 µs units),
  contact-state bits, geometry in the report descriptor, device-side mode
  gate, serial-prefix matching. See upstreaming-todo.md.
- [ ] **LICENSE** on whatever repo ships (MIT; state the descriptor and
  report layout are unencumbered).
- [ ] Decide **0xFF00/0x01 vs QMK's 0xFF60/0x61** deliberately. ZMK's
  vendor-HID ecosystem has converged on QMK's page; our kext scan showed
  0xFF00/0x01 is free on macOS while Apple's `MTUserDevice` squats
  0xFF60/0x07. Not a slam dunk — decide, don't drift.

## Recommended order (from publish-strategy.md)

Small, generic patches first — they cost little, and each merge makes you a
known contributor rather than a stranger dropping a fork:

1. Two Cirque patches to **Zephyr** (0xFF/SW_DR guard; ERA edge
   sensitivity) — but **message Peter Johanson first**: all three patches
   are his, and he paused mid-migration of his module into Zephyr's driver.
2. `inputScale` to **LinearMouse**, reframed as generalizing
   `LogitechHighResolutionWheelNormalizer` rather than adding a knob.
3. **Then** this module.

---

## Prompt for a fresh session (front-loaded / autonomous)

Stored verbatim below; the user pastes this into a new session. It is
deliberately structured so everything that does **not** need hardware runs
without confirmation, and all hardware-dependent verification is queued for
when the user is at the desk.

```text
Front-load this: I'm away from my desk for about 2 hours. Do as much as
possible WITHOUT stopping for my confirmation. Anything needing hardware
(flashing, BLE behaviour, feel) gets queued for my return, not blocked on.

GOAL: republish my ZMK raw-touch-stream firmware as a standalone
out-of-tree ZMK module, so it no longer needs a fork of MoErgo's fork of
ZMK. Second goal: end state builds from STOCK moergo-sc/zmk plus modules,
deleting the kalakris/zmk fork entirely (west gives the top-level manifest
precedence, so config/west.yml can override MoErgo's cirque-input-module
pin and add the new module).

READ FIRST (all prior research and decisions live here — do not redo them):
- docs/module-publish-brief.md   (the plan, work items, blockers)
- docs/publish-strategy.md       (why a module; reference implementations)
- docs/raw-touch.md              (what exists today; tuning; ops; gotchas)
- docs/zephyr-41-migration.md    (the vendored in-tree Cirque driver plan)
- docs/raw-touch-protocol.md on the raw-touch branch (wire format)

WHERE THINGS ARE
- ~/src/zmk (kalakris/zmk@raw-touch, fork of moergo-sc/zmk@go60-zmk0.3.0,
  Zephyr 3.5). Pieces to move: app/src/pointing/touch_stream.c, the marker
  input processor, and vendor HID plumbing in app/include/zmk/hid.h,
  app/src/hid.c, usb_hid.c, hog.c, endpoints.c.
- ~/src/cirque-input-module (kalakris fork; also has an intree-driver-test
  branch from a proven vendoring experiment — reuse it).
- ~/src/linearmouse (kalakris/linearmouse@go60-inputscale) — host side.
- ~/zmk-config — read CLAUDE.md for build/flash workflows. IMPORTANT: read
  the TCC rule before running any xcodebuild test.

USE SUBAGENTS AGGRESSIVELY, in parallel where the work is independent:
- Opus subagents for research/analysis (e.g. the BLE question below,
  studying zzeneg/zmk-raw-hid's structure).
- Default-model subagents for mechanical implementation and porting.
Don't serialise work that could run concurrently.

DO THESE, IN THIS ORDER, WITHOUT ASKING ME:

1. De-risk the BLE question by RESEARCH, not hardware (Opus subagent):
   do two BLE HIDS instances actually work on macOS? zzeneg/zmk-raw-hid and
   badjeff/zmk-hid-io both ship BLE — dig through their issues, READMEs and
   any user reports for macOS-specific evidence. Report confidence. This is
   the one architectural unknown; if research says it's likely broken on
   macOS, prioritise the USB path and structure the module so BLE is
   additive rather than assumed.
2. Study zzeneg/zmk-raw-hid properly (46 stars, ~283 lines of vendored
   transport: usb_hid.c, hog.c, events.c) before writing anything.
3. Create the module repo under my account (gh repo create, private is
   fine for now) and scaffold it: zephyr/module.yml, CMakeLists.txt,
   Kconfig, dts/bindings, src/. Name it something NEUTRAL and temporary —
   the real name is still undecided and must NOT contain "touchstream"
   (FingerWorks collision, see prior-art-survey.md §5).
4. Vendor the transport (USB + BLE), including our BLE feature-report
   characteristic — neither reference module has one, and the
   self-describing feature report is the part of the design most worth
   keeping.
5. Port touch_stream.c and the marker input processor. Move stream-tap-*
   off the Cirque driver binding onto our own node so the module is
   driver-agnostic.
6. Also fold in the vendored in-tree Cirque driver (docs/zephyr-41-
   migration.md — proven to compile on Zephyr 3.5, green CI, branches
   already exist). Re-apply the three missing patches on top of pristine
   upstream as separate labelled commits: 0xFF/SW_DR guard,
   ERA edge sensitivity, force_recalibrate. NOTE: SW reset is already
   upstream — vendor from Zephyr main, not the v4.1.0 tag.
7. Rewire ~/zmk-config's west.yml on a NEW branch to: stock
   moergo-sc/zmk + the driver override + the new module. Get the
   "Build and Draw" CI green for all 5 targets. Iterate until green.
8. Write the README (protocol as an appendix, not the headline), an
   example devicetree overlay, and a LICENSE (MIT).
9. Download the built firmware so it's ready to flash the moment I'm back.

THEN STOP and give me:
- A summary of what's done and what the CI proved.
- A written, ordered VERIFICATION CHECKLIST for when I'm at my desk:
  exactly what to flash, what to test (scroll, momentum, catch,
  tap-to-click, dual-mode wheel fallback, pointer speed, BLE session,
  Sofle-with-both-connected), and what each result would mean.
- Any decisions you had to make, and anything you'd have asked me about.

CONSTRAINTS
- Work on branches. Do NOT merge to main or raw-touch. Do NOT flash.
- Do NOT publish anything or make any repo public — the rename and the
  protocol-v3 decision are still open (blockers section of the brief).
- If something is genuinely blocked, note it and move on to the next item
  rather than waiting for me.
```
