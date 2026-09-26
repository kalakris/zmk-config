# Contributing to zmk-raw-touch

Issues and pull requests are welcome. This file covers what helps most
and the checks a change has to pass.

## Reporting a working or failing configuration

Include the exact firmware tree (ZMK fork and commit, Zephyr version,
Cirque driver module and commit), the board and trackpad, the transport
(USB or Bluetooth), and the module's log lines. To capture them, build
the central half with ZMK's
[USB logging](https://zmk.dev/docs/development/usb-logging) and
`CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF=y`, then read its USB serial
console; the log level alone prints nothing. The README's
[Diagnostics](README.md#diagnostics) section has the details. For
Bluetooth problems, say whether you forgot and re-paired the keyboard
after flashing: macOS caches the HID report map, so a stale pairing
looks like dead frame parsing while keys keep working.

## What is useful

- Tested board configurations. Put each one in its own directory,
  `configs/<keyboard>/`, with the overlay or keymap fragment and a short
  README naming the board, trackpad, driver, firmware tree, transports
  tested, and what worked. Add a line for it to the README's
  Compatibility section. Leave `examples/` alone: it holds only the files
  the README walks through, and the README check fails on any file there
  the README does not embed. CI builds only the Go60, so say in the pull
  request how you built and tested the configuration.
- Ports of the transport files to newer ZMK and Zephyr trees. The module
  targets ZMK 0.3 / Zephyr 3.5 because that is what the maintainer can
  run; keep the current tree building.
- Host implementations for other operating systems, built against the
  [wire format](docs/protocol.md).

## Rules for changes

- **The README's config blocks are files.** Every block under
  `examples/` is inserted into the README by
  `scripts/check-readme-examples.py`. Edit the file, run the script with
  `--write`, and commit both. A plain run is the CI check; an example
  file the README does not reference fails it.
- **CI builds the examples on real targets.** `ci/` compiles the
  after-files for both Go60 halves against the pinned trees in
  `ci/west.yml`. A change to an example must build there.
- **Wire-format changes are protocol changes.** The protocol integer in
  `include/zmk/raw_touch/hid.h` is the only compatibility contract with
  hosts. Bump it, update `docs/protocol.md`, and note that a report-layout
  change needs a Bluetooth forget and re-pair. Coordinate with the
  RawTouch host before merging.
- **The module version lives in `include/zmk/raw_touch/version.h`.**
  Tags are `v0.y.z`; CI checks a tag against the header.
- Do not assume a pad count or a specific keyboard. Slot counts come
  from the devicetree, and the feature report describes the pads.
- New files carry the MIT SPDX header used throughout `src/`.

## Style

Match the surrounding code. Keep processors and transports independent
of any particular trackpad driver: the module consumes standard absolute
input events and nothing in it should know about the Pinnacle.
