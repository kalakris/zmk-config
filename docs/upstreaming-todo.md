# Raw touch stream — pre-upstreaming punch list

> **Read [publish-strategy.md](publish-strategy.md) first** — it ranks
> these by realistic acceptance odds and reorders the plan. Headlines:
> ZMK core is effectively closed (~1 outside core PR/month), so the touch
> stream ships as an **out-of-tree module** (proven pattern, no ZMK fork)
> rather than a core PR; Zephyr and LinearMouse are open and fast, so the
> small patches go first; and lead publication with a video, not a spec.

Deferred findings from the v2 cleanup reviews (2026-08-25), to implement
before (or as part of) upstreaming. Sources: 4-angle /simplify review +
adversarial cross-review of the v2 implementation.

## LinearMouse fork (`kalakris/linearmouse`, branch `go60-inputscale`)

- [ ] **Delete the deprecated host tap-to-click path** (~560 lines:
  `TouchTapRecognizer`, `TouchStreamClickPoster`, `TapToClick` config,
  manager wiring, tests, docs). Blocked on: firmware tap-to-click passing
  hardware validation. Both enabled at once = double-click, so the host
  path must not survive into an upstream PR.
- [ ] **Device-agnostic discovery**: drop the hardcoded ZMK VID/PID
  (0x16C0/0x27D9) from the IOHIDManager matching dictionary in
  `TouchStreamManager`. The usage-pair match (0xFF00/0x01) plus the
  feature-report version handshake already reject non-streaming devices;
  VID/PID filtering makes the feature ZMK-specific for no benefit.
- [ ] **Split the branch into two PRs**: (a) `scrolling.smoothed.inputScale`
  + GUI slider + docs (first two commits, self-contained, generic);
  (b) the touch-stream feature. They are interleaved on one branch today.
- [ ] Tiny separate upstream fix: `SmoothedScrollingTransformer`'s per-event
  `os_log` at `.info` should be `.debug` (we already fixed our poster;
  upstream's line was deliberately left alone in the fork).
- [ ] Config hygiene when the dust settles: the Go60 scheme still carries
  v0 smoothed-wheel tuning (`inputScale 0.03`) that is dead while the
  stream is active and mistuned for the ÷8 fallback; retune or remove.
  Also refresh/delete the stale v0 `linearmouse/linearmouse.json`
  snapshot on the zmk-config `raw-touch` branch when merging.

## ZMK fork (`kalakris/zmk`, branch `raw-touch`)

- [ ] **Move `stream-tap-click` / `stream-tap-max-ms` /
  `stream-tap-max-movement` out of the Cirque driver binding** into
  ZMK-owned config (Kconfig options or a ZMK-side DT node). The driver
  explicitly ignores these; consumer config does not belong in the shared
  driver binding. **Now also the migration enabler** — with the props and
  `INPUT_ABS_*` consumption on our own node, the module works against
  Zephyr's in-tree driver unmodified and our Cirque fork can be deleted.
- [ ] **hog.c hardcoded GATT attribute indices**: propose an
  index-computing cleanup (enum arithmetic or report-reference lookup at
  init) as its OWN upstream PR — it fixes all four report senders and
  should not be buried in the feature branch. Our feature followed the
  existing hardcoded convention on purpose.
- [ ] **Submit the ungated zero-report suppression as a standalone bugfix
  PR** (commit `cfc4b3e6` was deliberately written upstream-shaped: any
  sync accumulating no nonzero motion and no button transitions skips the
  mouse report).
- [ ] Do NOT macro-generalize the per-report HID plumbing across
  hid.c/usb_hid.c/endpoints.c/hog.c — the four-file repetition is
  upstream's own convention and matching it is what keeps the diff
  reviewable.

## cirque-input-module fork (`kalakris/cirque-input-module`)

**PLAN SUPERSEDED (2026-08-26)** — see docs/pinnacle-driver-landscape.md.
Zephyr's in-tree `input_pinnacle` driver (on ZMK main via the Zephyr 4.1
bump) has had absolute mode since Feb 2024, and petejohanson's module is
effectively EOL (abs-mode PRs from others already rotting there). Revised:

- [ ] ~~PR `abs-mode` to petejohanson/cirque-input-module~~ — CANCELLED,
  redundant with upstream `data-mode = "absolute"`.
- [ ] Refactor our ZMK touch-stream module to be driver-independent:
  move `stream-tap-*` props onto our own node, consume standard
  `INPUT_ABS_X/Y/Z` (folds in the "tap props out of the driver binding"
  item above — same work, stronger motivation).
- [ ] Rebase `kalakris/zmk:raw-touch` onto `moergo-sc/zmk@zephyr-4-1`
  (MoErgo's in-tree-driver migration branch) once MoErgo blesses it.
  Port forward: 0xFF STATUS1 glitch guard, ERA Z-min, activity-tied
  sleep. Gain: `idle-packets-count` DT prop, SW-reset-on-init (possible
  proper fix for the baseline-drift jitter), hw clipping/scaling.
- [ ] Driver-work upstream target is now **Zephyr**, not the module:
  0xFF guard / ERA Z-min / secondary-tap control as small PRs; plus a
  trivial ZMK docs PR (pointing.mdx still shows old module props).

## Protocol / design decisions to settle (not code cleanups)

- [ ] **Gate frame streaming on scroll context?** Today frames stream
  during all touches; pointer-context frames are consumed only by the
  deprecated host tap path. Gating would ~halve BLE airtime while
  pointing (battery win) but forecloses future host-side pointer-context
  features (drag-lock, gestures, host taps). Decide after the host tap
  path is deleted; if gated, keep it firmware-configurable.
- [ ] Publish `docs/raw-touch-protocol.md` as a standalone versioned spec
  so other keyboards (Charybdis, Svalboard, other Cirque boards) and
  other hosts (Linux uinput, Windows RawInput) can implement it.

## Roadmap (post-upstream, not blockers)

- Left pad streaming (`pad_id 1` reserved; needs split-transport work)
- 2D panning / horizontal scroll (frames already carry both axes)
- Tap-and-drag / drag-lock; palm rejection; edge zones
- Battery impact measurement for the 100 Hz stream (for the README)

## Post-review addendum

- [x] Direction polish — DONE (`b9a7858`): Raw Touch's baseline now follows
  the system Natural Scrolling preference (live-observed), so
  `scrolling.reverse` means the same thing in every mode: off = system
  default, on = flipped.

## Licensing (check before any sharing)

- [x] ~~cirque-input-module has NO license~~ — **MOOT once we migrate off it.**
  Both petejohanson's module and our fork lack a LICENSE/SPDX headers, so
  redistributing that driver was legally murky — but the plan is now to drop
  the fork for Zephyr's in-tree driver (Apache-2.0, clean). Only revisit if
  we end up shipping the module fork after all.
- [ ] zmk-config has no LICENSE; add MIT (or similar) covering our scripts,
  docs, and the protocol spec before pointing the community at them. The
  protocol spec is wholly ours — consider CC-BY or MIT explicitly in the doc.
- linearmouse (MIT) and zmk (MIT) forks are clean; keep new files carrying
  the upstream header conventions.

## From the prior-art survey (docs/prior-art-survey.md, 2026-08-26)

Protocol v3 candidates (do before publishing the spec):
- [ ] Per-frame device timestamp (HID Scan Time, 100 µs units) — momentum
  quality over jittery BLE depends on it; every Apple touch report has one
- [ ] Explicit contact-state bits in flags (incl. contact-without-motion →
  host can emit kCGScrollPhaseMayBegin for rubber-band pre-arm)
- [ ] Move geometry to the report descriptor (Logical/Physical Max + Unit,
  0.01 mm) — feature report shrinks to version + orientation + pads
- [ ] Device-side mode gate so vendor frames and relative reports never
  emit concurrently (PTP's one-collection rule; kills double-count risk)
- [ ] Mode re-assert watchdog after sleep/BLE reconnect (hid-magicmouse
  and bcm5974 both learned this the hard way)
- [ ] Serial-number-prefix device matching as the normative spec rule
  (0x16C0/0x27D9 is a shared pool; MoErgo ships compliant serials)
- [ ] Optional single-slot Linux digitizer collection on a SECOND USB HID
  interface (evdev touchpad w/ edge scroll for free); never on the same
  interface (macOS Device-Mode write would kill the pointer fallback)

Host-side tests before release:
- [ ] Scroll Reverser interaction (may misclassify stream as mouse without
  NSEventTypeGesture companions)
- [ ] Apps reading PointDelta/FixedPtDelta fields (Calendar paging, WebKit/
  Chromium, Adobe palettes) — verify our events carry them

Spec authoring:
- [ ] Rename: "touch stream" collides catastrophically with FingerWorks
  TouchStream (the ur-keyboard-touchpad, acquired by Apple). Survey
  recommends "PadWire" (clean) or "ZipTouch" (ZMK-flavored); unit noun
  "touch frame"
- [ ] Standalone spec repo when publishing; cite Tier-1/Tier-2 prior art
  and include the pre-emption paragraphs from survey §6 (VoodooInput,
  PTP gates, halfdane, badjeff/zmk-hid-io, upstream LinearMouse deltas)
- [ ] Test-host TCC hygiene: the xcodebuild test host still raises TCC
  prompts sometimes (TouchStreamManager.start() is guarded; some other
  path — likely event-tap creation or an AX check in app startup — is
  not). Instrument next time it fires during a test run and suppress it
  under ProcessEnvironment.isRunningTest, so agent test runs never prompt.
