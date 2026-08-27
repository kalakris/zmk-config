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

## Prompt for a fresh session

> I want to republish my ZMK raw-touch-stream firmware as a standalone
> out-of-tree ZMK module, so it stops requiring a fork of MoErgo's fork of
> ZMK and can be used by anyone with a Cirque trackpad.
>
> Read these first, in order — they contain all the prior research and
> decisions, don't redo them:
> - `docs/module-publish-brief.md` (this plan)
> - `docs/publish-strategy.md` (why a module; the reference implementations)
> - `docs/raw-touch.md` (what exists today, tuning, ops)
> - `docs/raw-touch-protocol.md` **on the `raw-touch` branch** (wire format)
>
> Current firmware lives in `~/src/zmk` (`kalakris/zmk@raw-touch`, a fork of
> `moergo-sc/zmk@go60-zmk0.3.0`, Zephyr 3.5) with the pieces to move being
> `app/src/pointing/touch_stream.c`, the marker processor, and the vendor
> HID plumbing in `app/include/zmk/hid.h`, `app/src/hid.c`, `usb_hid.c`,
> `hog.c`, `endpoints.c`. Host side is `~/src/linearmouse`
> (`kalakris/linearmouse@go60-inputscale`). Config repo is `~/zmk-config`
> (build/flash workflows and gotchas are documented in `CLAUDE.md` — read
> the TCC rule before running any `xcodebuild test`).
>
> Model the module on `zzeneg/zmk-raw-hid` (46★, vendors ~283 lines of
> transport: `usb_hid.c`, `hog.c`, `events.c`) — study it before writing
> anything.
>
> Do this in stages, and stop for my confirmation between them:
> 1. **Bench check first**: prove a second BLE HIDS instance works on macOS
>    with this hardware. If it doesn't, stop and tell me — the whole shape
>    depends on it.
> 2. Scaffold the module repo and vendor the transport (USB + BLE, incl. our
>    BLE feature-report characteristic, which neither reference module has).
> 3. Port `touch_stream.c` and the marker processor; move `stream-tap-*`
>    onto our own node so the module is driver-agnostic.
> 4. Build green in CI against ZMK v0.3.0 / Zephyr 3.5, then flash and
>    verify on hardware: scroll, momentum, catch, tap-to-click, and the
>    dual-mode wheel fallback.
> 5. Only then: README with the video, example overlay, LICENSE.
>
> Do NOT publish anything yet — the rename and the protocol-v3 decision are
> still open (see the blockers section of the brief). Work on branches;
> don't merge to `main` or `raw-touch` without asking.
