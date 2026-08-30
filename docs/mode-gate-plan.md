# Mode gate (j) + standalone host agent (k) — implementation plan

Written 2026-08-28. Companion to [next-steps.md](next-steps.md) items j/k,
which carry the full issue survey. This doc pins the wire contract and the
branch/repo map so the two implementations can proceed independently.

**Standing constraints for this round:**

- **No flashing.** There are unflashed pending tests (the
  `hold-while-undecided` keymap fix). Nothing here goes on hardware yet.
- **Keep all existing binaries.** `firmware/` is untouched; CI builds land
  per-branch and overwrite nothing.
- **zmk-config `main` stays buildable and identical in firmware terms** —
  all gate work lands on branch `mode-gate`.

## Wire contract (fixed — both sides implement against this)

Protocol version stays **3**; the gate is advertised by a capability bit,
not a version bump. The reserved bits in the v3 spec are assigned as
follows (README appendix is updated to match by the firmware side):

- **Feature report (GET, report ID 0x04, 20 bytes)** — unchanged layout;
  byte 2 `capabilities` **bit 0 = 1** ⇒ gate supported.
- **Claim (SET feature report, report ID 0x04, 4-byte body)** — the
  module's first host→device path (today USB declines SET_REPORT and the
  BLE feature characteristic is read-only):

  | offset | size | field |
  |---|---|---|
  | 0 | 1 | command — `0x01` = gate claim |
  | 1 | 1 | `0x01` claim/refresh, `0x00` release |
  | 2 | 1 | timeout in seconds; firmware clamps to [5, 120]; `0` rejected |
  | 3 | 1 | reserved, must be 0 |

  Any other command byte, length, or nonzero reserved byte is rejected.
  The host refreshes at ≤ half the timeout it wrote.
- **Frame flags bit 2 = gate engaged**: set iff the ÷24 wheel fallback is
  suppressed for the endpoint this frame is being sent to. **Hosts
  synthesize scroll only when bit 2 is set** (and bit 1 scroll mode, as
  today). This makes firmware the single source of truth — wheel and
  synthesized scroll are mutually exclusive by construction, closing the
  double-scroll window after sleep/reboot/re-pair.

Gate semantics (from next-steps item j): claim is scoped to the
**endpoint instance** the write arrived on (transport + BLE profile);
wheel suppression applies only while the claiming endpoint is the
selected one; claim clears on disconnect / profile switch / USB detach
and on timeout expiry. Fork compatibility: the v1 LinearMouse fork never
claims, so bit 2 stays 0 for it and its host-side wheel suppression keeps
working unchanged.

## Repo / branch map

| Repo | Branch | Work |
|---|---|---|
| `~/src/zmk-raw-touch` | `mode-gate` (new, from `main`) | Gate state, USB set_report, BLE writable feature char, flag bit, wheel suppression, README spec appendix |
| this repo | `mode-gate` (new, from `main`) | Vendored module sync + any keymap/config change; pushed so CI validates the build |
| `~/src/rawtouch` | new local repo (`main`) | **RawTouch**, the standalone SwiftPM host program (item k). Name decision landed on RawTouch — pairs with zmk-raw-touch and retires PadWire, one brand for module + protocol + host |
| `~/src/linearmouse` | untouched | v1 ships as-is |

`~/src/zmk-raw-touch` is left checked out on `main` after the work, so a
later `sync-raw-touch-module.sh` run on zmk-config `main` cannot pick up
gate code by accident.

## Firmware side (item j)

1. Gate state module: per-endpoint-instance claim + `k_work_delayable`
   expiry; subscribe to endpoint/connection events to clear on
   disconnect, profile switch, USB detach.
2. USB: implement `set_report` for the feature report (today explicitly
   absent, `src/usb_hid.c:120`).
3. BLE: add write permission + handler to the feature report
   characteristic (`src/hog.c:150`). **This changes the GATT DB → macOS
   needs forget + re-pair when this firmware is eventually flashed**, with
   the known deceptively-partial failure mode. Document in README.
4. Set frame flags bit 2 when the gate is engaged for the endpoint the
   frame goes to.
5. Suppress the ÷24 wheel while engaged. The wheel is produced by the
   keymap's nav_scroll listener chains; the module already owns processors
   in those chains (`zip_raw_touch_scroll` marker, idle filter) — prefer a
   gate check inside module code over new keymap wiring, but the
   implementer picks the exact point. Suppression must affect only
   trackpad-derived wheel events and only while engaged + selected.
6. README wire-format appendix: fill in the reserved bits per the
   contract above, claim command, liveness rules, re-pair note.
7. Validation without hardware: module syncs into zmk-config `mode-gate`
   via `scripts/sync-raw-touch-module.sh`, branch pushed, CI build green.
   Hardware bench (flash, claim over USB + BLE, sleep/wake, re-pair) is a
   **later, separate step** — blocked behind the pending flash queue.

## Host side (item k)

New SwiftPM executable in `~/src/raw-touch-agent`, extracted from the
fork per the next-steps k inventory:

- Port as-is: `TouchStreamFrame`, `TouchStreamDeviceClock`,
  `TouchStreamCapabilities`, `TouchStreamScrollPoster`,
  `GestureScrollSeriesPoster`, `SystemScrollingPreference`.
- `TouchScrollEngine`: config becomes a plain struct (drop the
  `Scheme`-derived type).
- New slim manager replacing `TouchStreamManager`'s
  `ConfigurationState`/`DeviceManager` coupling: IOHIDManager matching on
  usage pair 0xFF00/0x01, normative feature-report validation before
  trusting a device (squatter defense), per-pad state, first-touch-wins
  arbitration.
- New: claim writer — claim on device open, refresh at ≤ timeout/2,
  release on clean exit, re-assert on device re-enumeration (IOHIDManager
  matching callback covers replug/wake/re-pair). Requires capabilities
  bit 0; exits with a clear message against gateless firmware.
  Synthesizes only on frames with flags bit 2 set.
- Config: JSON at `~/.config/raw-touch-agent/config.json` carrying the
  fork's `touchStream` knobs (ballistics, axis, natural-scroll follow,
  per-pad settings). No GUI.
- LaunchAgent plist template + README (install, Accessibility grant
  timing, the accessibility-loop warning from raw-touch.md).
- Tests: port engine/parser/clock/capabilities/poster unit tests; use the
  injectable event sink — **tests must never post real CGEvents or
  trigger TCC prompts**. `swift build` + `swift test` green is this
  round's done-bar; the agent is not run live (that needs an
  Accessibility grant and gate firmware on hardware).

## What "done" means this round

Both implementations exist on their branches, compile, and pass their
respective checks (CI firmware build; `swift test`). Nothing flashed,
nothing deployed, no re-pair performed. The hardware bench script for the
gate (claim/refresh/expiry over both transports, endpoint switch,
sleep/wake, re-pair) gets written as part of the firmware work but runs
in a later session, after the pending keymap-fix flash clears the queue.
