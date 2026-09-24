# zmk-raw-touch

> **Early release.** Interfaces may still change; issues and PRs welcome.

A ZMK module that sends a keyboard trackpad's absolute position and contact
state to a host application over USB or Bluetooth. The macOS companion,
[RawTouch](https://github.com/kalakris/rawtouch), turns those reports into
native macOS scroll gestures: vertical and horizontal scrolling, lift-off
momentum, and touch-to-stop.

The module also derives ordinary pointer movement and optional tap-to-click
from the same input, and your wheel processors keep scrolling working when
no host app is running. It adds its own HID interface and Bluetooth HID
service without patching ZMK core.

[Compatibility](#compatibility) · [Setup](#setup) ·
[Split keyboards](#split-keyboards) · [Configuration](#configuration-reference) ·
[Troubleshooting](#troubleshooting) · [Protocol](docs/protocol.md)

## What it does

Your keymap decides when a pad scrolls. Put the wheel processors in the
base chain for a [dedicated scrolling pad](#dedicated-scrolling-pad), or
in a layer overlay to switch between pointing and scrolling. Activate
that layer however your keymap defines it: for example, by holding or
toggling a key.

- **RawTouch mode:** the module sends touch samples to the host, including
  finger position, contact state, timing, and whether the active processor
  chain is marked for scrolling. RawTouch uses these samples to generate
  scroll gestures and lift-off momentum on both axes. Touching a pad in
  scroll mode stops momentum. The module suppresses its own wheel motion
  for those touches; configured pointer movement and taps stay in
  firmware.
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

## Compatibility

Tested on a MoErgo Go60 with two Cirque Pinnacle pads, running MoErgo's
ZMK distribution (ZMK 0.3 / Zephyr 3.5), with RawTouch on macOS 26 over
USB and Bluetooth.

| Component | Tested revision |
|---|---|
| `moergo-sc/zmk` | Release `v25.11` (the [Go60 starter configuration](https://github.com/kalakris/go60-rawtouch-config)), and `57a7b8e0` on the `go60-zmk0.3.0` branch (this repository's CI) |
| `kalakris/cirque-input-module`, `intree-driver` branch | `89a08962f1c0bde4b499badd68cb068b8a369000` |

That driver module packages Zephyr's Pinnacle driver for the older tree,
with three patches for status handling, edge sensitivity, and calibration
on initialization.

Other boards and drivers need integration and testing. Upstream ZMK
`main` and Zephyr 4.1 have not been verified here. The USB implementation
uses Zephyr's legacy USB stack; `USB_DEVICE_STACK_NEXT` requires a port.
Check this before adding the module to an existing build.

Test reports from other keyboards, trackpads, and host setups are welcome
and will help expand this list. Please include your hardware, firmware
and OS versions, connection type, and what worked or failed in an
[issue](https://github.com/kalakris/zmk-raw-touch/issues).

### Versioning

Releases are tagged `v0.y.z`, starting at `v0.1.0`. The module is pre-1.0,
so a minor release may change interfaces. The moving tag `stable` always
points at the newest stable release.
[`include/zmk/raw_touch/version.h`](include/zmk/raw_touch/version.h) holds
the number: the firmware reports it to the host, and CI rejects a `v*` tag
that disagrees with it.

The protocol version is the only compatibility contract with a host; the
module version is diagnostic. RawTouch's
[compatibility table](https://github.com/kalakris/rawtouch#firmware-compatibility)
lists which app versions speak which protocol.

Changes to the HID report map, including adding or removing a pad,
require a fresh Bluetooth pairing on hosts that cache the map, including
macOS (see [Troubleshooting](#troubleshooting)). Release notes say when
an update does this.

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

**Go60 users:** the
[Go60 RawTouch configuration](https://github.com/kalakris/go60-rawtouch-config)
provides a stock keymap with RawTouch already configured. Fork it and
build with GitHub Actions, or use its adoption guide to add the changes
to an existing Go60 configuration.

For manual integration, you need an existing ZMK configuration that
builds for your board, a compatible absolute-mode driver, and
`CONFIG_ZMK_POINTING=y`. Decide how each pad should scroll: on a layer
(the walkthrough below), or always, as a
[dedicated scrolling pad](#dedicated-scrolling-pad) (a small change at
the end of step 3).

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
      revision: v0.1.0
```

Use a numbered tag from the
[releases](https://github.com/kalakris/zmk-raw-touch/releases) for a
reproducible build that changes only when you edit it, or `stable` to
follow new releases as they are published. Avoid `main`: it carries
unreleased work. GitHub Actions fetches `stable` afresh on every build; a
local west workspace keeps its old copy until `west update --fetch=always`.

On the tested Zephyr 3.5 configuration, also add or override the driver
project. Replace `<driver-release-tag>` with the driver tag listed in the
chosen module release's notes:

```yaml
    - name: cirque-input-module
      remote: kalakris
      revision: <driver-release-tag>
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
   the driver's relative-mode tap setting and button mapper (see *Why
   the module handles taps* below). Do not apply orientation twice;
   remove driver-level `invert-x`, `invert-y`, or `swap-xy` if present and
   describe the mounting on the raw-touch pad node instead.
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

If a hold-tap key activates the scroll layer, check its undecided window:
trackpad input does not resolve the key's hold-tap decision, so a quick
flick can start before the layer activates. `hold-while-undecided` makes
the layer active while the decision is pending; choose it deliberately
for your keymap.

#### Dedicated scrolling pad

To make the pad scroll without holding a key, put the marker and wheel
processors in its base chain. For the example above, this overlay removes
the `scroll` child, replaces the pointer chain, and keeps taps working:

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
    /* Every touch on this pad is scroll context, and the module drops
     * taps from scroll-context touches unless this is set. */
    tap-click-while-scrolling;
};
```

The pad node and includes stay as defined above. No layer binding is
needed for this pad. Check any other listener overlays too: an overlay
that replaces the base chain can change its behavior.

- In **Standard mode**, the wheel processors convert finger movement to
  horizontal and vertical wheel events.
- In **RawTouch mode**, the marker sets the scroll flag on the raw frames.
  The host generates two-axis scrolling and firmware suppresses the wheel
  motion. RawTouch's per-pad **Scroll axes** setting can restrict its
  output to one direction; it does not change Standard-mode wheel mapping.

This works independently per pad: one can always scroll while another
points or uses a scroll layer.

**Taps on a scrolling pad.** The module normally drops taps from touches
that were used for scrolling, which on a dedicated pad is every touch.
`tap-click-while-scrolling` lifts that for one pad, so its taps reach the
listener chain, which decides what they do. A tap left-clicks by default;
MoErgo's stock Go60 keymap, for example, turns the left pad's taps into
right clicks with a `zmk,input-processor-code-mapper` from `INPUT_BTN_0`
to `INPUT_BTN_1`. Keep such a mapper before the idle filter. Delete the
property instead if the pad should not click at all.

In Standard mode such a tap is emitted at lift-off like any other. In
RawTouch mode the firmware cannot tell a tap from a short touch that only
stopped the host's momentum, so it holds the tap for up to 250 ms and
emits it when the host confirms it. RawTouch confirms taps that did not
stop momentum and stayed within its own limits: 300 ms and 60 counts of
movement from touch-down on either raw axis. These host limits are fixed,
with no app setting; the firmware's `tap-max-ms` and `tap-max-movement`
still apply, so raising them beyond the host limits allows taps in
Standard mode that RawTouch mode rejects. See
[Tap confirm](docs/protocol.md#tap-confirm) for the protocol details.

<details>
<summary>Why the module handles taps</summary>

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
those touches unless the pad sets `tap-click-while-scrolling`. This keeps
tap behavior consistent in Standard and RawTouch modes, including when
the host app is not running.

The module emits `INPUT_BTN_0` directly, so the old `tap_to_click` mapper
is no longer needed. Remove processors that discard that button, such as
a `zip_button_behaviors` instance mapping it to `&none`.

</details>

### 4. Build and flash

Set `CONFIG_ZMK_POINTING=y` in your configuration. An enabled
`zmk,raw-touch-pad` node then enables the module automatically. Available
USB and BLE transports default to enabled on the central half.

With two or more pads, also give the input subsystem headroom. The stock
16-message input queue drops events when full, and the stock 512-byte
input thread stack is thin for long processor chains. The tested two-pad
build uses:

```conf
CONFIG_INPUT_QUEUE_MAX_MSGS=64
CONFIG_INPUT_THREAD_STACK_SIZE=2048
```

The module uses USB interface `HID_1` alongside ZMK's `HID_0`, and defaults
`CONFIG_USB_HID_DEVICE_COUNT` to 2. An explicit lower value fails the
build. Another module that also takes `HID_1` needs integration work;
increasing the count alone does not assign it a different interface.

Build with your usual ZMK workflow and flash the resulting firmware.
For a split keyboard, see [Split keyboards](#split-keyboards) first and
flash both halves from the same build.

### 5. Check both modes

1. With no host app running, check wheel scrolling on the dedicated pad
   or while your scroll layer is active, and pointer movement and taps
   where configured. This is Standard mode. Check USB and Bluetooth
   separately: after the first install, Bluetooth hosts need a
   [fresh pairing](#troubleshooting) because the HID services changed.
2. Install RawTouch and follow its
   [Get started](https://github.com/kalakris/rawtouch#get-started) steps,
   which end with a first RawTouch-mode scroll.
3. Quit RawTouch normally and check that Standard-mode scrolling resumes.

The firmware also works in Standard mode on hosts without RawTouch.
RawTouch-mode scrolling on another operating system needs a host
implementation of the [protocol](docs/protocol.md).

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

**The stamp processor sends each frame with the peripheral's own sample
time.** Without it, a relayed pad's frames are timestamped on the central
after the split hop, and the link's delivery jitter lands in the
timestamps the host derives velocity from. Chain it on the relay, on the
peripheral only; its cell is the relay's own `reg`. It sends one extra
input event per frame through the relay, and the central's frame handler
uses it in place of its own clock with no configuration. Nothing changes
on the wire to the host.

The same processor also announces the peripheral's module version before
its first frame after boot, and again after any pause of at least 500 ms,
so a host can show which build each half runs. Until an announcement
arrives, that pad's version reads as unknown.

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
keeping `zip_raw_touch_idle_filter_remote` as its filter, and add
`tap-click-while-scrolling` to `raw_touch_remote` if it should keep taps.

### Wireless splits

Over a Bluetooth split link, ZMK relays each input event in its own
notification from the input thread. A stamped frame is four events (X, Y,
stamp, Z), and with the stock three transmit buffers the fourth blocks the
input thread until the next connection event, which delays the next
frame's stamp. Add these to the **peripheral** half's `.conf`; they are
harmless on a wired split:

```conf
CONFIG_BT_L2CAP_TX_BUF_COUNT=8
CONFIG_BT_CONN_TX_MAX=8
CONFIG_BT_BUF_ACL_TX_COUNT=8
```

### Wired split timing

The Go60's wired split link is polled: the peripheral queues input until
the central asks for it. At stock timings a poll cycle measured about
22.5 ms, so a relayed pad's 10 ms samples arrive in bursts of two or three,
and key events wait for the next poll too. With the stamp processor the
bursts no longer affect scroll velocity, only latency. These central-side
settings shortened the cycle to about 5 ms on the Go60:

```conf
CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT=3
CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_TIMEOUT=5
```

Add them to the central half's `.conf`; changing only these needs a
reflash of the central alone. Treat them as Go60 tuning, not defaults for
every split keyboard: more frequent polling can cost power on both
halves, which has not been measured. Check typing as well as trackpad
input after changing them.

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
| `tap-click-while-scrolling` | absent | Also report taps for scroll-context touches (dedicated scrolling pads); held for host confirmation in RawTouch mode. Requires `tap-click`. |
| `tap-max-ms` | `180` | Maximum touch duration counted as a tap. |
| `tap-max-movement` | `30` | Maximum displacement from touch-down on either raw axis, in counts. |

Geometry defaults describe a Cirque Pinnacle. Override them for other
hardware. Frames carry raw coordinates; orientation flags describe how
to interpret them.

Pad IDs need only be unique within one keyboard. RawTouch keeps different
keyboards separate and can apply
[settings per keyboard](https://github.com/kalakris/rawtouch/blob/main/docs/configuration.md#per-keyboard-overrides).

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

The marker, idle-filter and stamp processors enable automatically when
their nodes are used. Queue sizes count frames across all pads, not per
pad. Larger queues can absorb bursts but also retain older input longer.
Disabling only `ZMK_RAW_TOUCH_BLE` leaves Standard-mode pointing and wheel
scrolling available over Bluetooth while retaining raw reports over USB.

## Troubleshooting

**USB works; Bluetooth raw scrolling does not.** Forget the keyboard on
the host, clear the corresponding keyboard bond with `&bt BT_CLR`, then
pair again. Do this after adding the second HID service or changing the
HID report layout, including adding or removing a streaming pad and
upgrading across a protocol version that changes a report body.
macOS can retain a stale report map even when typing and feature-report
reads still work. If a fresh pairing still fails, disconnect and
reconnect once before investigating further.

**Pointer works; RawTouch scrolling does not.** Check the host's
Accessibility permission and master switch, then confirm that the active
listener chain contains the scroll marker. Confirm that the physical
driver emits absolute events and release samples.

**Tap-to-click does nothing.** Enable `tap-click`, check release samples,
and make sure the listener does not consume the injected `INPUT_BTN_0`.
Taps are deliberately suppressed for touches that entered scroll context
unless the pad sets `tap-click-while-scrolling`; with it, a tap in
RawTouch mode needs the host's confirmation, which RawTouch sends for
taps that did not catch a coast.

**A pad is dead even in Standard mode, while keys work.** Check the
driver, GPIOs, and split relay before the host app. Try power-cycling the
affected half. If it recurs, report the driver revision, how long the half
had been running, and whether a power cycle restored it.

**A relayed pad stutters over a wireless split.** Add the peripheral's
[transmit buffer settings](#wireless-splits).

**Scrolling stops after the host app is force-quit.** Relaunch the app
or wait up to 30 seconds for Standard mode to return. A normal quit
returns to Standard mode promptly.

### Diagnostics

To check that the host can see the device, independent of RawTouch,
inspect the HID device list on macOS:

```sh
ioreg -c IOHIDDevice -r -l
```

Look for a collection with `PrimaryUsagePage` 65280 (`0xFF00`) and
`PrimaryUsage` 1. This confirms that a matching HID collection is
visible, not that touch samples are being delivered. Other devices may
use the same usage values.

To check mode switching, enable `CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF=y`
and run RawTouch with Accessibility permission. A successfully acquired
USB lease logs `Raw touch lease acquired by USB (timeout 30s)`. The log
also records lease releases, disconnects, endpoint switches, and expiry.

When reporting a problem, include the board, ZMK/Zephyr, module and
driver revisions, the transport, and whether Standard mode works. Battery
impact, other boards, and deep sleep/wake still need broader testing.

## Implementation and contributions

Sources compile into ZMK's `app` target to use its private headers. The
USB and BLE code registers a separate vendor collection using transports
derived from ZMK's HID implementation. No ZMK core files are modified.

Useful contributions include tested board configurations, ports to newer
ZMK/Zephyr transports, and host implementations for other operating
systems; see [CONTRIBUTING](CONTRIBUTING.md). Wire-format changes need
coordinated firmware and host work, and can require Bluetooth re-pairing.

## Credits and license

[MIT](LICENSE), including the [protocol specification](docs/protocol.md).
The transport code derives from ZMK. The module approach follows
[`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) and
[`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io).
The pad integration builds on Zephyr's Pinnacle driver and Peter
Johanson's Cirque work. The macOS companion credits its
[LinearMouse](https://github.com/linearmouse/linearmouse) origins separately.
