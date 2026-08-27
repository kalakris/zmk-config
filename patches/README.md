# Salvaged upstream-PR candidates

Patches preserved here because their only other home is a fork slated for
deletion.

## zmk-skip-empty-mouse-report-syncs.patch

`cfc4b3e6` from `kalakris/zmk@raw-touch` — "fix(pointing): Skip mouse
reports for syncs with no accumulated changes". A general ZMK-core fix:
the input listener sends a mouse HID report on every sync event even when
nothing was accumulated, a host-invisible no-op that still costs a USB
transfer or BLE notification per sync, up to the device's full sample
rate.

The zmk-raw-touch module reimplements the same suppression as an input
processor (`zip_raw_touch_idle_filter`) because a module cannot patch
core — but the processor is the wrong shape for an upstream PR, and this
is the right one. Written against ZMK v0.3.0-era `input_listener.c`;
expect light rebasing onto ZMK main before submitting.
