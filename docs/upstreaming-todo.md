# Raw touch stream — pre-upstreaming punch list

> **Read [publish-strategy.md](publish-strategy.md) first** — it ranks
> these by realistic acceptance odds and reorders the plan. Headlines:
> ZMK core is effectively closed (~1 outside core PR/month), so the touch
> stream ships as an **out-of-tree module** (proven pattern, no ZMK fork)
> rather than a core PR — **as of 2026-08-26 that module is built and
> CI-green**: [module-publish-brief.md](module-publish-brief.md); Zephyr and
> LinearMouse are open and fast, so the small patches go first; and lead
> publication with a video, not a spec.

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

## Raw touch module (`kalakris/zmk-raw-touch-wip`)

**Retitled 2026-08-26** — this section used to be "ZMK fork
(`kalakris/zmk`, branch `raw-touch`)". The fork is no longer load-bearing:
the stream now lives in an out-of-tree module built against stock
`moergo-sc/zmk`.

- [x] **Move `stream-tap-click` / `stream-tap-max-ms` /
  `stream-tap-max-movement` out of the Cirque driver binding** — DONE. They
  are `tap-click` / `tap-max-ms` / `tap-max-movement` on the module's own
  `zmk,raw-touch-pad` node, along with the pad geometry (`rotate-90`,
  `x-invert`, `y-invert`, `x-max`, `y-max`, `resolution`, `pad-id`). The
  module consumes standard `INPUT_ABS_X/Y/Z` and knows nothing about any
  ASIC, so it works against Zephyr's in-tree driver unmodified.
- [ ] **BEFORE deleting `kalakris/zmk`**: that fork is the only place the
  two upstream-shaped bugfix commits exist. Cherry-pick or export them
  first — see the next item.
- [ ] **Submit the ungated zero-report suppression as a standalone bugfix
  PR** (commit `cfc4b3e6`, deliberately written upstream-shaped: any sync
  accumulating no nonzero motion and no button transitions skips the mouse
  report). Note the module does *not* carry this as a core change — it is
  reimplemented as the `zip_raw_touch_idle_filter` input processor, which is
  the right shape for a module but the wrong shape for an upstream PR. The
  ZMK-core version only exists on the fork.
- [ ] **hog.c hardcoded GATT attribute indices**: propose an
  index-computing cleanup (enum arithmetic or report-reference lookup at
  init) as its OWN upstream PR — it fixes all four report senders and
  should not be buried in the feature branch. Our feature followed the
  existing hardcoded convention on purpose, and the module's vendored
  `hog.c` still does.
- [ ] Do NOT macro-generalize the per-report HID plumbing across
  hid.c/usb_hid.c/endpoints.c/hog.c — the four-file repetition is
  upstream's own convention and matching it is what keeps the diff
  reviewable. Still true of the module's vendored copy.

## cirque-input-module fork (`kalakris/cirque-input-module`)

**PLAN SUPERSEDED (2026-08-26)** — see docs/pinnacle-driver-landscape.md.
Zephyr's in-tree `input_pinnacle` driver (on ZMK main via the Zephyr 4.1
bump) has had absolute mode since Feb 2024, and petejohanson's module is
effectively EOL (abs-mode PRs from others already rotting there). Revised:

- [ ] ~~PR `abs-mode` to petejohanson/cirque-input-module~~ — CANCELLED,
  redundant with upstream `data-mode = "absolute"`.
- [x] Refactor our touch-stream code to be driver-independent — DONE
  (2026-08-26). `tap-*` props and pad geometry live on the module's own
  `zmk,raw-touch-pad` node and it consumes standard `INPUT_ABS_X/Y/Z`.
  Nothing in the module knows about the Pinnacle.
- [x] ~~Rebase `kalakris/zmk:raw-touch` onto `moergo-sc/zmk@zephyr-4-1`~~ —
  **OBSOLETE**: there is no ZMK fork left to rebase, and adopting the
  in-tree driver no longer requires waiting for MoErgo. It is done on
  `zmk-config@module-port-intree` at Zephyr **3.5**, via
  `kalakris/cirque-input-module@intree-driver` — Zephyr main's
  `input_pinnacle.c` vendored pristine (`27150c9d`) plus three labelled
  patches: 0xFF/SW_DR guard, per-axis ERA edge sensitivity,
  force-recalibrate-on-init. SW-reset-on-init came free (already upstream,
  landed after v4.1.0 was cut), as did `idle-packets-count` — which is
  **mandatory**, since upstream defaults it to 0 and no lift-off packets
  means no release frame, no momentum, no tap. Not yet benched.
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
  the upstream header conventions. The raw touch module ships MIT with
  SPDX headers throughout.

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
