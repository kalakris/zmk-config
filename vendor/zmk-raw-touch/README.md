# zmk-raw-touch

> **Early release.** Interfaces may still change; issues and PRs welcome.

Trackpad scrolling on a ZMK keyboard that feels like a Magic Trackpad —
gesture phases, lift-off momentum, catch-to-stop — instead of notched
wheel clicks.

ZMK's pointing stack turns a trackpad into a mouse: relative deltas, and
`REL_WHEEL` ticks for scrolling. Everything that makes trackpad scrolling
feel good on a laptop needs the *contact itself* — where the finger is,
how hard it presses, exactly when it lands and lifts. This module sends
that: the pad runs in absolute mode and every sample goes to the host as
a small vendor HID report ("raw touch frame") at the pad's native rate
(~100 Hz), over USB and BLE. A host app turns the frames into real scroll
gestures.

The keyboard has two scrolling modes. In **Standard mode** — no host
software running — it is just a normal trackpad: the module derives
relative deltas from the touch samples and re-injects them into the pad's
normal input chain, so the cursor, tap-to-click, buttons and a
wheel-scroll processor overlay all work on their own, and no frames are
sent. When a host app claims the frames (see the appendix), the keyboard
switches to **RawTouch mode**: samples go to the host, which drives
scrolling itself, and the firmware's own wheel scrolling stands down. The
moment the host goes away — quit, crash, sleep, disconnect — the keyboard
reverts to Standard mode on its own.

**The host app for macOS is [RawTouch](https://github.com/kalakris/rawtouch)**
(menubar app, MIT). Firmware alone gives you Standard mode. The wire
format is documented in the [appendix](#appendix-wire-format-protocol-v3)
for anyone writing a host for another platform.

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

Declare your pad and its input listener in your keymap or overlay (full
worked example, including the trackpad driver config and scroll-layer
wiring: [`examples/example.overlay`](examples/example.overlay)):

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

    trackpad_listener: trackpad_listener {
        compatible = "zmk,input-listener";
        device = <&trackpad>;
        input-processors = <&zip_xy_scaler 1 1>,
                           <&zip_raw_touch_idle_filter>;
    };
};
```

In your `.conf`, nothing is required: the module enables itself when a
`zmk,raw-touch-pad` node exists, and it raises `CONFIG_USB_HID_DEVICE_COUNT`
to 2 (ZMK owns HID interface `HID_0`; this module claims `HID_1`). If your
config pins that symbol lower, the build fails on purpose — the runtime
failure would be a silently missing interface.

On a **split** keyboard, flash both halves from the same build. Frames
are sent by the central; a pad on the peripheral is still streamed (its
input events cross the split link like any other), but the transports
only exist on the central.

Push, let the standard ZMK build workflow produce your firmware, flash.

### 2. Host app (macOS)

Install [RawTouch](https://github.com/kalakris/rawtouch) and grant it
Accessibility when asked. Scroll feel is tuned in its Settings window.

### 3. Verify

Hold your scroll layer and drag the pad: smooth, phased scrolling with
momentum on lift-off, and a finger back down catches the fling — that is
RawTouch mode. Quit RawTouch and the keyboard drops back to Standard
mode: the same gesture scrolls as an ordinary notched wheel.

Without a host app, you can still check that the firmware side is alive:

- On macOS, `ioreg -c IOHIDDevice -r -l | grep -B2 -A6 'PrimaryUsagePage" = 65280'`
  lists the second HID interface (usage page `0xFF00`, usage `0x01`).
- With `CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF=y` and a host that claims,
  the ZMK log shows `Raw touch frames claimed by USB (timeout 30s)`,
  then `cleared (released by host)` / `cleared (USB detached)` /
  `cleared (BLE profile disconnected)` / `cleared (endpoint switched away)`,
  or `expired without a refresh` if the host stops refreshing.

## Requirements

- **ZMK with `CONFIG_ZMK_POINTING=y`.** Built and tested against ZMK
  v0.3.0 / Zephyr 3.5. `src/raw_touch_usb_hid.c` uses the legacy USB
  device stack and will need porting for `USB_DEVICE_STACK_NEXT` — PRs
  welcome.
- **A trackpad driver in absolute mode**, emitting `INPUT_ABS_X/Y/Z` with
  the sync flag on Z. For Cirque Pinnacle pads, Zephyr's `cirque,pinnacle`
  driver does this with `data-mode = "absolute"`. It is in-tree from
  Zephyr 3.6; ZMK v0.3.0 pins Zephyr 3.5, which has no Pinnacle driver,
  so the reference build carries Zephyr's driver as a module —
  [`kalakris/cirque-input-module`](https://github.com/kalakris/cirque-input-module)
  branch `intree-driver` (Zephyr's `input_pinnacle.c` plus three small
  patches). Add it to `west.yml` alongside this module. The example
  overlay uses that binding's property names.
- **Lift-off packets.** Set `idle-packets-count = <3>` on the Pinnacle
  node — the driver defaults it to 0, which emits no release at all: no
  momentum, no tap-to-click. Also wire `data-ready-gpios`; the driver
  needs the DR line.
- **Host:** macOS with RawTouch. Other platforms: see the appendix.

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
                           /* Standard-mode wheel scrolling */
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
chain. In RawTouch mode the firmware does not inject scroll-context
deltas at all, so the wheel processors after the marker fall silent
without any host-side filtering.

## `&zip_raw_touch_idle_filter`

Put this **last** in every chain on a streaming pad. An input listener
sends a mouse report on every sync event, including the value-less syncs
an absolute pad produces at ~100 Hz; on BLE those compete with the raw
touch frames for airtime. The filter drops syncs that accumulated no
host-visible change, while always letting button transitions through.
One filter node per listener — do not share a node between two listeners.

## Configuration

| Kconfig | Default | |
|---|---|---|
| `CONFIG_ZMK_RAW_TOUCH` | y if a pad node exists | The module |
| `CONFIG_ZMK_RAW_TOUCH_USB` | y | Second USB HID interface (`HID_1`) |
| `CONFIG_ZMK_RAW_TOUCH_BLE` | y | Second BLE HIDS instance |
| `CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE` | 8 | Frames buffered for BLE — ~80 ms, enough for arrival batching (when full the oldest *motion* frame is evicted; release frames are never evicted) |
| `CONFIG_ZMK_RAW_TOUCH_BLE_THREAD_STACK_SIZE` | `ZMK_BLE_THREAD_STACK_SIZE` | Stack of the module's BLE send work queue |
| `CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES` | 4 | Input devices the scroll marker can track |
| `CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_*` | inherits | Module log level |

Devicetree properties for `zmk,raw-touch-pad` (geometry, orientation,
tap-to-click tuning) are documented in
[`dts/bindings/input/zmk,raw-touch-pad.yaml`](dts/bindings/input/zmk,raw-touch-pad.yaml).

## Known issues

- **Expect to re-pair Bluetooth after first flashing this.** The second
  HID service changes the GATT database; hosts with a cached copy will
  not see it.
- **Re-pair after any firmware update that changes the report layout or
  the GATT attribute layout, too** (e.g. a protocol version bump, or
  adding/removing a trackpad — the feature report carries one 8-byte slot
  per pad, so the pad count is part of the report map). macOS
  caches the HOGP report map and GATT structure at pairing time, and a
  change to the map's *contents* does not move any GATT handles, so no
  Service Changed indication fires and the host never re-reads it. The
  failure is deceptively partial: keyboard, USB, and the (live-read)
  feature report all keep working — only BLE frame parsing breaks, and
  with it smooth scrolling. Symptom: scrolling dead over BLE, fine over
  USB. Fix: forget the device on the host, `&bt BT_CLR` on the keyboard,
  pair fresh.
- **macOS can mis-bind report maps on the very first pairing** of a
  device with two HID services. Disconnect and reconnect once and it
  corrects itself (open macOS bug, not specific to this module).
- Android before ~9 may not tolerate two HID services at all.
- Tap-to-click silently dead? Make sure the pad's listener chain does not
  contain a processor that consumes `INPUT_BTN_0` (e.g.
  `zmk,input-processor-behaviors` mapping it to `&none`).

## How it is built

Every source compiles into ZMK's `app` target rather than a
`zephyr_library()` of its own, because ZMK declares its headers
`PRIVATE` to `app` and a separate library could not see
`<zmk/endpoints.h>` or `<drivers/input_processor.h>`. The USB and BLE
transports are parallel copies of ZMK core's `usb_hid.c` and `hog.c`,
trimmed to one report and registered as a second HID interface / HIDS
instance — the same approach as
[`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) and
[`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io), plus a
feature report. Nothing in ZMK core is patched.

## License

MIT. See [LICENSE](LICENSE). The wire format in the appendix is
documented so anyone may write a host for it; no additional permission is
needed.

Thanks to the ZMK contributors (`app/src/hog.c`, `usb_hid.c` and
`endpoints.c` are the basis of the transport files), to zzeneg and
badjeff for showing that a second HID interface from a module works, and
to Peter Johanson for `cirque-input-module`.

---

## Appendix: wire format (protocol v3)

Vendor-defined usage page `0xFF00`, usage `0x01`, one top-level
application collection on its own HID interface / HIDS instance. Report ID
`0x04`. Over USB the report ID is the first payload byte of every
transfer; over BLE it lives in the GATT report-reference descriptor and
notifications, reads and writes carry the body alone. **Normalize before
parsing.**

### Input report — 11 bytes

Emitted **only in RawTouch mode** — while the sending endpoint's host
claim is live (see [Host claim](#host-claim)): one report per pad sample
while touched (~100 Hz on a Cirque Pinnacle), plus one release report on
lift-off. While no host has claimed, nothing is sent — nobody is
listening, and ~100 Hz of 11-byte reports is real BLE airtime — with one
exception: if the claim clears mid-touch, the firmware emits a single
synthetic release report (`touched` clear, x/y/z zero, **bit 2 clear**;
bit 1 keeps the frame's scroll context) so the host closes out its
gesture, then goes silent.

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
scrolling. Because frames are only emitted while claimed, bit 2 is
implied-set on ordinary frames; the one frame that carries it clear is
the trailing synthetic release above, which tells the host both that the
touch is over *and* that the wheel is live again — so the host MUST
close the gesture without adding lift-off momentum (the finger may still
be down and scrolling via the wheel; momentum on top would
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
batching distorts. `seq` exposes silent drops (under pressure the BLE
send queue drops the oldest *motion* frame — see below); with device
timestamps, velocity across a gap remains correct, so `seq` is
diagnostic.

**Release frames are delivered.** A report with `touched` clear — the
lift-off report, and the synthetic one from a mid-touch declaim — is the
one report whose loss is visible to a user, as a phantom finger-down and
runaway momentum. The firmware treats it as durable rather than
best-effort:

- a full BLE send queue evicts the oldest *motion* frame, never a
  release; motion frames are ~10 ms apart and carry absolute positions,
  so a gap costs nothing;
- a release that fails to notify (typically because no TX buffer is free
  this connection event) is retried, one connection interval apart, a few
  times before being given up on;
- retries are head-of-line and the queue drains in order, so per pad a
  release never overtakes earlier motion, and a later touch's frames
  never overtake the release that closed the previous one.

Hosts SHOULD still treat frame silence longer than ~150 ms while
`touched` was last set as a lift-off — but as a **safety net** for what
the firmware cannot cover (link loss, disconnect, host sleep, the
keyboard losing power mid-touch), not as a correctness requirement on a
healthy link.

### Feature report — 4 + 8×*N* bytes, same report ID

Readable over USB `GET_REPORT(FEATURE)` and the BLE feature report
characteristic (report-reference type `0x03`). The body is a 4-byte
header plus one 8-byte slot per pad the firmware was built with, so its
length is **4 + 8×*N***, 1 ≤ *N* ≤ 8. Over USB the response additionally
carries the report ID (`0x04`) as its first byte, making it 5 + 8×*N*;
over BLE the characteristic read returns the body alone. On a two-pad
build such as the reference Go60 that is **20 bytes** (21 over USB) —
the sizes earlier revisions of this document hard-coded.

**Hosts MUST accept any body length ≥ 12 that is 4 + 8×*N***, recover
*N* as `(length − 4) / 8`, and read `min(N, popcount(pads_present))`
slots. A host that requires exactly 20 bytes will refuse a one-pad or
three-pad keyboard running the same firmware. **Hosts MUST read and
validate this report before treating a `0xFF00`/`0x01` collection as
this protocol** — that vendor pair is widely squatted.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `protocol_version` = 3 |
| 1 | 1 | `pads_present` bitmask, bit *N* = `pad_id` *N* |
| 2 | 1 | `capabilities` — bit 0 = host claim supported; rest reserved (0) |
| 3 | 1 | reserved (0) |
| 4 + 8×*i* | 8 | pad slot *i*, for 0 ≤ *i* < *N* |

Slots describe present pads in ascending `pad_id` order. There is one
slot per pad, so on any build of eight pads or fewer every present pad is
described; `pads_present` can only outrun the slots past eight pads,
where the extra pads set their bit but get no slot. Hosts MUST NOT assume
slot *i* corresponds to the *i*-th set bit beyond the slots they actually
received. Each slot:

| Offset | Size | Field |
|---|---|---|
| +0 | 1 | `resolution`, counts/mm, 0 = unknown |
| +1 | 1 | `orientation` — bit 0 rotate-90, bit 1 x-invert, bit 2 y-invert |
| +2 | 2 | `x_max`, little-endian |
| +4 | 2 | `y_max`, little-endian |
| +6 | 1 | `max_contacts` (1 on a Pinnacle) |
| +7 | 1 | reserved (0) |

Coordinates stream raw and untransformed; `orientation` tells the host
what to apply. The report descriptor also declares real logical ranges on
the x/y fields — those of one pad node (the only one, on a single-pad
build) — so generic HID tooling sees true geometry without parsing the
feature report. Multi-pad hosts must use the slots.

*N* is the pad count of the firmware, not a runtime value, so it is baked
into the HID report descriptor's feature `REPORT_COUNT`. **Adding or
removing a pad is therefore a report-map change and needs a Bluetooth
forget + re-pair on macOS** (see [Known issues](#known-issues) — the
failure is deceptively partial: USB and the live-read feature report keep
working while BLE frame parsing dies).

### Host claim

How a host switches the keyboard into RawTouch mode — and the protocol's
only host→device path (still protocol v3 — the claim is advertised by
`capabilities` bit 0, not a version bump). A host that consumes the
frames **claims** them by writing the feature report — USB
`SET_REPORT(FEATURE)` or a GATT write to the BLE feature-report
characteristic — with a 4-byte command body:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | command — `0x01` = host claim |
| 1 | 1 | `0x01` = claim/refresh, `0x00` = release |
| 2 | 1 | timeout in seconds; firmware clamps to [5, 120]; `0` rejected (ignored on release) |
| 3 | 1 | reserved, must be 0 |

Any other command byte, length, or nonzero reserved byte is rejected.
Over USB the request is stalled; the control payload may carry the
leading report-ID byte (5 bytes total) or the bare body. Over BLE the
write carries the body alone and a rejection is an ATT error:
`Invalid Attribute Value Length` for a wrong length, `Value Not Allowed`
for a bad command/operation/timeout/reserved byte, `Invalid Offset` for
a non-zero offset, and `Write Not Permitted` from a peer that is not a
bonded host profile (e.g. a generic GATT tool that has not paired as a
host). Hosts MUST check `capabilities` bit 0 before claiming.

**Effect.** While the claim is live and its endpoint is the one the
keyboard is sending to, the firmware emits the input reports above — and
stops injecting the relative deltas for scroll-context frames, which
silences any wheel processor chain downstream, setting frame flags bit 2
to say so. Pointer-context deltas, tap-to-click and all key reports are
unaffected, and everything reverts on the very next sample after the
claim clears (with the single trailing release report if that happens
mid-touch). Hosts that never claim get no frames and the wheel keeps
working unchanged.

**Endpoint scoping.** A claim is scoped to the endpoint *instance* the
write arrived on: the USB connection, or the specific BLE profile that
wrote it. Claims on different endpoints are independent — one host's
claim never mutes the wheel of a host on another transport or profile.
Suppression (and bit 2) applies only while the claiming endpoint is the
selected one.

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

A dead host can therefore never leave the wheel disabled for longer than
its own stated timeout.

**BLE cache note.** The feature characteristic being writable is a
characteristic *permission*, not part of the report map or attribute
layout, so it does not invalidate a host's cached GATT database. The
forget + re-pair rule applies to changes that touch the report map or
the attribute layout (see Known issues — that failure mode is
deceptively partial).
