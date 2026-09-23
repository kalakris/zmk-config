# zmk-raw-touch

> **Early release.** Interfaces may still change; issues and PRs welcome.

A ZMK module that sends a keyboard trackpad's absolute position and contact
state to a host application over USB or Bluetooth. The macOS companion,
[RawTouch](https://github.com/kalakris/rawtouch), uses those reports for
vertical and horizontal scrolling with lift-off momentum and touch-to-stop
behavior.

The module also derives ordinary pointer movement and optional tap-to-click
from the same input. A wheel processor chain keeps scrolling available
when no host app is running. It adds its own HID interface and Bluetooth HID
service without patching ZMK core.

[Compatibility](#compatibility) · [Setup](#setup) ·
[Split keyboards](#split-keyboards) · [Configuration](#configuration-reference) ·
[Troubleshooting](#troubleshooting) · [Protocol](#appendix-wire-format-protocol-v4)

## What it does

Your keymap decides when a pad scrolls. Put the wheel processors in the
base chain for a [dedicated scrolling pad](#dedicated-scrolling-pad), or
in a layer overlay to switch between pointing and scrolling. Activate
that layer however your keymap defines it: for example, by holding or
toggling a key.

- **RawTouch mode:** the module sends touch samples to the host, including
  finger position, contact state, timing, and whether the active processor
  chain is marked for scrolling. RawTouch uses these samples to generate
  scroll gestures and lift-off momentum. Touching a pad in scroll mode
  stops momentum. The module suppresses its own wheel motion for those
  touches; configured pointer movement and taps stay in firmware.
- **Standard mode:** the module derives pointer movement and optional
  taps from the absolute samples, and your wheel processors handle
  scrolling. No host app is needed and no raw touch samples are sent.

RawTouch switches modes automatically when enabled and ready to send
scroll events. If it is disabled or lacks Accessibility permission, the
keyboard stays in Standard mode. Quitting normally returns to Standard
mode promptly; after a crash or force-quit, this can take up to 30 seconds.

The module currently handles one contact per pad because I only have
Cirque Pinnacle pads to develop and test with. Multi-touch is within the
project's scope: contributions for TPS43 and other multi-touch pads are
welcome. These would need additional firmware and host support, and may
need protocol extensions.

RawTouch supports vertical, horizontal, and diagonal scrolling and
momentum, with an option to restrict each pad to one axis.

## Compatibility

Tested on a MoErgo Go60 with two Cirque Pinnacle pads, MoErgo ZMK v0.3.0 /
Zephyr 3.5, and RawTouch on macOS 26 over USB and Bluetooth.

Test reports from other keyboards, trackpads, and host setups are welcome
and will help expand this compatibility list. Please include your
hardware, firmware and OS versions, connection type, and what worked or
failed in an [issue](https://github.com/kalakris/zmk-raw-touch/issues).

Tested configurations include dedicated scrolling pads and pads that use
a layer to switch between pointing and scrolling, with vertical,
horizontal, and diagonal scrolling in both Standard and RawTouch modes.

Tested dependency versions:

| Component | Tested revision |
|---|---|
| `moergo-sc/zmk` | `57a7b8e06b19898e59a4dbd5f554b7ed5677493b` |
| `kalakris/cirque-input-module`, `intree-driver` branch | `cbb4eaada3b3be052939a16c0441081a72d7d6b9` |

That driver module packages Zephyr's Pinnacle driver for the older tree,
with three patches for status handling, edge sensitivity, and calibration
on initialization.

Other boards and drivers need integration and testing. Upstream ZMK
`main` and Zephyr 4.1 have not been verified here. The USB implementation
uses Zephyr's legacy USB stack; `USB_DEVICE_STACK_NEXT` requires a port.
Check this before adding the module to an existing build.

### Versioning

The module is semantically versioned from git tags, starting at `v0.1.0`.
It is pre-1.0, so a minor release may change interfaces.
[`include/zmk/raw_touch/version.h`](include/zmk/raw_touch/version.h) is the
single source of truth for the number: the firmware reports it in the
feature report, and CI fails a `v*` tag whose major.minor disagrees with it.

The protocol version is the only compatibility contract with a host. The
module version is diagnostic — useful for a bug report, and for seeing
which build each half of a split is running, but never something a host
should parse the wire by. Today's pairing is RawTouch 0.1.x ↔ protocol 4 ↔
module 0.2.x.

Protocol 4 added the feature report's four identification bytes and an
eight-byte device id, moving the geometry slots by twelve bytes; there is
no protocol 3 compatibility in either the firmware or RawTouch. It is a report-map change, so hosts that
cache the map — macOS over Bluetooth — need a forget and re-pair after
upgrading, exactly as when a pad is added or removed
(see [Troubleshooting](#troubleshooting)).

### Driver requirements

The module does not depend on a particular sensor driver, but it does
require this input format:

- Absolute `INPUT_ABS_X`, `INPUT_ABS_Y`, and `INPUT_ABS_Z` events, with the
  sync flag on the final Z event.
- Samples while a finger remains down, including while it is stationary.
- A release sample with X, Y, and Z all zero. The module currently uses
  this convention to detect lift-off; a driver with a different contact
  representation needs adaptation.
- Untransformed coordinates. Declare mounting orientation on the module's
  pad node, so the pointer path and host receive consistent geometry.

For the Pinnacle driver used by the reference build, set
`data-mode = "absolute"`, wire `data-ready-gpios`, and set
`idle-packets-count = <3>`. A zero idle-packet count prevents release
samples, breaking normal lift-off and firmware tap detection.

## Setup

You need an existing ZMK configuration that builds for your board, a
compatible absolute-mode driver, and `CONFIG_ZMK_POINTING=y`.

### 1. Add the module to your manifest

Merge these entries into the existing `remotes` and `projects` lists in
`config/west.yml`; keep your board's ZMK project and other dependencies:

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

Pin `revision` to a commit once you have a working build. On the tested
Zephyr 3.5 configuration, also add or override the driver project:

```yaml
    - name: cirque-input-module
      remote: kalakris
      revision: cbb4eaada3b3be052939a16c0441081a72d7d6b9
```

### 2. Start from a trackpad configuration

Here is an illustrative configuration written for this guide: a Pinnacle
pad points normally, supports taps, and scrolls while layer 2 is active.
It uses the driver described under [Compatibility](#compatibility).
The board already defines a physical device named `trackpad`, including
its SPI or I²C bus and data-ready GPIO. Those hardware definitions are
omitted here.

The example pad is mounted with its axes swapped and Y inverted, so both
listener chains apply that transform. Use the orientation and layer
number that match your keyboard. Bind a key to activate `SCROLL_LAYER`.
The `tap_to_click` mapper converts the driver's `INPUT_BTN_TOUCH` tap
report to ZMK's left mouse button, `INPUT_BTN_0`.

**Before — relative-mode pointing and wheel scrolling:**

<!-- example: examples/trackpad/before.overlay -->
```dts
#include <input/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/input_transform.h>

#define SCROLL_LAYER 2

&trackpad {
    data-mode = "relative";
    primary-tap-enable;
};

/ {
    input_processors {
        tap_to_click: tap_to_click {
            compatible = "zmk,input-processor-code-mapper";
            #input-processor-cells = <0>;
            type = <INPUT_EV_KEY>;
            map = <INPUT_BTN_TOUCH INPUT_BTN_0>;
        };
    };

    trackpad_listener: trackpad_listener {
        compatible = "zmk,input-listener";
        device = <&trackpad>;
        input-processors = <&tap_to_click>,
                           <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
                           <&zip_xy_scaler 1 1>;

        scroll {
            layers = <SCROLL_LAYER>;
            input-processors = <&tap_to_click>,
                               <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
                               <&zip_xy_to_scroll_mapper>,
                               <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                               <&zip_scroll_scaler 1 8>;
        };
    };
};
```

If your keyboard uses a different Cirque driver, first adapt it to a
[compatible absolute-mode driver](#driver-requirements). Property names
vary between drivers: this is not a drop-in patch for every trackpad.
When replacing a driver, remove inherited properties that its replacement
does not recognize. Use `/delete-property/ property-name;` inside the
driver node's overlay, then set the properties required by the new driver.
Keep the bus and GPIO assignments specific to your board.

### 3. Add raw touch to that configuration

The diff below shows the changes to the example above. Lines starting
with `-` are removed; lines starting with `+` are added. Edit the existing
pad and listener rather than adding a second active listener.

<!-- example-diff: trackpad.overlay examples/trackpad/before.overlay examples/trackpad/raw-touch.overlay -->
```diff
--- trackpad.overlay (before)
+++ trackpad.overlay (with raw touch)
@@ -1,38 +1,39 @@
 #include <input/processors.dtsi>
+#include <raw_touch/processors.dtsi>
 #include <zephyr/dt-bindings/input/input-event-codes.h>
 #include <dt-bindings/zmk/input_transform.h>
 
 #define SCROLL_LAYER 2
 
 &trackpad {
-    data-mode = "relative";
-    primary-tap-enable;
+    data-mode = "absolute";
+    idle-packets-count = <3>;
+    /delete-property/ primary-tap-enable;
 };
 
 / {
-    input_processors {
-        tap_to_click: tap_to_click {
-            compatible = "zmk,input-processor-code-mapper";
-            #input-processor-cells = <0>;
-            type = <INPUT_EV_KEY>;
-            map = <INPUT_BTN_TOUCH INPUT_BTN_0>;
-        };
+    raw_touch_pad: raw_touch_pad {
+        compatible = "zmk,raw-touch-pad";
+        device = <&trackpad>;
+        pad-id = <0>;
+        rotate-90;
+        y-invert;
+        tap-click;
     };
 
     trackpad_listener: trackpad_listener {
         compatible = "zmk,input-listener";
         device = <&trackpad>;
-        input-processors = <&tap_to_click>,
-                           <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
-                           <&zip_xy_scaler 1 1>;
+        input-processors = <&zip_xy_scaler 1 1>,
+                           <&zip_raw_touch_idle_filter>;
 
         scroll {
             layers = <SCROLL_LAYER>;
-            input-processors = <&tap_to_click>,
-                               <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
+            input-processors = <&zip_raw_touch_scroll>,
                                <&zip_xy_to_scroll_mapper>,
                                <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
-                               <&zip_scroll_scaler 1 8>;
+                               <&zip_scroll_scaler 1 24>,
+                               <&zip_raw_touch_idle_filter>;
         };
     };
 };
```

The changes have five jobs:

1. **Report finger positions and releases.** Absolute mode provides the
   coordinates; `idle-packets-count = <3>` supplies lift-off samples.
2. **Register the pad.** `zmk,raw-touch-pad` streams its touch data when
   requested by the host and derives ordinary pointer motion for the
   listener. Give each pad its own ID.
3. **Move orientation and tap handling to the module.** `rotate-90` and
   `y-invert` replace this example's listener transforms so both pointer
   motion and host scrolling use the same mounting. `tap-click` replaces
   the driver's relative-mode tap setting and button mapper. Do not apply
   orientation twice; remove driver-level `invert-x`, `invert-y`, or `swap-xy` if
   present and describe the mounting on the raw-touch pad node instead.
4. **Mark the scrolling chain.** `zip_raw_touch_scroll` tells the host
   when to scroll. Keep the wheel processors for Standard mode. Absolute
   counts can need a different wheel divisor: `1 24` is a starting point
   replacing this example's `1 8`, not a required conversion ratio.
5. **Filter unchanged reports.** Put the idle filter last in each chain.

<details>
<summary>Complete configuration after the changes</summary>

<!-- example: examples/trackpad/raw-touch.overlay -->
```dts
#include <input/processors.dtsi>
#include <raw_touch/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/input_transform.h>

#define SCROLL_LAYER 2

&trackpad {
    data-mode = "absolute";
    idle-packets-count = <3>;
    /delete-property/ primary-tap-enable;
};

/ {
    raw_touch_pad: raw_touch_pad {
        compatible = "zmk,raw-touch-pad";
        device = <&trackpad>;
        pad-id = <0>;
        rotate-90;
        y-invert;
        tap-click;
    };

    trackpad_listener: trackpad_listener {
        compatible = "zmk,input-listener";
        device = <&trackpad>;
        input-processors = <&zip_xy_scaler 1 1>,
                           <&zip_raw_touch_idle_filter>;

        scroll {
            layers = <SCROLL_LAYER>;
            input-processors = <&zip_raw_touch_scroll>,
                               <&zip_xy_to_scroll_mapper>,
                               <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                               <&zip_scroll_scaler 1 24>,
                               <&zip_raw_touch_idle_filter>;
        };
    };
};
```

</details>

The files in this section live under [`examples/trackpad/`](examples/trackpad/).
CI compiles the "after" files on a Go60 against the pinned trees in
[`ci/`](ci/), so the configuration shown here is known to build.

For a pad that always scrolls, use the
[dedicated scrolling variant](#dedicated-scrolling-pad) below.

`zip_xy_to_scroll_mapper` is provided by ZMK's `input/processors.dtsi`
in the tested tree: it maps `INPUT_REL_X` to `INPUT_REL_HWHEEL` and
`INPUT_REL_Y` to `INPUT_REL_WHEEL`. The scroll scaler handles both wheel
axes.

Only Y is inverted here: the oriented pad deltas increase to the right
and downward, while positive horizontal wheel means right and positive
vertical wheel means up. This mapping was verified on the Go60; the host's
Natural scrolling setting then determines content direction. These wheel
processors affect Standard mode. RawTouch separately derives direction
from the reported pad orientation and its host settings.

Two processor rules matter:

- **Scroll marker first:** `&zip_raw_touch_scroll` marks samples handled
  by this chain as scrolling. In RawTouch mode, firmware suppresses the
  derived relative motion before it can become wheel events. The marker
  follows the listener's actual layer/processor selection.
- **Idle filter last:** `&zip_raw_touch_idle_filter` drops syncs with no
  mouse-report change, reducing redundant HID traffic. Each listener
  needs its own filter instance. A listener may reuse its instance in
  its base and layer chains; different listeners must not share it.

#### Why the module handles taps

In relative mode, the Pinnacle hardware recognizes taps and reports them
as button presses. The driver forwards those reports; it does not detect
taps itself. The driver's `primary-tap-enable` option applies only in
relative mode.

Raw touch needs absolute mode to get finger positions and lift-off samples.
In that mode, the Pinnacle driver explicitly disables hardware tap
recognition and reports X, Y, and touch strength instead. It has no
software tap detector for those samples, so leaving `primary-tap-enable`
set does not preserve tap-to-click. This is how the supported Pinnacle
driver works, not a general rule that absolute-mode drivers cannot detect
taps.

With `tap-click`, the module supplies that detector: a touch must lift
within `tap-max-ms` without exceeding `tap-max-movement`. The module also
knows whether the touch was used for scrolling and suppresses clicks for
those touches. This keeps tap behavior consistent in Standard and RawTouch
modes, including when the host app is not running.

The module emits `INPUT_BTN_0` directly, so the old `tap_to_click` mapper
is no longer needed. Remove processors that discard that button, such as
a `zip_button_behaviors` instance mapping it to `&none`.

For a hold-tap key that activates the scroll layer, check the undecided
window: trackpad input does not resolve the key's hold-tap decision.
Consider `hold-while-undecided` if quick flicks start before the layer
activates. It makes the layer active while the hold-tap decision is still
pending, so choose it deliberately for your keymap.

#### Dedicated scrolling pad

To make the pad scroll without holding a key, put the marker and wheel
processors in its base chain. For the example above, this overlay removes
the `scroll` child and replaces the pointer chain:

<!-- example: examples/trackpad/dedicated-scroll.overlay -->
```dts
&trackpad_listener {
    /delete-node/ scroll;
    input-processors = <&zip_raw_touch_scroll>,
                       <&zip_xy_to_scroll_mapper>,
                       <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                       <&zip_scroll_scaler 1 24>,
                       <&zip_raw_touch_idle_filter>;
};

&raw_touch_pad {
    /* Firmware suppresses taps for scroll-context touches, so tap-click
     * would have no effect on a pad that always scrolls. */
    /delete-property/ tap-click;
};
```

The pad node and includes stay as defined above. No layer
binding is needed for this pad. Check any other listener overlays too:
an overlay that replaces the base chain can change its behavior.

- In **Standard mode**, the wheel processors convert finger movement to
  horizontal and vertical wheel events.
- In **RawTouch mode**, the marker sets the scroll flag on the raw frames.
  The host generates two-axis scrolling and firmware suppresses the wheel
  motion. RawTouch's per-pad **Scroll axes** setting can restrict its
  output to one direction; it does not change Standard-mode wheel mapping.

This works independently per pad: one can always scroll while another
points or uses a scroll layer. The overlay also removes `tap-click` from
the dedicated pad's node: firmware suppresses taps for scroll-context
touches, so the property would have no effect. Keyboard mouse-button
bindings can still provide clicks.

### 4. Build and flash

Set `CONFIG_ZMK_POINTING=y` in your configuration. An enabled
`zmk,raw-touch-pad` node then enables the module automatically. Available
USB and BLE transports default to enabled on the central half.

The module uses USB interface `HID_1` alongside ZMK's `HID_0`, and defaults
`CONFIG_USB_HID_DEVICE_COUNT` to 2. An explicit lower value fails the
build. Another module that also takes `HID_1` needs integration work;
increasing the count alone does not assign it a different interface.

Build with your usual ZMK workflow and flash the resulting firmware.
For initial split-keyboard setup, build and flash both halves together;
see [Split keyboards](#split-keyboards) before doing so.

### 5. Check both modes

1. With no host app running, check wheel scrolling on the dedicated pad
   or while your scroll layer is active. Also check pointer movement and
   taps where configured. This is Standard mode.
2. Install [RawTouch](https://github.com/kalakris/rawtouch),
   then grant it Accessibility.
3. Drag vertically, horizontally, and diagonally in a view that scrolls
   both ways, activating the scroll layer if configured. Flick and lift
   to check momentum; touch down again in scroll mode to stop it.
4. Quit RawTouch normally and verify Standard-mode scrolling resumes.
5. Test USB and Bluetooth separately. After the first firmware install,
   Bluetooth may need a fresh pairing because the HID services changed.

The firmware also works in Standard mode on hosts without RawTouch.
RawTouch-mode scrolling on another operating system needs a host
implementation of the [protocol below](#appendix-wire-format-protocol-v4).

You can check device discovery separately from scrolling. On macOS,
inspect the HID device list:

```sh
ioreg -c IOHIDDevice -r -l
```

Look for a collection with `PrimaryUsagePage` 65280 (`0xFF00`) and
`PrimaryUsage` 1. This does not require RawTouch to be running. It confirms
that a matching HID collection is visible, not that touch samples are
being delivered. Other devices may use the same usage values.

To check mode switching, enable `CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF=y`
and run RawTouch with Accessibility permission. A successfully acquired
USB lease logs `Raw touch lease acquired by USB (timeout 30s)`. The log
also records when leases clear after a release, disconnect, or endpoint
switch, and when a lease expires without a renewal.

## Split keyboards

The central half sends reports to the host. A pad on the peripheral half
sends its input through `zmk,input-split`; the central processes that input
and sends mouse or raw-touch reports to the computer.

This example starts with a split keyboard that already relays relative
motion and taps to the central. The changes make it relay absolute touch
samples instead. The physical pad is `remote_trackpad`; the relay is
`trackpad_split`. Reuse your board's existing nodes and labels where
present. The central and peripheral roles stay the same.

### Shared definitions — unchanged

Both halves use the same relay ID (`reg = <0>` here). This identifies the
split input channel; it is independent of the raw-touch `pad-id` added
later. The listener is enabled only on the central.

<!-- example: examples/split/shared.dtsi -->
```dts
/ {
    inputs {
        #address-cells = <1>;
        #size-cells = <0>;

        trackpad_split: trackpad_split@0 {
            compatible = "zmk,input-split";
            reg = <0>;
        };
    };

    remote_trackpad_listener: remote_trackpad_listener {
        compatible = "zmk,input-listener";
        device = <&trackpad_split>;
        status = "disabled";
    };
};
```

These snippets assume that the peripheral's board definition already
configures `remote_trackpad` with its bus and data-ready GPIO. The examples
below show only the remote pad's configuration. If the central also has
the local pad example above, keep its configuration, reuse the includes
and `SCROLL_LAYER` definition, and give the two pads different IDs.

### Peripheral: send absolute samples

**Before — send relative motion and hardware taps through the split:**

<!-- example: examples/split/peripheral-before.overlay -->
```dts
&remote_trackpad {
    data-mode = "relative";
    primary-tap-enable;
};

&trackpad_split {
    device = <&remote_trackpad>;
};

&remote_trackpad_listener {
    status = "disabled";
};
```

**Changes on the peripheral:**

<!-- example-diff: peripheral.overlay examples/split/peripheral-before.overlay examples/split/peripheral.overlay -->
```diff
--- peripheral.overlay (before)
+++ peripheral.overlay (with raw touch)
@@ -1,10 +1,22 @@
 &remote_trackpad {
-    data-mode = "relative";
-    primary-tap-enable;
+    data-mode = "absolute";
+    idle-packets-count = <3>;
+    /delete-property/ primary-tap-enable;
+};
+
+/ {
+    input_processors {
+        zip_raw_touch_split_stamp: zip_raw_touch_split_stamp {
+            compatible = "zmk,input-processor-raw-touch-split-stamp";
+            #input-processor-cells = <1>;
+        };
+    };
 };
 
 &trackpad_split {
     device = <&remote_trackpad>;
+    /* The cell is the relay's own reg (trackpad_split@0 -> 0). */
+    input-processors = <&zip_raw_touch_split_stamp 0>;
 };
 
 &remote_trackpad_listener {
```

The relay keeps its device and the disabled listener stays as it was. The
peripheral now sends unprocessed X/Y/Z samples, including lift-off.
Do not add a `zmk,raw-touch-pad` node or derive pointer motion on this
half: the central does that. If your existing driver or split relay
transforms or filters the input, remove those transformations here and
apply the mounting on the central's raw-touch node instead.

**The stamp processor on the relay sends each frame with the peripheral's
own sample time.** The module's frame handler runs on the central, so a
relayed pad's frames would otherwise be timestamped after the split hop,
with the link's delivery jitter in the timestamp the host derives velocity
from (up to one poll cycle on a polled wired link — see
[Wired split timing](#wired-split-timing)). Chain it on the relay, on the
peripheral only; its cell is the relay's own `reg`. The processor sends one
extra input event per frame through the relay, just ahead of the frame's
sync; the central's frame handler uses its value in place of its own clock
and needs no configuration. The stamp itself changes nothing on the wire
to the host, and it is in the peripheral's clock domain, which is fine for
hosts that follow the protocol (one timeline per pad, see the
[appendix](#input-report)). Both halves must run a build that includes the
module.

The same processor also announces the peripheral's own module version,
ahead of the first frame of each touch. The central publishes it in that
pad's feature-report slot, so a host reading the feature report sees the
build each half runs and not just the central's (see
[Versioning](#versioning) and the [appendix](#feature-report)). That slot
reads 0, for unknown, until the first touch after boot.

### Central: process the relayed touch samples

**Before — turn relayed motion and taps into pointer, click, and wheel
reports:**

<!-- example: examples/split/central-before.overlay -->
```dts
#include <input/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/input_transform.h>

#define SCROLL_LAYER 2

&trackpad_split {
    /delete-property/ device;
};

/ {
    input_processors {
        remote_tap_to_click: remote_tap_to_click {
            compatible = "zmk,input-processor-code-mapper";
            #input-processor-cells = <0>;
            type = <INPUT_EV_KEY>;
            map = <INPUT_BTN_TOUCH INPUT_BTN_0>;
        };
    };
};

&remote_trackpad_listener {
    status = "okay";
    device = <&trackpad_split>;
    input-processors = <&remote_tap_to_click>,
                       <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
                       <&zip_xy_scaler 1 1>;

    scroll {
        layers = <SCROLL_LAYER>;
        input-processors = <&remote_tap_to_click>,
                           <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
                           <&zip_xy_to_scroll_mapper>,
                           <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                           <&zip_scroll_scaler 1 8>;
    };
};
```

The receiver has no `device` property on `trackpad_split`; the split link
supplies its events. The listener uses that relay as its input device.
The button mapper and orientation work as in the local-pad example.

**Changes on the central:**

<!-- example-diff: central.overlay examples/split/central-before.overlay examples/split/central.overlay -->
```diff
--- central.overlay (before)
+++ central.overlay (with raw touch)
@@ -1,4 +1,5 @@
 #include <input/processors.dtsi>
+#include <raw_touch/processors.dtsi>
 #include <zephyr/dt-bindings/input/input-event-codes.h>
 #include <dt-bindings/zmk/input_transform.h>
 
@@ -9,12 +10,19 @@
 };
 
 / {
+    raw_touch_remote: raw_touch_remote {
+        compatible = "zmk,raw-touch-pad";
+        device = <&trackpad_split>;
+        pad-id = <1>; /* Local pad uses 0. */
+        rotate-90;
+        y-invert;
+        tap-click;
+    };
+
     input_processors {
-        remote_tap_to_click: remote_tap_to_click {
-            compatible = "zmk,input-processor-code-mapper";
+        zip_raw_touch_idle_filter_remote: zip_raw_touch_idle_filter_remote {
+            compatible = "zmk,input-processor-raw-touch-idle-filter";
             #input-processor-cells = <0>;
-            type = <INPUT_EV_KEY>;
-            map = <INPUT_BTN_TOUCH INPUT_BTN_0>;
         };
     };
 };
@@ -22,16 +30,15 @@
 &remote_trackpad_listener {
     status = "okay";
     device = <&trackpad_split>;
-    input-processors = <&remote_tap_to_click>,
-                       <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
-                       <&zip_xy_scaler 1 1>;
+    input-processors = <&zip_xy_scaler 1 1>,
+                       <&zip_raw_touch_idle_filter_remote>;
 
     scroll {
         layers = <SCROLL_LAYER>;
-        input-processors = <&remote_tap_to_click>,
-                           <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)>,
+        input-processors = <&zip_raw_touch_scroll>,
                            <&zip_xy_to_scroll_mapper>,
                            <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
-                           <&zip_scroll_scaler 1 8>;
+                           <&zip_scroll_scaler 1 24>,
+                           <&zip_raw_touch_idle_filter_remote>;
     };
 };
```

The new raw-touch node points to **the relay**, not to the physical pad on
the other half. It takes over tap detection and orientation, while the
listener's scroll marker selects when the host should scroll. The wheel
chain still provides Standard-mode scrolling.

The remote listener gets its own idle-filter instance. It can share the
scroll marker with the local listener, but not that listener's idle filter.

<details>
<summary>Complete peripheral and central configurations after the changes</summary>

**Peripheral overlay:**

<!-- example: examples/split/peripheral.overlay -->
```dts
&remote_trackpad {
    data-mode = "absolute";
    idle-packets-count = <3>;
    /delete-property/ primary-tap-enable;
};

/ {
    input_processors {
        zip_raw_touch_split_stamp: zip_raw_touch_split_stamp {
            compatible = "zmk,input-processor-raw-touch-split-stamp";
            #input-processor-cells = <1>;
        };
    };
};

&trackpad_split {
    device = <&remote_trackpad>;
    /* The cell is the relay's own reg (trackpad_split@0 -> 0). */
    input-processors = <&zip_raw_touch_split_stamp 0>;
};

&remote_trackpad_listener {
    status = "disabled";
};
```

**Central overlay:**

<!-- example: examples/split/central.overlay -->
```dts
#include <input/processors.dtsi>
#include <raw_touch/processors.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/zmk/input_transform.h>

#define SCROLL_LAYER 2

&trackpad_split {
    /delete-property/ device;
};

/ {
    raw_touch_remote: raw_touch_remote {
        compatible = "zmk,raw-touch-pad";
        device = <&trackpad_split>;
        pad-id = <1>; /* Local pad uses 0. */
        rotate-90;
        y-invert;
        tap-click;
    };

    input_processors {
        zip_raw_touch_idle_filter_remote: zip_raw_touch_idle_filter_remote {
            compatible = "zmk,input-processor-raw-touch-idle-filter";
            #input-processor-cells = <0>;
        };
    };
};

&remote_trackpad_listener {
    status = "okay";
    device = <&trackpad_split>;
    input-processors = <&zip_xy_scaler 1 1>,
                       <&zip_raw_touch_idle_filter_remote>;

    scroll {
        layers = <SCROLL_LAYER>;
        input-processors = <&zip_raw_touch_scroll>,
                           <&zip_xy_to_scroll_mapper>,
                           <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                           <&zip_scroll_scaler 1 24>,
                           <&zip_raw_touch_idle_filter_remote>;
    };
};
```

</details>

Build and flash both halves for this conversion: the peripheral changes
what it sends and the central changes how it handles that input. These
files live under [`examples/split/`](examples/split/); CI compiles the
peripheral one on the Go60's left half and the central one on its right.

For a dedicated remote scrolling pad, move its scroll processors into its
base chain as in the [dedicated scrolling example](#dedicated-scrolling-pad),
keeping `zip_raw_touch_idle_filter_remote` as its filter. Remove `tap-click`
from `raw_touch_remote`, since touches used for scrolling do not generate
clicks.

### Wired split timing

The tested Go60 uses a polled, half-duplex wired link: the peripheral
queues events until the central asks for them. With the stock timings,
active polling was measured at roughly 22.5 ms, so the pad's roughly
10 ms samples arrived in bursts of two or three. Pointer and key events
also wait for the next poll.

The module's USB and BLE queues can absorb these bursts, but cannot
remove time already spent waiting on the split link. The following
central-side settings reduced the measured poll cycle to roughly 5 ms.
In the tested wired-split/USB setup, samples then arrived individually
at about 10 ms intervals.

Add these settings to the central half's `.conf` file in your ZMK config
repository—for example, `config/go60_rh.conf` when the Go60's right half
is the central:

```conf
CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT=3
CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_TIMEOUT=5
```

Changing only these timing settings requires rebuilding and reflashing
only the central. Treat the values as Go60 tuning, not defaults for every
split keyboard. More frequent polling can increase power use on both
halves; that cost has not been measured. Check typing as well as trackpad
input after changing them.

A wireless split has different timing and batching behavior and needs its
own checks. A queue on the central can buffer samples it has received;
it cannot recover events lost before they arrive or remove split-link
delay. See [Delivery and lost releases](#delivery-and-lost-releases) for
the limits of the module's USB and BLE queues.

The tested two-pad build also uses `CONFIG_INPUT_QUEUE_MAX_MSGS=64` and
`CONFIG_INPUT_THREAD_STACK_SIZE=2048` for the input queue and processor
chains.

The module timestamps frames where it processes them. For a relayed pad,
that is on the central, after the split hop — unless the peripheral runs
the stamp processor (see [Peripheral: send absolute
samples](#peripheral-send-absolute-samples)), in which case the frame
carries the peripheral's own sample time and the split link's batching
no longer shows in the timestamps at all. Measured on the Go60 without
the stamp: the relayed pad's inter-frame timestamp spacing was bimodal
at 0.3 / 22.8 ms with stock polling and still varied by up to 5 ms with
the settings above, against a steady 10 ms for the central's own pad.
With the stamp, the poll cadence only decides delivery latency (frames
still arrive in bursts, one poll cycle late at worst) and peripheral
power, not velocity accuracy. Wireless splits need their own timing and
reliability checks.

## Configuration reference

### Pad properties

Each `zmk,raw-touch-pad` node describes one input device. See the
[binding](dts/bindings/input/zmk,raw-touch-pad.yaml) for full definitions.

| Property | Default | Meaning |
|---|---|---|
| `device` | required | Physical absolute-input device, or a split relay on the central. |
| `pad-id` | `0` | Unique ID from 0 to 7. Give every pad a different ID. |
| `x-max` / `y-max` | `2047` / `1535` | Maximum raw coordinates; set these for your sensor. |
| `resolution` | `38` | Sensor counts/mm; `0` means unknown. |
| `rotate-90` | absent | Swap axes when deriving pointer motion; advertise the mounting to the host. |
| `x-invert` / `y-invert` | absent | Invert the corresponding pointer axis after the swap; advertise the flags to the host. |
| `tap-click` | absent | Enable firmware tap-to-click. |
| `tap-max-ms` | `180` | Maximum touch duration counted as a tap. |
| `tap-max-movement` | `30` | Maximum displacement from touch-down on either raw axis, in counts. |

Geometry defaults describe a Cirque Pinnacle. Override them for other
hardware. Frames carry raw coordinates; orientation flags describe how
to interpret them.

Pad IDs need only be unique within one keyboard. RawTouch keeps different
keyboards separate; see its [per-keyboard overrides](https://github.com/kalakris/rawtouch#per-keyboard-overrides)
if you need different settings for each.

### Kconfig

All names below have the `CONFIG_` prefix in a `.conf` file.

| Symbol | Default | Purpose |
|---|---|---|
| `ZMK_RAW_TOUCH` | `y` when a pad node and pointing support exist | Enable the module. |
| `ZMK_RAW_TOUCH_USB` | `y` when USB/central dependencies allow | Raw reports over the second USB HID interface. |
| `ZMK_RAW_TOUCH_BLE` | `y` when BLE/central dependencies allow | Raw reports over the second Bluetooth HID service. |
| `ZMK_RAW_TOUCH_USB_QUEUE_SIZE` | `4` | Buffered USB frames; range 2–255. |
| `ZMK_RAW_TOUCH_BLE_QUEUE_SIZE` | `8` | Buffered BLE frames; range 2–255. |
| `ZMK_RAW_TOUCH_BLE_THREAD_STACK_SIZE` | `ZMK_BLE_THREAD_STACK_SIZE` | Stack for the module's BLE work queue. |
| `ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES` | `4` | Input devices tracked by the scroll marker. Increase when using more marked devices. |
| `ZMK_RAW_TOUCH_LOG_LEVEL_*` | inherited | Module logging level. |

The marker and idle-filter processors enable automatically when their
nodes are used. Queue sizes count frames across all pads, not per pad.
Larger queues can absorb bursts but also retain older input longer.
Disabling only `ZMK_RAW_TOUCH_BLE` leaves Standard-mode pointing and wheel
scrolling available over Bluetooth while retaining raw reports over USB.

## Troubleshooting

**USB works; Bluetooth raw scrolling does not.** Forget the keyboard on
the host, clear the corresponding keyboard bond with `&bt BT_CLR`, then
pair again. Do this after adding the second HID service or changing the
HID report/GATT layout, including adding or removing a streaming pad and
upgrading across a protocol version that changes a report body.
macOS can retain a stale report map even when typing and feature-report
reads still work. If a fresh pairing still fails, disconnect and
reconnect once before investigating further. A change only to feature
characteristic write permission does not itself change the report map.

**Pointer works; RawTouch scrolling does not.** Check the host's
Accessibility permission and master switch, then confirm that the active
listener chain contains the scroll marker. Confirm that the physical
driver emits absolute events and release samples.

**Tap-to-click does nothing.** Enable `tap-click`, check release samples,
and make sure the listener does not consume the injected `INPUT_BTN_0`.
Taps are deliberately suppressed for touches that entered scroll context.

**A pad is dead even in Standard mode, while keys work.** Check the
driver, GPIOs, and split relay before the host app. Try power-cycling the
affected half. If it recurs, report the driver revision, how long the half
had been running, and whether a power cycle restored it.

**Scrolling stops after the host app is force-quit.** Relaunch the app
or wait up to 30 seconds for Standard mode to return. A normal quit
returns to Standard mode promptly.

For connection diagnostics, enable `CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF=y`.
Logs distinguish lease acquisitions, releases, disconnects, endpoint
switches, and expiry. Report the board, ZMK/Zephyr and module revisions,
driver revision, transport, and whether Standard mode works. Battery impact, other boards,
and deep sleep/wake still need broader testing.

## Implementation and contributions

Sources compile into ZMK's `app` target to use its private headers. The
USB and BLE code registers a separate vendor collection using transports
derived from ZMK's HID implementation. No ZMK core files are modified.

Useful contributions include tested board configurations, ports to newer
ZMK/Zephyr transports, and host implementations for other operating
systems. Include the exact firmware tree and hardware used when reporting
a working configuration. Wire-format changes need coordinated firmware
and host work, and can require Bluetooth re-pairing.

## Credits and license

[MIT](LICENSE), including the protocol documentation below. The transport
code derives from ZMK. The module approach follows
[`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) and
[`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io).
The pad integration builds on Zephyr's Pinnacle driver and Peter
Johanson's Cirque work. The macOS companion credits its
[LinearMouse](https://github.com/linearmouse/linearmouse) origins separately.

## Appendix: wire format (protocol v4)

This appendix defines the reports for host implementers. The current
firmware sends one contact per pad; the presence of a contact ID field
does not imply implemented multi-touch support.

### Collection and report framing

| Field | Value |
|---|---|
| Usage page | `0xFF00` |
| Usage | `0x01` |
| Report ID | `0x04` for input and feature reports |
| USB | Separate HID interface, currently `HID_1` |
| Bluetooth | Separate HID-over-GATT service instance |

The vendor usage pair is not unique to this project. A host **must read
and validate the feature report before interpreting input or acquiring
a lease on the device**: the four magic bytes, the protocol version, and the body
length must all match before a device is treated as this protocol, and a
device that fails the check must never be sent a lease command.
Discovery is protocol identification, not authentication.

All lengths and offsets below refer to report bodies. USB transfers
include a leading report ID; BLE uses the report-reference descriptor
and carries the body alone. Host HID APIs may expose the ID separately
or retain it in a returned buffer, so normalize framing before parsing.
In particular, macOS feature GETs have been observed ID-prefixed on USB
and bare on BLE.

### Input report

The body is **11 bytes**. All multi-byte integers are little-endian.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | `pad_id`, 0–7 |
| 1 | 1 | `contact_id`, currently always 0 |
| 2 | 2 | `x`, unsigned raw coordinate |
| 4 | 2 | `y`, unsigned raw coordinate |
| 6 | 1 | `z`, touch strength; not a calibrated force measurement |
| 7 | 1 | `flags` |
| 8 | 1 | `seq`, per-pad counter incremented for each emitted report, wrapping modulo 256 |
| 9 | 2 | `timestamp`, device-side time in 100 µs units, wrapping every 6.5536 seconds |

Flag bits:

| Bit | Name | Meaning |
|---|---|---|
| 0 | `touched` | A contact is present. Clear means release. |
| 1 | `scroll_mode` | The pad's events reached a processor chain marked for scrolling. |
| 2 | `lease_held` | The selected sending endpoint held a live host lease when the report was produced. |
| 3–7 | reserved | Sent as zero. |

While a lease is held, firmware emits reports for active samples (about 100 Hz
with the tested pads), including pointer-context samples, and one report
on lift-off. It suppresses repeated idle reports. A normal release has
`touched` clear and X/Y/Z zero; hosts **must use the flag**, not the
coordinate values, to determine contact state.

If the lease lapses mid-touch, firmware attempts one trailing release
with `touched` and `lease_held` clear, X/Y/Z zero, and `scroll_mode`
reflecting that frame's context. It then stops sending until a lease is
acquired again. That trailing release may not reach the previous host after an
endpoint switch or disconnection.

Hosts **must generate scrolling only while both `scroll_mode` and
`lease_held` are set**. They must still handle transitions out of those
states: losing scroll context ends the drag, and a trailing release
without the lease bit cancels it **without momentum**, because firmware scrolling has
resumed. Do not simply discard these transitions and leave a gesture open.

Use the device timestamp for velocity estimation, with wrap handling;
USB/BLE arrival times can be batched. The timestamp is taken during
module processing, not read from the sensor. On a split relay it is
taken after events reach the central, unless the peripheral runs the
module's stamp processor, in which case it is the peripheral's own
clock. Pads may therefore be in different clock domains: hosts MUST keep
one timeline per pad and MUST NOT compare timestamps across pads (the
timestamp field itself is unchanged either way). Sequence gaps indicate
reports missing from the delivered sequence; they do not measure events
lost before the module produced a report. Reconnects and endpoint changes
also require resetting or reconciling host timing state.

### Feature report

Read with USB `GET_REPORT(FEATURE)` or the BLE feature characteristic
(report-reference type `0x03`). Its body is **16 + 8 × N bytes**, for
1–8 configured pads. A two-pad build is 32 bytes, or 33 including the
USB report ID. Hosts must support variable pad counts and must not
hard-code the two-pad length.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | `protocol_version`, 4 |
| 1 | 1 | `pads_present`, bit `p` set for pad ID `p` |
| 2 | 1 | `capabilities`, bit 0 = host lease supported; other bits reserved |
| 3 | 1 | `module_version` of the half answering, packed major.minor; 0 = unknown |
| 4 | 4 | `magic`, the ASCII bytes `R` `A` `W` `T` (`52 41 57 54`) |
| 8 | 8 | `device_id`, the SoC's hardware identifier; all-zero = unknown |
| 16 + 8 × i | 8 | Geometry slot `i` |

The magic is fixed and is what separates this protocol from anything else
that happens to sit on `0xFF00`/`0x01`. `protocol_version` stays at byte 0
in every version of the protocol, so a host can read it before it decides
which layout to parse. **Reject** a body whose magic differs, whose
protocol version is not 4, or whose length is not `16 + 8 × N`, and do
not write a lease command to it.

`device_id` is the value Zephyr's `hwinfo_get_device_id()` returns for the
SoC — the FICR `DEVICEID` pair on an nRF52 — in the byte order hwinfo
returns it, zero-padded if the SoC reports fewer than eight bytes and
truncated if it reports more. It is stable for the life of the chip and
identical on every transport, which is what lets a host recognize that the
keyboard it sees over USB and the one it sees over Bluetooth are the same
keyboard; nothing else in either transport exposes a shared identifier.
All-zero means the firmware could not read one, which hosts must tolerate
along with every other value: **never reject or admit a device over this
field**. It is readable by any host that can read the feature report —
over Bluetooth, one that is bonded — so treat it as an identifier, not a
secret, and do not authorize anything on the strength of it. Note that a
board may already derive its USB serial number from the same hwinfo id
(the MoErgo Go60 does), in which case the two agree by construction.

For a valid firmware configuration, slots describe the present pads in
ascending pad-ID order. IDs need not be contiguous: a mask naming pads
0 and 3 has two slots, for pads 0 and 3. IDs must be unique and less
than 8; configurations exceeding that are not supported.

Recover the number of complete slots from the normalized body length as
`(len - 16) / 8` and read no more than `min(N, popcount(pads_present))`
slots. Valid bodies have the `16 + 8 × N` shape; a host that requires
exactly 32 bytes will refuse a one-pad or three-pad keyboard using the
same protocol.

Each slot contains:

| Slot offset | Bytes | Field |
|---|---|---|
| +0 | 1 | `resolution`, counts/mm; 0 = unknown |
| +1 | 1 | `orientation`: bit 0 swaps X/Y (`rotate-90`), bit 1 inverts X, bit 2 inverts Y |
| +2 | 2 | `x_max`, unsigned little-endian |
| +4 | 2 | `y_max`, unsigned little-endian |
| +6 | 1 | `max_contacts`, currently 1 |
| +7 | 1 | `module_version` of the half that owns the pad, packed; 0 = unknown |

Orientation applies after reading raw coordinates: swap axes first,
then apply axis inversions. The HID descriptor also declares coordinate
ranges from one pad node; use the per-pad feature slots when pads differ.

Both module versions pack major in the high nibble and minor in the low
one: `0x01` is 0.1 and `0x10` is 1.0. Byte 3 is the half that answers the
report, the central. A slot's byte 7 is the half that owns that pad, which
for a pad relayed from a split peripheral is the peripheral — 0 until it
announces its version, which it does with the first frame of a touch.
Treat both as diagnostic: `protocol_version` is the compatibility contract.

The configured pad count determines the descriptor's feature-report
length. Adding or removing a pad — or upgrading across a protocol version
that changes the body — therefore changes the report map and requires a
fresh Bluetooth pairing on hosts that cache it, including macOS.

### Host lease

Check `capabilities` bit 0 before writing. The feature report accepts this
**4-byte command body** through USB `SET_REPORT(FEATURE)` or a BLE
feature-characteristic write:

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | Command `0x01` |
| 1 | 1 | `0x01` acquire or renew; `0x00` release |
| 2 | 1 | Lease timeout in seconds; nonzero to acquire or renew, clamped to 5–120; ignored for release |
| 3 | 1 | Reserved, must be zero |

For example, `01 01 1e 00` requests 30 seconds and `01 00 00 00`
releases. USB accepts a body with or without its leading report ID;
BLE writes carry only the body. Invalid USB requests are stalled. BLE
rejects a wrong length with `Invalid Attribute Value Length`, invalid
fields with `Value Not Allowed`, nonzero offsets with `Invalid Offset`,
and writes from a peer outside the bonded host profiles with
`Write Not Permitted`.

A lease belongs to the USB endpoint or the specific BLE profile that
wrote it. It suppresses wheel motion only while that endpoint is
selected. It covers all configured pads on that endpoint, not an
individual pad. Pointer-context relative motion, taps, and key reports
continue in both modes.

Hosts must renew at intervals no longer than half the effective lease,
and should release on a clean exit. Release is safe when no lease is
held. A lease lapses on expiry, USB detach/reset, disconnection of its
BLE profile, or an endpoint switch away from it, and ends on explicit
release. A lease already held by the newly selected endpoint can take
effect when switching to it. Switching back does not restore a lapsed
lease; the host must acquire it again.

Hosts must re-acquire leases after resume, reconnect, and device
re-enumeration. They should acquire a lease only when ready to generate
scroll events. RawTouch leaves the lease released when disabled or when
Accessibility permission is unavailable.

### Delivery and lost releases

Both transports use bounded queues shared by all pads. When full, a queue
evicts the oldest eligible motion frame, preserving queued releases and
the head currently being transmitted. If no entry is eligible, the
incoming report is dropped, even if it is a release. Queues preserve
order among retained reports; a new touch does not overtake an earlier
queued release.

- **USB:** transfer completion drains the queue. A busy interrupt
  endpoint can defer a release without immediately losing it. Bus reset,
  detach, and transport errors can flush pending reports.
- **BLE:** failed release notifications are retried at the head of the
  queue, 8 ms apart, for at most four attempts. Queued entries are bound
  to the profile selected at enqueue time. Endpoint switches flush the
  queue; disconnects discard entries for that profile. Old reports are
  not forwarded to a different host.

These measures protect releases during ordinary congestion; they do not
guarantee delivery. Link loss, power loss, endpoint changes, exhausted
retries, or a queue with no evictable entry can still lose a release.

**A host must close a contact after roughly 150 ms of report silence if
its last state was touched.** This watchdog is required on both
transports. Device removal and a trailing release without the lease bit
should also close the current gesture. Absolute coordinates allow motion to
continue after a missing sample, but do not make dropped input or
transport latency irrelevant.
