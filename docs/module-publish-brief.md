# Brief: republish the raw touch stream as a standalone ZMK module

Status: **shipped and daily-driven; not yet public** (2026-08-27). The
module at
[`kalakris/zmk-raw-touch`](https://github.com/kalakris/zmk-raw-touch)
(private, branch `main`) is what zmk-config `main` builds and what the
keyboard runs — benched over USB and BLE, promoted from
`module-port-intree`, now on **protocol v3** with **both pads streaming**.
The name is final (`zmk-raw-touch`, renamed from `-wip`) and the usage
page is decided (0xFF00/0x01 stays). **The 219-line ZMK core patch is
gone: `kalakris/zmk` is no longer load-bearing and can be deleted** (its
one PR-worthy commit is salvaged in zmk-config `patches/`). What remains
before going public is the short list at the bottom of this file.

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

`kalakris/zmk-raw-touch` — private, branch `main`. The name is **final**
(see the rename blocker, now closed), and the identifier prefixes match
it: Kconfig `ZMK_RAW_TOUCH_`, devicetree compatibles `zmk,raw-touch-*`,
C symbols `zmk_raw_touch_*`. Since the original port the module has
gained protocol v3 (per-frame timestamps, contact/seq bytes, per-pad
feature-report geometry slots, real logical ranges in the descriptor),
left-pad streaming, a user-facing README with the wire spec as an
appendix (the authoritative protocol doc), and the review-pass cleanups.

```
zmk-raw-touch/
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
- **The port kept the wire format byte-identical to the fork's** (v2) so
  the bench pass had one variable; **protocol v3 then shipped on top**
  (2026-08-27): 11-byte frames, 20-byte feature report, same usage page
  0xFF00/0x01 and report ID 0x04. (The fork accepted v2+v3 until
  2026-08-28; it is v3-only now, so the old fork build rolls back to
  wheel-fallback scrolling.) Gotcha: on BLE, macOS
  caches the HOGP report map — a report-layout change needs forget +
  re-pair (in the README's known issues).
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
   listener's callback and the module's is link-order defined). A
   `scroll-layers` escape hatch (plain layer-state evaluation) existed as
   insurance; the latch proved itself on hardware and the hatch was removed
   in the 2026-08-27 review pass.
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

(Historical since 2026-08-27: `module-port-intree` was promoted to `main`
after the bench pass and both staging branches are frozen.)

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
- [x] **Bench pass on hardware** — DONE. Parity confirmed (scroll,
  momentum, catch, tap-to-click, wheel fallback, pointer speed), then USB
  *and* BLE verified; `module-port-intree` was promoted to `main`
  (2026-08-27) and is the daily driver.
- [ ] **Delete the ZMK core patch** — i.e. delete `kalakris/zmk`. Nothing
  depends on it any more, and `cfc4b3e6` is salvaged as
  `patches/zmk-skip-empty-mouse-report-syncs.patch` on zmk-config `main`.
  Just do it.
- [x] **Host side**: the deprecated host tap-to-click path is deleted
  (`f5e8a33`) and the hardcoded ZMK VID/PID is out of
  `TouchStreamManager`'s matching dictionary (`7e5dfb9`) — the feature is
  *keyboards-with-touchpads*, not *Go60*. Still open: re-land the reverted
  review-pass cleanup (see the blockers below).
- [ ] **Video.** The README is written demo-first with the protocol as an
  appendix, but the 30-second clip of the gesture working is still missing.
  Shot list agreed: 30 s cut, cold-open on the catch, a contrast cut with
  LinearMouse quit (÷24 wheel vs stream), hands cam tight on the right
  half. Never present this as "a vendor HID protocol specification" — in
  this community that reads as XAP.

## The BLE question — settled by research, then confirmed on hardware

**Confirmed on hardware 2026-08-27**: the second HIDS instance works on
macOS, and the stream runs over BLE — with one addition the research
missed: **macOS caches the HOGP report map, so any report-layout change
(e.g. v2→v3) needs a forget + re-pair**, and the failure is deceptively
partial (keys, USB, and the live-read feature report all keep working;
only BLE frame parsing dies). That is in the module README's known
issues. The research record:

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

Two gotchas, both confirmed live:

- **Expect to re-pair after the first flash carrying the second HIDS.** The
  GATT database changes, and Zephyr's
  `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION` is `default y` — the host's CCC
  subscription for the new input characteristic is not inherited from the
  bond. If the host does not re-discover and subscribe,
  `bt_gatt_notify_cb()` refuses to send and the stream is *silently* dead.
- **On macOS, disconnect and reconnect once before judging a fresh
  pairing** (the minible mis-binding).

If BLE ever needs to be taken out of the equation, the fallback is
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

The original decision blockers are all closed:

- [x] **Rename: DONE 2026-08-27.** Final name **`zmk-raw-touch`** — the
  working title, kept. Collision-checked (GitHub clean; no tech products
  named Raw Touch anywhere), same register as `zmk-raw-hid`/`zmk-hid-io`,
  and every symbol already used the `raw_touch` prefix, so the rename cost
  nothing. Repo renamed from `zmk-raw-touch-wip`; GitHub redirects the old
  URL. "Touch stream" stays banned (FingerWorks); "touch frame" is the
  unit noun.
- [x] **Protocol v3: DONE 2026-08-27** — shipped and bench-verified
  end-to-end. Per-frame device timestamp (100 µs units, u16LE, drives
  host-side device-time reconstruction that fixes BLE-batching velocity
  distortion), `contact_id` + flags + `seq`, per-pad geometry slots in the
  20-byte feature report, real logical ranges in the report descriptor
  (macOS-verified). The device-side mode gate is *reserved, not
  implemented* — the spec says so explicitly. Spec = the module README's
  wire-format appendix, authoritative.
- [x] **LICENSE** on whatever repo ships — MIT, in the module repo. Still to
  do: state in the spec that the descriptor and report layout are
  unencumbered, and give `zmk-config` a LICENSE too.
- [x] **Usage page: DONE 2026-08-27** — **0xFF00/0x01 stays** (fixed
  `#define`s now, not Kconfig). Our kext scan showed it free on macOS
  while Apple's `MTUserDevice` squats QMK's 0xFF60/0x07. Decided, not
  drifted.

### Remaining before flipping the repo public

**Host-plan change 2026-08-30: the public host is RawTouch**
(`~/src/rawtouch`), not the LinearMouse fork — the fork stays private
and unreleased (decision recorded in next-steps item k). The README's
host instructions must point at RawTouch before the flip.

- [x] ~~Re-land the host cleanup~~ — DONE 2026-08-28 (batch exonerated,
  re-landed on the fork's main; next-steps item b). Moot for the release
  anyway now that the fork is not the shipped host.
- [ ] **RawTouch public repo + menubar app** (next-steps item k) — the
  release's host artifact.
- [ ] **Demo video** (shot list above) — the release leads with it.
- [ ] **Notarized RawTouch releases** (next-steps item f — now a
  prerequisite, not optional: a background tool demanding Accessibility
  needs Developer ID + notarization to be installable).
- [ ] **Flip the repo public and un-vendor**: uncomment the `zmk-raw-touch`
  entry in `config/west.yml`, delete `vendor/`, drop the two `cmake-args`
  from `build.yaml`.

## Recommended order (from publish-strategy.md)

Small, generic patches first — they cost little, and each merge makes you a
known contributor rather than a stranger dropping a fork:

1. Two Cirque patches to **Zephyr** (0xFF/SW_DR guard; ERA edge
   sensitivity) — but **message Peter Johanson first**: all three patches
   are his, and he paused mid-migration of his module into Zephyr's driver.
2. `inputScale` to **LinearMouse**, reframed as generalizing
   `LogitechHighResolutionWheelNormalizer` rather than adding a knob.
3. **Then** this module.
