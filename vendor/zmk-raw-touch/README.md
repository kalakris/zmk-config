# zmk-raw-touch

> **Status: work in progress, not yet supported.** Interfaces may still
> change.

Trackpad scrolling on a ZMK keyboard that feels like a Magic Trackpad —
gesture phases, lift-off momentum, catch-to-stop — instead of notched
wheel clicks.

*(Demo clip goes here.)*

ZMK's pointing stack turns a trackpad into a mouse: relative deltas, and
`REL_WHEEL` ticks for scrolling. Everything that makes trackpad scrolling
feel good on a laptop needs the *contact itself* — where the finger is,
how hard it presses, exactly when it lands and lifts. This module streams
that: the pad runs in absolute mode and every sample goes to the host as
a small vendor HID report at the pad's native rate (~100 Hz), over USB
and BLE. A host-side consumer turns the frames into real scroll gestures.

Ordinary pointing keeps working the whole time: the module derives
relative deltas from the frames and re-injects them into the pad's normal
input chain, so the cursor, wheel overlays and buttons behave exactly as
before. On a host without the consumer, the keyboard is just a normal
trackpad — including a wheel-scroll fallback.

**The host consumer for macOS is a
[LinearMouse fork](https://github.com/kalakris/linearmouse/tree/go60-inputscale)**
— see [Host setup](#2-host-setup-macos). Firmware alone only gets you the
fallback wheel.

## Quick start (~10 minutes)

### 1. Firmware

In your [zmk-config](https://zmk.dev/docs/user-setup)'s
`config/west.yml`, add:

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

Declare your pad in the keymap (full worked example, including the
trackpad driver config and scroll-layer wiring:
[`boards/example.overlay`](boards/example.overlay)):

```dts
#include <raw_touch/processors.dtsi>

/ {
    raw_touch_pad: raw_touch_pad {
        compatible = "zmk,raw-touch-pad";
        device = <&trackpad>;   /* an absolute-mode input device */
        pad-id = <0>;
        rotate-90;              /* match the pad's physical mounting */
        y-invert;
        tap-click;              /* firmware tap-to-click */
    };
};
```

In your `.conf`:

```conf
CONFIG_ZMK_RAW_TOUCH=y

# MANDATORY for USB - ZMK owns HID interface HID_0, this module claims
# HID_1. Missing it is a build error (by design; the runtime failure mode
# would be silent).
CONFIG_USB_HID_DEVICE_COUNT=2
```

Push, let the standard ZMK build workflow produce your firmware, flash.

### 2. Host setup (macOS)

Stock LinearMouse does not consume the stream — you need the fork:

```sh
git clone -b go60-inputscale https://github.com/kalakris/linearmouse
cd linearmouse
Scripts/configure-code-signing   # auto-discovers your Apple Development cert
xcodebuild -scheme LinearMouse archive -archivePath build/LinearMouse.xcarchive
ditto build/LinearMouse.xcarchive/Products/Applications/LinearMouse.app /Applications/LinearMouse.app
open -a LinearMouse
```

Grant Accessibility when prompted (once — the grant persists across
rebuilds as long as the app is signed with the same certificate). Scroll
settings live in LinearMouse's normal per-device configuration.

### 3. Verify

Hold your scroll layer and drag the pad: smooth, phased scrolling with
momentum on lift-off, and a finger back down catches the fling. Quit
LinearMouse and the same gesture falls back to ordinary wheel scrolling.

## Requirements

- A trackpad in **absolute** mode emitting `INPUT_ABS_X/Y/Z` with the
  sync flag on Z. For Cirque Pinnacle pads use Zephyr's in-tree
  `input_pinnacle` driver with `data-mode = "absolute"`.
- **Lift-off packets.** On the in-tree Pinnacle driver set
  `idle-packets-count = <3>` — it defaults to 0, which emits no release
  at all: no momentum, no tap-to-click.
- ZMK with `CONFIG_ZMK_POINTING=y`. Built and tested against ZMK v0.3.0 /
  Zephyr 3.5; `src/usb_hid.c` uses the legacy USB device stack and will
  need porting for `USB_DEVICE_STACK_NEXT`. PRs welcome.
- Host: macOS with the LinearMouse fork (above). The wire format is
  documented in the appendix for other-platform consumers.

## Scroll mode

Frames carry a scroll-mode flag so the host knows to synthesize scrolling
rather than pointer motion. Put `&zip_raw_touch_scroll` in the processor
chain that should mean "scroll" — typically a layer overlay on the pad's
input listener:

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
                           <&zip_scroll_scaler 1 24>,
                           <&zip_raw_touch_idle_filter>;
    };
};
```

The marker is a pure pass-through; because it only runs when its chain is
the one actually handling the pad's events, layer ordering and
`process-next` shadowing are honoured for free. Keep it first in the
chain. If that does not suit your setup, `scroll-layers = <NAV>;` on the
pad node evaluates layer state directly instead.

## `&zip_raw_touch_idle_filter`

Put this **last** in every chain on a streaming pad. An input listener
sends a mouse report on every sync event, including the value-less syncs
an absolute pad produces at ~100 Hz; on BLE those compete with the raw
stream for airtime. The filter drops syncs that accumulated no
host-visible change, while always letting button transitions through.
One filter node per listener — do not share a node between two listeners.

## Configuration

| Kconfig | Default | |
|---|---|---|
| `CONFIG_ZMK_RAW_TOUCH` | y if a pad node exists | The module |
| `CONFIG_ZMK_RAW_TOUCH_USB` | y | Second USB HID interface (`HID_1`) |
| `CONFIG_ZMK_RAW_TOUCH_BLE` | y | Second BLE HIDS instance |
| `CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE` | 30 | Frames buffered for BLE |
| `CONFIG_ZMK_RAW_TOUCH_USAGE_PAGE` | 0xFF00 | Vendor usage page |
| `CONFIG_ZMK_RAW_TOUCH_USAGE` | 0x01 | Usage in that page |
| `CONFIG_ZMK_RAW_TOUCH_REPORT_ID` | 0x04 | Report ID |

Devicetree properties for `zmk,raw-touch-pad` (geometry, orientation,
tap-to-click tuning) are documented in
[`dts/bindings/input/zmk,raw-touch-pad.yaml`](dts/bindings/input/zmk,raw-touch-pad.yaml).

## Known issues

- **Expect to re-pair Bluetooth after first flashing this.** The second
  HID service changes the GATT database; hosts with a cached copy will
  not see it.
- **macOS can mis-bind report maps on the very first pairing** of a
  device with two HID services. Disconnect and reconnect once and it
  corrects itself (open macOS bug, not specific to this module).
- Android before ~9 may not tolerate two HID services at all.
- Tap-to-click silently dead? Make sure the pad's listener chain does not
  contain a processor that consumes `INPUT_BTN_0` (e.g.
  `zmk,input-processor-behaviors` mapping it to `&none`).

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

v2 frames were 7 bytes with an 8-byte feature report, `protocol_version` = 2.
The reference host still accepts both; new firmware ships v3 only.
