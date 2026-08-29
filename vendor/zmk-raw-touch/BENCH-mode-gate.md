# Mode gate — hardware bench checklist

Written 2026-08-28 alongside the `mode-gate` branch. **USB half run
2026-08-29 (sections 0–2 PASS)** — and §2 caught a real bug on its
first run: `k_work_delayable_is_pending()` counts `K_WORK_RUNNING`, so
the expiry callback always saw itself as a racing refresh and claims
never expired. Fixed in `20c84e5` (guard on `K_WORK_DELAYED |
K_WORK_QUEUED`), reflashed, re-verified: a timeout-1 claim clamped to
5 s and expired mid-gesture with a 9 ms frame gap (flags 7 → 3 on
consecutive frames). Tooling: `scripts/gate-claim.swift` in zmk-config
(claim/release/hold/raw, usb|ble pin). Note for §3: over USB, macOS
`IOHIDDeviceGetReport` returns the feature report WITH the report-ID
byte prefixed; over BLE it comes bare — normalize before parsing.
BLE/endpoint/sleep sections remain. Follow zmk-config's standing rules: announce every host
deploy, ONE flash watcher at a time, and grant Accessibility only right
after a deploy (never during `xcodebuild test`).

Prerequisites: `mode-gate` firmware on the Go60 (both halves — the gate
itself is central-only, but flash from a single build so the split
protocol stays matched), `scripts/raw-touch-monitor.swift` for frame
inspection, and a claim writer (the item-k agent, or an ad-hoc
`IOHIDDeviceSetReport` / CoreBluetooth script sending
`04 01 01 1E 00` = claim 30 s, `04 01 00 00 00` = release over USB;
drop the leading `04` for the BLE GATT write).

**Expected log lines** (`CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL_INF`): "claimed
by", "claim refreshed by" (DBG), "cleared (released by host)", "cleared
(USB detached)", "cleared (BLE profile disconnected)", "cleared
(endpoint switched away)", "expired without a host refresh".

## 0. Baseline / fork compatibility (no claim writer running)

- [x] Feature report reads `protocol_version = 3`, `capabilities` bit 0
      = 1, over USB GET_REPORT **and** the BLE characteristic.
      (2026-08-29, dual-connected: USB ID-prefixed, BLE bare.)
- [x] With the v1 LinearMouse fork (which never claims): frames show
      flags bit 2 = **0** at all times — touch, scroll (flags=3), and
      release frames. Wheel fallback works with LinearMouse quit;
      host-side wheel suppression works with it running. Nothing about
      v1 behavior changes on gate-capable firmware.

## 1. Claim / refresh / release over USB

- [x] Claim (30 s) accepted; log shows the USB endpoint label.
      (Log not inspected — engagement verified on the wire instead.)
- [x] While claimed + Nav held: frames carry flags bit 2 = 1 and **zero
      wheel events** reach the host (monitor + `hidutil`/event viewer).
- [x] Pointer motion, tap-to-click, and typing all unaffected while
      claimed (suppression touches scroll-context deltas only).
- [x] Refresh at 15 s intervals: claim holds indefinitely; bit 2 stays 1
      across refreshes (no flicker at the refresh instant).
- [x] Explicit release: bit 2 → 0 and wheel fallback returns on the
      immediately-next Nav-scroll gesture (no timeout wait).
- [x] Malformed writes rejected AND state unchanged: wrong length,
      command ≠ 0x01, op ≠ 0x00/0x01, timeout 0, reserved ≠ 0.
      (All six probes stalled with 0xE0005000; ID-prefixed valid form
      accepted — both framings behave per spec.)
- [x] Clamp low: timeout 1 expired at ~4.7 s measured on the wire.
- [ ] Clamp high: timeout 200 expires at ~120 s (not yet run).

## 2. Expiry (dead-host simulation)

- [x] USB: one-shot claim with no refresher expired on schedule; wheel
      restored mid-gesture (flags 7 → 3, 9 ms frame gap). First run
      caught the is_pending bug (see header); fixed in `20c84e5`.
- [ ] Repeat over BLE.

## 3. Claim over BLE

- [ ] Forget + re-pair FIRST (the writable characteristic changed the
      GATT DB — §6 verifies the stale-cache failure deliberately).
      **Early finding 2026-08-29:** claim writes over BLE succeeded
      against the PRE-gate pairing (only a permission changed, not the
      report map) — §6's premise may not reproduce; re-check there.
- [ ] GATT write of the 4-byte body claims; log names the BLE profile.
- [ ] Bit 2 = 1 on BLE-delivered frames while claimed; wheel suppressed;
      pointer/taps/keys unaffected. Release restores instantly.
- [x] A claim written on profile N does NOT suppress the wheel while a
      different profile (or USB) is selected. (Verified accidentally
      2026-08-29: BLE-endpoint claim while USB selected — bit 2 stayed
      0, wheel unaffected. Deliberate re-run still worthwhile.)

## 4. Endpoint switching mid-claim

- [ ] Claim over USB (USB selected), then `&out OUT_BLE`: log shows
      "endpoint switched away", wheel works on the BLE host immediately,
      bit 2 = 0.
- [ ] Switch back to USB with the claim writer still refreshing: gate
      re-engages within one refresh interval (≤ timeout/2), untouched
      in between the wheel works.
- [ ] Two hosts, both claiming (USB host + BLE host): switching between
      them hands the gate over seamlessly — each side sees bit 2 = 1
      only while it is selected, and neither ever sees double scroll.
- [ ] USB cable pull mid-claim: "cleared (USB detached)"; wheel-over-BLE
      unaffected. Re-plug + host re-claim recovers.
- [ ] BLE disconnect mid-claim (power off host radio or walk away):
      "cleared (BLE profile disconnected)".

## 5. Sleep / wake

- [ ] Host sleep 2+ min with an active claim, then wake: claim expired
      or cleared during sleep (no stuck suppression); the host's
      re-assert (watchdog / re-enumeration callback) re-claims; bit 2
      returns; NO double scroll in the gap (host must not synthesize
      until it sees bit 2 = 1 again — the anti-double-scroll property).
- [ ] Keyboard idle/sleep and wake mid-claim: same outcome.

## 6. The stale-GATT-cache failure (deliberate)

- [ ] On a host paired against PRE-gate firmware (do NOT forget/re-pair
      yet): confirm the deceptively-partial mode — keys, USB claim, and
      feature read fine, but the BLE claim write fails or is silently
      dropped. Then forget + re-pair and confirm §3 passes. This is the
      known macOS HOGP-cache gotcha; documenting the observed symptom
      here closes the loop.

## 7. Regression sweep (gate never touched)

- [ ] Full existing bench: smooth scroll via stream (USB + BLE), wheel
      fallback with consumer quit, tap-to-click, both pads, LH pad over
      the tuned wire, typing under load. Nothing regresses with the gate
      code present but unclaimed.
