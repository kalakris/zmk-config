# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-28
(end of day — items a, b, c, i all closed today).
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

**Current state (2026-09-04, evening):** item p pass 2 (sub-items 1, 6, 7)
is committed and pushed in all three repos and CI-green. **RH flashed
with `e689a8c` at 20:43** (undroppable release frames, queue 8, per-pad
feature slots live on hardware). The RawTouch app was rebuilt from `4e7c553`
and relaunched at ~20:50 (Accessibility re-granted for the new bundle
ID, as predicted); the passive monitor saw both transports validate the
feature report and frames flowing under the app's claim. Feel test over
USB and BLE still to be reported by the user. **Safari lockout bench
(item n) rerun on the pass-2 host, 2026-09-04 evening, no regression:**
`run.py --resample --delays 250 --rescroll-frames 60 --repeat 3` → 3/3
ok, 99–100 % applied, response 101–117 ms after the edge (reference
98–100 %, 100–133 ms); fast flick (`--flick 80 --start 400`) 3/3 ok,
99–100 %, response 7–8 ms after a 160–177 ms catch; plain drag
(`--flick 10 --flick-frames 30 --flick-settle 60 --start 50`) 3/3,
102 %, response at the catch. Then the user's felt lockout was recorded
live and root-caused (item n, second cause: sub-point first-motion began);
the poster fix (rawtouch `9023988`) is deployed to the app and the
user confirmed the lockout is gone. **The bundle ID changed to
`io.github.kalakris.RawTouch` (rawtouch `4e7c553`), so that deploy needs
a fresh Accessibility grant even with `RAWTOUCH_SIGN_ID`** (TCC keys on
the bundle ID). Item p sub-items 2, 4, 5, 8 await the user's decisions;
signing/notarization (item f) is DONE (2026-09-16: Developer ID + notarized DMG/zip releases on tag push). Everything before that: item q (physical-1:1
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
a future boot is now a real bug, not the known race. **First recurrence
observed 2026-09-14:** LH pad dead for everything (no pointer, no tap, no
wheel; keys fine) while running the 2026-09-05 hardware checklist,
discovered after the RH had been reflashed on 2026-09-06 (the LH was not
reflashed and had been up since 2026-09-04). Power-cycling the LH
revived it. Unknown whether it died at the RH reboot (peripheral sits
unpolled while the central is in bootloader) or earlier; not
reproduced. Watch for the next one and note what preceded it. Fixing this was the MUST-FIX gate before upstreaming the
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

**2026-09-16 — mostly done.** Paid membership active; Developer ID
Application cert in the keychain (Team ID `7WBD7URF58`, distinct from
the free personal team's `L2ZD95Z272`; .p12 backup in ~/Documents).
`make-app.sh` signs with `--timestamp`; the installed app was notarized
by hand (first submission took ~30 min, Accepted), stapled, and `spctl`
reports "Notarized Developer ID". The Team-ID change cost the one
expected Accessibility re-grant. Pipeline in rawtouch `e4193e1`:
`scripts/notarize.sh` + `.github/workflows/release.yml` — **`v*` tags
only**, `release` environment secrets (cert, ASC API key `2VP94428ZG`),
throwaway keychain, SHA-pinned actions, `attest-build-provenance`,
`gh release create`. **End-to-end verified the same evening:** the .p12
was re-exported with a fresh password (the first one was reused
elsewhere), both password-bearing secrets set by the user at a `gh
secret set` prompt (never via chat), and tag `v0.1.0` (rawtouch
`2548d31`) produced GitHub release "RawTouch v0.1.0" in 52 s — CI
notarization is seconds, not the 30 min of the first manual
submission. The downloaded zip, quarantined as if from Safari, passes
`codesign --verify --strict`, `stapler validate` and `spctl` ("Notarized
Developer ID"). The attestation step is gated `if: !private` because
GitHub does not store attestations for private user-owned repos (first
run failed there) — it becomes live at the public flip, and
`gh attestation verify RawTouch-<tag>.zip --owner kalakris` is the
check. **Universal build** (rawtouch `966f76a`): `make-app.sh` runs
`swift build --arch arm64 --arch x86_64`, whose product lives under
`.build/apple/Products/Release/` (xcbuild backend); release `v0.1.1`
verified the same way, both slices hardened-runtime signed. Local
deploys use the same path, ~17 s clean. **App icon** (rawtouch
`e09adca`): graphite tile, white fingertip contact with a soft cyan glow,
five cyan frames fading with age — SVG sources in `resources/icon/`, a
two-frame variant for the 16/32 px slots, `scripts/make-icon.sh` (needs
`rsvg-convert`) renders the committed `resources/RawTouch.icns`;
DESIGN.md records it. The user rejected the impeccable-pass palette
(moss green) and oval contact; the original A-trace look won. **DMG**
(rawtouch `b74bc41` + `8cfe0cd`): `notarize.sh` now also builds a DMG
(stapled app + Applications symlink, `hdiutil` UDZO, signed with the
identity read back from the app, notarized as a second submission,
stapled); the workflow stamps `${GITHUB_REF_NAME#v}` into both plist
version fields via `RAWTOUCH_VERSION` and attaches DMG + zip. First
`v0.1.2` run shipped `CFBundleVersion = "v0.1.2"` (prefix not stripped);
release + tag deleted, fixed, re-tagged. Not done: the workflow runs no
tests (`xcodebuild test` raises TCC prompts); no DMG background art
(`create-dmg` if the bare window bothers anyone); Homebrew cask after the
public flip.

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

**Second cause, found and fixed 2026-09-04 (rawtouch, "Poster: defer the
began until a whole point of motion").** The user still felt a >100 ms
lockout the bench could not reproduce. A new live recorder
(`bench/safari-bounce/record.py`: page scrollY + raw pad frames via
`raw-touch-monitor.swift`, aligned by cross-correlation) over 73 real
touches showed it exactly: every re-scroll whose first movement landed
while the bounce was still settling (residual stretch 1–31 px) vanished
whole, momentum included; every one after the settle responded in ≤ 16 ms.
Difference from the bench: a finger starts from rest with sub-point
deltas, and Safari reads the INTEGER point delta
(`kCGScrollWheelEventPointDeltaAxis1` backs AppKit's `scrollingDeltaY` and
WebKit's unaccelerated delta — verified with an NSEvent probe), so "began
carries the first motion" was still a zero-delta began to WebKit. Without
it `ScrollingEffectsController::handleWheelEvent` never clears
`m_ignoreMomentumScrolls` (set when the flick's momentum reached the
stretched edge) nor stops the snap-back, and its early-return ignores
every gesture event while the animation runs and every momentum event
after. Fix: the poster defers the began until |Σdelta| ≥ 1 pt and carries
the sum. `scroll-bench --catch-ramp --catch-rest` reproduces it (dead /
23 % / 65 % at residual 10 / 4 / 1 px → 80 % = the ramp's own deficit at
every delay after the fix); standard sweep unchanged. Deployed to the app
2026-09-04 ~22:05 (rawtouch `9023988`); **user's verdict: "lockout is
gone"** — item n is closed for real.

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
   Developer ID signing + notarization (item f) done 2026-09-16.
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
   app icon, `v0.1.0` tag, CONTRIBUTING, CI workflows in BOTH release
   repos (rawtouch: `swift build` + `swift test` on macOS; module: a
   pinned example firmware build), a public `kalakris/rawtouch` remote.
   Added by the 2026-09-05 review (item r): decide before freezing the
   protocol whether the feature report needs a distinctive magic /
   identification field and range checks on reserved bytes (accidental
   0xFF00/0x01 squatter hardening, not authentication); either select
   one physical keyboard and leave others in Standard mode or key host
   state by device-and-pad (host side DONE 2026-09-15, uncommitted —
   see item t); decide Fast User Switching behaviour
   (the instance lock is per home directory).

## r. Codex release review — fixes applied 2026-09-05 (UNCOMMITTED in all three repos; not deployed, not flashed)

Codex's open-source release review (`docs/reviews/rawtouch-2026-09-05/`,
reviewed rawtouch `9023988` + module `1f58b7f`) was independently
assessed (all nine findings confirmed; the lease-locking concern disputed
on reachability, hardened anyway because it was cheap) and the fixes were
applied by four opus subagents, then hand-reviewed. State at the end of
the session — **everything below is in the working trees only**:

- **rawtouch** (280 tests green, release build warning-free, offline
  bench byte-identical): posting readiness gates the claim —
  `RawTouchService` polls the injected trust check (1 s untrusted / 5 s
  trusted, never the prompting API) and pushes `setPostingReady(_:)` into
  the manager, which releases/claims on every device and interrupts the
  pipeline; `enabled` is never rewritten; status carries `postingReady`
  and the menu says "Not claimed — Accessibility needed to scroll". Claim
  writes carry a `GateClaimState` generation so a claim in flight cannot
  land after a release. `TouchScrollPipeline.configure` interrupts when
  the active pad leaves the set or its axis changes. Feature-report read
  buffer is 69 bytes (8 slots + report ID). Settings: per-pad "Direction"
  is a 3-state picker (Automatic/Normal/Inverted = nil/false/true).
  Bundle ships `Resources/LICENSE`; LICENSE inventory corrected (17
  derived files). LaunchAgent `KeepAlive` → `Crashed` (was
  `SuccessfulExit=false`, which looped on exit 2/3). README: no
  "keystroke access" overclaim, 30 s crash-fallback bound, one-keyboard
  limitation, tested = Apple silicon + macOS 26 + Go60.
- **zmk-raw-touch** (CI-compiled green via zmk-config branch
  `review-fixes-2026-09-05`, run 34002376780; **not flashed**): shared
  transmit ring `src/raw_touch_txq.h` (motion-only eviction, generation
  counter, per-entry binding); USB frames queue
  (`CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE`, default 4) and are drained
  from `in_ready_cb`, so a release is never dropped on a healthy bus;
  BLE entries are bound to the profile they were sampled for, discarded
  at drain on mismatch, flushed on endpoint switch and on the bound
  profile's disconnect; gate state + expiry timer move under one
  spinlock. README appendix rewritten honestly (both transports protect
  releases; watchdog still required for link loss/bus reset/endpoint
  switch mid-touch); new "Split keyboards" section (relay wiring example
  from the Go60 keymaps + the RX_COMPLETE_TIMEOUT=3/RX_TIMEOUT=5 poll
  tweak); "Tested configuration" paragraph; crash fallback bounded by
  the lease. Verified with source-extracted C harnesses
  (`$TMPDIR/rt-harness/` — volatile).
- **zmk-config**: CLAUDE.md/AGENTS.md no longer call the dead-pad boot
  race open; `vendor/` re-synced from the dirty module tree.

**Deployed 2026-09-06 00:14:** RH flashed from main run 34017041142
(module `1d5c41f`), app redeployed from rawtouch `8ca32d4`. First
hardware evidence, passive monitor right after the flash: both endpoints
validate (v3, pads 0x03, capabilities 0x01); 240 pad-0 frames over USB,
seq-contiguous (0 dropped), 7 release frames delivered, all with the
claim bit, 10 ms median cadence — so the `in_ready_cb` re-arm works for
one pad. **Checklist run 2026-09-14, tests 1–8 PASSED:** feel check;
Accessibility revoke/re-grant switches modes in well under a second
each way with no dead window; rapid enable toggling leaves wheel
scrolling live at once; disabling the active pad and changing its axis
mid-gesture (via a delayed config edit) both end the gesture cleanly and
the other pad scrolls immediately; direction picker correct incl.
`"invert": false`; menu quit mid-drag ends without momentum, wheel live
at once, RawTouch mode back almost instantly on relaunch; two-pad USB
capture: pad 0 829 frames / 40 touches / 40 releases / 0 dropped,
pad 1 (split relay) 1084 / 42 / 42 / 0, no late releases, batch size
1.00 on both. (LH pad was found dead before this test — see item a.)
**Test 9 (unplug mid-touch) PASSED 2026-09-14** — and taught something:
because the app holds a claim on BOTH endpoints, a USB unplug mid-touch
is a *handover*, not a stop: capture showed pad 1 over USB (198 frames,
0 dropped, finger down), a 31 ms gap, then the same touch continuing
over BLE (361 frames, 0 dropped) with the lift-off release delivered
claimed; exactly one frame (the one flushed with the dying bus) lost at
the switch. No runaway momentum. The host closes the USB gesture on
device removal and starts a fresh one from the BLE frames, so the user
sees a small hitch and then continuous scrolling. **Replug PASSED** (USB claim back, no double scroll). **Test 10 (BLE
profile switch) PASSED 2026-09-14:** over BLE, switch to an empty
profile and back, finger down across the switch-away. Capture: 2411
frames, all claimed; the only missing seq is the firmware's trailing
release for the gesture open at the switch, which was bound to the newly
selected (empty) profile and discarded on the way back — the documented
watchdog case; no stale frame delivered; frames resumed on the first
touch after switching back. Host-side note: the Mac stays connected on
its own profile while the keyboard talks to the other one, so the app
keeps refreshing its claim and the menu keeps saying RawTouch mode —
correct from the host's view, and why the switch-back was instant rather
than <=10 s. Possible refinement (not a bug): bind that trailing release
to the profile that HAD the gesture and deliver it over its still-live
connection instead of leaving it to the watchdog. Test-procedure lesson:
profile keys + System + Nav need three hands — drag in pointer context
(frames stream for any touch while claimed). **Test 12 (rapid re-touch) PASSED:** 20/20, no joins. **Test 11 (hard
kill, lease expiry) PASSED:** wheel scrolling back 18 s after `kill -9`
(30 s lease measured from the last refresh, so 20–30 s after that
refresh is the expected window). **Deferred to the next natural occurrence:** test 13, keyboard
deep-sleep (60 min idle, `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT`) and wake with
the app running — expect RawTouch mode back on the first touch, no
double scroll; wake is a re-enumeration, the same claim path the replug
already passed. Check the first scroll after the next long idle. Then: the
module history-squash decision (item p.4) before the public flip.

Not done from the review (deliberately), now tracked in item p.8:
device-and-pad identity for multiple keyboards (documented as a
limitation instead), stronger feature-report validation / a magic field
(protocol decision), CI workflows in the two release repos, Fast User
Switching behaviour; notarization stays item f.

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

## s. Left pad as a dedicated two-axis scroll pad — DONE, FULLY HARDWARE-VERIFIED 2026-09-15

The left Cirque is now a **scroll pad only** — no layer to hold — and
both pads scroll in **two axes**.

**Firmware** (`config/go60_rh.keymap`, the central; zmk-config `5e32fe1`,
CI green, RH flashed 15:37):

- `&zip_raw_touch_scroll` moved into `&cirque_lh_listener`'s **base**
  chain, so every LH touch is scroll context on every layer. The LH
  `nav_scroll` overlay is deleted (it would have been a duplicate), and
  the LH pointer stage (`&zip_xy_scaler 1 1`) is gone — that pad emits no
  pointer motion any more.
- `tap-click` removed from the `raw_touch_lh` node: the module suppresses
  the firmware tap for any touch that was in scroll context
  (`tap_scroll_seen` in `src/raw_touch.c`), so on an always-scroll pad the
  property could only ever promise a click that never fires.
- The one-axis `zip_xy_to_vscroll_mapper` (both axes → `REL_WHEEL`) is
  replaced by `zip_xy_to_hvscroll_mapper` (`REL_X` → `REL_HWHEEL`,
  `REL_Y` → `REL_WHEEL`), used by the LH base chain and the RH
  `nav_scroll` overlay. The label is deliberately *not*
  `zip_xy_to_scroll_mapper`: the pinned MoErgo ZMK
  (`57a7b8e0`) already ships a node with that label and an identical map
  in `app/dts/input/processors/code_mapper.dtsi`, and a duplicate label is
  a build error. (Switching to the stock node instead would also work —
  noted in case the local one ever becomes a maintenance burden.)
- Fallback signs: `INPUT_TRANSFORM_Y_INVERT` only. The module's derived
  deltas are already oriented (+X = finger right, +Y = finger down);
  `REL_WHEEL` positive means scroll UP so Y is inverted for the old-school
  direction, while `REL_HWHEEL` positive means scroll RIGHT, which already
  matches finger-right. macOS Natural Scrolling then flips both.
  **The horizontal fallback direction was confirmed on hardware** the same
  day (checklist item 1) — no X_INVERT needed.
- Verified against the pinned tree so no local shim was needed: its
  `zip_scroll_scaler` already covers `<INPUT_REL_WHEEL INPUT_REL_HWHEEL>`
  and `zip_scroll_transform` already has `x-codes = <INPUT_REL_HWHEEL>`;
  `INPUT_REL_HWHEEL` exists in the pinned Zephyr (`v3.5.0+zmk-fixes`); and
  ZMK's mouse report descriptor carries AC Pan, so horizontal wheel
  reports have somewhere to go.
- `config/go60_lh.keymap` (the peripheral) got the same mapper rename for
  consistency only — its listeners are `status = "disabled"` and the
  central does all processing, so **the LH half does not need
  reflashing**.

**Host** (RawTouch `16e8f80`, 301 tests, deployed 15:36 with the Apple
Development cert so the AX grant carried over): two-axis synthesis, with
config keys `axes`, `pads.<id>.axes` and `pads.<id>.invertHorizontal`.
The inferred horizontal sign (`!invertX`, true on the Go60) was confirmed
on hardware — no override needed.

**Module README**: a "dedicated scroll pad" subsection under Scroll mode
documents the pattern (marker in the base chain, `tap-click` inert,
two-axis fallback chain); `vendor/zmk-raw-touch/` re-synced.

**Flash instruction:** **right half only** — the change is entirely on the
central. Follow the firmware loop in CLAUDE.md: push `main`, wait for the
"Build and Draw" run, `./scripts/download-firmware.sh`, then
`./scripts/flash-go60.sh firmware/main/firmware --halves rh` in the
background (bootloader: RH T3 + `/`). No report-layout change, so no BLE
forget/re-pair.

**Hardware checklist (ALL PASSED 2026-09-15, USB and BLE):**

1. ✅ **Standard mode** (quit RawTouch): the LH pad scrolls **vertically and
   horizontally** through the wheel fallback, with no layer held, and both
   directions read sanely against the macOS Natural Scrolling setting.
   This is the test that settles the unverified horizontal sign — if
   sideways scrolling goes the wrong way, add `INPUT_TRANSFORM_X_INVERT`
   to the LH (and RH `nav_scroll`) `&zip_scroll_transform` argument.
2. ✅ **RawTouch mode**: the LH pad gives smooth two-axis scrolling with
   lift-off momentum; diagonal drags track the finger; horizontal
   direction is correct — if not, override `pads.1.invertHorizontal`
   host-side rather than changing the firmware (the fallback sign and the
   host sign are independent). *Result: both axes and diagonals track the
   finger, directions match the Natural Scrolling setting.*
   *Standard-mode observation, not a bug: in Apple Maps the LH vertical
   wheel ZOOMS while the horizontal wheel pans — Maps treats phase-less
   mouse-wheel events as zoom; RawTouch mode's gesture-phase events pan.
   Every other app scrolls normally.*
3. ✅ **RH pad unchanged**: Nav-held scrolling still works and is now
   two-axis; pointer and tap-to-click on the base layer are unaffected.
4. ✅ **LH tap does nothing**: quick taps on the left pad produce no click,
   anywhere, and no stray clicks during scroll gestures.
5. ✅ **Cross-pad catch**: a fling on the LH pad caught by a touch on the RH
   pad (and vice versa) still cancels momentum — the host's first-touch-
   wins arbitration is unchanged, but the LH pad now enters scroll context
   without a layer, so the arbitration sees it far more often.
6. ✅ **Both transports**: run 1–5 over USB and over BLE.

**Known, deliberately left alone (2026-09-15):** both pads are mounted
angled inwards, and two-axis scrolling makes the skew visible (a straight
vertical drag leaks a little horizontal). The pointer shows about half the
tilt the overlay's dots suggest, so the electrode grid sits at a smaller
angle than the module — the number would have to be MEASURED, not read off
the case. Nothing in MoErgo's firmware, ZMK's processors or the Pinnacle
compensates for it (checked the pinned tree: only rotate-90/y-invert
exist), and it is identical with RawTouch and LinearMouse off. User's
call: not worth a `rotationDegrees` knob. If it ever is, the host is the
place (per-pad rotation of the 2-D delta after orientation mapping) plus
a `--calibrate` drag-along-a-ruler mode in `scripts/raw-touch-monitor.swift`.

## t. Multiple RawTouch keyboards on one Mac — IMPLEMENTED + DEPLOYED 2026-09-15 (rawtouch working tree, UNCOMMITTED; single-keyboard checks 1–3 PASSED)

Host-only change in `~/src/rawtouch` (16 files, +1411/−430, six new
files; wire protocol untouched). Built by a Fable subagent, independently
reviewed (7 findings, all fixed), verified by the main agent: `swift test`
349/349 (301 original + 48 new), `scroll-bench --offline` tables
byte-identical to HEAD. The README rewrite drafts under
`/private/tmp/rawtouch-readme-drafts.ocsu3G/` were updated to match.

Design: runtime identity = `RawTouchEndpointID` (one IOHIDDevice
appearance, generation allocated by `RawTouchEndpointRegistry`, never
reused) × pad ID = `RawTouchSourceID`, keying clock, seq, 150 ms watchdog,
geometry/orientation and gesture ownership in the pipeline; stale
timers/capability reads/claim completions carry the endpoint ID (claims
also the `GateClaimState` generation) and are dropped for departed
endpoints. Arbitration generalised from pad to source: first touch owns
the drag, any other scroll-context source may catch momentum, pointer
touches never steal. Removing an inactive endpoint leaves the owner's
gesture and timing alone; removing the owner cancels without momentum
(`endGesture()`), global transitions still `interrupt()`. Claims are per
endpoint with their own serial report queue (shutdown releases run
concurrently under the 2 s bound); `devices.<key>.enabled=false` releases
that endpoint (Standard mode, "switched off" menu line). Persistent
identity `RawTouchDeviceKey` = `usb:<serial>` (unless a known placeholder:
`0123456789AB`, `0.01`, `moergo.com:GO60-0123456789ABCDEF`, empty) or
`bt:<address>` — nothing is common to both transports (live `ioreg`: USB
has SerialNumber+LocationID, BLE has DeviceAddress+PhysicalDeviceUniqueID,
no serial), so one keyboard on USB+BLE = two keys and NO automatic
grouping; product name/VID/PID/geometry never merge devices. The Go60's
USB serial is hwinfo-derived (`usb_serial_number.c` in MoErgo's board
dir); whether stock ZMK boards get a unique serial is NOT verified (Zephyr
may derive one from hwinfo) — READMEs hedge accordingly. Config:
`devices.<key>.pads.<id>` > `pads.<id>` > global, field by field; old
files parse identically; the app never persists an empty `devices` entry.
CLI exits 2 only when every present endpoint is unsupported AND none is
still validating (`status.validatingEndpointCount`).

Single-keyboard-visible changes after deploy: "RawTouch mode" if EITHER
transport's claim landed (a failed transport gets its own line); two
endpoint lines for USB+BLE instead of "connected over USB and Bluetooth";
Settings gains a Keyboards section and titles pad sections "Pad N — all
keyboards"; saved configs gain `"devices": {}`.

DEPLOYED 2026-09-15 evening on the user's request (uncommitted tree; both
endpoints admitted: USB endpoint#1 `usb:moergo.com:GO60-A856ED2AC49F3E97`,
BLE endpoint#2 `bt:de-3c-19-f8-23-05`). User ran and PASSED the same day:
(1) both endpoints RawTouch mode, scroll on each; (2) USB unplug
mid-drag → clean stop, fresh BLE gesture, no double scroll, replug
re-claims; (3) per-transport Enabled off → Standard mode on that
transport (wheel back, pads not dead), other transport unaffected.
Grouping USB+BLE into one device was discussed and declined for now: it
only buys one config entry + one menu row; revisit only alongside the
p.8 magic-field decision (a chip ID could ride in the same revision).
Remaining: commit; checks 4–7 below (4 per-transport override, 5 AX
revoke/re-grant, 6 old-firmware neighbour, 7 two physical keyboards —
the last two need hardware not owned). Original checklist: (1) Go60 USB+BLE: two endpoint lines both
RawTouch mode, scroll on each, per-transport latency still switches;
(2) USB unplug mid-drag: gesture ends w/o momentum, next touch scrolls
over BLE fresh, no double scroll, replug re-claims; (3) BT profile switch
mid-touch closes via watchdog as before; (4) Settings → Keyboards: USB and
BLE rows with key tails, "Customize pads" on one row adds per-keyboard
pad sections whose gain applies to that transport only, "Enabled" off on
one row → that transport goes to Standard mode (wheel scrolling returns,
pads NOT dead) while the other keeps RawTouch mode; (5) TWO physical
keyboards (needs a second RawTouch board — none owned): both listed and
distinguishable, first-touch ownership + cross-keyboard catch, unplug idle
one mid-drag leaves the drag intact, unplug the scrolling one ends it and
the other scrolls immediately, each pad 0 on its own orientation; (6) an
old-firmware keyboard alongside: listed as unsupported, Go60 unaffected,
warning clears on unplug; (7) revoke/grant Accessibility: all claims drop
and return together. Then update CLAUDE.md's "301 unit tests" and README
claims once hardware-verified.

## u. RawTouch app UI rework from an `/impeccable critique` — DONE + DEPLOYED 2026-09-16 (rawtouch, committed locally, NOT pushed)

Baseline critique (dual-agent, snapshot
`~/src/rawtouch/.impeccable/critique/2026-09-16T23-17-29Z__sources-rawtouchapp.md`):
23/40, four P1s — one 2,146-pt scroll in a fixed 640-pt window with no
navigation; accessibility defects (Bluetooth row switches announced as the
USB row's, gain slider announcing its log10 track position, per-section
Reset buttons); "Customize pads" acting off-screen and deleting overrides
without asking; physics vocabulary on the two sections that decide feel.
User direction: structure first, then accessibility and vocabulary; hide
the per-pad orientation matrix; keep "RawTouch mode / Standard mode";
wanted live per-keyboard readouts for tuning.

What changed (all in `~/src/rawtouch`, 360/360 tests, deployed to
`~/Applications/RawTouch.app`):
- Settings is now the `Settings` scene: toolbar tabs **Scrolling**
  (last-gesture readout, gain/axes/direction, momentum), **Advanced**
  (acceleration, display sync, config-file row with Show in Finder),
  **Keyboards** (one section per endpoint with a live status row, its own
  pad sections directly below when customized, then shared Pad N
  sections). Each tab is one screen, sized per tab (724/770/704 pt);
  only Keyboards scrolls. Opened via `openSettings` on macOS 14+, the
  app-menu item's own action on 13 (`showSettingsLegacy`; the by-name
  `showSettingsWindow:` selector is NOT handled on macOS 26).
- One confirmed "Restore Defaults…" per tab (`restoreScrollingDefaults` /
  `restoreAdvancedDefaults` / `restoreKeyboardDefaults`); per-section
  Reset rows gone. "Customize pads" off with saved overrides asks first
  (`hasSavedDeviceOverrides`); disconnected entries confirm before
  removal.
- Pad sections: "Use this pad", "Custom gain" (off = inherited-gain
  readout, on = slider seeded with it), and an "Orientation" disclosure
  row — a hand-rolled button, because `DisclosureGroup` in a grouped Form
  exposes nothing to VoiceOver — holding the axes/axis/direction pickers
  as pop-ups; opens by itself only when something inside is set.
- Slider rows: fixed 58-pt unit column ("×", "s", "pt/s", "ms",
  "counts/s") so every row's slider/field/unit align; plain labels
  ("Coasting time", "Minimum flick speed", "Speed for 1× gain", "Lowest /
  Highest gain", "Curve steepness"); every control's `.help` names its
  config key; one-line footers; nothing focused on open.
- Accessibility: sliders carry `accessibilityValue` in real units; the
  Bluetooth-row identity collision is gone with one Section per endpoint;
  toggles are scoped by keyboard/pad in their labels. NOTE: System
  Events' `name` only reports AXTitle, which SwiftUI buttons never set —
  their label is AXDescription. Verify with the AX API
  (`/tmp/claude-501/ax-dump.swift` this session), not `name of button`.
- Live tuning readout: `RawTouchGestureSummary` (peak finger speed in
  counts/s, peak gain, touch distance, lift-off speed in pt/s, coast
  distance + duration, `complete`) built by `TouchScrollEngine`
  (`lastGestureSummary`), forwarded by `TouchScrollPipeline.onGestureSummary`
  tagged with the owning source (the coast half keeps its touch half's
  tag across a cross-source catch), published as `RawTouchStatus.lastGesture`,
  shown on the Scrolling tab as "Finger: Peak 1,240 counts/s · gain up to
  3.2×" / "Lift-off: 1,850 pt/s · coasted 1,900 pt in 0.8 s". NOT yet
  seen with a real flick (user was away) — first thing to eyeball.
- Menu: "Enable Scrolling" (verb phrase); `.pending` reads "Switching to
  RawTouch mode…", `.failed` "Standard mode — switch failed, reconnect to
  retry"; a config error reads "config.json couldn't be read — last good
  settings in use" instead of the decoder message. Status lines stay
  disabled items (system convention) despite 2.2:1 contrast — documented
  in DESIGN.md.
- Onboarding: says exactly where to click (+ under the Accessibility
  list), "Not Now" instead of a disabled Done, Done once granted.
- Docs: DESIGN.md components section rewritten; README menubar/keyboards
  paragraphs updated.

Follow-up critique after the rework: **30/40** (trend 23 → 30), no P0/P1;
the AX API confirmed every prior accessibility defect gone. Its cheap
fixes were applied the same session (Keep as the confirmations' default,
"connection" wording on per-transport rows, last-touch line repeated
under Acceleration, one Accessibility line in the menu, no "re-grant after
every update", reduce-motion chevron). Open P2/P3s (in the second snapshot
under `.impeccable/critique/`): Orientation "Automatic" should say what it
resolves to (needs the pad orientation in `RawTouchStatus.Endpoint`); Scroll
gain and Highest gain live on different tabs; pop-up capsules sit ~4 pt
left of other right-aligned content; "Customize pads" is session state;
Lowest gain / Bluetooth latency default to the end of their tracks.

Third pass, same evening, on the user's direction: the shared `pads.<id>`
config layer is GONE (pad IDs mean nothing across keyboards) — every pad
setting is `devices.<key>.pads.<id>`, an old top-level `pads` key is
reported unknown and ignored; the core tests write per-pad settings via
a `RawTouchTestSupport` shorthand (`configuration[pad: "0"]` under
`testDeviceKey`, and the single-endpoint `configure(capabilities:…)` takes
a `deviceKey:`). The Keyboards tab is a pop-up naming each connection
(plus "not connected" saved entries) that follows the last-scrolled
connection (`AppModel.lastScrolledEndpointID`) until picked, with that
connection's status, switch and pad sections always shown below; no
Customize toggle; Restore Defaults restores the selected connection
(`removeDevice`). The Advanced tab's config-file row was dropped;
the USB/Bluetooth latency sliders stay (user: fine as an advanced
experiment knob). Three local commits in rawtouch, 361 tests.

Not done / decided against: option-click slider reset (promise removed
from DESIGN.md); light-appearance screenshots (would flip the user's
system appearance); two-keyboard and empty/disconnected states only
code-reviewed, not seen live; Orientation "Automatic" still does not
show the resolved value. Next: user flicks a pad and checks the readout
and the pop-up following the pad's connection, then push.

## v. RawTouch live readout: graphical add-ons — IDEAS, NOT STARTED (2026-09-16)

The last-gesture readout now lives in the bottom bar of every settings
tab (pad · connection / peak finger speed + gain / lift-off speed + coast),
so tuning reads its effect on whichever tab the knob is. Three graphical
follow-ons the user liked the sound of, in value = build order:

1. **Gain curve on Advanced** (pure UI, Swift Charts, macOS 13 floor):
   gain vs finger speed from the current parameters, a vertical rule at
   "Speed for 1× gain", floors at lowest/highest gain; sliders reshape it
   live (≤200 ms); the last gesture drops a dot at its peak speed/gain.
   The one that makes the exponent legible (RawAccel-style).
2. **Per-gesture trace** (small core addition): at lift-off, finger speed
   over time with gain shaded behind it and the coast as a decaying tail
   — shows where acceleration kicked in, lift-off sharpness, coast length.
   Redraws once per gesture; the engine already buffers the samples it
   needs for the velocity fit, so expose a compact per-gesture sample
   array alongside `RawTouchGestureSummary`.
3. **Live finger canvas on Keyboards** (needs a telemetry channel): one
   rectangle per pad in the pad's aspect ratio, raw finger position with a
   fading trail — the honest orientation check (raw axes vs screen
   direction) and "which pad is which" by touch. Needs a 100 Hz stream
   separate from the gesture-granular `RawTouchStatus`, throttled to the
   display link, published only while the window is visible.

Not doing: speedometers, sliding momentum balls, anything animating at
rest (PRODUCT.md's gaming-control-panel anti-reference).

## w. Firmware-declared scroll axes (protocol addition) — IDEA, NOT STARTED (2026-09-16)

Context: the same evening the host lost its global `axes` and
`followSystemNaturalScrolling` keys (rawtouch commit after `a0f9cd3`):
Natural scrolling is always followed (macOS applies it to the firmware's
wheel events in Standard mode, so following it is what makes RawTouch mode
scroll the same way), and "which directions a pad scrolls in" is per pad
only (`devices.<key>.pads.<id>.axes`, default both). The Scrolling tab is
gain + Reverse scrolling + momentum.

The lens (user's): enabling RawTouch mode should change nothing but
momentum and smoothness. Today a dedicated vertical scroll pad is
configured in the keymap's Standard-mode chain (the LH pad's two-axis
÷24 wheel chain, or a one-axis one), and that knowledge does not reach
RawTouch mode — the user has to repeat it per connection on the
Keyboards tab. Proposal: let each pad's slot in the feature report carry
a scroll-axes hint (both / vertical / horizontal) next to the orientation
byte, declared on the `zmk,raw-touch-pad` node alongside `rotate-90` /
`y-invert`; the host's per-pad default becomes "Automatic (from
firmware)" the way orientation already is, and the Keyboards control a
rarely needed override. Module + host change, feature-report version
bump (spec appendix), tests on both sides. Firmware is the single source
of truth for what a pad is for; the host owns feel only.

## x. RawTouch began-direction rule vs. app axis latching — IN PROGRESS 2026-09-17 (rawtouch, 4 local commits `46a99ea`..`9bd4e0f`, deployed, NOT pushed)

Symptom: some scrolls "missed" although the live readout showed them.
Recorded with two passive recorders — the app's debug log (`log stream
--level debug --predicate 'subsystem == "io.github.kalakris.RawTouch"'`,
which prints every posted event) and a listen-only session-level scroll
tap (`bench/recordings-2026-09-17/scrolltap.swift`) — event for event
identical, so nothing is lost between RawTouch and the apps. Cause:
WebKit AND Chromium (Claude Code, Electron) latch a gesture to the
dominant axis of its first event; with two-axis scrolling the began went
out at the first whole point on either axis, and the finger's landing
produces 1–5 pt of purely sideways centroid slide (contact flattening)
over 1–4 frames before a vertical flick, so the began named horizontal
and the vertical motion + momentum were delivered and ignored. The Magic
Trackpad has the same latching; two-finger centroid averaging hides it.

Rule as of `9bd4e0f` (`RawTouchScrollPoster.releasedBegan()`): a
flick-sized FRAME (≥6 pt on one axis, other < 1/3) releases at once on
that axis; else a 3-frame landing window; then vertical at 2 pt (3:1) or
4 pt (2:1), horizontal only at 8 pt (3:1) or 12 pt (2:1); ambiguous at
8 frames → vertical unless horizontal ≥ 8 pt at 2:1. The began carries
one axis; the held minor-axis motion rides on the next changed event.
Trial results (begans on the wrong axis): old rule 12/103; 2-frame rule
6/48; landing window 7/64 (all horizontal); the asymmetric rule was
deployed at 01:14 and NOT yet trialled — first thing tomorrow: user
scrolls, then tally with the script pattern in this session (group
"post scroll" lines by phase=1; began axis vs. sign of the gesture's
dominant sum). Recordings of all trials: `bench/recordings-2026-09-17/`
(log 36,956 lines / tap 18,445 lines; trial boundaries at lines
1519/1470, 10104/5194, 15606/9996, 24793/10950, 32096/15067,
36956/18445). If misses persist, the remaining lever is the user's pad
config (Pad 0 Vertical-only removes the problem entirely) or the
firmware axes hint (item w).

## y. Peripheral sample-time stamp for the LH pad — DONE, HARDWARE-VERIFIED 2026-09-21 (module `1911f70`, zmk-config `6bcdbe8`; checks 1–2 passed, feel check 4 pending)

**Result (2026-09-21, `captures/capture7-split-stamp.csv` — local only,
`captures/` is git-ignored — 90 s passive capture over USB with RawTouch
holding the claim):** LH inter-frame
device-ts spacing sd 1.44 → 0.36 ms (RH 0.34 ms), histogram 9:14% /
10:84% / 11:2% — the 5 ms and 15 ms modes are gone. LH host-arrival
spacing sd unchanged at ~1.2 ms, as expected: the stamp fixes what the
timestamps say, not when frames arrive. Keys, RH pointer/tap and LH
Standard-mode scroll all fine (check 1).

**Four-configuration survey (2026-09-21, `scripts/analyze-touch-lateness.py`
over `captures/capture7..10*.csv`, local only).** Stamp spacing = are
the stamps honest; arrival lateness = arrival minus stamped time,
min-anchored per touch = the resampling latency that pad needs:

| Host link | Split link | Pad | stamp spacing sd | lateness p50 / p95 / max |
|---|---|---|---|---|
| USB | wire | RH | 0.34 ms | 0.6 / 1.0 / 1.1 ms |
| USB | wire | LH | 0.36 ms | 2.8 / 5.3 / 6.1 ms |
| BLE | wire | RH | 0.36 ms | 8.1 / 14.8 / 19.9 ms |
| BLE | wire | LH | 0.35 ms | 9.3 / 17.1 / 30.0 ms |
| BLE | BLE (3 TX bufs) | LH | **2.92 ms** | 9.4 / 16.6 / 28.9 ms |
| BLE | BLE (8 TX bufs) | LH | 0.43 ms | 8.3 / 15.3 / 23.3 ms |

Findings:
- **Feel regression on the LH over USB is expected** and is the BLE
  problem in a new place: honest stamps + 0 ms USB resampling latency
  means the resampler runs at the edge of a timeline whose frames
  arrive up to 6 ms late (one poll cycle). The LH needs ~6 ms over USB.
  `RawTouchConfiguration.resampling.latencyMs` is per TRANSPORT, so
  there is no per-pad path today → host change (item z).
- The current constants are guesses: BLE's 10 ms covers only the RH
  median (p95 is 15 ms) yet feels fine, while USB's 0 ms with a 2.8 ms
  LH median feels worse. Target is between p50 and p90, tuned by feel.
- The relay adds ~2–3 ms at p95 on BLE vs ~5 on USB (the poll wait and
  the connection-interval wait overlap in the queue), so a fixed
  "relay latency" added to the transport latency over-corrects; measure.
- **BLE split trap, FIXED in `5a2e0b5`:** ZMK's BLE split peripheral
  `bt_gatt_notify()`s each relayed input event straight from the input
  thread (`app/src/split/bluetooth/service.c`,
  `zmk_split_bt_report_input`), and Zephyr allocates that buffer with
  K_FOREVER (att.c: only responses/confirmations get a timeout). With
  the stock 3 TX buffers, the stamp's 4th notification per frame blocked
  the input thread until the next 7.5 ms connection event, so the NEXT
  frame's stamp was late (p90 4.6 / max 15 ms, ~10% of frames). Fix:
  `CONFIG_BT_L2CAP_TX_BUF_COUNT=8`, `CONFIG_BT_CONN_TX_MAX=8`,
  `CONFIG_BT_BUF_ACL_TX_COUNT=8` in `config/go60_lh.conf`; LH reflashed
  2026-09-21 and verified (row 6). The wired split is immune (ring
  buffer, no blocking).

## z. RawTouch: adaptive per-source resampling latency — NEXT, NOT STARTED (2026-09-21)

Replace the per-transport latency constants (0 USB / 10 BLE) with a
live, per-source estimate: lateness of each frame against the
anchor-tightened device timeline (`RawTouchDeviceClock` already tracks
the running minimum, so lateness is one subtraction), kept as a decaying
~p90 with headroom, clamped [floor, cap], adopted at touch-down only
(never mid-gesture), seeded from the transport default or — better —
persisted per source key (`usb:<serial>` / `bt:<address>`) so a known
keyboard starts converged. Keep a manual per-pad override
(`pads.<id>.latencyMs`) and show the live estimate in the gesture
readout. This subsumes the "relayed pad" feature-report bit idea (slot
byte +7 is reserved; firmware could set it from
`DT_NODE_HAS_COMPAT(pad device, zmk_input_split)`) — persistence seeds
better than a flag. First step to confirm the diagnosis cheaply: the
per-pad override alone, pad 1 at 6 ms over USB, then a feel check.

Why: the LH pad is relayed over the polled wired split and was stamped
on the central after the hop, so its frame timestamps carried the poll
cadence. Measured on `captures/` (2026-08-28 session, same-touch
consecutive-seq pairs): RH pad inter-frame device-ts spacing sd 0.3 ms;
LH pad sd 1.4 ms at the current 3/5 ms cadence (92% at 10 ms, ~5% at
15 ms, ~3% at 4–5 ms — one pair in twelve off by a full poll cycle,
i.e. 50% instantaneous velocity error) and bimodal 0.3/22.8 ms at stock
cadence. Analysis script pattern: group by (dev, pad), keep pairs with
seq+1 and both touched, wrap the 16-bit ts delta.

What was built (all in the module, no ZMK core change, no host change):
- `src/input_processor_raw_touch_split_stamp.c` +
  `include/zmk/raw_touch/split_stamp.h` + binding
  `zmk,input-processor-raw-touch-split-stamp` (one cell = the relay's
  `reg`; a phandle back to the relay was the first attempt and
  gen_defines.py rejected it as a devicetree cycle). Peripheral-only Kconfig
  `ZMK_INPUT_PROCESSOR_RAW_TOUCH_SPLIT_STAMP` (depends on
  `ZMK_INPUT_SPLIT && !ZMK_SPLIT_ROLE_CENTRAL`). Hook: the peripheral's
  `zmk,input-split` node runs its `input-processors` before forwarding
  each event (`app/src/pointing/input_split.c`, `ZIS_INST` peripheral
  branch), so an event reported from inside the processor reaches the
  wire ahead of the frame's sync. On each `INPUT_EV_ABS` sync it reports
  `{type INPUT_EV_VENDOR_START (0xf0), code 0x5453, value = uptime/100 µs,
  sync 0}` with the relay's reg.
- `src/raw_touch.c`: latches the stamp (latest wins), consumes it at the
  top of `raw_touch_process_frame()` (before the idle early-return), and
  uses it in both send sites instead of `raw_touch_timestamp()`.
- Verified by reading, not by running: ZMK's central listener switch
  handles only REL/ABS/KEY (unknown type + sync 0 = no-op); the stock
  scaler and both module processors pass unknown types through.
- `config/go60_lh.keymap`: declares `zip_raw_touch_split_stamp` and
  chains it on `&cirque_split` (`cirque_split@0`, reg 0, in MoErgo's
  `go60_lh.dts`). This is the FIRST time the module compiles anything on
  the LH build (`raw_touch_log.c` + the processor) — CI is the compile
  check, there is no local west workspace. First CI run failed twice
  over: the cycle above, and `.gitignore`'s unanchored `zmk/` silently
  dropped the new `vendor/.../include/zmk/raw_touch/split_stamp.h`
  (the older headers had been force-added); now anchored as `/zmk/`.
- Cost: one extra 23-byte wired envelope per frame (~2.3 kB/s at 100 Hz
  on a 921600-baud link); 4 bytes of pad state.
- Clock domains: the stamp is LH uptime, unsynchronised with the RH. The
  host keeps one `RawTouchDeviceClock` per (endpoint, pad) source
  (`TouchScrollPipeline.SourceStreamState`), anchored to arrival and
  advanced by device deltas, and cross-pad catch is state-based — so no
  host change. README appendix now says hosts MUST NOT compare
  timestamps across pads.
- Module README (the WIP rewrite in the working tree, uncommitted) got
  the peripheral snippet, a timing-section update and the appendix rule;
  the vendored README is the committed pre-rewrite one, so it lacks them.

To test (BOTH halves must run the same build — the wire format between
halves gained an event type; a mismatched LH/RH pair is harmless but
stamps nothing):
1. ~~Flash both halves~~ DONE 2026-09-20: RH 16:28:47, LH 16:29:29 from
   the `6bcdbe8` CI build (`./scripts/flash-go60.sh ... --halves both`).
2. ~~Keys on both halves, RH pointer + tap, LH scroll in Standard mode~~
   PASSED 2026-09-21 (user).
3. ~~RawTouch mode: capture frames while scrolling on the LH, rerun the
   pair analysis~~ PASSED 2026-09-21 — see the result above. Recipe:
   `swiftc -O scripts/raw-touch-monitor.swift -o /tmp/raw-touch-monitor`,
   run it passively (no `--claim`) into a CSV for ~90 s, then the pair
   analysis (group by dev+pad, consecutive seq, both touched, wrapped
   16-bit ts delta).
4. Feel: LH flick momentum consistency vs RH. Then decide whether to
   relax the poll cadence (item c's 3/5 ms) — with honest stamps it
   only trades delivery latency and LH battery; a per-pad resampling
   latency in the host would be needed before going back to stock
   (`RawTouchConfiguration.latencyMs` is per transport, not per pad).
