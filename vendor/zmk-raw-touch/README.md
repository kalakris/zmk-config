# zmk-raw-touch

> **Status: work in progress. Not published, not supported.**
> Nothing here is stable yet. See [Open questions](#open-questions).

Trackpad scrolling on a ZMK keyboard that feels like a Magic Trackpad —
gesture phases, lift-off momentum, catch-to-stop — instead of notched
wheel clicks.

*(Demo clip goes here. Lead with the gesture, not the protocol.)*

ZMK's pointing stack turns a trackpad into a mouse: relative deltas, and
`REL_WHEEL` ticks for scrolling. That is the right abstraction for a mouse
and the wrong one for a finger. Everything that makes trackpad scrolling
feel good on a laptop — kinetic momentum, rubber-banding, putting a finger
down to stop a fling — needs the *contact itself*: where the finger is,
how hard it presses, and exactly when it lands and lifts.

This module streams that. The pad runs in absolute mode and each sample
goes to the host as a small vendor HID report — position, touch strength,
and a couple of flag bits — at the pad's native rate. A host-side consumer
turns the frames into real scroll gestures.

Ordinary pointing keeps working the whole time. The module also derives
relative deltas from successive frames and re-injects them into the pad's
normal input listener chain, so the cursor, your wheel-mapping overlays
and your button processors behave exactly as before. A host with no
consumer for the stream just sees a normal trackpad.

## What makes this different from a fork

ZMK core has no extension point for vendor HID reports: one static
`zmk_hid_report_desc`, one `usb_hid_register_device()` call, hand-counted
GATT attribute indices. The usual answer is to fork ZMK — and then nobody
with a different keyboard can try your work.

This module instead ships a **parallel copy of the transport layer**: its
own HID report descriptor, on its own USB HID interface (`HID_1`, next to
ZMK's `HID_0`) and its own HID-over-GATT service instance. ZMK core is
untouched. The same approach is used by
[`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) and
[`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io).

It is also **driver-agnostic**. Nothing in the module knows about any
particular touchpad ASIC. It consumes `INPUT_ABS_X` / `INPUT_ABS_Y` /
`INPUT_ABS_Z` from whatever input device you point it at, and takes its
geometry, orientation and tap-to-click settings from its own devicetree
node rather than from the driver's.

## Requirements

- A trackpad running in **absolute** mode, emitting `INPUT_ABS_X`,
  `INPUT_ABS_Y`, and `INPUT_ABS_Z` with the sync flag on the last of the
  three. (For Cirque Pinnacle pads: Zephyr's in-tree `input_pinnacle`
  driver, or a fork with absolute-mode support.)
- The pad must emit **lift-off packets**. Without an all-zero Z-idle frame
  at the end of a touch the module never sees a release, so there is no
  momentum and no tap. On the in-tree Pinnacle driver this means setting
  `idle-packets-count` to a non-zero value — it defaults to 0.
- ZMK with `CONFIG_ZMK_POINTING=y`.
- A host-side consumer, if you want gesture scrolling rather than just a
  stream of bytes.

## Setup

Add the module to `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: kalakris
      url-base: https://github.com/kalakris
  projects:
    - name: zmk-raw-touch
      remote: kalakris
      revision: main
```

Add a `zmk,raw-touch-pad` node to your keymap pointing at your pad, and
include the module's processor nodes:

```dts
#include <raw_touch/processors.dtsi>

/ {
    raw_touch_rh: raw_touch_rh {
        compatible = "zmk,raw-touch-pad";
        device = <&glidepoint>;
        pad-id = <0>;

        /* Must match how the pad is physically mounted. Frames stream
         * untransformed; these bits are published in the feature report
         * so the host can apply them, and are applied to the relative
         * deltas the module derives for the fallback pointer path. */
        rotate-90;
        y-invert;

        /* Absolute mode usually disables the pad's hardware tap-to-click.
         * This is the firmware replacement. */
        tap-click;
    };
};
```

Set in your `.conf`:

```conf
CONFIG_ZMK_RAW_TOUCH=y

# MANDATORY, and it fails at RUNTIME, not at build time, if you forget:
# ZMK core owns USB HID interface HID_0, this module claims HID_1.
CONFIG_USB_HID_DEVICE_COUNT=2
```

A complete worked example is in
[`boards/example.overlay`](boards/example.overlay).

### Scroll mode

Streamed frames carry a scroll-mode flag so the host knows to synthesize
scrolling rather than pointer motion. Mark the context by putting
`&zip_raw_touch_scroll` in the processor chain that should mean "scroll" —
typically a layer overlay on the pad's input listener:

```dts
&trackpad_listener {
    input-processors = <&zip_xy_scaler 1 1>,
                       <&zip_raw_touch_idle_filter>;

    nav_scroll {
        layers = <NAV>;
        input-processors = <&zip_raw_touch_scroll>,
                           /* wheel fallback for hosts with no consumer */
                           <&zip_xy_to_vscroll_mapper>,
                           <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                           <&zip_scroll_scaler 1 8>,
                           <&zip_raw_touch_idle_filter>;
    };
};
```

The marker is a pure pass-through. Because it only runs when the chain
containing it is the one actually handling the pad's events, layer
ordering and `process-next` shadowing are honoured for free — there is no
second copy of the listener's routing logic to keep in sync. Put it first
in the chain, before anything that might consume events.

If that does not suit your chain, `scroll-layers = <NAV>;` on the pad node
switches to plain layer-state evaluation instead.

### `&zip_raw_touch_idle_filter`

Put this **last** in every chain on a streaming pad. An input listener
sends a mouse HID report on every sync event, including syncs that
accumulated no motion, no scroll and no button change — host-invisible
no-ops that still cost a USB transfer or a BLE notification each. An
absolute pad produces one per frame (absolute events contribute nothing to
a mouse report), and a downscaling wheel overlay adds more by emitting
value-0 relative events. At ~100 Hz these compete with the raw stream for
BLE connection events. The filter drops them, while always letting button
transitions through.

## Configuration

| Kconfig | Default | |
|---|---|---|
| `CONFIG_ZMK_RAW_TOUCH` | y if a pad node exists | The module |
| `CONFIG_ZMK_RAW_TOUCH_USB` | y | Second USB HID interface |
| `CONFIG_ZMK_RAW_TOUCH_BLE` | y | Second BLE HIDS instance |
| `CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE` | 30 | Frames buffered for BLE |
| `CONFIG_ZMK_RAW_TOUCH_USAGE_PAGE` | 0xFF00 | Vendor usage page |
| `CONFIG_ZMK_RAW_TOUCH_USAGE` | 0x01 | Usage in that page |
| `CONFIG_ZMK_RAW_TOUCH_REPORT_ID` | 0x04 | Report ID |

Devicetree properties for `zmk,raw-touch-pad` are documented in
[`dts/bindings/input/zmk,raw-touch-pad.yaml`](dts/bindings/input/zmk,raw-touch-pad.yaml).

## Known issues and gotchas

- **You will probably need to re-pair over Bluetooth after first flashing
  this.** Adding a second HIDS service changes the GATT database, and a
  host with a cached database will not see it.
- **macOS mis-binds report maps on the very first pairing** of a device
  with two HID services — it can hand both services the same report map.
  Disconnect and reconnect once (or toggle Bluetooth) and it corrects
  itself. This is a macOS bug, reported by others, still open.
- **Android before ~9 may not tolerate two HID services at all.**
- `CONFIG_USB_HID_DEVICE_COUNT=2` is easy to forget and fails silently at
  runtime. The module raises a build assertion, but check it first if the
  stream simply is not there.

## Target version

Built and tested against **ZMK v0.3.0 / Zephyr 3.5**, which is what the
author's hardware runs. The Zephyr 4.1 port is confined to the vendored
transport (`src/usb_hid.c` uses the legacy USB device stack, which the new
`USB_DEVICE_STACK_NEXT` replaces; `src/hog.c` is unaffected). PRs welcome.

## Naming

`zmk-raw-touch` is the final name (decided 2026-08-27): descriptive, in the
same register as the modules this one's transport approach comes from
(`zmk-raw-hid`, `zmk-hid-io`), and collision-checked.

The obvious alternative vocabulary — "touch stream" — is deliberately
avoided everywhere in this project: **FingerWorks TouchStream** (1998-2005)
was a multitouch split ergonomic keyboard that streamed absolute finger
contacts to a host for gesture synthesis — this project's problem
statement, verbatim — and Apple acquired it to build the iPhone. There is
also an active patent-litigation entity of the same name.

The unit noun is **"touch frame"**: one sampled snapshot of a contact.
Microsoft, HID, and Apple all use "frame" for this, so the spec reads
familiarly to anyone who has implemented Precision Touchpad.

## Decisions (2026-08-27)

- **Usage page: `0xFF00`/`0x01`, decided.** Free on macOS (Apple claims
  eight usages on this page; 0x01 is not one of them), no cross-talk with
  QMK Raw HID tooling (which owns 0xFF60/0x61 and speaks a bidirectional
  command protocol this is not), and the squatting hazard is answered by
  the normative feature-report validation. Not drifted into: checked and
  chosen.
- **Protocol v3 before publication.** Per-frame device timestamp and
  sequence number (both motivated by measured BLE behaviour: connection
  intervals batch frames, and the BLE queue drops oldest silently),
  explicit contact-state with a contact-id field (so multi-touch pads are
  not a breaking change), real geometry in the report descriptor, per-pad
  geometry in the feature report. The device-side mode gate is
  **reserved, not implemented**: a stateful "host is consuming, stop the
  fallback" flag whose failure mode is no-scrolling-at-all (host crash,
  kill, profile switch) is strictly worse than host-side suppression,
  whose suppression dies with the consuming process. A flags bit and a
  capability bit are reserved so a leased implementation can be added
  compatibly if ever justified.

## License

MIT. See [LICENSE](LICENSE). The report descriptor and report layouts are
unencumbered.

---

## Appendix: wire format (protocol v3)

Vendor-defined usage page `0xFF00`, usage `0x01`, one top-level
application collection on its own HID interface / HIDS instance. Report ID
`0x04`. Over USB the report ID is the first payload byte; over BLE it
lives in the GATT report-reference descriptor and notifications carry the
body alone.

### Input report — 11 bytes

One report per pad sample while touched (~100 Hz on a Cirque Pinnacle),
plus one release report on lift-off.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `pad_id` |
| 1 | 1 | `contact_id` — 0 on single-touch pads; distinguishes fingers on multi-touch pads |
| 2 | 2 | `x`, little-endian, raw pad counts |
| 4 | 2 | `y`, little-endian, raw pad counts |
| 6 | 1 | `z`, touch strength |
| 7 | 1 | `flags` |
| 8 | 1 | `seq` — per-pad counter, +1 per emitted report, wraps |
| 9 | 2 | `timestamp`, little-endian, 100 µs units, wraps at 6.5536 s |

`flags`: bit 0 = touched, bit 1 = scroll mode, bit 2 = *reserved* for the
mode gate (always 0), bits 3–7 reserved (0).

The `touched` bit is normative for contact state — release is `touched`
clear, not "coordinates are zero" (release reports do also carry zeroed
x/y/z, but hosts MUST key on the bit). `timestamp` is the device-side
sample time (HID Scan Time convention); hosts MUST derive finger velocity
from it rather than from arrival time, which BLE connection-interval
batching distorts. `seq` exposes silent drops (the BLE send queue drops
oldest under pressure); with device timestamps, velocity across a gap
remains correct, so `seq` is diagnostic.

**Host requirement:** a release report can itself be dropped. Hosts MUST
treat frame silence longer than ~150 ms while `touched` was last set as a
lift-off.

### Feature report — 20 bytes, same report ID

Readable over USB `GET_REPORT` and the BLE feature report characteristic
(report-reference type `0x03`). **Hosts MUST read and validate this
report before treating a `0xFF00`/`0x01` collection as this protocol** —
that vendor pair is widely squatted.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `protocol_version` = 3 |
| 1 | 1 | `pads_present` bitmask, bit *N* = `pad_id` *N* |
| 2 | 1 | `capabilities` — bit 0 *reserved* for the mode gate (0); rest 0 |
| 3 | 1 | reserved (0) |
| 4 | 8 | pad slot 0 |
| 12 | 8 | pad slot 1 |

Slots describe present pads in ascending `pad_id` order. Each slot:

| Offset | Size | Field |
|---|---|---|
| +0 | 1 | `resolution`, counts/mm, 0 = unknown |
| +1 | 1 | `orientation` — bit 0 rotate-90, bit 1 x-invert, bit 2 y-invert |
| +2 | 2 | `x_max`, little-endian |
| +4 | 2 | `y_max`, little-endian |
| +6 | 1 | `max_contacts` (1 on a Pinnacle) |
| +7 | 1 | reserved (0) |

Coordinates stream raw and untransformed; `orientation` tells the host
what to apply. The report descriptor also declares the primary pad's real
logical ranges on the x/y fields, so generic HID tooling sees true
geometry without parsing the feature report.

### v2 (legacy)

v2 frames were 7 bytes (no `contact_id`, `seq`, `timestamp`) with an
8-byte single-geometry feature report, `protocol_version` = 2. Hosts MAY
accept both versions during migration, distinguishing by the feature
report; firmware ships v3 only.
