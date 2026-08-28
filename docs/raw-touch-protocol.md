# Raw touch streaming protocol (v2)

> **SUPERSEDED (2026-08-27).** The wire is now **protocol v3**, and the
> authoritative spec is the wire-format appendix of the `zmk-raw-touch`
> module README (`~/src/zmk-raw-touch/README.md`). This document is kept
> as the historical v2 spec, matching the still-flashable `raw-touch`
> fork build; the reference host accepts both versions.

A Cirque Pinnacle trackpad running ZMK streams absolute touch frames
(position + touch strength) to the host over a vendor-defined HID report,
so host software can synthesize Magic-Trackpad-quality scrolling with true
lift-off momentum. Firmware lives on branch `raw-touch` of
[kalakris/zmk](https://github.com/kalakris/zmk) (fork of
`moergo-sc/zmk:go60-zmk0.3.0`); the reference host consumer is the patched
LinearMouse fork at
[kalakris/linearmouse](https://github.com/kalakris/linearmouse) (branch
`main` since 2026-08-28). In this repo, the Go60's right-hand pad streams.

## Transport

HID vendor-defined report, **usage page `0xFF00`, usage `0x01`**, in its
own top-level application collection, so macOS exposes it as a separate
`IOHIDDevice`. Sent over both USB HID and BLE HID-over-GATT (a dedicated
input-report characteristic with a report-reference descriptor) alongside
the existing keyboard/consumer/mouse reports.

**Report ID: `0x04`** (`ZMK_HID_REPORT_ID_TOUCH_STREAM`; keyboard = 0x01,
consumer = 0x02, mouse = 0x03). Hosts should match by usage page, not ID.
Note: BLE HOG notifications carry only the payload bytes (the report is
identified by the characteristic, per HID-over-GATT); USB in-band reports
are prefixed with the report ID as usual.

## Input report (7-byte payload)

One report per Pinnacle sample while touched (~100 Hz); exactly one
release report (touched = 0, z = 0, x = y = 0) on lift-off; nothing while
idle.

Lift-off redundancy: the driver programs the Pinnacle to emit **3** Z-idle
(all-zero) packets on lift-off instead of 1, because packets are
occasionally dropped (e.g. the STATUS1 = 0xFF glitch guard). The
touch-stream module dedups repeated Z-idle frames, so still exactly one
release report reaches the wire — but a single lost packet can no longer
strand the host in a touching state.

Dual-mode hygiene: the input listener skips a mouse report at sync time
when no nonzero x/y/wheel value was accumulated and no button transition
was requested — such a report would be a host-invisible no-op (zero
relative deltas, unchanged button state). Without this, the 1:8-scaled
wheel overlay emits `REL_WHEEL` value 0 on most frames during scroll,
producing ~100 Hz empty mouse reports alongside the stream. Button
presses and releases always go out, so click/drag semantics are
unchanged. This check is general (not gated on the touch stream) and is
a candidate for upstreaming.

| byte | field  | meaning                                                           |
|------|--------|-------------------------------------------------------------------|
| 0    | pad_id | 0 = right pad (central). 1 = left pad — reserved, not implemented |
| 1-2  | x      | uint16 LE, absolute raw counts (0-2047)                           |
| 3-4  | y      | uint16 LE, absolute raw counts (0-1535)                           |
| 5    | z      | uint8 touch strength (6-bit from hardware; 0 when not touched)    |
| 6    | flags  | bit0 = touched, bit1 = scroll mode, bits 2-7 reserved (0)         |

## Feature report (8-byte payload) — NEW in v2

Same report ID `0x04`, HID type **Feature**, declared in the vendor
collection (vendor usage `0x03`), so hosts see both a vendor Input and a
vendor Feature usage on the device. Readable at any time:

- **USB:** `GET_REPORT(Feature, id 0x04)` on the control endpoint. The
  returned buffer is report-ID-prefixed as usual (9 bytes total).
- **BLE:** a HOG Report characteristic with report-reference type Feature
  (readable). Carries the 8 payload bytes only.

| byte | field            | meaning                                                    |
|------|------------------|------------------------------------------------------------|
| 0    | protocol version | `2`                                                        |
| 1    | pads present     | bitmask: bit0 = pad 0, bit1 = pad 1                        |
| 2    | resolution       | counts/mm (Cirque Pinnacle ≈ 38); 0 = unknown              |
| 3    | orientation      | bit0 = rotate-90, bit1 = x-invert, bit2 = y-invert         |
| 4-5  | x-max            | uint16 LE (2047)                                           |
| 6-7  | y-max            | uint16 LE (1535)                                           |

The orientation bits mirror the pad's devicetree transform properties
verbatim (the Go60 pads have `rotate-90` + `y-invert`). Streamed
coordinates are **raw** — hosts must apply the orientation mapping
themselves; the feature report tells them which one. x-max/y-max describe
the numeric coordinate range; the usable active area is inset from it
(roughly X 128-1920, Y 64-1472 per Cirque's docs), so expect values to
pin near those edges.

## Scroll mode (input-processor driven)

The scroll-mode flag (input report flags bit1) is controlled from the
keymap via a marker input processor,
**`&zip_touch_stream_scroll`** (`zmk,input-processor-touch-stream-scroll`,
zero cells). The processor passes events through untouched; its *presence*
in a listener chain is what matters:

> Frames from a pad carry the scroll-mode flag whenever the processor
> chain that would currently handle that pad's events contains
> `&zip_touch_stream_scroll`.

Mechanism: the touch-stream module evaluates the pad's input listener
configuration directly against current layer state on every frame
(`zmk_input_listener_touch_stream_scroll_active()` in
`input_listener.c`), mirroring the listener's own routing rules — layer
overlays are scanned in devicetree order, an overlay counts if any of its
`layers` is active, and an active overlay without `process-next` shadows
everything after it, including the base chain. Because the check is pure
layer state (not observed events), the flag is correct from the **first
frame** of a touch even when the layer was held before touch-down, and it
flips mid-touch if the layer is pressed or released while the finger is
down (no release report is generated for a flag flip).

### Configuration recipes

- **Layer-overlay scrolling** (this repo's setup): put the marker in a
  layer overlay on the pad's listener, alongside a standard wheel-mapping
  fallback chain (see "Dual mode" below):

  ```dts
  &cirque_rh_listener {
      input-processors = <&zip_xy_scaler 1 1>, <&zip_button_behaviors>,
                         <&zip_temp_layer MOUSE_LAYER MOUSE_LAYER_MS>;
      nav_scroll {
          layers = <2>;   // scroll while Nav is held
          input-processors = <&zip_touch_stream_scroll>,
                             <&zip_xy_to_vscroll_mapper>,
                             <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>,
                             <&zip_scroll_scaler 1 8>,
                             <&zip_temp_layer MOUSE_LAYER MOUSE_LAYER_MS>;
      };
  };
  ```

- **Dedicated scroll pad**: put the marker in the listener's *base*
  `input-processors` — the pad's frames are then always flagged as
  scroll.

- **No scrolling / pointer-only**: simply don't reference
  `&zip_touch_stream_scroll` anywhere. Frames stream with bit1 always
  clear (`/omit-if-no-ref/` keeps the unused node and driver out of the
  build).

Position within the chain doesn't matter (the marker never modifies
events), but note that an overlay only shadows later chains for real
events — the scroll flag honors the same shadowing.

## Dual mode: firmware fallback scrolling

Unlike v1, the firmware **never withholds** the pad's derived relative
motion: REL_X/REL_Y deltas (orientation-corrected) always flow into the
normal input-listener pipeline, scroll mode or not. A keymap's existing
xy→wheel overlay therefore produces standard wheel scrolling as a
fallback on any host with no stream consumer running. Hosts that *do*
consume the raw frames must suppress the redundant wheel (and, if they
synthesize pointing, pointer) events themselves while a scroll gesture is
active — the stream is informationally a superset, so nothing is lost.

## Firmware tap-to-click

The Pinnacle's hardware tap gesture only exists in relative mode, so the
touch-stream module can detect taps from the absolute stream instead.
Configured on the pad's devicetree node (defaults shown; **off** unless
`stream-tap-click` is present):

```dts
&glidepoint {
    abs-mode;
    stream-tap-click;             // enable (default: off)
    stream-tap-max-ms = <180>;    // max touch duration
    stream-tap-max-movement = <30>; // max travel, raw counts (Chebyshev
                                    // distance from the touch-down point)
};
```

A qualifying touch (short, low-travel, and **no frame of the sequence had
scroll-mode set**) injects an `INPUT_BTN_0` press + release (each sync'd)
into the pad's normal input pipeline at lift-off, so existing button
processors apply — e.g. this repo's Mouse-layer click behaviors treat a
tap exactly like the left-click key. Hosts consuming the raw stream can
ignore the resulting click reports and synthesize their own taps if they
prefer; the firmware tap is the zero-software fallback.

## Host-side notes (beyond the frozen contract)

- Z is a coarse 6-bit strength (0-63), not calibrated pressure. Its
  resting "touched" value varies with finger size/humidity; treat it as
  presence + rough contact-size signal only.
- Scroll-flag transitions can happen mid-touch (layer pressed/released
  while a finger is down); frames flip bit1 without a release.
- Frames may occasionally drop on a congested BLE link (firmware drops
  oldest first; the lift-off release report is always the newest frame
  and effectively always delivered).
- The release report carries whatever scroll flag was in effect at
  lift-off, so the host can start momentum from it.
- Read the feature report once at device discovery for version,
  orientation and ranges rather than hardcoding them.

## Firmware architecture

In the [kalakris/zmk](https://github.com/kalakris/zmk) `raw-touch` branch:

- `app/src/pointing/touch_stream.c` — the module (Kconfig
  `CONFIG_ZMK_TOUCH_STREAM`, default n; only the Go60 RH target enables
  it). Consumes `INPUT_ABS_X/Y/Z` events from the pad, emits vendor input
  reports, fills the feature report from devicetree at init, re-injects
  `REL_X/REL_Y` into the existing input-listener chain (always — dual
  mode), and detects taps.
- `app/src/pointing/input_processor_touch_stream_scroll.c` + node
  `zip_touch_stream_scroll` in `app/dts/input/processors/` — the scroll
  marker processor.
- `app/src/pointing/input_listener.c` —
  `zmk_input_listener_touch_stream_scroll_active()`, the layer-state
  evaluation described above.
- `app/include/zmk/hid.h`, `app/src/hid.c` — report descriptor (vendor
  collection, Input + Feature) + report storage.
- `app/src/usb_hid.c`, `app/src/hog.c`, `app/src/endpoints.c` — USB and
  BLE send/read paths (mirrors the mouse report plumbing).
- `app/west.yml` — pulls
  [kalakris/cirque-input-module](https://github.com/kalakris/cirque-input-module)
  branch `raw-touch`, which adds the `abs-mode` devicetree property
  (absolute-mode register setup + 6-byte packet parsing) and the
  `stream-tap-*` properties to the Pinnacle driver binding.

  > **Transitional — do not treat this driver fork as part of the
  > protocol.** Zephyr's in-tree `input_pinnacle` driver has supported
  > absolute X/Y/Z (`data-mode = "absolute"`) since 2024 and is on ZMK
  > `main` via the Zephyr 4.1 upgrade, while petejohanson's module (our
  > fork base) is EOL. The plan is to drop this fork and consume standard
  > `INPUT_ABS_X/Y/Z` from any driver. **Nothing in this protocol depends
  > on the fork** — an implementation only needs absolute X/Y/Z frames
  > from whatever driver its sensor uses. See
  > `docs/pinnacle-driver-landscape.md` on `main`.

In this repo (branch `raw-touch`): `config/west.yml` points at the fork,
`config/go60_rh.conf` enables `CONFIG_ZMK_TOUCH_STREAM`, and
`config/go60_rh.keymap` sets `abs-mode` + `stream-tap-click` on
`&glidepoint` and carries the dual-purpose `nav_scroll` overlay. Only the
right pad streams; the left pad keeps its relative behavior over the
split link.
