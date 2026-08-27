# Brief: republish the raw touch stream as a standalone ZMK module

Status: **built and CI-green; not flashed, not public** (2026-08-26). The
module exists at
[`kalakris/zmk-raw-touch-wip`](https://github.com/kalakris/zmk-raw-touch-wip)
(private, branch `main`, HEAD `5199d34`, 22 files / ~1450 lines of C), and
two `zmk-config` branches build against it — `module-port` and
`module-port-intree`, both green on all 5 targets, first try. **The 219-line
ZMK core patch is gone: `kalakris/zmk` is no longer load-bearing and can be
deleted.** What remains is a hardware bench pass and the blockers at the
bottom of this file.

Prerequisite reading: [publish-strategy.md](publish-strategy.md) (why a
module, why now, what else to publish first), [raw-touch.md](raw-touch.md)
(what exists today), [prior-art-survey.md](prior-art-survey.md) (claims to
avoid making).

## Why

The system works and is in daily use, but it was **unpublishable in its
original shape**: the firmware lived in `kalakris/zmk@raw-touch`, a fork of
*MoErgo's fork* of ZMK. Anyone with a different keyboard would have had to
adopt MoErgo's ZMK to try it — at any Zephyr version. That is the
archetypal "drive-by fork dump" and it would have defined the first
impression.

ZMK core has no extension point for custom HID reports (single static
`zmk_hid_report_desc`, one `usb_hid_register_device()` call, hand-counted
GATT attribute indices), and maintainers have declined to add vendor
payloads there — [zmk#962](https://github.com/zmkfirmware/zmk/pull/962)
(Plover HID) has been open **five years**. But two projects prove the
out-of-tree route works by shipping a **parallel copy of the transport
layer** inside a module:

- [`zzeneg/zmk-raw-hid`](https://github.com/zzeneg/zmk-raw-hid) — 46★,
  283 lines total (`usb_hid.c` 98, `hog.c` 178, `events.c`), USB + BLE
- [`badjeff/zmk-hid-io`](https://github.com/badjeff/zmk-hid-io) — 15★,
  same approach on usage page 0xFF0C

Estimated cost was ~280–350 lines of vendored transport against the 219-line
core patch. Actual: 22 files, ~1450 lines of C — the transport landed in
that range, and the rest is the ported frame handler, two input processors,
bindings, and the README. `touch_stream.c` did port near-verbatim; the
marker processor did **not** (see "Two ZMK-core patches a module cannot
carry" below). The result needs no ZMK fork at all.

## Shape as built

`kalakris/zmk-raw-touch-wip` — private, branch `main`, HEAD `5199d34`. The
name is deliberately provisional and neutral, and **must not contain
"TouchStream"** (see the rename blocker). Every identifier prefix renames
together, once, when the real name is chosen: Kconfig `ZMK_RAW_TOUCH_`,
devicetree compatibles `zmk,raw-touch-*`, C symbols `zmk_raw_touch_*`.

```
zmk-raw-touch-wip/
  zephyr/module.yml            # needs `settings: dts_root: .`
  CMakeLists.txt, Kconfig, LICENSE (MIT), README.md  # demo first, protocol an appendix
  boards/example.overlay       # copy-pasteable worked example
  dts/bindings/input/zmk,raw-touch-pad.yaml    # the module's whole config surface
  dts/bindings/input_processors/               # raw-touch-scroll, raw-touch-idle-filter
  dts/raw_touch/processors.dtsi                # /omit-if-no-ref/ node instances
  src/
    hid.c        # private report descriptor + report state
    usb_hid.c    # second USB HID interface (HID_1)
    hog.c        # second BLE HIDS instance, incl. the feature-report characteristic
    endpoints.c, raw_touch.c, log.c
    input_processor_raw_touch_scroll.c
    input_processor_raw_touch_idle_filter.c
```

Three things to know before touching it:

- **Sources compile into ZMK's `app` target, not a `zephyr_library()`.**
  ZMK declares its headers with `target_include_directories(app PRIVATE
  include)`, so a separate library cannot see `<zmk/endpoints.h>`,
  `<zmk/keymap.h>` or `<drivers/input_processor.h>`. This is what
  `cirque-input-module` does too. (Both reference modules instead add a
  global `zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)`.)
- **The wire format is byte-identical to the fork's** — same usage page
  0xFF00, usage 0x01, report ID 0x04, same 7-byte input report, same 8-byte
  feature report. **The LinearMouse fork needs no changes**: it matches on
  VID/PID + usage page + report ID, none of which moved. Deliberate, to keep
  the bench pass down to one variable.
- **`stream-tap-*` and pad geometry moved off the Cirque driver binding**
  onto `zmk,raw-touch-pad` (`tap-click`, `tap-max-ms`, `tap-max-movement`,
  plus `rotate-90` / `x-invert` / `y-invert` / `x-max` / `y-max` /
  `resolution` / `pad-id` / `device`). That was the "driver-independent
  refactor" punch-list item — done. It is what makes the module
  driver-agnostic, and it makes the Zephyr-4.1 property-rename question moot
  as far as the module is concerned.

### Two ZMK-core patches a module cannot carry, and what replaced them

1. **Scroll-context detection.** Was
   `zmk_input_listener_touch_stream_scroll_active()`: ~140 lines patched into
   `app/src/pointing/input_listener.c` that re-walked the listener's layer
   overlays. Now the marker processor sets a latch itself — reaching the
   marker flags the originating device, and that pad's frame handler consumes
   the flag. This is **more** faithful than what it replaced: the marker runs
   if and only if the chain containing it actually handles the event, so
   layer-overlay ordering and `process-next` shadowing are honoured by
   construction rather than reimplemented. The latch is taken on every ABS
   event of a frame and discarded on the frame's first event, so it is
   correct under either input-callback ordering (the order between the
   listener's callback and the module's is link-order defined). Escape hatch
   if it ever misbehaves: `scroll-layers = <2>;` on the pad node switches to
   plain layer-state evaluation — no logic to rewrite.
2. **Zero-report suppression** (fork commits `32a13a5b` and `cfc4b3e6`) is
   now the `zip_raw_touch_idle_filter` input processor: it drops sync events
   that accumulated no host-visible change. Not cosmetic — an absolute pad
   emits one value-less sync per frame, and a downscaling wheel overlay emits
   value-0 relative events, so without it ~100 Hz of all-zero mouse HID
   reports compete with the raw stream for BLE connection events. **Place it
   LAST in every chain.** Its state is per devicetree node: sharing one node
   between a listener's base chain and that listener's layer overlays is
   fine; sharing between two different listeners is not.

## It works with the MoErgo fork — and lets us drop `kalakris/zmk`

Verified 2026-08-26. Two facts:

1. **West resolves duplicate project names in favour of the top-level
   manifest** — *"If the same name occurs in multiple manifests, the first
   definition is used"*, and the top-level file is processed before project
   imports. So `config/west.yml` can override anything MoErgo's ZMK pulls.
2. **MoErgo's `app/west.yml` @ `go60-zmk0.3.0` pins the driver by name**:
   `cirque-input-module`, remote `petejohanson`, revision `0de55f36`.

So the end state needs **no ZMK fork at all** — this is what
`config/west.yml` on `module-port` now says:

```yaml
projects:
  - name: zmk
    remote: moergo-sc          # stock MoErgo, unforked
    revision: 57a7b8e0…        # SHA, not the branch name: go60-zmk0.3.0 is
    import: app/west.yml       # a branch, and this is a daily driver.
                               # 57a7b8e0 is exactly what kalakris/zmk@raw-touch
                               # forked from, so the ZMK side of this change is
                               # a pure subtraction of our own patches.
  - name: cirque-input-module  # top-level wins over MoErgo's pin
    remote: kalakris           # our fork, or (on -intree) the vendored in-tree driver
    revision: <sha>
  # - name: zmk-raw-touch      # commented out while the repo is private;
  #   remote: kalakris         # vendored into vendor/ instead — see below
  #   revision: main
```

**Payoff beyond publishability:** `kalakris/zmk` disappears. No fork-of-a-
fork to rebase, and MoErgo's upstream fixes flow to us for free (e.g. the
Go60 physical-layout key-order fix `5b43b3f8`, 2026-08-21, which we had
patched by hand in `config/go60-layouts.dtsi`).

Caveats to prove in stage 1 — **all proven by CI, 2026-08-26**:
- The vendored transport builds against **MoErgo's** ZMK at Zephyr 3.5, not
  just upstream ZMK.
- `CONFIG_USB_HID_DEVICE_COUNT=2` and the second BLE HIDS instance compile
  and link on MoErgo's build (`_USB=y`, `_BLE=y` both in the Kconfig output).
- One driver override remains either way (abs-mode isn't in petejohanson's
  module) — `module-port-intree` takes the vendored in-tree driver, per the
  ~2h plan in zephyr-41-migration.md.

## The two branches

| Branch | ZMK | Cirque driver | What it isolates |
|---|---|---|---|
| `module-port` (`13a68eb`) | stock `moergo-sc/zmk` @ `57a7b8e0` | existing `kalakris` fork | *Only* the module change — same driver, same deltas, same report bytes as today |
| `module-port-intree` (`b2bf28c`) | same | `kalakris/cirque-input-module@intree-driver` | The full end state |

Both green on all 5 targets, first try. Verified from the CI logs rather
than "it compiled": `CONFIG_ZMK_RAW_TOUCH=y`, `_USB=y`, `_BLE=y`,
`CONFIG_USB_HID_DEVICE_COUNT=2`, report ID 0x04, usage page 0xFF00/0x01,
both input processors enabled, and a `raw_touch_rh` node present in the
generated devicetree with the right properties. On `module-port-intree`,
`glidepoint` came out with `data-mode = "absolute"`, `idle-packets-count =
<3>`, `data-ready-gpios`, and no leftover fork properties. Zero compiler
warnings from module code. go60_rh: FLASH 31.51%, RAM 22.98%.

**On `module-port` the `go60_lh` firmware is byte-identical to what is
running today from `raw-touch`** — proof that the ZMK-core patches were
purely additive and central-only. Only the right half needs reflashing to
try it. (On `module-port-intree` the left half's driver genuinely changes,
so flash both halves there.)

### The in-tree Cirque driver branch

`kalakris/cirque-input-module@intree-driver` is Zephyr **main**'s
`input_pinnacle.c` vendored pristine from `27150c9d` (`7d6f543`), plus three
labelled patches: 0xFF/SW_DR guard (`66897c3`), per-axis ERA edge
sensitivity (`995e9e0`), force-recalibrate-on-init (`b5c2365`).
SW-reset-on-init turned out to be **already upstream** in `main` (it landed
after v4.1.0 was cut) — so three patches, not four. Property renames the
keymaps absorb: `abs-mode` → `data-mode = "absolute"`, `dr-gpios` →
`data-ready-gpios`, `rotate-90` → `swap-xy`, `x-invert`/`y-invert` →
`invert-x`/`invert-y`, `no-secondary-tap` gone (always on upstream). And
`idle-packets-count` is **mandatory**: upstream defaults it to 0, meaning no
lift-off packets — no release frame, therefore no momentum and no tap.

### The private-repo CI workaround (temporary — must be undone)

The module repo is private and `west update` in CI authenticates
anonymously, so it cannot clone it. Both branches therefore vendor the
module into `vendor/zmk-raw-touch/` and pass it with `-DZMK_EXTRA_MODULES`
via per-target `cmake-args` in `build.yaml` (both go60 targets; **both paths
must be listed**, because the reusable ZMK workflow already passes
`-DZMK_EXTRA_MODULES=$GITHUB_WORKSPACE` and a later `-D` on the same command
line replaces rather than appends). `scripts/sync-raw-touch-module.sh`
refreshes the copy. It lives under `vendor/` and not `modules/` because
`modules/` is gitignored as a west workspace artifact. The real west entry
is in `config/west.yml`, commented out, with instructions. To undo:
uncomment it, delete `vendor/`, drop the two `cmake-args`.

## Work items

- [x] **Vendor the transport.** USB: `device_get_binding("HID_1")` +
  `usb_hid_register_device()` with `CONFIG_USB_HID_DEVICE_COUNT=2`. BLE: a
  second `BT_GATT_SERVICE_DEFINE(…, BT_UUID_HIDS, …)` instance, **including
  the feature-report characteristic** — neither reference module has one,
  and the self-describing feature report is the part of the design most
  worth keeping.
- [x] **Port `touch_stream.c` + the marker processor.** The frame handler
  was near-verbatim; the marker processor was redesigned around a latch, and
  a second processor (`raw_touch_idle_filter`) was added to replace the
  other core patch. See the section above.
- [x] **Move `stream-tap-*` off the Cirque driver binding** onto our own
  node. Done as `tap-click` / `tap-max-ms` / `tap-max-movement` on
  `zmk,raw-touch-pad`, along with the pad geometry.
- [ ] **Bench pass on hardware.** Flash `module-port` (right half only) and
  check parity: scroll, momentum, catch, tap-to-click, ÷8 wheel fallback,
  pointer speed, then a BLE session (see the BLE section for what to expect
  on first pairing). Then `module-port-intree` (both halves).
- [ ] **Delete the ZMK core patch** — i.e. delete `kalakris/zmk` — once the
  bench pass confirms parity. Nothing depends on it any more.
- [ ] **Host side**: delete the deprecated host tap-to-click path (~560
  lines — both paths enabled = double-click), and drop the hardcoded ZMK
  VID/PID from `TouchStreamManager`'s matching dictionary so the feature is
  *keyboards-with-touchpads*, not *Go60*. Neither is needed for parity; both
  are needed before publishing.
- [ ] **Video.** The README is written demo-first with the protocol as an
  appendix, but the 30-second clip of the gesture working is still missing.
  Never present this as "a vendor HID protocol specification" — in this
  community that reads as XAP.

## The BLE question — settled by research, not by hardware

Two HIDS instances on one peripheral **work; confidence medium-high.** HOGP
1.0 §2.5 explicitly permits multiple HID Service instances, and §4.5.1
requires a Report Host to discover *all* of them. Both `zzeneg/zmk-raw-hid`
and `badjeff/zmk-hid-io` ship a genuine second
`BT_GATT_SERVICE_DEFINE(BT_UUID_HIDS)`. The strongest macOS evidence is
**Mooltipass Mini BLE**, a shipping product with two HIDS instances:
[mooltipass/minible#126](https://github.com/mooltipass/minible/issues/126)
documents that macOS *does* enumerate both, but **mis-binds the report maps
on the very first pairing** — one disconnect/reconnect corrects it, and the
macOS bug is still open. There is no report anywhere of macOS failing to
enumerate a second HIDS. Android before ~9 may not tolerate two HID
services.

Two gotchas for the bench pass:

- **Expect to re-pair after the first flash carrying the second HIDS.** The
  GATT database changes, and Zephyr's
  `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION` is `default y` — the host's CCC
  subscription for the new input characteristic is not inherited from the
  bond. If the host does not re-discover and subscribe,
  `bt_gatt_notify_cb()` refuses to send and the stream is *silently* dead.
- **On macOS, disconnect and reconnect once before judging a fresh
  pairing** (the minible mis-binding).

Keep a BLE failure in proportion: raw-touch.md already records that the BLE
HOG path for the vendor report **has never been tested on any build**, so a
failure now is not a regression. Fallback is
`CONFIG_ZMK_RAW_TOUCH_BLE=n` — USB-only stream, with the relative-delta
wheel fallback still working over BLE.

The second-USB-HID-interface question is moot: we shipped one and it builds.
(It is also likely a *win* on macOS — separate `IOHIDInterface` nubs, see
prior-art survey §3 — and leaves room for the optional single-slot Linux
digitizer collection later.)

## Target version: build against what we can test

Build against **ZMK v0.3.0 / Zephyr 3.5** — what our hardware actually
runs. Note in the README that the 4.1 port is confined to the vendored
transport files and invite PRs. Rationale: the official ZMK config template
tracks `main` (Zephyr 4.1), so 3.5 serves a shrinking audience — but
**never publish what you cannot run**; targeting 4.1 for reach would mean
shipping firmware we cannot reproduce bugs in. Marking the delta turns our
blocker into a contribution opportunity.

## Blockers before anything goes public

Unchanged by the port — all still open, and the reason the module repo is
private.

- [ ] **Rename.** "touch stream" collides with **FingerWorks TouchStream**
  — the 1998–2005 multitouch split keyboard that streamed finger contacts
  to a host, acquired by Apple to build the iPhone. r/ErgoMechKeyboards
  knows this history. Survey suggests **PadWire** (clean across npm/PyPI/
  crates/GitHub) or **ZipTouch** (ZMK-flavoured), with "touch frame" as the
  unit noun. One-shot first impression.
- [ ] **Protocol v3 or an explicit "provisional" label.** Breaking the wire
  format under early adopters costs more than a two-week delay. The v3
  items: per-frame device timestamp (HID Scan Time, 100 µs units),
  contact-state bits, geometry in the report descriptor, device-side mode
  gate, serial-prefix matching. See upstreaming-todo.md.
- [x] **LICENSE** on whatever repo ships — MIT, in the module repo. Still to
  do: state in the spec that the descriptor and report layout are
  unencumbered, and give `zmk-config` a LICENSE too.
- [ ] Decide **0xFF00/0x01 vs QMK's 0xFF60/0x61** deliberately. ZMK's
  vendor-HID ecosystem has converged on QMK's page; our kext scan showed
  0xFF00/0x01 is free on macOS while Apple's `MTUserDevice` squats
  0xFF60/0x07. Not a slam dunk — decide, don't drift.

## Recommended order (from publish-strategy.md)

Small, generic patches first — they cost little, and each merge makes you a
known contributor rather than a stranger dropping a fork:

1. Two Cirque patches to **Zephyr** (0xFF/SW_DR guard; ERA edge
   sensitivity) — but **message Peter Johanson first**: all three patches
   are his, and he paused mid-migration of his module into Zephyr's driver.
2. `inputScale` to **LinearMouse**, reframed as generalizing
   `LogitechHighResolutionWheelNormalizer` rather than adding a knob.
3. **Then** this module.
