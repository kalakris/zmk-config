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

The keyboard has two scrolling modes. In **Standard mode** — no host
software running — it is just a normal trackpad: the module derives
relative deltas from the touch samples and re-injects them into the pad's
normal input chain, so the cursor, tap-to-click, buttons and a
wheel-scroll processor overlay all work on their own, and the touch
stream stays silent. When a host consumer claims the stream (see the
appendix), the keyboard switches to **RawTouch mode**: samples stream to
the host, which drives scrolling itself, and the firmware's own wheel
scrolling stands down. The moment the host goes away — quit, crash,
sleep, disconnect — the keyboard reverts to Standard mode on its own.

**The host consumer for macOS is a
[LinearMouse fork](https://github.com/kalakris/linearmouse)**
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
git clone https://github.com/kalakris/linearmouse
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
momentum on lift-off, and a finger back down catches the fling — that is
RawTouch mode. Quit LinearMouse and the keyboard drops back to Standard
mode: the same gesture scrolls as an ordinary notched wheel.

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
chain.

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
| `CONFIG_ZMK_RAW_TOUCH_REPORT_ID` | 0x04 | Report ID |

Devicetree properties for `zmk,raw-touch-pad` (geometry, orientation,
tap-to-click tuning) are documented in
[`dts/bindings/input/zmk,raw-touch-pad.yaml`](dts/bindings/input/zmk,raw-touch-pad.yaml).

## Known issues

- **Expect to re-pair Bluetooth after first flashing this.** The second
  HID service changes the GATT database; hosts with a cached copy will
  not see it.
- **Re-pair after any firmware update that changes the report layout or
  the GATT database, too** (e.g. a protocol version bump — or the
  host-claim path's arrival, which made the feature-report characteristic
  writable).
  macOS caches the HOGP report map and GATT structure at pairing time,
  and a change to the map's *contents* does not move any GATT handles, so
  no Service Changed indication fires and the host never re-reads it. The
  failure is deceptively partial: keyboard, USB, and the (live-read)
  feature report all keep working — only BLE frame parsing (or, for the
  gate, the BLE claim write) breaks, and with it both smooth scrolling
  and the suppressed wheel fallback. Symptom: scrolling dead over BLE,
  fine over USB. Fix: forget the device on the host, `&bt BT_CLR` on the
  keyboard, pair fresh.
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

Emitted **only in RawTouch mode** — while the sending endpoint's host
claim is live (see [Host claim](#host-claim)): one report per pad sample while touched
(~100 Hz on a Cirque Pinnacle), plus one release report on lift-off.
While no host has claimed, the stream is silent — nobody is listening,
and ~100 Hz of 11-byte reports is real BLE airtime — with one exception:
if the claim clears mid-touch, the firmware emits a single synthetic
release report (`touched` clear, x/y/z zero, **bit 2 clear**) so the
host closes out its gesture, then goes silent.

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

`flags`: bit 0 = touched, bit 1 = scroll mode, bit 2 = `host_claimed`
(see [Host claim](#host-claim) — set iff a host claim was live for the
frame's endpoint when it was sampled), bits 3–7 reserved (0).

Bit 2 is computed per frame against the endpoint the frame is sent to:
it is set iff that endpoint held a live host claim when the frame was
sampled, which is also exactly when the firmware suppresses its wheel
fallback. Because frames are only emitted while claimed, bit 2 is
implied-set on ordinary frames; the one frame that carries it clear is
the trailing synthetic release above, which tells the host both that the
touch is over *and* that the wheel fallback is live again — so the host
MUST close the gesture without adding lift-off momentum (the finger may
still be down and scrolling via the wheel; momentum on top would
double-scroll). **Hosts MUST synthesize scroll only from frames with
bits 1 and 2 both set.** Firmware is thereby the single source of truth —
wheel and synthesized scroll are mutually exclusive by construction, with
no host-side racing after sleep, reboot or re-pair (a host that has not
yet re-claimed gets no frames and stays silent while the wheel works).

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
| 2 | 1 | `capabilities` — bit 0 = host claim supported; rest reserved (0) |
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

### Host claim

How a host switches the keyboard into RawTouch mode — and the protocol's
only host→device path (still protocol v3 — the claim is advertised by
`capabilities` bit 0, not a version bump). A host that consumes the
stream **claims** it by writing the feature report — USB
`SET_REPORT(FEATURE)` or a GATT write to the BLE feature-report
characteristic — with a 4-byte command body:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | command — `0x01` = host claim |
| 1 | 1 | `0x01` = claim/refresh, `0x00` = release |
| 2 | 1 | timeout in seconds; firmware clamps to [5, 120]; `0` rejected (ignored on release) |
| 3 | 1 | reserved, must be 0 |

Any other command byte, length, or nonzero reserved byte is rejected
(USB: request stalled; BLE: ATT error). Over USB the control payload may
carry the leading report-ID byte (5 bytes total) or the bare body; over
BLE the write carries the body alone, as usual. Hosts MUST check
`capabilities` bit 0 before claiming.

**Effect.** While the claim is live and its endpoint is the one the
keyboard is sending to, the firmware emits the input reports above — and
stops injecting the relative deltas for scroll-context frames, which
silences any wheel-fallback processor chain downstream, setting frame
flags bit 2 to say so. Pointer-context deltas, tap-to-click and all key
reports are unaffected, and everything reverts on the very next sample
after the claim clears (with the single trailing release report if that
happens mid-touch). Hosts that never claim get no frames and the wheel
fallback keeps working unchanged.

**Endpoint scoping.** A claim is scoped to the endpoint *instance* the
write arrived on: the USB connection, or the specific BLE profile that
wrote it. Claims on different endpoints are independent — one host's
claim never mutes the fallback of a host on another transport or
profile. Suppression (and bit 2) applies only while the claiming
endpoint is the selected one.

**Liveness.** The host MUST refresh the claim (re-write it) at intervals
of no more than **half** the timeout it wrote, and SHOULD release
(`0x00`) on clean exit. It MUST re-assert after resume, reconnect and
device re-enumeration — claims do not survive on their own: firmware
clears a claim on

- timeout expiry (no refresh arrived in time);
- explicit release from the same endpoint;
- USB detach/reset (for a USB claim);
- disconnect of the claiming BLE profile's connection;
- any endpoint switch away from the claiming endpoint (a switch back
  does not restore it — the host's next refresh does, at most half a
  timeout later).

A dead host can therefore never leave the wheel fallback disabled for
longer than its own stated timeout.

**BLE cache note.** Making the feature characteristic writable changes
only a characteristic *permission*, not the report map or attribute
layout — **bench-verified 2026-08-29 that a host paired against
pre-claim firmware claims and releases correctly with no re-pair**. The
forget + re-pair rule still applies to any change that touches the
report map or GATT attribute layout (see Known issues — that failure
mode is deceptively partial).

### v2 (legacy)

v2 frames were 7 bytes with an 8-byte feature report, `protocol_version` = 2.
Dead: the firmware ships v3 only, and the reference host dropped v2
acceptance 2026-08-28 (it never existed outside pre-release prototypes).
