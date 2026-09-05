# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-28
(end of day — items a, b, c, i all closed today).
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

**Current state (2026-09-04, evening):** item p pass 2 (sub-items 1, 6, 7)
is committed and pushed in all three repos and CI-green, but the RH is
NOT yet flashed with `e689a8c` and the RawTouch app is NOT yet rebuilt
from `18c2eb0` — do both first (flash RH; then quit app → `make-app.sh`
→ relaunch → re-grant Accessibility unless `RAWTOUCH_SIGN_ID` is set),
then a feel test over USB and BLE. **The bundle ID changed to
`io.github.kalakris.RawTouch` (rawtouch `4e7c553`), so that deploy needs
a fresh Accessibility grant even with `RAWTOUCH_SIGN_ID`** (TCC keys on
the bundle ID). Item p sub-items 2, 4, 5, 8 await the user's decisions;
signing/notarization (item f) is deferred by the user. Everything before that: item q (physical-1:1
gain defaults, per-transport latency) is deployed; all three repos are
pushed (rawtouch is now a private GitHub repo). RawTouch app runs with
**display-rate resampling, carry semantics, latency 0** (item o) — feel
test PASSED, Safari `--resample` lockout sweep rerun 3/3 ok (98–100 %
applied, response 100–133 ms after the edge), plain-drag total 2082–2083
vs 2077 before = the ~6 px carried offset the offline bench predicts at
latency 0, and a 100 Hz cadence capture over BLE confirmed both pads at
~10 ms device spacing (pad 0 p50 9.70 / p90 10.30 ms; pad 1 p50 9.70 /
p90 9.90 ms; zero drops; the usual 15 ms BLE arrival batching). Release
prep pass 1 (item p) is committed, CI-verified and flashed. The Safari
ProMotion lockout is fixed at the source (item n, poster began carries
the first motion delta). Firmware: both halves on current `main` with
the pad sample rate at the 100 SPS default (the 120 experiment proved
the ASIC clamps at 100). `scripts/flash-go60.sh` now self-exits when
the requested halves are flashed, locks against a second watcher, and
waits indefinitely (user preference). LinearMouse `stock-inputscale`
runs alongside RawTouch for pointer processing. Everything committed
and pushed in all repos.

In flight / current state (2026-08-31): **RawTouch is the live scroll
host** (menubar app — see item k for the full operational state;
LinearMouse stays quit). Items l (claim-gated frame emission) and m
(Standard mode / RawTouch mode naming) are **DEPLOYED AND VALIDATED
LIVE**: both halves flashed from current `main` (LH is finally on
current main — the pre-claim-build note is closed), the app rebuilt and
relaunched, scrolling confirmed. The stable-signature TCC carry-over
also proved out: the rebuild needed no Accessibility re-grant. Both
halves now run the emission-gated firmware, so a quit app = Standard
mode with a silent stream. Monitoring: with the app running, run
`raw-touch-monitor.swift` passively (the app's claim keeps frames
flowing; HID reports fan out to all clients — preferred debug mode);
`--claim` is only for capturing with no host running, and its caveat is
the observer effect (wheel fallback stops while held), not double
scroll. The merged `mode-gate` branches
(both repos) are deletable. **The menubar app (item k #1) is LIVE** (the user
switched over 2026-08-30 — settings work, both pads scroll, the
enabled-toggle returns the keyboard to Standard mode, icons render
well). The iTerm-tab arrangement is retired. Previous round's flashes
(boot-race fix, poll tuning, click keymap, `hold-while-undecided`) all
validated.

## a. Fix the dead-pad boot race (firmware, real bug) — DONE 2026-08-28

**Fixed 2026-08-28.** The amended patch 3/3 (clear SW_CC and re-verify
DR after the recalibrate) is the tip of `cirque-input-module@
intree-driver` (SHA `89a08962`); the pinned revision in
`config/west.yml` is bumped and pushed on zmk-config `main`, and both
halves are flashed. **Hardware verification ongoing**: any dead pad on
a future boot is now a real bug, not the known race. Fixing this was the MUST-FIX gate before upstreaming the
ERA/recalibrate patches to Zephyr (see
[upstreaming-todo.md](upstreaming-todo.md)).

## b. Re-land the LinearMouse cleanup — DONE 2026-08-28

The reverted `a204c40` batch was **exonerated** by the discriminating
deploy from [linearmouse-reland-plan.md](linearmouse-reland-plan.md):
the exact batch build ran clean for 26 min of live use, pinning the
original regression on deploy-environment TCC-grant state, not the
code. All items re-landed on `main` as three commits (frame parser +
guard order; VerifiedDevice merge + capabilities; lazy watchdog +
process-routed release, adapted to the per-pad dict), minus the
obsolete pad-0 collapse (conflicts with pad-1 arbitration). Full unit
suite green, all six live scroll checks passed. The old
`go60-inputscale` branch is deleted (remote); local tag
`pre-restructure-go60-inputscale` keeps the forensic history reachable.

## c. LH pointer choppiness — FIXED 2026-08-28 (wired-split poll cadence)

**Root-caused, fixed, and hardware-validated the same day.** The fix is
the two-line Kconfig change in `config/go60_rh.conf` (poll timeouts
20/15 → 3/5 ms); with it, the wire beats every other configuration:

| Split link | Host link | Dropped | Cadence at host |
|---|---|---|---|
| Wire (stock 20/15) | USB | **45%** | 2–3-frame bursts every ~22.5 ms |
| Wire (stock 20/15) | BLE | 0% | same 22.5 ms bursts, re-batched by the ~15 ms host BLE interval |
| Radio | BLE | 0% | ~7.5/15 ms alternation, small batches |
| Radio | USB | 0% | ~7.5/15 ms alternation, no batching |
| **Wire (tuned 3/5)** | USB | **0%** | **~10 ms per-frame, batch size 1.00, σ≈2 ms — LH ≈ RH parity** |

Validated over ~30 s of continuous LH streaming (zero drops, zero
batching, LH device timestamps now honest to ±2 ms) plus typing with no
missed LH keystrokes (the collision risk of the tighter half-duplex
turnaround did not materialize; margin math said ~1 ms per exchange at
921600 baud). LH key latency improves too — keys ride the same poll
loop.

The RH pad is a clean 10 ms metronome in every configuration. **Root
cause: the wired split transport's batched delivery** — the Go60's
wired split is *half-duplex polled*: the peripheral only transmits when
the central sends a `POLL_EVENTS` command, and the central schedules
the next poll `CONFIG_ZMK_SPLIT_WIRED_HALF_DUPLEX_RX_COMPLETE_TIMEOUT`
(default 20, applied as **ms** in `publish_events_work`,
`app/src/split/wired/central.c`) after the previous response — plus
~2.5 ms of turnaround, giving the measured ~22.5 ms burst period. See
raw-touch.md → "Split-link timing" for the full mechanism. (Running the
halves wireless was the interim mitigation before the tuning landed; it
is no longer needed.) Captures lived in
`/tmp/claude-501/capture[2-6].csv` (wire+USB, wire+BLE, radio+BLE,
radio+USB, tuned-wire+USB) — `/tmp` is volatile; re-capture if gone.

Residual items, all **optional polish** now (the fixes they were
designed for no longer occur under the tuned wire):

1. A small USB TX ring in the module's `src/raw_touch_usb_hid.c` — the
   single-slot `hid_sem` drop path only bites on multi-frame bursts,
   which the tuned poll cadence no longer produces. Worth doing only as
   robustness for users running stock poll timings.
2. Central-side timestamp reconstruction (LH stamps are relay-arrival,
   not sample time — but the error is now ±2 ms over the tuned wire,
   ±4 ms over radio; the split relay carries no timestamps, so this
   must be inferred). Only worth it if scroll feel ever regresses.
3. Host pointer synthesis from stream frames — refinement, not a fix;
   pointer deltas now arrive per-frame anyway.

Watch-item: battery on both halves (more UART/CPU wakeups from the 3×
faster idle poll; each half runs on its own battery — the wire carries
data, not power). If LH keystrokes ever start dropping with the wire
in, that's the half-duplex collision signature — back off to e.g. 5/8.

## d. Demo video

Shot list agreed: **30-second cut, cold-open on the catch, a contrast
cut with LinearMouse quit** (÷24 wheel fallback vs the stream), **hands
cam tight on the right half**. The module README is written demo-first;
the release leads with this clip.

## e. Public release train — REROUTED through RawTouch (2026-08-30)

**The host artifact is RawTouch, not the LinearMouse fork** (the fork
will never be released — see item k). Revised order: RawTouch menubar
app (item k #1) → demo video (d) → RawTouch GitHub repo + notarized
release (f) → flip `kalakris/zmk-raw-touch` public → un-vendor
(uncomment the west entry in `config/west.yml`, delete `vendor/`, drop
the two `cmake-args` from `build.yaml`) → announce. The old
prerequisite "host cleanup re-landed (b)" is DONE (2026-08-28) and was
a fork-era concern anyway.

## f. Notarized releases — now a RawTouch prerequisite

Applies to **RawTouch** (the fork's inherited pipeline is irrelevant —
it stays private). A publicly-distributed background tool that demands
an Accessibility grant effectively requires Developer ID signing +
notarization: paid Apple Developer Program membership, signing cert +
notarization credentials as repo secrets, a release workflow in the
future RawTouch GitHub repo. Optional: provenance via
`actions/attest-build-provenance`. Promoted from optional-polish to
release prerequisite when the standalone-host decision landed
(next-steps k, 2026-08-28/30).

## g. Upstream patches

- Cirque driver patches to **Zephyr** (0xFF/SW_DR guard, ERA edge
  sensitivity — after fixing (a)): **message Pete Johanson first**; the
  patches are his unfinished migration.
- `inputScale` PR to **LinearMouse**, framed as generalizing
  `LogitechHighResolutionWheelNormalizer`. The PR-ready base now exists:
  `stock-inputscale` (2026-08-31, upstream `9843332` + the 2 commits,
  UI commit amended to drop the fork-only `fieldRange:` param). Same
  branch is what's installed at /Applications/LinearMouse.app — pointer
  processing + inputScale, no touch-stream code, runs alongside
  RawTouch.

## h. Delete the `kalakris/zmk` fork

Nothing depends on it; `cfc4b3e6` is already salvaged as
`patches/zmk-skip-empty-mouse-report-syncs.patch` on `main`. Just
delete the GitHub repo and `~/src/zmk`.

## i. Drop protocol v2 from the LinearMouse fork — DONE 2026-08-28

Landed on the fork's `main` (net −142 lines): fixed 11-byte frame
layout, capability validation admits v3 alone, v2 tests deleted and
the capability tests re-anchored on the v3 fixture (ground-truth
direction table assertions unchanged). Unit suite green, live scroll
test passed. **Standing caveat**: the `v0-prototype` rollback binaries
(and the historical `raw-touch` branch build) speak v2 — rolling back
to them now gets wheel-fallback scrolling only.

## j. Device-side mode gate — DONE 2026-08-30, merged to main

**Complete and mainline.** Full story: wire contract in
[mode-gate-plan.md](mode-gate-plan.md) (now historical); measurements
and findings in the module repo's `BENCH-mode-gate.md` (retired from the
module 2026-09-02, release prep; recoverable from git history) — every
section passed. Highlights: claim/refresh/release/expiry/clamp verified over
USB and BLE; endpoint switch-away/switch-back/two-host handoff
seamless; **§6 does not reproduce** — a permission-only GATT change
leaves the macOS HOGP cache valid, so upgrading across the gate needs
NO forget/re-pair (README warning softened accordingly). Two real
firmware bugs found and fixed along the way: (1) expiry never fired —
`k_work_delayable_is_pending()` counts `K_WORK_RUNNING`, so the expiry
callback always saw itself as a racing refresh (fixed `20c84e5`);
(2) the pre-existing hid_sem USB wedge — a mid-transfer cable pull
leaked the single-slot semaphore, killing the vendor USB stream until
power-cycle (fixed `72a26f7` via a `zmk_usb_conn_state_changed`
listener; retested 3/3 on hardware). `mode-gate` is merged into both
repos' `main` (module `859b03e`, zmk-config merge `3535109`); the
`mode-gate` branches are now redundant and can be deleted. Bench
tools kept on main: `scripts/gate-claim.swift` (claim/release/hold/raw,
usb|ble pin; handles the macOS quirk that USB feature-report GETs come
back report-ID-prefixed while BLE's come bare).

## k. RawTouch — the standalone host program — LIVE 2026-08-30

**Built, validated live, and now the daily scroll driver.** Local repo
`~/src/rawtouch` @ `458d830` (no remote yet): SwiftPM, executable
`rawtouch` over `RawTouchCore` + `RawTouchSPI`, 91 tests green. Named
**RawTouch** (pairs with zmk-raw-touch; retires PadWire — one brand
for module + protocol + host; "agent" avoided, it now connotes AI).
First live run 2026-08-30: gate-arbitrated momentum scrolling on both
pads, cross-pad alternation, sleep/wake (48 s Deep Idle → instant
re-claim on wake, no double scroll), and a mid-scroll cable pull all
passed. The live run surfaced a host-side defect inherited from the
fork: first-touch-wins pad arbitration held the claim through the
5–10 s momentum tail, locking the other pad out — fixed by
**cross-pad catch** (`458d830`: a scroll touch on the other pad
during a momentum tail steals the claim and closes the old series;
an active touch still wins). The fork keeps this bug — see below.

Operational state:
- `rawtouch` runs FOREGROUND in an iTerm tab (burn-in mode; deliberate
  choice). Accessibility is granted to **iTerm** (CLI under a terminal
  attributes TCC to the terminal — no prompt appears, grant manually
  in System Settings if needed). The LaunchAgent install
  (`resources/com.kalakris.RawTouch.plist`, program `$HOME/bin/rawtouch`)
  is the next step after burn-in and will need its OWN Accessibility
  grant. Binary installed at `~/bin/rawtouch`.
- Config: `~/.config/rawtouch/config.json` — populated with the tuned
  values from the LinearMouse config (scale 0.6 — the library default
  0.25 is why the first run felt slow). Load-at-start only; live-reload
  is a menubar-app work item.
- LinearMouse is QUIT and must stay quit while rawtouch runs (both
  would consume the stream → double scroll). It remains installed as
  the fallback: quit rawtouch, launch LinearMouse.

**Strategic decision (2026-08-30): the LinearMouse fork will NEVER be
released publicly** — RawTouch is the whole release story (simpler
rollout: one small tool vs "install my fork of an existing app"). The
fork is a private interim tool, frozen (it keeps the cross-pad lockout
bug; not worth fixing there). This reroutes items e/f: the release
train's host artifact is RawTouch, and notarization (f) applies to
RawTouch, not the fork. The `inputScale` upstream PR (item g) is
unaffected.

Remaining for RawTouch:
1. **Menubar app + config UI — DONE, LIVE 2026-08-30** (user switched
   over same day: settings live-apply, both pads scroll, enabled-toggle
   restores fallback, icons good; sliders gained editable value fields
   in `b95f894`).
   Single-process design (app embeds `RawTouchCore`; TCC rationale: the
   app bundle is the one identity whose Accessibility grant survives
   rebuilds when signed with a stable identity — a split app+daemon
   would pin the grant on the fragile bare-binary identity plus an IPC
   layer). Shipped at `~/src/rawtouch` @ `4691395`, 149/149 tests:
   `RawTouchAppCore` (testable model: AppModel, icon-state precedence,
   status formatters, login-item wrapper) + `RawTouchApp` (MenuBarExtra
   menu style, grouped-Form settings window with log-scale sliders +
   per-pad sections, one-screen AX onboarding with 1 s trust poll,
   lock-conflict alert) + `scripts/make-app.sh` (assembles + signs
   RawTouch.app; `RAWTOUCH_SIGN_ID` env for a stable TCC identity, ad-hoc
   otherwise). Along the way the core grew: `RawTouchStatus` +
   `onStatusChange`, `apply(configuration:)` live-reload with gate
   reconciliation (fixed a real bug: `enabled: false` used to claim the
   gate anyway → no scrolling at all), `ConfigFileWatcher` (CLI has
   live-reload now too — config is no longer load-at-start), `save()`,
   and a flock `InstanceLock` (CLI exit 3 on conflict) so app/CLI/
   LaunchAgent can never double-consume the stream. PRODUCT.md +
   DESIGN.md in the repo carry the design language.
   **Redeploy loop after app edits:** quit the app (releases the gate)
   → `./scripts/make-app.sh` → relaunch `~/Applications/RawTouch.app`.
   With ad-hoc signing each rebuild is a new TCC identity (re-grant
   Accessibility); `RAWTOUCH_SIGN_ID` with a stable cert avoids that.
2. LaunchAgent switchover after burn-in (own TCC grant — grant only
   right after install; the accessibility-loop trap applies). If the
   menubar app becomes the daily driver, the LaunchAgent path is for
   headless users only.
3. GitHub repo + release pipeline (item f: Developer ID + notarization
   before public release).
4. Watch-item: keyboard's own idle-sleep mid-claim — confirm scroll
   resumes the first time it happens naturally (bench §5 second box).
5. Onboarding polish: wire the "Open System Settings" button to also
   call `AXIsProcessTrustedWithOptions` with the prompt option — the
   explicit-button case the design allows. macOS then auto-creates the
   (unchecked) Accessibility row, so granting is a toggle instead of
   the "+"/⌘⇧G dance. Surfaced 2026-08-31 by the first re-sign: a TCC
   row from a previous signature shows "granted" while the new build is
   untrusted; recovery is quit → `tccutil reset Accessibility
   com.kalakris.RawTouch` → relaunch → grant fresh (now that the
   signature is cert-stable, this was one-time).

## l. Gate frame emission on the claim — DONE 2026-08-31 (firmware + host)

Frames now exist only in RawTouch mode. `raw_touch.c` emits the 11-byte
report only while `zmk_raw_touch_gate_engaged_for_selected()` — the same
per-endpoint claim state that already drives the wheel suppression — is
true; unclaimed, the stream is silent (saves ~100 Hz of 11-byte BLE
reports whenever no host is running, the published module's common
case). The relative-delta pointer path, tap detection and `prev_x/prev_y`
tracking keep running unclaimed — that is Standard mode. If the claim
clears mid-touch (timeout / release / endpoint switch while
`prev_touched`), the firmware emits exactly ONE synthetic release frame
(touched = 0) with bit 2 CLEAR, then goes silent; bit 2 keeps its exact
meaning ("host claim live when this frame was sampled") and is
implied-set on ordinary frames. Host side: an unclaimed frame arriving
for a pad WITH an active series (touching or coasting) cancels that
series like `interrupt()`, deliberately WITHOUT lift-off momentum — a
declaim means firmware wheel scrolling resumed, and momentum on top
would double-scroll; unclaimed frames for pads with no series stay
silently dropped (4 new pipeline tests, suite at 152). Because a passive
monitor now sees silence, `scripts/raw-touch-monitor.swift` grew an
opt-in `--claim` flag (claim + periodic refresh + release on Ctrl-C,
USB report-ID-prefix quirk handled; --claim makes it a stream
consumer — quit RawTouch first). Compatibility: new firmware + an old
non-claiming host degrades via the stale-touch watchdog; old firmware +
new host unchanged. Spec updated in the module README appendix (still
protocol v3, no layout change). Deploy: flash BOTH halves (frame path)
+ redeploy the host app.

## m. Mode naming: scrub "claim the gate" everywhere — DONE 2026-08-31

Chosen: **Standard mode** (the firmware scrolls on its own — pointer,
tap, ÷24 wheel; never "legacy"/"basic"/"fallback mode", though calling
the wheel a fallback *mechanism* in protocol prose stays fine) ↔
**RawTouch mode** (a host drives scrolling from the touch stream).
"touch stream" / "raw touch stream" is KEPT everywhere — the product's
core noun, not jargon. The protocol/firmware layer names the actual
state: `ZMK_RAW_TOUCH_FLAGS_HOST_CLAIMED` (frame flags bit 2, formerly
`_MODE_GATE`), `ZMK_RAW_TOUCH_CAP_HOST_CLAIM` (capability bit 0), Swift
mirrors `TouchStreamFrame.hostClaimed` / `Capability.hostClaim` /
`supportsHostClaim`; the verb "claim" survives ONLY there (the
acquire/refresh/release/expiry lifecycle is real). Mechanism-code names
(`gate.c`/`gate.h`, `zmk_raw_touch_gate_*`, `TouchStreamGate`, the
`gateless*` identifiers) deliberately kept — minimal churn. Swept: the
module README (appendix section retitled Mode gate → Host claim) +
sources, RawTouch's README ("The mode gate" → "Scrolling modes") / CLI
help / log strings / menu lines ("Gate claimed" → "RawTouch mode",
"Firmware fallback active" → "Standard mode", gateless attention line →
"Firmware doesn't support RawTouch mode — update needed") /
PRODUCT.md / DESIGN.md, CLAUDE.md's RawTouch section, raw-touch.md's
header, and the two stale upstreaming-todo.md gate items. Historical
docs (BENCH-mode-gate.md, mode-gate-plan.md, DONE-item records, commit
messages) untouched.

## n. Safari ProMotion post-bounce lockout — CHARACTERIZED 2026-09-02

Symptom: after a flick rubber-bands at a page edge in Safari on the
MacBook's 120 Hz display, a re-scroll is ignored for a moment (not on
the 60 Hz external display, not in Chrome; an Apple trackpad shows it
too, but only for a very fast re-scroll). Two real host fixes fell out:
synthesized events now carry real uptime timestamps (they had none;
WebKit's ProMotion momentum interpolator fits velocity from them — this
alone made it "better but not gone"), and the momentum stop floor rose
10 → 40 pt/s (an invisible crawl that kept consumers in their momentum
state for >1 s). The residual was then measured with a new bench:
`~/src/rawtouch/bench/safari-bounce/` (`scroll-bench` = the real
engine+poster fed synthetic frames, driven against an instrumented page,
per-frame scrollY read back over AppleScript). Result: a ~400 ms lockout
floor on the ProMotion path, momentum-triggered (a plain drag into the
edge has none), absent at 60 Hz, growing mildly with impact speed
(400→516 ms for 625→2000 pt/s seeds), shortened by decay only at the
margin (0.83→0.5 ≈ 10 %) and only collapsing at decay ≤ 0.3. Stop floor,
timestamps and phase sequence are at parity with Apple hardware; the
rest is WebKit's. **RESOLVED same day** by reading the WebKit source:
a zero-delta `began` is never delivered to WebKit's rubber-band
controller (`canHandleWheelEvent`), so the snap-back reset never fired;
our poster sent began with delta 0 at touch-down. It now defers the
began to the first movement and carries its delta (mayBegin +
series-start still at touch-down; a no-move touch ends as `cancelled`,
like hardware). Bench after: 98 % of a re-scroll applied at a catch
76 ms after the edge, response ~25 ms later (was dead until 433 ms);
same for a 2000 pt/s flick. No WebKit bug to file. Also learned: events
made with `CGEvent(...)` carry NO IOHIDEvent, so WebKit's ProMotion
momentum synthesizer never engages on our stream (Safari consumes our
momentum verbatim), the IOHIDEvent momentum-interrupted bit cannot be
set, and the inherited `ioHidScrollY` setter has always been a no-op.
Findings + resolution: the bench README.

## o. Pinnacle sample rate — CEILING IS 100 SPS (measured 2026-09-02)

Motivation: a 100 Hz drag on a 120 Hz display is a 5:6 beat (content
holds every sixth frame). The in-tree driver never wrote the SampleRate
register, so `cirque-input-module@intree-driver` gained an optional
`sample-rate` DT property (commit `cbb4eaa`, pinned in west.yml).
Measured with the passive monitor + `analyze-touch-timing.py` (device
timestamps, both pads): 120 → unchanged ~9.8 ms; then LH 60 / RH 200 as
a discriminator → LH 16.6 ms (the write lands), RH 9.8 ms (clamped). So
the ASIC clamps anything above 100 in normal mode; the 120 Hz idea is
dead short of ERA-level tricks (AnyMeas mode). Both pads reverted to
the default (property omitted). Follow-up SHIPPED the same day:
**host-side display-rate resampling** in RawTouch (`~/src/rawtouch`,
7 commits on `ffab49a`, 203 tests): `FrameResampler` emits one position
estimate per vsync of the display under the cursor (CVDisplayLink;
exact interpolation when bracketed, capped extrapolation via the shared
`WeightedVelocityFit` otherwise), momentum rides the same vsync, config
`resampling.enabled` (default true) + `resampling.latencyMs` (default
**0**, 0–10; Settings → "Display sync"; the knob is only for BLE-batching
smoothness — try 10 over BLE). Stops use **carry semantics** (3 more
commits, `ea0f855`..`fae77fe`, 207 tests): emission is relative and a
retraction of our own extrapolation is never posted — content lands ~1
sample period past a hard stop (6 px at 600 px/s) and stays; genuine
reversals pass at true magnitude; no dead zone on resume (asserted);
the carried offset is dropped at lift. User's reasoning: scrolling is
relative and ballistics already makes the mapping non-absolute, so a
gesture-local bias is imperceptible while a snap-back or sticky restart
is not. Offline bench: 0 % empty display frames vs 17 % (clean) / 45 %
(BLE) before, totals exact, zero added latency at 0 ms; Safari lockout
sweep 6/6 before the carry change — **rerun pending** (screen was
locked): `run.py --resample --delays 250 --rescroll-frames 60 --repeat 3`
plus the plain-drag total (`--flick 10 --flick-frames 30 --flick-settle
60 --start 2000`, expect ~2077). Two extras:
the poster now holds `began` until the first non-zero delta, and the
device clock tightens its anchor over BLE (opt-in, pipeline enables).
Deployed 2026-09-02; **feel check PASSED 2026-09-02** (user: "beautiful"
at latency 0). Safari sweep rerun 3/3 ok at latency 0 (response 100–133
ms after the edge); plain-drag final 2082–2083, i.e. ~6 px past the
5 ms-era 2077 — the carried offset, by design. Cadence capture: both pads
~10 ms device spacing over BLE, zero drops. Item closed.

## p. Open-source release prep — pass 1 DONE 2026-09-02; deferred sub-items 1, 6, 7 DONE 2026-09-04 (2–5, 8 still open, discussion pending)

A `/simplify` + release-lens sweep over both repos (six review agents:
reuse, simplification, efficiency, altitude, and two open-source-hygiene
passes). Committed as zmk-raw-touch `f431278`, rawtouch `4aa7189`,
zmk-config `655952d`; CI run 33710477203 built the vendored module on
both Go60 targets; both halves flashed from it and the app rebuilt and
relaunched the same evening. Feel test passed (item o). No BLE re-pair
needed: the report map and GATT layout are unchanged.

**Module.** Sources renamed `src/raw_touch_*.c` (no more shadowing of ZMK
core's `usb_hid.c`/`hog.c`/`hid.c`/`endpoints.c`); example overlay moved
`boards/` → `examples/` (`boards/` is a Zephyr board root); license
headers corrected (original files: Mrinal; `raw_touch_hog.c`,
`raw_touch_usb_hid.c`, `raw_touch_endpoints.c`: ZMK 2020 + Mrinal + a
derived-from note); `CONFIG_ZMK_RAW_TOUCH_REPORT_ID` **removed** — the
report ID is a fixed `0x04` in `hid.h` like the usage pair (a tunable a
host cannot follow); `ZMK_RAW_TOUCH_SCROLL_MAX_DEVICES` renamed
`ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES`; new
`ZMK_RAW_TOUCH_BLE_THREAD_STACK_SIZE` (default = ZMK's); processors
`depends on ZMK_POINTING`; dead `HID_REPORT_TYPE_OUTPUT`/`HIDS_OUTPUT`
gone; `gate_slot()` helper; BLE drain looks the conn up once per drain
not per frame; device timestamp computed only when a report goes out;
duplicate `pad-id` logged; driver-api structs `const`; every bench-doc
citation, fork war story and "touch stream" gone from comments;
`BENCH-mode-gate.md` deleted; README rewritten (host = RawTouch,
listener node in the quickstart, Verify section with the log lines,
driver guidance for Zephyr 3.5, USB feature GET = 21 bytes ID-prefixed
vs BLE 20 bare, 2-slot cap stated, release-frame bit 1 stated, BLE ATT
error codes listed, the re-pair contradiction resolved in favour of "a
permission change does not invalidate the cache", v2 section dropped,
acknowledgements). Config impact on this repo: none (`go60_rh.conf`
sets nothing that changed).

**RawTouch.** `TouchStreamCapabilities` lost its primary-pad forwarders
and both `scrollInverted` helpers (tests now pin
`RawTouchConfiguration.engineConfig`, the shipping derivation);
`supportedVersions` → `protocolVersion`; `TouchStreamGate.allowsSynthesis`
/ `refreshInterval(forTimeoutSeconds:)` gone (pipeline reads
`frame.hostClaimed`); `RawTouchStatus.deviceCount` → `padIDs` and the
Settings window now shows one section per pad the device reports (or
per pad the config overrides), named **"Pad 0" / "Pad 1"** — the sided
"Right pad"/"Left pad" names were a Go60 guess; `momentumEnded` lost its
unused `interrupted` payload; `stopSpeed` is a private engine constant
(bench `--stop` removed); `GestureEvent` class → two functions; AppKit
is no longer linked by Core; per-frame `String(padID)` lookup gone
(disabled pads simply get no engine config); os_log in the poster
guarded; `RawTouchLog.fail`; `Comparable.clamped` public in its own file
and used by the Settings fields; **unknown config keys are now logged**
(`RawTouchConfiguration.unknownKeys(in:)`, schema derived from the
encoder); decoders use a `decode(_:or:)` helper; axis picker relabelled
"Sensor X / Sensor Y" (the old "Horizontal" meant vertical scrolling from
the X sensor); log categories `Device` / `Scroll` / `HostClaim`; README
rewritten (hardware + firmware step, vertical-only, full uninstall,
config ranges + per-pad keys, no-network statement, debug-log note, no
LinearMouse fork), DESIGN.md's icon table and section list now match the
code, PRODUCT.md no longer says "one user", bench README stripped of
dates and "rerun pending", `config.example.json` matches the documented
defaults (acceleration off, no Go60 `pads` block), tracked `.pyc`
removed + `.gitignore` extended, Info.plist copyright string. 208 tests
green; `swift build` warning-free. Behaviour changes the user will
notice: pad section names, axis labels, unknown-key
log lines.

**Pass 2 (2026-09-04, three opus subagents):** sub-items 1, 6 and 7
below are done. rawtouch `741d141` (rename), `9db48ba` `32ceed4`
`48db3e0` `ca1220e` `18c2eb0` (cleanup; 256 tests, offline bench
byte-identical); module `4776642` `ef2625b` `1f58b7f`, vendored as
zmk-config `e689a8c`, CI run 33939572008 green on both Go60 targets.
**Not yet flashed / not yet redeployed** at the time of writing — RH
flash is the only firmware step (the module compiles out on the LH), no
BLE re-pair needed (Go60 feature body is still exactly 20 bytes). The new
host accepts both the old fixed-20 and the new 4 + 8N feature bodies, so
host and firmware can be updated in either order.

**Deferred — decisions for the user, not done:**
1. ~~Rename the `TouchStream*` types/files~~ — DONE 2026-09-04
   (`741d141`): `RawTouchFrame`, `RawTouchCapabilities`,
   `RawTouchTransport`, `RawTouchDeviceManager`/`-Managing`,
   `RawTouchGate` (suffix kept pending item 2), `RawTouchDeviceClock`,
   `RawTouchScrollPoster`, `RawTouchAxis`; 13 `git mv`s, zero
   occurrences of the term remain.
2. "gate" vs "claim" in identifiers (`TouchStreamGate`, `gateClaimed`,
   `onGatelessFirmware`) — prose is uniformly "claim" now.
3. ~~`com.kalakris.*` bundle ID / LaunchAgent label / log subsystem~~ —
   DONE 2026-09-04 (`4e7c553`): `io.github.kalakris.RawTouch` everywhere
   (19 sites; plist renamed to match its label). Chosen over
   `net.mrinal.*` by the user; the reverse-DNS of a namespace he
   controls. Consequence: next deploy = fresh Accessibility grant;
   `log stream --predicate 'subsystem == "io.github.kalakris.RawTouch"'`.
   Developer ID signing + notarization (item f) deferred by the user.
4. Module git history: squash before going public (commits name the
   `-wip` repo, the LinearMouse fork, "item-k agent"); delete the
   `mode-gate` branch on origin. RawTouch has no remote yet and two
   author identities in its history.
5. Module `zmk,` devicetree vendor prefix (a maintainer may object; the
   `zmk,input-processor-*` convention argues for keeping it).
6. ~~Firmware: undroppable release frame, queue 30 → 8, feature length
   4 + 8 × pads~~ — DONE 2026-09-04 (module `4776642` `ef2625b`
   `1f58b7f`). The BLE send path is a spinlocked ring drained by a
   delayable work item: a full queue evicts the oldest *motion* frame
   (never a release; slot 0 is never a candidate because the drain
   works on a copy of the head), a release that fails to notify is
   retried head-of-line every 8 ms up to 4 attempts, ordering is one
   FIFO for all pads. Queue default 8 (`range 2 255`). The slot count is
   `CLAMP(DT_NUM_INST_STATUS_OKAY(zmk_raw_touch_pad), 1, 8)` and the
   descriptor's feature REPORT_COUNT is `sizeof` the body, so a pad-count
   change is a report-map change (BLE re-pair) — the README says so. The
   host watchdog is now documented as a safety net (README "Release
   frames are delivered" block).
7. ~~Host cleanup~~ — DONE 2026-09-04, five commits: `9db48ba` one
   `ReportFraming` type replaces the four report-ID heuristics and
   `RawTouchCapabilities` accepts any 4 + 8N body (N floored from the
   length, capped at 8; a 19-byte body now parses as one slot rather
   than being rejected; pads beyond the carried slots get default
   geometry); `32ceed4` IOKit Get/SetReport on a serial `reportQueue`,
   state stays main-queue-only, shutdown releases every claim and waits
   ≤ 2 s on a semaphore (no sync hop back, so no deadlock); `48db3e0`
   `RawTouchService` owns lock + config + watcher + manager lifecycle
   for both CLI and app; `ca1220e` resampler fits only the read axis
   (offline bench tables byte-identical before/after); `18c2eb0`
   `RawTouchTestSupport` target for `ManualDisplayTicker` + fixtures
   (scroll-bench depends on it; Core does not). Not covered by tests:
   `RawTouchDeviceManager` itself (IOKit boundary) — reviewed by hand.
8. Still missing for the flip: demo video, notarized RawTouch build,
   app icon, `v0.1.0` tag, CONTRIBUTING, module CI workflow, a public
   `kalakris/rawtouch` remote.

## q. Apple-like scroll defaults + per-transport latency — DONE 2026-09-04 (rawtouch `c7a4999`, `aa50533`)

`scale` is now a **gain relative to physical 1:1** (points/count derived
per gesture = gain × display pt/mm under the cursor ÷ pad counts/mm;
fallbacks 4.0 pt/mm, 38 counts/mm; both panels here measure ≈4.3 pt/mm).
Defaults tuned on hardware by the user: gain 1.0, acceleration ON
(exponent 0.9, reference 1500 counts/s, minGain 1.0, maxGain 16),
momentum decay 0.55 s, seed cap 20000 pt/s. The user's config carries
gain 1.14 (= the 0.13 pt/count he settled on). Reasoning: Apple is ~1:1
at slow speed; the 54 × 40 mm pad needs a steep curve for reach; Apple's
momentum is ≈0.5 s. Then **per-transport display-sync latency**: frames
are tagged with their IOHIDDevice's transport, the pipeline adopts it at
touch-down, `resampling.latencyMs` (USB/unknown, 0) vs the new
`resampling.bluetoothLatencyMs` (10); Settings has two sliders; the menu
device line says "connected over USB and Bluetooth". Backups of the
pre-change config: `~/.config/rawtouch/config.json.pre-apple-defaults-2026-09-03`
and `.pre-gain-2026-09-03`. rawtouch now lives at **github.com/kalakris/rawtouch (private, created 2026-09-04)**; the module README already links there.
