# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-28
(end of day — items a, b, c, i all closed today).
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

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

1. A small USB TX ring in the module's `src/usb_hid.c` — the
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
- `inputScale` PR to **LinearMouse** (first two commits of the fork,
  self-contained), framed as generalizing
  `LogitechHighResolutionWheelNormalizer`.

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
and findings in the module repo's `BENCH-mode-gate.md` — every section
passed. Highlights: claim/refresh/release/expiry/clamp verified over
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
