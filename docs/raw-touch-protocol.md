# Raw touch streaming protocol (prototype)

The Go60's right-hand Cirque Pinnacle trackpad streams absolute touch
frames (position + touch strength) to the host over a vendor-defined HID
report, so host software can synthesize Magic-Trackpad-quality scrolling
with true lift-off momentum. Firmware lives on branch `raw-touch` of
[kalakris/zmk](https://github.com/kalakris/zmk) (fork of
`moergo-sc/zmk:go60-zmk0.3.0`); the host consumer is the patched
LinearMouse fork at
[kalakris/linearmouse](https://github.com/kalakris/linearmouse) branch
`go60-inputscale`.

## Transport

HID vendor-defined report, **usage page `0xFF00`, usage `0x01`**, in its
own top-level application collection, so macOS exposes it as a separate
`IOHIDDevice`. Sent over both USB HID and BLE HID-over-GATT (a dedicated
input-report characteristic with a report-reference descriptor) alongside
the existing keyboard/consumer/mouse reports.

**Report ID: `0x04`** (`ZMK_HID_REPORT_ID_TOUCH_STREAM`; keyboard = 0x01,
consumer = 0x02, mouse = 0x03). Hosts should match by usage page, not ID.
Note: BLE HOG notifications carry only the 7 payload bytes (the report is
identified by the characteristic, per HID-over-GATT); USB in-band reports
are prefixed with the report ID as usual.

## Payload

7 bytes after the report ID, little-endian:

| byte | field  | meaning                                                        |
|------|--------|----------------------------------------------------------------|
| 0    | pad_id | 0 = right pad (central). 1 = left pad — reserved, not implemented |
| 1-2  | x      | uint16 LE, absolute Cirque coordinate (~0-2047)                |
| 3-4  | y      | uint16 LE, absolute Cirque coordinate (~0-1535)                |
| 5    | z      | uint8 touch strength (6-bit from hardware; 0 when not touched) |
| 6    | flags  | bit0 = touched, bit1 = scroll mode, bits 2-7 reserved (0)      |

Cadence: one report per Pinnacle sample while touched (~100 Hz); exactly
one release report (touched = 0, z = 0, x = y = 0) on lift-off; nothing
while idle.

Scroll mode (flags bit1) is set while the keymap's Nav layer
(`CONFIG_ZMK_TOUCH_STREAM_SCROLL_LAYER`, layer 2) is active. While it is
set, firmware withholds the pad's motion from the normal pointer pipeline
— the host is expected to scroll from these frames. The release report
carries whatever scroll flag was in effect at lift-off, so the host can
start momentum. Outside scroll mode, firmware derives relative deltas
from successive frames and feeds the regular mouse pipeline; the stream
still runs, so hosts must gate scrolling on bit1.

## Host-side notes (beyond the frozen contract)

- Coordinates are the pad's **raw** absolute output: no rotation or
  inversion applied (the pad is mounted rotated: firmware maps
  pointer-mode deltas via rotate-90 + y-invert, hosts consuming the
  stream must apply their own orientation mapping). The usable active
  area is inset from the numeric range (roughly X 128-1920, Y 64-1472
  per Cirque's docs); expect values to pin near those edges.
- Z is a coarse 6-bit strength (0-63), not calibrated pressure. Its
  resting "touched" value varies with finger size/humidity; treat it as
  presence + rough contact-size signal only.
- Scroll-flag transitions can happen mid-touch (layer pressed/released
  while a finger is down); frames flip bit1 without a release.
- Frames may occasionally drop on a congested BLE link (firmware drops
  oldest first; the lift-off release report is always the newest frame
  and effectively always delivered).
- Hardware tap-to-click is a Pinnacle relative-mode feature and is
  disabled by absolute mode; clicks come from keymap keys (Mouse layer
  RH T1/T2). If tap-to-click is wanted, synthesize it host-side from
  short low-travel touches.

## Firmware architecture

In the [kalakris/zmk](https://github.com/kalakris/zmk) `raw-touch` branch:

- `app/src/pointing/touch_stream.c` — the module (Kconfig
  `CONFIG_ZMK_TOUCH_STREAM`, default n; only the Go60 RH target enables
  it). Consumes `INPUT_ABS_X/Y/Z` events from the pad, emits vendor
  reports, and re-injects `REL_X/REL_Y` into the existing input-listener
  chain when not in scroll mode.
- `app/include/zmk/hid.h`, `app/src/hid.c` — report descriptor
  (vendor collection) + report storage.
- `app/src/usb_hid.c`, `app/src/hog.c`, `app/src/endpoints.c` — USB and
  BLE send paths (mirrors the mouse report plumbing).
- `app/west.yml` — pulls
  [kalakris/cirque-input-module](https://github.com/kalakris/cirque-input-module)
  branch `raw-touch`, which adds the `abs-mode` devicetree property to the
  Pinnacle driver (absolute-mode register setup + 6-byte packet parsing).

In this repo (branch `raw-touch`): `config/west.yml` points at the fork,
`config/go60_rh.conf` enables the Kconfig options, and
`config/go60_rh.keymap` sets `abs-mode` on `&glidepoint` and drops the
old nav_scroll wheel-mapping chain. Only the right pad streams; the left
pad keeps its relative behavior over the split link.
