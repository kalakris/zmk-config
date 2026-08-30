# Mode gate — hardware bench checklist

Written 2026-08-28 alongside the `mode-gate` branch. **Sections 0–4
and 6 PASS (2026-08-29); §5 deferred to the standalone agent's live
run; §7 spot-checked. One non-gate firmware bug found: the USB-wedge
finding in §4.** Earlier: USB half run 2026-08-29 (sections 0–2 PASS) — and §2 caught a real bug on its
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
- [x] Clamp high: timeout 200 expired between t+95 s and t+123 s
      (touch-cadence bounds) — consistent with 120, rules out 200.

## 2. Expiry (dead-host simulation)

- [x] USB: one-shot claim with no refresher expired on schedule; wheel
      restored mid-gesture (flags 7 → 3, 9 ms frame gap). First run
      caught the is_pending bug (see header); fixed in `20c84e5`.
- [x] Repeat over BLE: timeout-1 claim, gated frames t+1.5 s (touch
      down) to t+5.0 s, first ungated frame at exactly t+5.0 s.

## 3. Claim over BLE

- [x] **Re-pair NOT needed** — the whole section passed against the
      PRE-gate pairing. Only a characteristic permission changed, not
      the report map, so the HOGP cache stays valid. See §6.
- [x] GATT write of the 4-byte body claims (bare framing; log not
      inspected — engagement verified on the wire).
- [x] Bit 2 = 1 on BLE-delivered frames while claimed (every frame in
      the window: 875/875 gated); wheel suppressed; pointer/taps/keys
      unaffected. Release restored the wheel instantly.
- [x] A claim written on profile N does NOT suppress the wheel while a
      different profile (or USB) is selected. (Verified accidentally
      2026-08-29: BLE-endpoint claim while USB selected — bit 2 stayed
      0, wheel unaffected. Deliberate re-run still worthwhile.)

## 4. Endpoint switching mid-claim

- [x] Switch-away (run as BLE claim + USB plug-in, same mechanism as
      the `&out` variant): wheel worked over USB immediately, dormant
      re-claims on the deselected endpoint stayed dormant.
- [x] Switch-back with the writer still refreshing: gate re-engaged
      IMMEDIATELY on unplug (a refresh had already re-established the
      claim on the deselected endpoint, so the switch found it live —
      better than the ≤ timeout/2 worst case).
- [x] Two hosts, both claiming (one Mac playing both roles, one holder
      per transport): two full pull/replug cycles, scroll dead on both
      sides of every switch, ~1400 gated frames delivered per transport,
      zero double scroll. Handoff seamless.
- [x] USB cable pull mid-claim: BLE side took over cleanly (see
      handoff above). **BUT see the finding below — pulling the cable
      MID-SCROLL wedged the vendor USB stream itself.**

**FINDING (2026-08-29, not a gate bug): mid-transfer cable pull wedges
the vendor USB stream.** After the hot-plug handoff cycles (cable pulled
while frames were in flight), the USB interrupt-IN stream went
permanently silent: fresh host handles enumerate fine, feature-report
GETs work (control pipe alive), keys fine, BLE stream fine — but zero
input frames over USB until the half is power-cycled. Prime suspect:
`src/usb_hid.c`'s single-slot `hid_sem`, released only by the transfer
completion callback, which never fires for a transfer killed by detach.
Symptom in daily life: LinearMouse scroll death after replugging
mid-scroll (fork suppresses the wheel per device identity while the
stream device is present, and no frames arrive to synthesize from).

**FIXED (2026-08-29, module `72a26f7`).** Suspect confirmed by
inspection: the send path's existing bus-down `k_sem_give` could never
fire in this repro, because a detach switches the selected endpoint to
BLE (silencing the USB send path while the bus is down) and after
replug `zmk_usb_get_status()` is healthy again, leaving that branch
unreachable. `usb_hid.c` now subscribes to `zmk_usb_conn_state_changed`
(the same event `src/gate.c` uses) and re-arms `hid_sem` whenever the
connection state leaves `ZMK_USB_CONN_HID` -- exactly the transitions
(detach, bus reset, error) that abandon an in-flight transfer, so the
give cannot race a live transfer's DMA out of `tx_report`. A stale-sem
force-reclaim in the send path was considered and REJECTED: a pending
interrupt-IN transfer has no deadline when the host merely stops
polling, and reclaiming would overwrite the buffer the endpoint is
still armed to DMA from (reasoning in the `usb_conn_state_listener`
comment). The "small USB TX ring" stays a separate polish item -- it is
no longer needed for correctness here.

- [x] RETEST PASSED 2026-08-29 (fix `72a26f7` on hardware): three
      mid-scroll cable pulls, three clean recoveries, no power-cycle.
- [ ] BLE disconnect mid-claim (power off host radio or walk away):
      "cleared (BLE profile disconnected)".

## 5. Sleep / wake

- [x] Host sleep with an active claim (2026-08-29, RawTouch live):
      `pmset sleepnow`, 48 s in Deep Idle (pmset log), > the 30 s claim
      timeout. Recovery instant on wake — RawTouch re-claimed before
      the user cleared the lock screen; no stuck suppression, no double
      scroll observed.
- [ ] Keyboard idle/sleep and wake mid-claim: same outcome. WATCH-ITEM:
      confirm the first time the keyboard's own idle-sleep happens
      naturally with RawTouch running.

## 6. The stale-GATT-cache failure (deliberate)

- [x] **Does not reproduce — and that is the finding.** The entire §3
      suite (claim, gated frames, suppression, instant release) passed
      against the PRE-gate pairing with no forget/re-pair. A
      permission-only GATT change leaves the macOS HOGP cache valid; the
      re-pair tax applies only to report-map/attribute-layout changes.
      README's re-pair warning should be softened accordingly at merge
      time.

## 7. Regression sweep (gate never touched)

- [x] Covered across two live sessions: stream scroll (USB + BLE, via
      the fork AND via RawTouch's first live run), wheel fallback with
      consumer quit, tap-to-click, pointer, typing, both pads incl.
      rapid cross-pad alternation (which surfaced and then verified the
      fix for the host-side momentum-lockout defect — RawTouch
      `458d830`), sleep/wake. The pre-existing USB-wedge finding above
      was fixed and retested 3/3 along the way.
