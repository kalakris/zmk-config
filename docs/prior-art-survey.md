# Prior-art survey: keyboard-embedded absolute touch streaming over vendor HID

Research report (2026-08-26, Opus deep-research agent) surveying prior art for
the raw-touch-stream protocol before community release. Verbatim findings;
actionable items extracted into docs/upstreaming-todo.md.

## 1. Landscape summary

| Prior work | What it is | Overlap with our design | Differs | Implication |
|---|---|---|---|---|
| **Windows Precision Touchpad (PTP)** | MS spec: Digitizer TLC 0x0D/0x05 + Device Config TLC 0x0D/0x0E, mandatory Contact ID/Tip/Confidence/X/Y + Scan Time + Contact Count, ContactMax feature, 256-byte PTPHQA blob | Same data model: absolute X/Y, per-contact ID, frame timestamp, self-described geometry | Requires **3–5 contacts** and a **≥32×64 mm** sensor. Both fatal for a 40 mm single-touch Cirque | **Differentiate**, cite as the standard we'd have used. The PTPHQA blob is *not* the barrier (see §3) |
| **Apple Accessory Design Guidelines §15 "Trackpads"** | Apple's own accessory-trackpad HID spec: Digitizer page, Finger/Tip/Confidence/Transducer Index/X/Y/Scan Time/Surface Switch/Report Rate | Apple's blessed path for third-party trackpads | **"This feature is supported starting in iPadOS 14.5."** iPadOS only. Requires 2–5 contacts. Explicitly forbids using the Apple VID | **Cite as the definitive proof** that Apple documents third-party trackpads for iPad and *not* for Mac |
| **Linux `hid-multitouch` / Win8 class** | Binds any device whose descriptor contains the PTPHQA feature shape (usage 0xFF00/0xC5, count 256, size 8). No blob validation whatsoever | Would give us a real evdev touchpad on Linux for ~120 descriptor bytes | 1 slot ⇒ pointer + tap + **edge scroll** only; libinput needs ≥2 slots for two-finger scroll, ≥3 for swipe | **Interoperate** — cheapest real win available, but see the macOS hazard in §3 |
| **libinput scrolling model** | Transports scroll *source* (wheel/finger/continuous); explicitly delegates kinetic scrolling to the caller | Same layering as ours: device supplies phase, host supplies physics | libinput gets phase free from finger-down/up; we must declare it | **Cite as validation** of the abstraction boundary |
| **`REL_WHEEL_HI_RES` / HID Resolution Multiplier (GD 0x48)** | 120 units = one detent; MS "Enhanced Wheel Support" | Exact prior art for "the standard quantum is too coarse" | Buys **magnitude** resolution only — no phase, no source, no momentum. Logitech defected to vendor HID++ for the same feature | **Cite as the closest true standards precedent**, and as precedent for defecting to vendor HID |
| **Apple `hid-magicmouse` / MT2 protocol** | Vendor-defined HID; touch report **not even declared in the descriptor**; prefix + N×9-byte bit-packed fingers (x, y, size, touch_major/minor, orientation, pressure, id, 2–4-bit state); MT mode via SET_FEATURE | Structurally the same architecture we chose | They carry ellipse geometry, a per-contact tracking ID, an explicit state lifecycle, and a ~22-bit **device timestamp** | **Cite as strongest precedent.** Steal the timestamp and the state bits (§6) |
| **Apple MT2 feature reports 0xD0/0xD3/0xD9/0xDB** | Device self-describes sensor rows×cols, surface dimensions in 0.01 mm, region descriptor, plus one aggregated capability blob | **Exactly what our 8-byte feature report does** | Theirs is 8 undocumented reports; ours is one versioned report | **Cite — ours is the tidier version.** Consider adopting their 0.01 mm unit |
| **`bcm5974`** | Apple's older internal-trackpad protocol; 28/30-byte `tp_finger` structs; mode switch by read-modify-write of a mode blob, with a re-assert watchdog | Absolute contacts with ellipse + pressure | Not HID at all; **no self-description** → a 14-entry hardcoded per-model magic-number table | **Cite as the cost of not having a version field** |
| **`MultitouchSupport.framework`** | Private macOS API; `MTTouch` carries normalized *and* mm position, each with velocity, plus an 8-state hover→contact→lift lifecycle | Field set validates ours | **Enumerates only `AppleMultitouchDevice` nubs** — unreachable for third-party hardware | **Do not claim as a fallback.** It is not available to us |
| **VoodooInput / VoodooI2C** | Hackintosh kext that gives arbitrary touchpads *native* macOS gestures by impersonating a Magic Trackpad 2 | Same goal, same input (absolute touch) | Solves it at the **source** (fake Apple device, kernel) vs our **sink** (userspace CGEvent synthesis) | **Closest prior art in existence. Pre-empt it explicitly** (§6) |
| **QMK digitizer (`quantum/digitizer.c`)** | Stylus: `{in_range, tip, barrel, x, y}`, coordinates normalized 0..1, single contact, no pressure | Abstract idea of absolute HID from a keyboard | Throws away resolution and pressure at the API boundary; it's an absolute *cursor*, not a touch surface | **Cite as baseline**, differentiate |
| **QMK PR #24964 (george-norton, open since Feb 2025)** | Contact-based trackpad digitizer + firmware-side gesture detection + mouse fallback | Same problem space, same era | Firmware-side gestures; blocked on feature reports (QMK #23243) | **Cite as contemporaneous work.** Its author writes: *"Apple also do not support 3rd party trackpads, so it will only work on Linux hosts"* |
| **halfdane/cirque-input-module + zmk-input-gestures** | ZMK Cirque absolute mode + firmware gestures incl. an inertial *cursor* | Absolute mode in a ZMK Cirque driver; the idea of inertia | Clamps/rescales to screen dimensions at the driver (destroys sensor resolution); never crosses the HID boundary with touch data; inertia is cursor coast, not scroll momentum | **Cite prominently; best collaboration candidate** |
| **petejohanson/cirque-input-module (our fork base)** | Relative-only: `input_report_rel` only, no `input_report_abs`, no Z | — | — | ⚠️ **CORRECTED 2026-08-26** — this survey originally called our abs-mode work "legitimately novel in ZMK". **It is not.** Zephyr's in-tree `input_pinnacle` has had `data-mode = "absolute"` with Z since Feb 2024 and reached ZMK main via the Zephyr 4.1 bump; pete's module is EOL. Never claim abs-mode novelty. See docs/pinnacle-driver-landscape.md |
| **Zephyr in-tree `input_pinnacle` (ZMK main)** | Absolute mode with X/Y/Z, `idle-packets-count`, hw clipping/scaling, SW reset on init | **Supersedes our driver patch entirely** | Lacks: 0xFF STATUS1 glitch guard, ERA Z-min, secondary-tap control, activity-tied sleep. Its listener still never converts ABS→cursor, so a consumer module remains required | **Migrate to it**; upstream our small robustness fixes to Zephyr |
| **QMK Raw HID (0xFF60/0x61), VIA/Vial, XAP, HID-IO, Plover HID (0xFF50), badjeff/zmk-hid-io (0xFF0C)** | Vendor HID sideband + host daemon | **The transport pattern is thoroughly established** | None carry pointer or touch data | **Concede loudly.** Claim novelty only on payload + consumer. Plover HID is the best precedent — it was *merged into mainline Plover* |
| **Mac Mouse Fix** | Deepest open-source Apple-like scroll synthesis: dual type-22 + type-29 events, **velocity-seeded momentum from measured lift-off**, solved drag ODE `v' = -a·v^b` | Our momentum model, almost exactly | Its input is wheel detents, not touch | **Cite as the closest host-side prior art.** Its header: *"they don't contain any raw touch information"* — that's the gap we close |
| **LinearMouse upstream (as of Mar–Aug 2026)** | `SmoothedScrollingEngine` with 6-phase mapping, `NSEventTypeGesture` companions, geometric momentum decay | **More than our doc assumes** | No real touch edges (staleness timeout), momentum not velocity-seeded, touch-to-catch approximated | **⚠️ Verify our "what the fork adds" claim against current main** (§6) |
| **Mos** | 9-state phase machine; sets phase + momentum fields | Phase vocabulary | Momentum is the tail of a lerp filter **relabelled**; `smoothSimTrackpad` is off by default | Cite; differentiate on physics |
| **Wacom macOS driver** | `CMacHIDGestureEventOSX1010::PostGesture` builds type-29 gesture CGEvents with real phases | A third-party absolute-touch device driving macOS gestures from userspace, shipping commercially | Closed source; tablet not keyboard | **Cite — best commercial precedent for our exact architecture** |
| **calftrail/Touch (natevw, 2010)** | `tl_CGEventCreateFromGesture` — splices real touch records into a synthetic type-29 event | Ancestor of the whole userspace-synthesis lineage | — | Cite as lineage |
| **Cyzor/tablet-driver ("MockTab")** | Absolute-tablet → phased macOS scroll with constant-deceleration momentum, max-magnitude release-velocity window, explicit zero-delta momentum-`.end` on catch | **Uncannily close, independently derived** | Three weeks old, 8 stars | Cite as convergent design; **its engineering notes will save us debugging** |
| **UHK touchpad module** | Azoteq IQS572 does gestures **on-chip**; UHK's firmware consumes only the pre-quantized `int8` wheel ticks | Multitouch keyboard trackpad | **A firmware choice, not a chip limit.** The IQS5xx-B000 family exposes per-finger absolute XY over I2C (`ABS_X` @ 0x0016, + touch strength and area) — the ZMK community driver reads it directly | Cite as a cautionary tale about discarding available data, **not** as evidence that raw access is unique to the Pinnacle |
| **Cirque GlidePoint TM105065 USB PTP** | Cirque's own PTP-compatible standalone pad | Same vendor | Different SKU, USB-only, **Windows-only**; not loadable onto Pinnacle dev pads | Differentiate |

## 2. The macOS question, answered

**Claim: macOS gives no gesture support to any non-Apple HID touchpad or digitizer, and this has not changed through macOS 26. Confidence: HIGH.** Established from primary sources, several collected directly from this machine (macOS 26.5.2, build 25F84).

### 2.1 Exhaustive IOKit driver-matching evidence

All **985 `Info.plist` files** under `/System/Library/Extensions` and `/System/Library/DriverExtensions` were parsed for personalities matching the Digitizer usage page (0x0D). Complete result set:

| Kext | Personality | IOClass | Usage pairs | Gate |
|---|---|---|---|---|
| `AppleMultitouchDriver` | `AppleMultitouchHIDService (0x0D,0x04)` | `AppleMultitouchHIDService` | **0x0D/0x04 (Touch *Screen*)** | **`Manufacturer = "Apple"`** |
| `AppleUserHIDDrivers.dext` | `AppleUserHIDEventDriver` | `AppleUserHIDEventService` | page 0x0D wholesale (+ 0x0B/0x0C/0x10/0x90) | none |
| `IOHIDEventDriverSafeBoot` | HID Pointing/Consumer/System Control | `IOHIDEventDriver` | page 0x0D wholesale | none |
| `IOVersatile` | `IOVersatileHID` | — | 0x0D/0x02 (Pen) | none |

**Nothing anywhere in macOS claims Digitizer / Touch Pad (0x0D/0x05) — the PTP usage.** A PTP-compliant touchpad falls through to the generic `AppleUserHIDEventDriver`, which produces `IOHIDEventTypeDigitizer` events and no gestures.

Every Apple multitouch personality is hard-gated to Apple hardware — and binds on **Generic Desktop/Mouse**, not on the digitizer page:

- `AppleTopCaseHIDEventDriver.kext`: all trackpad personalities are `VendorID = 1452` (0x05AC) + explicit `ProductIDArray`, `DeviceUsagePairs = [{0x01, 0x02}]`, `IOClass = AppleMultitouchTrackpadHIDEventDriver`, carrying `MTEventSource = true` and `HIDScrollResolution = 26214400`.
- `AppleBluetoothMultitouch.kext`: **all six** personalities pinned to VendorID 1452/76 and ProductIDs 781/782/784 (Magic Mouse / Magic Trackpad / Magic Mouse 2011).

The `Manufacturer` key is a genuine matching constraint: `IOHIDInterface::matchPropertyTable` delegates to `MatchPropertyTable` in `IOHIDFamilyPrivate.cpp`, which compares `kIOHIDManufacturerKey` (score 100) alongside VID (5000), PID (1000–2000), transport, and usage pairs.

### 2.2 Independent corroboration: VoodooInput closes the loop

`VoodooInput` gives arbitrary touchpads native macOS gestures by publishing an `IOHIDDevice` reporting `VendorID 0x05AC`, `ProductID 0x0272`, `Manufacturer "Apple Inc."`, `Product "Magic Trackpad 2"`, plus a cloned Apple report descriptor and byte-identical MT2 frames.

`0x0272` = **626** — literally the first entry of `ProductIDArray` in AppleTopCase's *"Trackpad Gen2 - USB/SPI - Embedded"* personality on this machine. Two independent lines of evidence agree exactly. **If a third-party HID digitizer could reach Apple's multitouch stack, VoodooInput would not need to exist.**

### 2.3 Apple's own documentation confirms the macOS/iPadOS split

Apple *does* publish a third-party trackpad HID spec — **Accessory Design Guidelines §15 "Trackpads"** (R23, dated 2026-06-08). It mandates the Digitizer page with Finger/Tip Switch/Confidence/Transducer Index/X/Y/Surface Switch, recommends Scan Time and Report Rate, and specifies latency, jitter, and resolution budgets. Deciding lines:

> "This feature is supported starting in **iPadOS 14.5**."

> "Trackpads shall support **2-5 simultaneous contacts** on the digitizer surface."

> "Not identify themselves as Apple-branded accessories, for example, using the Apple Vendor ID (VID)."

macOS is never mentioned. The guidelines contain no mouse or pointing-device section at all — only Keyboards (§14), Trackpads (§15, iPadOS), and Braille (§16) — and no guidance on Resolution Multiplier or high-resolution scrolling.

### 2.4 A developer hit exactly this wall, and Apple never answered

[Apple Developer Forums #768586](https://developer.apple.com/forums/thread/768586) (Nov 2024): a BLE HID multi-touch clickpad, **fully gesture-capable on Windows 11 with no driver**.

> "When enumerating using stock-standard HID digitizer click/touch pad descriptors (those same descriptors used successfully on Windows 11), the device does nothing. No button, no cursor, no gestures, nothing."

A custom HIDDriverKit DEXT got buttons working via `dispatchRelativePointerEvent`, but `IOUserHIDEventService::dispatchDigitizerTouchEvent` produced nothing. Reports demonstrably reached the stack (visible in `IOHIDNXEventTranslatorSessionFilter`; they wake the display). Apple's only reply: *"We can take care of closer look if descriptor provided."* No resolution. A second developer hit the identical wall in Dec 2025.

The posted descriptor begins `05 0D 09 05 A1 01` — Digitizer / Touch Pad / Application. Precisely the collection nothing in macOS claims.

### 2.5 A complete causal explanation of that bug — and a trap for us

`IOHIDEventDriver::processDigitizerElements()`:

```c
if ( _digitizer.deviceModeElement ) {
    _digitizer.deviceModeElement->setValue(1);
    _relative.disabled  = true;
    _multiAxis.disabled = true;
}
```

Per the USB HID Usage Tables (v1.6, Digitizers page), **Device Mode**: *"0 represents reporting as a mouse, 1 represents single input device… and 2 represents multi-input device."* Microsoft's PTP adds **3 = PTP collection**, and instructs devices that *"It is possible for a non-touchpad capable host to send a value other than those listed… the device should interpret the value as zero (0), and switch to mouse mode, since only a touchpad capable operating system will issue mode 3."*

So: macOS sees a Device Configuration collection, writes Device Mode = **1**, the PTP device treats that as "not 3" and switches to **mouse mode** — and macOS has simultaneously set `_relative.disabled = true`, so it ignores the mouse reports it just asked for. Total silence.

**Direct consequence for us:** adding a PTP-shaped collection to our existing HID interface would set `_relative.disabled = true` on the same `IOHIDInterface` and **break our driverless relative-pointer fallback on macOS**. ZMK registers exactly one USB HID device (`usb_hid_register_device` in `app/src/usb_hid.c`), so today all collections share one interface. Confidence: HIGH on the mechanism; MEDIUM on descriptor-shape specifics without hardware test.

### 2.6 What has changed in recent macOS: nothing

Scan run on macOS 26.5.2. Apple's only recent movement was the opposite direction — Boot Camp 6.1.15 (2021) taught *Windows on Mac hardware* to use Apple's trackpad as a PTP device. Searches for macOS 26 changes surfaced only regression reports, no new capability. Confidence: HIGH.

### 2.7 Bottom line

| Sub-claim | Confidence |
|---|---|
| No macOS driver claims Digitizer/Touch Pad 0x0D/0x05 | **HIGH** — exhaustive scan of 985 plists |
| Apple's multitouch stack binds only Apple VID/PID or `Manufacturer = "Apple"` | **HIGH** — plists + IOHIDFamily source + VoodooInput corroboration |
| A standards-compliant PTP touchpad does nothing on macOS | **HIGH** — Apple forum thread with reproducer + driver analysis |
| Apple's third-party trackpad spec is iPadOS-only | **HIGH** — direct quote from ADG R23 |
| `MultitouchSupport.framework` is unreachable for third-party devices | **HIGH** (structural: VoodooInput's existence) |
| Adding a Device Config collection would break our mouse fallback on macOS | **HIGH** on mechanism, **MEDIUM** untested |
| Nothing changed in macOS 26 | **HIGH** for the driver-matching surface |

## 3. Assessment: was vendor HID the right call?

**Yes, and the justification is stronger than the one currently in the doc.** Three independent gates each rule out PTP/digitizer HID for this device:

1. **Contact count.** PTP requires 3–5 simultaneous contacts (HLK test `Device.Digitizer.PrecisionTouchPad.Performance.MinMaxContacts`). Apple's ADG requires 2–5. The Pinnacle reports one x/y/z per packet. Permanent hardware property.
2. **Physical size.** PTP requires a sensor ≥32 × 64 mm via Physical Maximum. A 40 mm circular pad fails on both axes.
3. **macOS.** Even a fully compliant PTP device gets nothing (§2).

**Certification is *not* the reason** — the PTPHQA blob is presence-only on Windows 10/11 (Microsoft publishes a dummy blob), and Linux gates the Win8 class purely on descriptor shape with no validation. We're excluded on physics, not paperwork.

Also genuinely novel: **there is no standard HID way to express gesture phase or scroll intent.** Nothing on page 0x0D; the `Gesture Character` usages are handwriting recognition; the only `Gesture State` usages live on the Sensors page (chassis flip/hinge fold). Our scroll-context bit fills a real gap.

### Should we add a standard digitizer collection for Windows/Linux?

**Recommendation: yes for Linux, as an opt-in, on a separate interface — not the default, not naively.**

| | Verdict |
|---|---|
| **Linux** | **Worth it.** ~120 descriptor bytes (Touch Pad TLC, one Finger collection, Contact ID/Tip/Confidence/X/Y, Scan Time, Contact Count, ContactMax=1, dummy PTPHQA) gets `hid-multitouch` to bind, producing a proper evdev touchpad with physical scaling. Ceiling: 1 slot ⇒ pointer + tap + **edge scroll**, never gestures |
| **Windows** | **Not worth it.** Fails contact-count and sensor-size gates; every PTP gesture is multi-finger |
| **macOS** | **Actively harmful.** §2.5 — the Device Configuration collection would disable our relative-pointer fallback |

Hard constraints on how:

- **Put it on a second USB HID interface** (`CONFIG_USB_HID_DEVICE_COUNT` + a second `usb_hid_register_device()`; ZMK registers one today). Separate interfaces = separate `IOHIDInterface` nubs, so macOS's `_relative.disabled` can't reach the mouse collection. BLE caveat: HOGP permits multiple HID Service instances (confidence MEDIUM); ZMK implements one.
- **Do not adopt the standard Device Mode usage (0x0D/0x52)** for our mode switching — it's a macOS tripwire. Use a vendor field in our feature report.
- **Fix the dual-emit hygiene issue regardless.** PTP's "one collection at a time" rule is sound; concurrent vendor frames + relative reports risk double-counted motion on hosts that open both. A device-side mode gate is the robust answer.

### Things worth adopting (protocol v3 candidates)

1. **Per-frame device timestamp** — every Apple touch report has one (~22-bit ms); host arrival times are useless for velocity over jittery BLE. Use HID's native **100 µs Scan Time** encoding (0x0D/0x56). Highest-value single addition: momentum quality depends on lift-off velocity accuracy.
2. **Explicit contact-state bits** — Apple has a start/drag/lift lifecycle; `MTTouch` has 8 states. macOS has `kCGScrollPhaseMayBegin` (128) — "finger down, not yet moving" — that rubber-banding scroll views use to pre-arm. If flags can't distinguish contact-without-motion, feel is subtly wrong.
3. **Geometry into the report descriptor** — resolution/extents are exactly `Logical Maximum` + `Physical Maximum` + `Unit` + `Unit Exponent` (how PTP and Apple's 0xD9 do it, in 0.01 mm units); generic HID tooling then computes counts/mm free, feature report shrinks to version + orientation + pads.
4. **Mode re-assert watchdog** — `hid-magicmouse` and `bcm5974` re-send mode-enable after resume because devices silently revert. ZMK sleep/BLE reconnect are exactly this case.
5. **Serial-number device matching, normatively.** 0x16C0/0x27D9 is Objective Development's free shared pool; V-USB terms require serial-based lookup. MoErgo complies (`SerialNumber = "moergo.com:GO60-…"`, distinct per half); stock ZMK does not emit a compliant serial (the generator lives in the Glove80 board dir) — exactly why our Sofle collides. The serial prefix is the correct cross-platform spec answer; `HIDPhysicalDeviceIdentity` remains the macOS implementation detail.

## 4. Closest prior works to cite

**Tier 1 — must cite, must address directly**

1. **`drivers/hid/hid-magicmouse.c`** + Magic Trackpad 2 descriptor dumps (gist cobbal/4771297, Apple Forums #69863). Apple's own multitouch protocol is vendor-defined HID on page 0xFF00, touch report not even declared in the descriptor. Same architectural choice, more transparently.
2. **`acidanthera/VoodooInput` / VoodooI2C.** Same goal, opposite fork: impersonate Apple hardware in a kext (source) vs userspace synthesis (sink). Requires an explicit "why not this" paragraph (§6).
3. **Apple Developer Forums #768586** — Windows-working PTP device doing nothing on macOS, unanswered by Apple. Best citation for the premise.
4. **Apple Accessory Design Guidelines R23 §15** — third-party trackpad spec, explicitly iPadOS-only, 2–5 contacts.
5. **Mac Mouse Fix** `GestureScrollSimulator.m` / `DragCurve.swift` — velocity-seeded momentum from measured lift-off, solved drag ODE. Header line: *"they don't contain any raw touch information"* — that's the gap we close.

**Tier 2 — cite for lineage and honesty**

6. **QMK PR #24964** (george-norton) + Peacock/Procyon/Dilemma v3 — contemporaneous; author independently concludes macOS is a dead end.
7. **halfdane/cirque-input-module + zmk-input-gestures** — the ZMK absolute-mode precedent; best collaboration candidate.
8. **Plover HID** (0xFF50, merged into mainline Plover) — "vendor HID + host consumer for a modality HID doesn't model" is an accepted design. Plus QMK Raw HID (0xFF60/0x61), HID-IO, XAP, `badjeff/zmk-hid-io` (0xFF0C) for the transport pattern.
9. **Wacom's macOS driver** (`CMacHIDGestureEventOSX1010::PostGesture`) and **calftrail/Touch** — commercial and ancestral precedent for third-party absolute-touch → phased macOS gestures from userspace.
10. **`Cyzor/tablet-driver` (MockTab)** — convergent independent design (3 weeks old). Directly actionable notes: never set both phase fields nonzero; `CGEvent(scrollWheelEvent2Source:)` leaves point/fixed-point deltas zero (WebKit, Calendar, Adobe palettes silently ignore such events); post explicit zero-delta momentum-`.end` when catching a coast.
11. **libinput's scrolling model** and **`REL_WHEEL_HI_RES` / Resolution Multiplier** — abstraction-boundary and resolution-escalation precedents.
12. **`bcm5974`** — the 14-entry hardcoded model table as the cautionary tale for omitting self-description.

## 5. Naming

**"TouchStream" must go. Confidence: HIGH.** **FingerWorks TouchStream LP / ST** (1998–2005) was *a multitouch split ergonomic keyboard that streamed absolute finger contacts to a host for gesture synthesis* — our problem statement verbatim. Apple acquired the IP and both founders (Elias, Westerman) in June 2005; it became the foundation of iPhone multitouch. Units are in the V&A and the Smithsonian. The r/ErgoMechKeyboards audience knows this history intimately.

Further claimants: **Touchstream Technologies** (active patent-litigation entity, $338.7M verdict v. Google 2023); **Touchstream** (OTT video, acquired 2025); `buserror/touchstream` (102★) and `disarmyouwitha/AppleMagicTouchstreamLP` (85★) on GitHub. Android also uses "touch stream" informally for MotionEvent sequences.

### The unit noun: "touch frame"

Microsoft's word for one timestamped snapshot of all current contacts is **frame**; HID and Apple agree. Adopting it makes the spec self-documenting to anyone who has implemented PTP. No protocol-name collision found.

### Candidates, collision-checked

| Name | Status | Note |
|---|---|---|
| **PadWire** | npm/PyPI/crates free; GitHub 0 repos; no company/product | Cleanest slate tested. "Wire protocol for a pad" |
| **Absolute Touch Reporting** | Nothing to collide with | Maps onto QMK's `CIRQUE_PINNACLE_ABSOLUTE_MODE` vocabulary; boring-names register. Don't abbreviate to ATR (smartcard) |
| **ZipTouch** (DT label `zip_touch_frames`) | Free; GitHub 0 repos | Best ZMK fit (`zip_` prefix). Parochial if QMK adoption matters |
| **TouchPipe / ContactPipe** | Free | Serviceable fallbacks |

Rejected after checking: TouchCast (live trademark), PointStream, FrameLink, RawPad, PadFrame, RawContact, anything Glide-flavored (GlidePoint® is Cirque's registered mark).

**Recommendation: PadWire** — "a vendor-HID protocol for streaming absolute **touch frames** from a keyboard to a host." If ZMK-community fit matters more: **ZipTouch**.

### Usage-page note

macOS kext scan: on **0xFF00**, Apple claims usages 0x04, 0x0B, 0x0D, 0x16, 0x23, 0x29, 0x4B, 0x4D — **0x01 (ours) is free**. On 0xFF60, Apple's `MTUserDevice` claims 0x07 and QMK Raw HID occupies 0x61. So stay on 0xFF00/0x01 — it's also the page Apple's own MT2 protocol uses. Real hazard: 0xFF00/0x01 is the most commonly squatted vendor pair in the industry, so make it normative: *hosts MUST validate the feature report before treating a 0xFF00/0x01 collection as this protocol*, combined with serial-prefix matching.

## 6. What would embarrass us (pre-emptions needed)

1. **"You reinvented VoodooInput."** Highest risk. Answer: kernel extension (dead on Apple Silicon), spoofs Apple's VID (forbidden by Apple's own ADG), VoodooI2C is Intel-only, and MT2 emulation demands multi-finger input a single-touch Pinnacle cannot supply.
2. **"Should have shipped a PTP descriptor."** The three gates (§3); certification was never the obstacle.
3. **"Fork claims may be stale."** Upstream LinearMouse landed `SmoothedScrollingEngine` (PR #1108, 2026-03-21 → #1337, 2026-08-07). Defensible fork-specific claims are narrower: real touch edges vs staleness timeout; momentum from *measured* lift-off velocity; touch-to-catch on actual finger-down. Upstream also has a raw-HID input-report path (`InputReportHandler.swift`) — cite as an upstreaming seam.
4. **"Your frame is poorer than Apple's."** True: no ellipse (meaningless for single-touch) — but the missing timestamp and state bits are real gaps to close, not argue away.
5. **"Why not build on `badjeff/zmk-hid-io`?"** Same input-processor idiom, different payload. One-paragraph answer needed.
6. **"You didn't cite halfdane."** Cite, differentiate (they rescale to screen coords destroying resolution; cursor coast ≠ phased scroll momentum), approach by name.
7. **"Gesture intent doesn't belong on the wire."** Apple keeps interpretation host-side. Ours is justified — the ZMK layer state that determines scroll-vs-point exists only on the keyboard — but document the bit properly.
8. **"Dual-mode emits two collections at once."** PTP forbids it; add a device-side mode gate.
9. **Two interop landmines to test before release:** (a) `IsContinuous = 1` without `NSEventTypeGesture` companions may make **Scroll Reverser** misclassify the stream as a mouse and invert it; (b) zeroed `PointDeltaAxis*`/`FixedPtDeltaAxis*` fields are silently ignored by Calendar's paged recognizer, WebKit/Chromium gesture-scroll, and Adobe palettes.
10. **Frame rate**: ~100 Hz sits just under PTP's 125 Hz single-contact floor — disclose as a measured choice.
11. **"You forked a driver to add absolute mode that Zephyr has had since 2024."** ⚠️ **HIGH RISK, added 2026-08-26** — and true. Zephyr's in-tree `input_pinnacle` (on ZMK main via the Zephyr 4.1 bump) supports `data-mode = "absolute"` with Z reporting; petejohanson's module, which we forked, is EOL. Never present abs-mode as our contribution. Correct framing: *we started on the module because that's what MoErgo's ZMK fork pins; the in-tree driver supersedes that patch and we migrate to it.* Our contribution is the touch-stream module + protocol + host, which the in-tree driver does not touch (ZMK still never converts ABS→cursor). Full detail: docs/pinnacle-driver-landscape.md.

**Nothing invalidates the design.** No project anywhere consumes raw touch from a third-party device on macOS *and* posts phased scroll — Wacom's closed driver and MockTab are the only neighbors. Concede the transport pattern generously; the payload, the self-describing feature report, and the touch-derived momentum engine are ours.
